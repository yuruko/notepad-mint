/* edit.c - the native EDIT control: (re)creation, subclass, font + zoom, text transfer, caret helpers.
 *
 * the control is a plain multiline EDIT holding utf-16 text with CRLF breaks. everything
 * the stock control can't do is done here around it: ctrl+wheel zoom (font rescale),
 * ctrl+backspace / ctrl+delete word delete, our own context menu, logical line numbers
 * (EM_LINEFROMCHAR counts wrapped rows) via a direct scan of the control's text buffer. */
#include "mp.h"

#define IDC_EDIT 100
#define SIZE_TIMER 0x4D50                           /* timer id: the re-wrap deferred during a drag */

HWND g_edit;

static WNDPROC g_orig;
static HFONT   g_font;
static HBRUSH  g_brEdit;
static int     g_wheel;
static int     g_paintDepth;                    /* nested native paint / print calls may erase; a standalone resize erase must wait */
static int     g_stubW, g_stubMax;                /* width of a selected line break's block, widest character of the font (see "selected line breaks" below); g_stubW 0 = measure again (the font changed) */

/* -------------------------------------------------- scrollbars on demand -- */
/* a native multiline edit always shows its bars (greyed out when there is nothing to scroll). we want them only when
 * needed, so after anything that can change the content or the room for it we ask the control for the range / page it
 * keeps for each bar and show or hide the bar to match. the check runs from a posted message so the control has finished
 * its own layout first; toggling a bar resizes the client area, which comes back here until nothing changes (it always
 * converges: showing a bar only ever makes the other one more needed, hiding one only ever less). */
#define WM_BARS (WM_APP + 7)
static int g_barsPending, g_inBars;

void EditScrollSoon(void)
{
    if (g_edit && !g_barsPending && !g_inBars) { g_barsPending = 1; PostMessageW(g_edit, WM_BARS, 0, 0); }
}

/* NB: once a bar is hidden (ShowScrollBar clears its WS_xSCROLL style) the control stops maintaining the range it keeps for it,
 * so a hidden bar can't be asked. vertical: exact from the line count vs the lines that fit (works with the bar hidden).
 * horizontal: show the bar for a moment (redraw off, nothing flickers) so the control reports its range, then hide it again if unneeded. */
static int LineHeight(void)
{
    HDC dc = GetDC(g_edit);
    HGDIOBJ of = SelectObject(dc, g_font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_edit, dc);
    return tm.tmHeight + tm.tmExternalLeading > 0 ? tm.tmHeight + tm.tmExternalLeading : 1;
}

static int VNeeded(void)
{
    RECT r;
    int lines = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0), vis;
    SendMessageW(g_edit, EM_GETRECT, 0, (LPARAM)&r);
    vis = (r.bottom - r.top) / LineHeight();
    if (vis < 1) vis = 1;
    return lines > vis;
}

static int HNeeded(void)                          /* only meaningful while the bar exists */
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE;
    if (!GetScrollInfo(g_edit, SB_HORZ, &si)) return 0;
    DBG(L"bar h range/page", si.nMax - si.nMin + 1, si.nPage);
    return si.nMax - si.nMin + 1 > (int)si.nPage;
}

#define BARS(e) (GetWindowLongPtrW(e, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL))

static void UpdateBars(void)
{
    int pass;
    LONG_PTR before;
    if (!g_edit) return;
    before = BARS(g_edit);
    g_inBars = 1;                                 /* our own show / hide resizes the edit: that must not schedule another check
                                                     (an endless posted-message loop would starve WM_PAINT) */
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    for (pass = 0; pass < 4; pass++) {            /* toggling one bar can change the other one's need: repeat until stable */
        int changed = 0, v = VNeeded(), hadV = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_VSCROLL) != 0;
        if (v != hadV) {
            if (!v) {                             /* going away: make sure nothing is left scrolled out of view */
                int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
                if (first) SendMessageW(g_edit, EM_LINESCROLL, 0, (LPARAM)-first);
            }
            ShowScrollBar(g_edit, SB_VERT, v);
            changed = 1;
        }
        if (!g_pf.wrap) {                         /* (a wrapping edit has no horizontal bar at all) */
            int hadH = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_HSCROLL) != 0, h;
            if (!hadH) ShowScrollBar(g_edit, SB_HORZ, TRUE);
            h = HNeeded();
            DBG(L"bars pass v/h", v, h);
            if (!h) {
                SendMessageW(g_edit, EM_LINESCROLL, (WPARAM)-100000, 0);       /* back to column 0 */
                ShowScrollBar(g_edit, SB_HORZ, FALSE);
            }
            if (h != hadH) changed = 1;
        }
        if (!changed) break;
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    if (BARS(g_edit) != before) RedrawWindow(g_edit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);   /* only when a bar really came or went */
    g_inBars = 0;
    SbarSync(g_edit);                             /* the classic bars over the native ones show / hide with them */
}

/* ----------------------------------------------------- text buffer access - */
/* the control keeps its text in a local-memory block we can read in place (no copy) */
static const WCHAR *TextLock(HLOCAL *h, int *n)
{
    *h = (HLOCAL)SendMessageW(g_edit, EM_GETHANDLE, 0, 0);
    *n = GetWindowTextLengthW(g_edit);
    return *h ? (const WCHAR *)LocalLock(*h) : NULL;
}

/* zero-copy read access for other modules (find / replace). the pointer is only valid until EditUnlockText
 * and the buffer is NOT guaranteed to be nul terminated: always honour *n */
const WCHAR *EditLockText(void **h, int *n)
{
    return TextLock((HLOCAL *)h, n);
}

void EditUnlockText(void *h)
{
    if (h) LocalUnlock((HLOCAL)h);
}

BOOL EditHasSel(void)
{
    DWORD s = 0, e = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    return s != e;
}

/* index of the end of the selection the caret sits on (the stock control can't say which end that is) */
static int CaretIndex(void)
{
    DWORD s = 0, e = 0;
    POINT cp;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s != e && GetFocus() == g_edit && GetCaretPos(&cp)) {
        LRESULT ps = SendMessageW(g_edit, EM_POSFROMCHAR, s, 0), pe = SendMessageW(g_edit, EM_POSFROMCHAR, e, 0);
        int ds = (cp.x - (short)LOWORD(ps)), dy = (cp.y - (short)HIWORD(ps));
        int de = (cp.x - (short)LOWORD(pe)), dz = (cp.y - (short)HIWORD(pe));
        ds = (ds < 0 ? -ds : ds) + 64 * (dy < 0 ? -dy : dy);
        de = (de < 0 ? -de : de) + 64 * (dz < 0 ? -dz : dz);
        return (int)(ds < de ? s : e);
    }
    return (int)e;
}

void EditCaretPos(int *line, int *col)
{
    static struct { DWORD rev; int n, idx, line, col, ok; } c;      /* the status bar asks several times for one keystroke: the same text and caret are not scanned again */
    HLOCAL h;
    int n, idx = CaretIndex(), i;
    const WCHAR *p;
    *line = 1; *col = 1;
    n = GetWindowTextLengthW(g_edit);
    if (c.ok && c.rev == g_textRev && c.n == n && c.idx == idx) { *line = c.line; *col = c.col; return; }
    p = TextLock(&h, &n);
    if (!p) return;
    if (idx > n) idx = n;
    *line = 1 + (int)mp_count_lf(p, (size_t)idx);
    for (i = idx; i > 0 && p[i - 1] != '\n'; i--) {}
    *col = idx - i + 1;
    LocalUnlock(h);
    c.rev = g_textRev; c.n = n; c.idx = idx; c.line = *line; c.col = *col; c.ok = 1;
}

/* the selection as the status bar shows it ("162:54 [5 L 54 B]"): TRUE when something is selected, then
 *   lines = the lines it covers, counted the way it looks: every line break in the selection ends one line and the text after the last break is one more,
 *           unless the selection ends right at the start of a line (shift+down three times from the start of a line selects 3 lines, not 4);
 *           a selection without a break is 1 line
 *   bytes = what a save would write for the selected text: the document's encoding and line ending (DocBodySize), no byte order mark.
 * the numbers are kept for as long as the selection, the text, the encoding and the line ending stay as they were: the status bar asks again on every
 * caret / mouse move, and a big selection is not scanned for each of them */
BOOL EditSelStats(int enc, int eol, int *lines, DWORD *bytes)
{
    static struct { DWORD s, e, rev; int len, enc, eol, lines, ok; DWORD bytes; } c;
    DWORD s = 0, e = 0;
    HLOCAL h;
    int n;
    const WCHAR *p;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s) return FALSE;
    if (!c.ok || c.s != s || c.e != e || c.rev != g_textRev || c.len != GetWindowTextLengthW(g_edit) || c.enc != enc || c.eol != eol) {
        p = TextLock(&h, &n);
        if (!p) return FALSE;
        if (e > (DWORD)n) e = (DWORD)n;
        if (s >= e) { LocalUnlock(h); return FALSE; }
        c.lines = (int)mp_count_lf(p + s, (size_t)(e - s)) + (p[e - 1] == '\n' ? 0 : 1);
        c.bytes = DocBodySize(p + s, (int)(e - s), enc, eol);
        LocalUnlock(h);
        c.s = s; c.e = e; c.rev = g_textRev; c.len = n; c.enc = enc; c.eol = eol; c.ok = 1;
    }
    *lines = c.lines;
    *bytes = c.bytes;
    return TRUE;
}

/* caret to the start of logical line `line` (1-based). FALSE when the document has fewer lines */
BOOL EditGotoLine(int line)
{
    HLOCAL h;
    int n, cur = 1, i = 0;
    const WCHAR *p = TextLock(&h, &n);
    if (!p) return line <= 1;
    if (line < 1) line = 1;
    while (cur < line) {
        while (i < n && p[i] != '\n') i++;
        if (i >= n) { LocalUnlock(h); return FALSE; }
        i++; cur++;
    }
    LocalUnlock(h);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)i, (LPARAM)i);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}

/* ctrl+k: empty the logical line the caret is on (the one the status bar shows, also with word wrap on: a paragraph is one line). its text
 * goes, its line break stays and the caret ends up at its start. one replace = one undo step. an empty line is left alone (no change at all) */
void EditClearLine(void)
{
    HLOCAL h;
    int n, idx = CaretIndex(), a, b;
    const WCHAR *p = TextLock(&h, &n);
    if (!p) return;
    if (idx > n) idx = n;
    for (a = idx; a > 0 && p[a - 1] != '\n'; a--) {}              /* the line starts after the previous line break */
    for (b = idx; b < n && p[b] != '\n'; b++) {}                  /* and ends before its own: the CR of a CR LF is not part of the text */
    if (b < n && b > a && p[b - 1] == '\r') b--;
    LocalUnlock(h);
    if (a == b) return;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);                   /* (a long line leaves the view scrolled sideways: back to the start) */
}

/* ------------------------------------------------------------ text i/o ---- */
BOOL EditSetDocText(const WCHAR *t)
{
    BOOL ok;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    ok = SetWindowTextW(g_edit, t ? t : L"");
    if (ok) {
        SendMessageW(g_edit, EM_SETSEL, 0, 0);
        SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    return ok;
}

WCHAR *EditGetDocText(int *len)
{
    int n = GetWindowTextLengthW(g_edit), got;
    WCHAR *buf = (WCHAR *)mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!buf) return NULL;
    got = GetWindowTextW(g_edit, buf, n + 1);
    if (got != n) { mem_free(buf); return NULL; }
    buf[got] = 0;
    if (len) *len = got;
    return buf;
}

void EditInsert(const WCHAR *s)
{
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)s);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);       /* the caret ends up after the text: show it (a dropped list of paths can run off the bottom) */
}

/* ---------------------------------------------------- font / colors / zoom - */
static int CALLBACK FoundCb(const void *lf, const void *tm, DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static BOOL FontInstalled(const WCHAR *face)
{
    LOGFONTW lf;
    int found = 0;
    HDC dc = GetDC(NULL);
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    wcopy(lf.lfFaceName, face, 32);
    EnumFontFamiliesExW(dc, &lf, FoundCb, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found != 0;
}

void FontResolve(WCHAR *face)
{
    static const WCHAR *const fb[] = { L"Consolas", L"Lucida Console", L"Courier New" };
    int i;
    if (face[0] && FontInstalled(face)) return;
    for (i = 0; i < COUNTOF(fb); i++) {
        if (FontInstalled(fb[i])) { wcopy(face, fb[i], 32); return; }
    }
    wcopy(face, L"Courier New", 32);
}

/* ----------------------------------------------------------------- caret -- */
/* the stock edit draws the selection in the system highlight colour, and its own caret stays too: an inverting rectangle, so it comes out
 * pure white on the dark theme's black editor and pure black on the light theme's white one (nothing here creates a caret of its own) */

/* the space around the text (no frame): the formatting rectangle, shrunk from the client area by EDIT_PAD on the left and bottom and by
 * EDIT_PAD_TOP on the top (a rectangle set with EM_SETRECT replaces the margins, so the left / right ones are part of it; EM_SETMARGINS keeps
 * them for whatever the control redoes by itself). the control resets that rectangle to the client area on every size change, so this runs
 * after those too.
 * it is a margin, not a padding: the control clips its text at that rectangle, so text that is scrolled out of the rectangle would stop short of the
 * edge of the editor and leave a blank frame. the blank strips at the left and at the top (and the band at the bottom) are therefore painted by BandDraw
 * (the rows that reach into them are drawn there), and with word wrap off the rectangle runs to the right edge of the client area. (a wrapped text, and a
 * right to left editor, keep the space on the right: nothing is ever scrolled out sideways there) */
BOOL EditIsRtl(void);
static void EditPad2(HWND h, int margins)
{
    RECT r;
    int pad = S(EDIT_PAD), top = S(EDIT_PAD_TOP);
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    int hidR = (st & WS_VSCROLL) ? 0 : S(SBAR_TRIM), hidB = (st & WS_HSCROLL) ? 0 : S(SBAR_TRIM);   /* the window overhangs the visible area (SBAR_TRIM): without a bar that part is client area, out of sight */
    if (margins) SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    GetClientRect(h, &r);
    r.right -= hidR; r.bottom -= hidB;
    if (r.bottom - r.top > top + pad + 8 && r.right - r.left > 2 * pad + 8) {
        r.left += pad;
        if (g_pf.wrap || EditIsRtl()) r.right -= pad;
        r.top += top; r.bottom -= pad;
        SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&r);
    }
}

static void EditPad(HWND h) { EditPad2(h, 1); }

/* the tab size (format > tab size): a tab stop every g_pf.tab average characters. EM_SETTABSTOPS counts in dialog units, a quarter of the font's average
 * character width, so 4 units per column; g_tabPx is the same distance in pixels, for the rows edit.c draws itself (TabbedTextOutW). the control
 * measures its text with the font it has, so this runs after every font change too */
static int g_tabPx;

void EditApplyTabs(void)
{
    HDC dc;
    HGDIOBJ of;
    TEXTMETRICW tm;
    UINT u = 4u * (UINT)g_pf.tab;
    if (!g_edit || !g_font) return;
    dc = GetDC(g_edit);
    of = SelectObject(dc, g_font);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_edit, dc);
    g_tabPx = g_pf.tab * tm.tmAveCharWidth;
    SendMessageW(g_edit, EM_SETTABSTOPS, 1, (LPARAM)&u);
    EditPad(g_edit);                                /* (a wrapped text breaks its rows again with the new tabs) */
    InvalidateRect(g_edit, NULL, TRUE);
    EditScrollSoon();
}

void EditApplyFont(void)
{
    LOGFONTW lf;
    HFONT nf, old = g_font;
    int px = MulDiv(g_pf.cur, g_dpi, 72);
    if (px < 1) px = 1;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -px;
    lf.lfWeight = g_pf.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)(g_pf.italic ? 1 : 0);
    lf.lfCharSet = DEFAULT_CHARSET;                 /* (ANSI_CHARSET would draw '\' as a yen sign in segoe ui / arial on a japanese system) */
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcopy(lf.lfFaceName, g_pf.font, 32);
    nf = CreateFontIndirectW(&lf);
    if (!nf) return;
    g_font = nf;
    g_stubW = 0;                                    /* (the width of a selected line break's block follows the font) */
    if (g_edit) {
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)nf, TRUE);
        EditApplyTabs();                            /* (the tab size is in units of this font's width; it also pads) */
        EditScrollSoon();
    }
    if (old) DeleteObject(old);
}

HBRUSH EditBrush(void)
{
    if (!g_brEdit) g_brEdit = CreateSolidBrush(g_pf.bg);
    return g_brEdit;
}

void EditApplyColors(void)
{
    HBRUSH old = g_brEdit;
    g_brEdit = CreateSolidBrush(g_pf.bg);
    if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
    if (old) DeleteObject(old);
}

/* ctrl+plus / ctrl+minus / ctrl+wheel change the font size itself (not a percentage): one step along this ladder.
 * 1pt steps while text is small, bigger jumps further up. the range is FONT_MIN..FONT_MAX */
static const int g_ladder[] = { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22, 24, 26, 28, 30, 32, 34, 36,
                                40, 44, 48, 54, 60, 66, 70 };

static int Ladder(int cur, int dir)            /* the next size in that direction, or cur if there is none */
{
    int i;
    if (dir > 0) {
        for (i = 0; i < COUNTOF(g_ladder); i++) if (g_ladder[i] > cur) return g_ladder[i];
    } else {
        for (i = COUNTOF(g_ladder) - 1; i >= 0; i--) if (g_ladder[i] < cur) return g_ladder[i];
    }
    return cur;
}

BOOL EditZoomCan(int dir)
{
    return Ladder(g_pf.cur, dir) != g_pf.cur;
}

void EditZoomStep(int dir)
{
    int n = Ladder(g_pf.cur, dir);
    if (n == g_pf.cur) return;
    g_pf.cur = n;
    EditApplyFont();
    AppUpdateStatus();
}

void EditZoomReset(void)                       /* ctrl+0: the size picked in the font dialog (or the default) */
{
    if (g_pf.cur == g_pf.pt) return;
    g_pf.cur = g_pf.pt;
    EditApplyFont();
    AppUpdateStatus();
}

/* -------------------------------------------------------------- rtl ------- */
#define RTL_BITS (WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR)

BOOL EditIsRtl(void)
{
    return (GetWindowLongPtrW(g_edit, GWL_EXSTYLE) & WS_EX_RTLREADING) != 0;
}

void EditToggleRtl(void)
{
    LONG_PTR ex = GetWindowLongPtrW(g_edit, GWL_EXSTYLE);
    ex = (ex & WS_EX_RTLREADING) ? (ex & ~(LONG_PTR)RTL_BITS) : (ex | RTL_BITS);
    SetWindowLongPtrW(g_edit, GWL_EXSTYLE, ex);
    SetWindowPos(g_edit, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(g_edit, NULL, TRUE);
}

/* ------------------------------------------------------- word delete ------ */
/* 0 blank, 1 word, 2 punctuation (also used by the dialog edits' ctrl+backspace in ui.c) */
int WordClass(WCHAR c)
{
    if (c == ' ' || c == '\t') return 0;
    if (c >= 0x80) return 1;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return 2;
}

/* ctrl+backspace (dir < 0) / ctrl+delete (dir > 0) */
static void DelWord(int dir)
{
    DWORD s = 0, e = 0;
    HLOCAL h;
    int n, a, b;
    const WCHAR *p;

    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s != e) { SendMessageW(g_edit, WM_CLEAR, 0, 0); return; }
    p = TextLock(&h, &n);
    if (!p) return;
    a = b = (int)s;
    if (a > n) a = b = n;
    if (dir < 0) {
        if (a > 0 && p[a - 1] == '\n') {                       /* just the line break */
            a--;
            if (a > 0 && p[a - 1] == '\r') a--;
        } else {
            while (a > 0 && WordClass(p[a - 1]) == 0) a--;     /* blanks, then one run of a kind */
            if (a > 0 && p[a - 1] != '\n') {
                int k = WordClass(p[a - 1]);
                while (a > 0 && p[a - 1] != '\n' && WordClass(p[a - 1]) == k) a--;
            }
        }
    } else {
        if (b < n && p[b] == '\r') {
            b++;
            if (b < n && p[b] == '\n') b++;
        } else if (b < n && p[b] == '\n') {
            b++;
        } else {
            if (b < n && WordClass(p[b]) != 0) {
                int k = WordClass(p[b]);
                while (b < n && p[b] != '\r' && WordClass(p[b]) == k) b++;
            }
            while (b < n && WordClass(p[b]) == 0) b++;
        }
    }
    LocalUnlock(h);
    if (a != b) {
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    }
}

/* ------------------------------------------------- selected line breaks ---- */
/* the stock control highlights the characters of a selection and nothing else, so a selected line break (CR LF) leaves no trace: an empty line
 * inside a selection looks unselected and the end of a selected line looks like the selection stops there. like other editors we draw a block
 * (the width of a space, a full row high, in the selection colour) at the end of the text of every row whose hard line break is selected.
 * a soft wrap (word wrap on) is not a line break: no block there, and none after the last line.
 *
 * the control paints a changed selection straight onto its window, not through WM_PAINT (painting over it in WM_PAINT flickered once, see NOTES), so
 * the blocks are drawn straight onto the window too: right after the control has handled a message that can change what it shows (StubSync)
 * and after each paint of its own (WM_PAINT, WM_PRINTCLIENT). a block that has to go is never painted over: its spot is invalidated and
 * updated, so the control repaints that spot itself (whatever it holds is then drawn exactly as the control draws it). */
#define STUB_MAX 512
#ifdef SHOTDC
static int g_shotWiped, g_shotKept;                 /* probe builds: blocks that were found wiped by the control / blocks that stayed, over the syncs so far */
#endif

typedef struct StubGeom {
    RECT fr;                                        /* the formatting rectangle (client coordinates) */
    int  first, total;                              /* first visible row, rows in all (a wrapped line is several rows) */
    int  y0, pitch, hoff;                           /* client y of the first visible row, the row height, pixels the text is scrolled sideways (0 when right to left) */
    int  w;                                         /* the width of a block */
    int  maxRow;                                    /* rows longer than this (characters) are skipped: EM_POSFROMCHAR packs x into 16 bits, a position that far out would wrap around
                                                       into view (the horizontal scroll range stops far short of such an end anyway) */
} StubGeom;

/* one block: the row, the client position of the end of the row's text (where the next character would go) and which way the block grows from there
 * (+1 right, -1 left: right to left text, and an empty row in a right to left editor, end at the right margin and grow towards the middle) */
typedef struct Stub { int row, x, y, dir; } Stub;

static struct { int n, row[STUB_MAX], xd[STUB_MAX], dir[STUB_MAX]; } g_stub;     /* what the last sync drew; xd = x in text space (x - left edge + scroll) */

static LRESULT Ctl(HWND h, UINT m, WPARAM w, LPARAM l)   /* straight to the stock control: none of our own bookkeeping (the scrollbar overlays poll on every EM_*) */
{
    return CallWindowProcW(g_orig, h, m, w, l);
}

static int StubHasSel(HWND h)
{
    DWORD s = 0, e = 0;
    Ctl(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    return e > s;
}

static void StubMetrics(HWND h)                     /* g_stubW = a space of the editor font, g_stubMax = its widest character */
{
    HDC dc = GetDC(h);
    SIZE sz;
    TEXTMETRICW tm;
    sz.cx = 0; sz.cy = 0;
    memset(&tm, 0, sizeof tm);
    if (dc) {
        HGDIOBJ of = SelectObject(dc, g_font);
        GetTextExtentPoint32W(dc, L" ", 1, &sz);
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, of);
        ReleaseDC(h, dc);
    }
    g_stubW = sz.cx > 3 ? (int)sz.cx : 3;
    g_stubMax = tm.tmMaxCharWidth > g_stubW ? (int)tm.tmMaxCharWidth : g_stubW;
}

/* where the rows are: FALSE when no visible row can have a line break after it */
static int StubGeomGet(HWND h, StubGeom *g)
{
    int s0, s1;
    LRESULT p0, p1;
    Ctl(h, EM_GETRECT, 0, (LPARAM)&g->fr);
    g->total = (int)Ctl(h, EM_GETLINECOUNT, 0, 0);
    g->first = (int)Ctl(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (g->first < 0 || g->first + 1 >= g->total) return 0;
    s0 = (int)Ctl(h, EM_LINEINDEX, (WPARAM)g->first, 0);
    s1 = (int)Ctl(h, EM_LINEINDEX, (WPARAM)(g->first + 1), 0);
    if (s0 < 0 || s1 < 0) return 0;
    p0 = Ctl(h, EM_POSFROMCHAR, (WPARAM)s0, 0);
    p1 = Ctl(h, EM_POSFROMCHAR, (WPARAM)s1, 0);
    g->y0 = (short)HIWORD(p0);
    g->pitch = (short)HIWORD(p1) - g->y0;
    if (g->pitch <= 0) g->pitch = LineHeight();
    g->hoff = EditIsRtl() ? 0 : g->fr.left - (short)LOWORD(p0);
    if (!g_stubW) StubMetrics(h);
    g->w = g_stubW;
    g->maxRow = 30000 / g_stubMax;
    return 1;
}

static void StubRect(RECT *r, const StubGeom *g, int x, int y, int dir)
{
    r->left = dir > 0 ? x : x - g->w;
    r->right = dir > 0 ? x + g->w : x;
    r->top = y;
    r->bottom = y + g->pitch;
}

/* the visible rows whose hard line break (CR LF at the end of the row) is inside the selection, top to bottom */
static int StubList(HWND h, const StubGeom *g, Stub *out, int cap)
{
    DWORD s = 0, e = 0;
    HLOCAL hl;
    const WCHAR *t;
    int n = 0, r, last, cur, nxt, rtl = EditIsRtl();

    Ctl(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s) return 0;
    hl = (HLOCAL)Ctl(h, EM_GETHANDLE, 0, 0);
    t = hl ? (const WCHAR *)LocalLock(hl) : NULL;
    if (!t) return 0;
    last = g->first + (g->fr.bottom - g->fr.top + g->pitch - 1) / g->pitch - 1;      /* the last row that is (partly) in view */
    if (last > g->total - 2) last = g->total - 2;                                     /* (the final row has no break after it) */
    cur = (int)Ctl(h, EM_LINEINDEX, (WPARAM)g->first, 0);
    for (r = g->first; r <= last && n < cap; r++) {
        int b;
        nxt = (int)Ctl(h, EM_LINEINDEX, (WPARAM)(r + 1), 0);
        if (nxt < 2) break;
        b = nxt - 2;
        if (b >= cur && b - cur <= g->maxRow && t[b] == '\r' && t[b + 1] == '\n' && (int)s <= b && (int)e > b) {
            LRESULT p = Ctl(h, EM_POSFROMCHAR, (WPARAM)b, 0);      /* the break's own position = the end of the row's text */
            out[n].row = r;
            out[n].x = (short)LOWORD(p);
            out[n].y = (short)HIWORD(p);
            out[n].dir = 1;
            if (rtl) {                                              /* the row's end is left of (or at) its start: the text runs right to left */
                LRESULT q = Ctl(h, EM_POSFROMCHAR, (WPARAM)cur, 0);
                if (out[n].x <= (short)LOWORD(q)) out[n].dir = -1;
            }
            n++;
        }
        cur = nxt;
    }
    LocalUnlock(hl);
    return n;
}

/* the blocks, onto `dc` (a DC given by a print request) or straight onto the window (the caret is taken away for the moment: it is drawn by
 * inverting, so a block painted under it would leave the next blink inverting the wrong pixels). clip = only what touches this rectangle */
static void StubDrawAll(HWND h, HDC dc, const StubGeom *g, const Stub *s, int n, const RECT *clip)
{
    HBRUSH br = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    int i, hid = 0;
    HDC own = NULL;
    if (!dc) {
        own = dc = GetDC(h);
        if (!dc) { DeleteObject(br); return; }
        hid = GetFocus() == h && HideCaret(h);
    }
    for (i = 0; i < n; i++) {
        RECT r;
        StubRect(&r, g, s[i].x, s[i].y, s[i].dir);
        if (!IntersectRect(&r, &r, &g->fr)) continue;
        if (clip && !IntersectRect(&r, &r, clip)) continue;
        FillRect(dc, &r, br);
    }
    if (hid) ShowCaret(h);
    if (own) ReleaseDC(h, own);
    DeleteObject(br);
}

/* the control has painted (or was asked to paint into `dc`): put the blocks on top of what it drew */
static void StubPaint(HWND h, HDC dc, const RECT *clip)
{
    StubGeom g;
    Stub cur[STUB_MAX];
    int n;
    if (h != g_edit || !StubHasSel(h) || !StubGeomGet(h, &g)) return;
    n = StubList(h, &g, cur, STUB_MAX);
    if (n) StubDrawAll(h, dc, &g, cur, n, clip);
}

/* the control has handled a message that can change the selection or what is on screen: bring the blocks up to date */
static void StubSync(HWND h)
{
    StubGeom g;
    Stub cur[STUB_MAX];
    int n = 0, i, j, any = 0, vis;

    if (h != g_edit || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) return;      /* (not while redraw is off: that clears WS_VISIBLE) */
    if (!StubHasSel(h) && !g_stub.n) return;                                           /* nothing selected, nothing drawn: the usual case */
    if (!StubGeomGet(h, &g)) { g_stub.n = 0; return; }
    n = StubList(h, &g, cur, STUB_MAX);
    vis = (g.fr.bottom - g.fr.top + g.pitch - 1) / g.pitch;
    for (i = 0, j = 0; i < g_stub.n; i++) {                                            /* blocks that were drawn and are not wanted any more */
        int row = g_stub.row[i];
        RECT r;
        while (j < n && cur[j].row < row) j++;
        if (j < n && cur[j].row == row && cur[j].dir == g_stub.dir[i] && cur[j].x - g.fr.left + g.hoff == g_stub.xd[i]) {
#ifdef SHOTDC
            HDC pdc = GetDC(h);                                                        /* probe: did the control wipe a block that stays? (counted, read with WM_APP + 91) */
            if (pdc) {
                int px = cur[j].dir > 0 ? cur[j].x + 2 : cur[j].x - 3, py = cur[j].y + g.pitch / 2;
                if (px < g.fr.right && GetPixel(pdc, px, py) != GetSysColor(COLOR_HIGHLIGHT)) g_shotWiped++;
                g_shotKept++;
                ReleaseDC(h, pdc);
            }
#endif
            continue;
        }
        if (row < g.first || row >= g.first + vis) continue;                           /* out of view */
        StubRect(&r, &g, g.fr.left - g.hoff + g_stub.xd[i], g.y0 + (row - g.first) * g.pitch, g_stub.dir[i]);
        if (IntersectRect(&r, &r, &g.fr)) { InvalidateRect(h, &r, TRUE); any = 1; }
    }
    if (any) UpdateWindow(h);                                                          /* (the control paints those spots now) */
    if (n) StubDrawAll(h, NULL, &g, cur, n, NULL);
    g_stub.n = n;
    for (i = 0; i < n; i++) { g_stub.row[i] = cur[i].row; g_stub.xd[i] = cur[i].x - g.fr.left + g.hoff; g_stub.dir[i] = cur[i].dir; }
}

static int StubWatch(UINT m, WPARAM w)              /* messages after which the blocks may be out of date (the queries are not: they come by the dozen) */
{
    switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
    case WM_SIZE: case WM_SETFONT: case WM_SETTEXT:
    case EM_SETSEL: case EM_SETRECTNP: case EM_SCROLL: case EM_LINESCROLL: case EM_SCROLLCARET: case EM_REPLACESEL: case EM_UNDO: case EM_SETMARGINS:
        return 1;
    case WM_IME_COMPOSITION: case WM_IME_CHAR: case WM_IME_ENDCOMPOSITION:                /* (text typed through an input method) */
        return 1;
    case WM_MOUSEMOVE:
        return (w & 1) != 0;                        /* dragging a selection */
    }
    return 0;
}

/* ------------------------------------------------ partly visible rows ----- */
/* the stock control draws whole rows only, and rounds its formatting rectangle down to whole rows: below the last whole row is a band (the bottom
 * padding and what is left of the height, a row and a bit at most) where the next row is cut off by blankness instead of by the edge of the editor.
 * like other editors we draw the rows that are partly in view there ourselves, clipped at the bottom of the editor (EDIT_BAND_INSET). what the control
 * counts as in view stays the whole rows: its scroll ranges, its page size and EM_SCROLLCARET are the stock ones.
 *
 * the band is painted in one go: into a bitmap, then one blit over what is there (EditProc keeps the control from erasing it: there is never a moment
 * with just the background in it). it is painted after each paint of the control's own (WM_PAINT, WM_PRINTCLIENT) and right after a message that can
 * change what it shows (BandSync: the control's scrolling only moves the pixels of its formatting rectangle, and the text under the band moves too).
 *
 * the text is drawn with TabbedTextOutW, which is what the control uses for text (same font, the default tab stops, the same language pack for shaped
 * scripts), in up to three pieces (before / inside / after the selection), pixel for pixel like the control (tests\ui T25). what is NOT drawn, and stays
 * blank: a right to left editor's band; rows of arabic / hebrew presentation forms, and rows with right to left letters
 * or bidi marks while any of the row is selected (BandKind); rows too wide for the 16 bits a text width has (like for the blocks above). */
#define BAND_SEL_EXTRA 1                            /* the control's highlight is this many pixels wider than the text it covers */

typedef struct BandGeom {
    RECT fr, band;                                  /* the formatting rectangle (whole rows), the strip below it that is ours */
    RECT top, left;                                 /* the strips above and to the left of it that are ours too (the space around the text is a margin: text scrolled out of the rectangle runs into them) */
    int  first, total, rows;                        /* first visible row, rows in all, whole rows in view */
    int  y0, x0, pitch;                             /* client y of the first visible row, client x where a row starts (scrolled sideways: left of the rectangle), row height */
    int  text;                                      /* 1 = rows below the last whole one exist and can be placed (else only the blank band is kept) */
    int  topText;                                   /* 1 = the row above the first whole one exists and reaches into the top strip */
    int  leftText;                                  /* 1 = the text is scrolled sideways: the rows reach into the left strip */
} BandGeom;

/* the strips around the formatting rectangle (g->fr, g->band, g->top, g->left; they tile the client area together with it, the one on the right
 * exists only for a wrapped text and has nothing in it): FALSE when none of it is ours (no room, or a right to left editor: it stays what the
 * control makes of it) */
static int BandRect(HWND h, BandGeom *g)
{
    RECT cr, f;
    if (EditIsRtl()) return 0;
    GetClientRect(h, &cr);
    Ctl(h, EM_GETRECT, 0, (LPARAM)&f);
    g->fr = f;
    g->band.left = cr.left;
    g->band.right = cr.right;
    g->band.top = f.bottom;
    g->band.bottom = cr.bottom - S(EDIT_BAND_INSET);
    g->top.left = cr.left; g->top.right = cr.right;
    g->top.top = cr.top; g->top.bottom = f.top;
    g->left.left = cr.left; g->left.right = f.left;
    g->left.top = f.top; g->left.bottom = f.bottom;
    return g->band.bottom > g->band.top && g->band.right > g->band.left;
}

static int BandGeomGet(HWND h, BandGeom *g)
{
    int s0, s1;
    LRESULT p0, p1;

    memset(g, 0, sizeof *g);
    if (!BandRect(h, g)) return 0;
    g->total = (int)Ctl(h, EM_GETLINECOUNT, 0, 0);
    g->first = (int)Ctl(h, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (g->first < 0) return 1;
    s0 = (int)Ctl(h, EM_LINEINDEX, (WPARAM)g->first, 0);
    s1 = g->first + 1 < g->total ? (int)Ctl(h, EM_LINEINDEX, (WPARAM)(g->first + 1), 0) : -1;
    if (s0 < 0) return 1;
    p0 = Ctl(h, EM_POSFROMCHAR, (WPARAM)s0, 0);
    p1 = s1 >= 0 ? Ctl(h, EM_POSFROMCHAR, (WPARAM)s1, 0) : -1;   /* (-1 when the next row is an empty last one, or there is none: the pitch falls back to the font's) */
    g->y0 = (short)HIWORD(p0);
    g->x0 = (short)LOWORD(p0);
    g->pitch = (short)HIWORD(p1) - g->y0;
    if (g->pitch <= 0) g->pitch = LineHeight();
    g->rows = (g->fr.bottom - g->y0) / g->pitch;
    if (g->x0 > g->fr.left || g->fr.left - g->x0 > 30000) return 1;                      /* (EM_POSFROMCHAR packs x into 16 bits: a text scrolled further out can't be placed) */
    g->topText = g->first >= 1 && g->top.bottom > g->top.top;
    g->leftText = g->x0 < g->fr.left && g->left.right > g->left.left;
    if (g->rows < 1 || g->first + 1 >= g->total || g->first + g->rows >= g->total) return 1;   /* (the last row is in view: nothing below it) */
    g->text = 1;
    return 1;
}

/* what a character means for a band row: 0 = nothing special. 1 = it can be reordered (hebrew, arabic ..., bidi marks): the row is drawn like the control
 * draws it, but while any of it is selected it stays blank (the highlight of a selection in reordered text is not one rectangle that measuring the text
 * would find). 2 = the presentation forms: plain GDI text output draws them differently from the control, the row stays blank. (shaped scripts that run
 * left to right, thai and the indic scripts, come out the same as the control's up to one level in one colour channel of the text inside a selection.) */
static int BandKind(WCHAR c)
{
    if ((c >= 0xFB1D && c < 0xFE00) || (c >= 0xFE70 && c < 0xFF00)) return 2;
    if ((c >= 0x0590 && c < 0x0900) || (c >= 0x200E && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) return 1;
    return 0;
}

static int BandWidth(HDC dc, const WCHAR *p, int n)  /* how far the text of a row gets (from the row's start: tab stops count from there) */
{
    return n > 0 ? (int)LOWORD(GetTabbedTextExtentW(dc, p, n, g_tabPx > 0, &g_tabPx)) : 0;
}

static int BandText(HDC dc, int x, int y, const WCHAR *p, int n, int x0)    /* one piece of a row; returns where the next piece starts */
{
    return n > 0 ? x + (int)LOWORD(TabbedTextOutW(dc, x, y, p, n, g_tabPx > 0, &g_tabPx, x0)) : x;
}

/* rows r0 .. r1 - 1 (those that exist), cut off at `cut`, into `dc` (a strip's bitmap: client coordinates, origin moved to the strip). sideways they are
 * cut off where the control cuts them off, at the right of its rectangle, and not at the left: the text runs on into the margin there */
static void BandRows(HWND h, HDC dc, const BandGeom *g, int r0, int r1, const RECT *cut)
{
    DWORD ss = 0, se = 0;
    HLOCAL hl;
    const WCHAR *t;
    HGDIOBJ of;
    HBRUSH hb;
    COLORREF hit = GetSysColor(COLOR_HIGHLIGHTTEXT);
    int n, r, s, maxRow;

    hl = (HLOCAL)Ctl(h, EM_GETHANDLE, 0, 0);
    t = hl ? (const WCHAR *)LocalLock(hl) : NULL;
    if (!t) return;
    if (!g_stubW) StubMetrics(h);
    maxRow = 30000 / g_stubMax;
    n = (int)Ctl(h, WM_GETTEXTLENGTH, 0, 0);
    Ctl(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
    hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    of = SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT);
    IntersectClipRect(dc, cut->left, cut->top, g->fr.right < cut->right ? g->fr.right : cut->right, cut->bottom);
    r = r0;
    if (r1 > g->total) r1 = g->total;
    s = r >= 0 && r < g->total ? (int)Ctl(h, EM_LINEINDEX, (WPARAM)r, 0) : -1;
    for (; r < r1 && s >= 0 && s <= n; r++) {
        int y = g->y0 + (r - g->first) * g->pitch, e, rs, re, a, b, i, cost, kind = 0, hard = 0;
        RECT q;
        if (y >= cut->bottom) break;
        e = r + 1 < g->total ? (int)Ctl(h, EM_LINEINDEX, (WPARAM)(r + 1), 0) : n;
        if (e < s || e > n) break;
        rs = s; re = e;
        s = e;
        if (r + 1 < g->total && re - rs >= 2 && t[re - 2] == '\r' && t[re - 1] == '\n') { re -= 2; hard = 1; }     /* (a soft wrap, and the last row, have no line break) */
        if (re - rs > maxRow) continue;
        for (i = rs, cost = 0; i < re && kind < 2; i++) {
            int k = BandKind(t[i]);
            if (k > kind) kind = k;
            cost += t[i] == '\t' ? 8 : 1;                                                   /* (a tab is up to 8 widest characters wide) */
        }
        if (cost > maxRow || kind == 2) continue;
        if (kind == 1 && se > ss && (int)ss < e && (int)se > rs) continue;                  /* (anything of the row selected, its line break too) */
        a = (int)ss < rs ? rs : ((int)ss > re ? re : (int)ss);                              /* the selected part of the row */
        b = (int)se < rs ? rs : ((int)se > re ? re : (int)se);
        q.top = y; q.bottom = y + g->pitch;
        if (b > a) {                                                                        /* the highlight goes under the text */
            q.left = g->x0 + BandWidth(dc, t + rs, a - rs);
            q.right = g->x0 + BandWidth(dc, t + rs, b - rs) + BAND_SEL_EXTRA;
            FillRect(dc, &q, hb);
        }
        if (hard && (int)ss <= re && (int)se > re) {                                        /* the row's line break is selected: a block after its text, like StubDrawAll's */
            q.left = g->x0 + BandWidth(dc, t + rs, re - rs);
            q.right = q.left + g_stubW;
            FillRect(dc, &q, hb);
        }
        {
            int x = g->x0;
            SetTextColor(dc, g_pf.fg);
            x = BandText(dc, x, y, t + rs, a - rs, g->x0);
            SetTextColor(dc, hit);
            x = BandText(dc, x, y, t + a, b - a, g->x0);
            SetTextColor(dc, g_pf.fg);
            BandText(dc, x, y, t + b, re - b, g->x0);
        }
    }
    SelectObject(dc, of);
    DeleteObject(hb);
    LocalUnlock(hl);
}

/* one strip (`area`, and the rows r0 .. r1 - 1 that reach into it when `text`) onto `dc` (a DC handed over by a print request, or the window's own):
 * only what touches `clip` when there is one */
static void BandStrip(HWND h, HDC dc, const BandGeom *g, const RECT *area, int text, int r0, int r1, const RECT *clip)
{
    RECT v = *area;
    HDC mdc;
    HBITMAP bmp;
    HGDIOBJ ob = NULL;

    if (area->right <= area->left || area->bottom <= area->top) return;
    if (clip && !IntersectRect(&v, &v, clip)) return;
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, area->right - area->left, area->bottom - area->top);
    if (mdc && bmp) {
        ob = SelectObject(mdc, bmp);
        SetViewportOrgEx(mdc, -area->left, -area->top, NULL);
        FillRect(mdc, area, EditBrush());
        if (text) BandRows(h, mdc, g, r0, r1, area);
        BitBlt(dc, v.left, v.top, v.right - v.left, v.bottom - v.top, mdc, v.left, v.top, SRCCOPY);
        SelectObject(mdc, ob);
    }
    if (bmp) DeleteObject(bmp);
    if (mdc) DeleteDC(mdc);
}

/* the strips below, above and left of the control's rectangle onto `dc` */
static void BandDraw(HWND h, HDC dc, const BandGeom *g, const RECT *clip)
{
    BandStrip(h, dc, g, &g->band, g->text, g->first + g->rows, g->total, clip);                 /* the rows below the whole ones */
    BandStrip(h, dc, g, &g->top, g->topText, g->first - 1, g->first, clip);                     /* the end of the row above the first whole one */
    BandStrip(h, dc, g, &g->left, g->leftText, g->first, g->first + g->rows, clip);             /* the start of every row, when the text is scrolled sideways */
}

/* the control has painted (or was asked to paint into `dc`): the strips on top. dc NULL = straight onto the window, clip = what was invalid */
static void BandPaint(HWND h, HDC dc, const RECT *clip)
{
    BandGeom g;
    RECT t;
    HDC own = NULL;

    if (h != g_edit || !BandRect(h, &g)) return;
    if (clip && !IntersectRect(&t, &g.band, clip) && !IntersectRect(&t, &g.top, clip) && !IntersectRect(&t, &g.left, clip)) return;   /* (a repaint of some rows: the usual case) */
    if (!BandGeomGet(h, &g)) return;
    if (!dc) {
        own = dc = GetDC(h);
        if (!dc) return;
    }
    BandDraw(h, dc, &g, clip);
    if (own) ReleaseDC(h, own);
}

/* the control has handled a message that can change what the band shows */
static void BandSync(HWND h)
{
    if (h != g_edit || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) return;           /* (not while redraw is off: that clears WS_VISIBLE) */
    BandPaint(h, NULL, NULL);
}

/* WM_ERASEBKGND: the stock erase without the strips (what it would erase is painted over in one go by BandPaint) */
static LRESULT BandErase(HWND h, WPARAM w, LPARAM l)
{
    BandGeom g;
    HDC dc = (HDC)w;
    int saved;
    LRESULT r;

    if (h != g_edit || !BandRect(h, &g)) return Ctl(h, WM_ERASEBKGND, w, l);
    saved = SaveDC(dc);
    ExcludeClipRect(dc, g.band.left, g.band.top, g.band.right, g.band.bottom);
    ExcludeClipRect(dc, g.top.left, g.top.top, g.top.right, g.top.bottom);
    ExcludeClipRect(dc, g.left.left, g.left.top, g.left.right, g.left.bottom);
    r = Ctl(h, WM_ERASEBKGND, w, l);
    RestoreDC(dc, saved);
    return r;
}

#ifdef SHOTDC
/* probe builds only (tools\probe.bat /DSHOTDC): WM_APP + 90 writes what is on the editor's window right now to %TEMP%\mint_dc.ppm (binary ppm,
 * wParam pixels wide, the low word of lParam rows high, from the row in the high word of lParam: 0 = the top left corner). it is read from the window's
 * own DC and nothing is repainted first, so it shows what the control painted straight onto the window (PrintWindow would repaint through WM_PAINT
 * and hide that). a process on another desktop cannot do this itself. a pixel read costs about 20 us: ask for the rows you need, not the whole window. */
API DWORD WINAPI GetTempPathW(DWORD, LPWSTR);

static int ShotInt(char *d, int v)
{
    char t[12];
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) d[i++] = t[--n];
    return i;
}

static void ShotDc(HWND h, int w, int ht, int y0)
{
    WCHAR path[300];
    char hdr[40];
    int n = 0, x, y;
    BYTE *buf;
    HDC dc = GetDC(h);
    HANDLE f;
    DWORD got;
    if (!dc || w < 1 || ht < 1) return;
    hdr[n++] = 'P'; hdr[n++] = '6'; hdr[n++] = '\n';
    n += ShotInt(hdr + n, w); hdr[n++] = ' '; n += ShotInt(hdr + n, ht);
    hdr[n++] = '\n'; hdr[n++] = '2'; hdr[n++] = '5'; hdr[n++] = '5'; hdr[n++] = '\n';
    buf = (BYTE *)mem_alloc((size_t)w * ht * 3);
    if (!buf) { ReleaseDC(h, dc); return; }
    for (y = 0; y < ht; y++)
        for (x = 0; x < w; x++) {
            COLORREF c = GetPixel(dc, x, y0 + y);
            BYTE *p = buf + ((size_t)y * w + x) * 3;
            p[0] = (BYTE)(c & 255); p[1] = (BYTE)((c >> 8) & 255); p[2] = (BYTE)((c >> 16) & 255);
        }
    ReleaseDC(h, dc);
    GetTempPathW(250, path);
    wcat(path, L"mint_dc.ppm", 300);
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, hdr, (DWORD)n, &got, NULL);
        WriteFile(f, buf, (DWORD)((size_t)w * ht * 3), &got, NULL);
        CloseHandle(f);
    }
    mem_free(buf);
}
#endif

/* ------------------------------------------------------------ subclass ---- */
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;

    switch (m) {
#ifdef SHOTDC
    case WM_APP + 90:
        ShotDc(h, (int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFFFF));
        return 0;
    case WM_APP + 91: {                                         /* (wiped << 16 | kept) so far, then back to zero */
        LRESULT v = (LRESULT)((g_shotWiped << 16) | g_shotKept);
        g_shotWiped = g_shotKept = 0;
        return v; }
    case WM_APP + 92: {                                         /* standalone resize erase, captured before the next WM_PAINT can run */
        HDC dc;
        if (!w) return 0x4D494E54;
        dc = GetDC(h);
        if (dc) {
            SendMessageW(h, WM_ERASEBKGND, (WPARAM)dc, 0);
            ReleaseDC(h, dc);
        }
        ShotDc(h, (int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFFFF));
        return 0; }
    case WM_APP + 93: {                                         /* a real invalidated repaint must clear stale pixels as well as draw the text */
        HDC dc = GetDC(h);
        HBRUSH br = CreateSolidBrush(0x00FF00FF);
        RECT cr;
        if (dc && br && GetClientRect(h, &cr)) FillRect(dc, &cr, br);
        if (br) DeleteObject(br);
        if (dc) ReleaseDC(h, dc);
        InvalidateRect(h, NULL, TRUE);
        UpdateWindow(h);
        ShotDc(h, (int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFFFF));
        return 0; }
#endif
    case WM_ERASEBKGND:                                         /* resize can erase now and paint later: leave the text visible until the native painter is ready */
        if (!g_paintDepth) return 0;                            /* keep erasure pending for BeginPaint, which then erases and draws in the same call */
        r = BandErase(h, w, l);
#ifdef FLICKER_PROBE
        Sleep(8);                                               /* calibration only (tools\flicker_test.ps1 must see this): a visible gap between the erase and the text */
#endif
        return r;
    case WM_SIZE: {                                             /* with word wrap on every size change re-wraps ALL the text (45 ms per 3000 lines): a size that is already the control's is skipped, and a new one sets the padded rectangle only (the same function behind EM_SETRECT does the work of WM_SIZE): one wrap */
        static HWND szH;
        static LPARAM szL;
        static DWORD lastAt, lastCost;
        DWORD now = GetTickCount();
        if (h == szH && l == szL) return 0;
        szH = h; szL = l;
        if (lastCost > 25 && now - lastAt < 2 * lastCost) {      /* the last re-wrap was slow and this one follows right on it (a drag): later, once, for the size it ends at (the text keeps its old wrap meanwhile) */
            SetTimer(h, SIZE_TIMER, 40, NULL);
            return 0;
        }
        KillTimer(h, SIZE_TIMER);
        EditPad2(h, 0);
        lastAt = GetTickCount(); lastCost = lastAt - now;
        EditScrollSoon();
        SbarSync(h);
        StubSync(h);
        BandSync(h);
        return 0; }
    case WM_TIMER:
        if (w == SIZE_TIMER) {                                  /* the deferred re-wrap, when the drag paused */
            KillTimer(h, SIZE_TIMER);
            EditPad2(h, 0);
            EditScrollSoon();
            SbarSync(h);
            StubSync(h);
            BandSync(h);
            return 0;
        }
        break;
    case WM_PAINT: {
        RECT ur;
        int have = !w && GetUpdateRect(h, &ur, FALSE);
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        if (w) { StubPaint(h, (HDC)w, NULL); BandPaint(h, (HDC)w, NULL); }      /* (painting into a DC somebody handed over) */
        else if (have) { StubPaint(h, NULL, &ur); BandPaint(h, NULL, &ur); }
        return r; }
    case WM_PRINTCLIENT:                                        /* PrintWindow / WM_PRINT: the whole client area into a DC */
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        StubPaint(h, (HDC)w, NULL);
        BandPaint(h, (HDC)w, NULL);
        return r;
    case WM_PRINT:                                              /* its erase can precede the nested WM_PRINTCLIENT */
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        return r;
    case WM_BARS:
        g_barsPending = 0;
        UpdateBars();
        return 0;
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000) {                /* ctrl+wheel = font size, one step per notch */
            g_wheel += GET_WHEEL_DELTA_WPARAM(w);
            while (g_wheel >= 120)  { g_wheel -= 120; EditZoomStep(1); }
            while (g_wheel <= -120) { g_wheel += 120; EditZoomStep(-1); }
            return 0;
        }
        break;
    case WM_CHAR:
        if (w == 0x7F) { DelWord(-1); return 0; }              /* ctrl+backspace arrives as DEL */
        break;
    case WM_SYSCHAR:                                            /* alt+letter: our menu bar (the default path never reaches the main window) */
        if (w != ' ' && w != VK_BACK && MenuBarMnemonic((WCHAR)w) >= 0) {
            SendMessageW(GetParent(h), WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)w);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (w == VK_DELETE && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            DelWord(1);
            return 0;
        }
        break;
    case WM_CONTEXTMENU: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (x == -1 && y == -1) {                               /* from the keyboard: at the caret */
            POINT p;
            if (!GetCaretPos(&p)) { p.x = 8; p.y = 8; }
            ClientToScreen(h, &p);
            x = p.x; y = p.y + S(20);
        }
        MenuPopup(GetParent(h), &g_mdEditCtx, x, y, 0);
        return 0; }
    }

    r = CallWindowProcW(g_orig, h, m, w, l);
    if (m == WM_SETTEXT) { g_textRev++; g_stub.n = 0; }         /* (a multiline edit sends no EN_CHANGE for WM_SETTEXT; it repaints all of itself: no selected line break block is left) */

    switch (m) {                                                /* anything that can change the room the text needs */
    case WM_SIZE: case WM_SETFONT: case WM_SETTEXT:
        if (m != WM_SETTEXT) EditPad(h);                        /* (the control puts its formatting rectangle back to the client area) */
        EditScrollSoon();
        break;
    }
    if (!g_inBars) switch (m) {                                 /* anything that can scroll the text or change a native bar: the classic bars over them follow at once (they poll too; not while UpdateBars shows the native horizontal bar for a moment to probe it: the overlay must not flash) */
    case WM_KEYDOWN: case WM_CHAR: case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL: case WM_SIZE: case WM_SETTEXT:
    case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER: case WM_SETFONT:
        SbarSync(h);
        break;
    case WM_MOUSEMOVE:
        if (w & 1) SbarSync(h);                                 /* dragging a selection past the edge scrolls */
        break;
    default:
        if (m >= 0x00B0 && m <= 0x00D8) SbarSync(h);            /* EM_* */
        break;
    }
    switch (m) {                                                /* anything that can move the caret / change the text */
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case EM_SETSEL: case EM_REPLACESEL:
        AppUpdateStatus();
        break;
    case WM_MOUSEMOVE:
        if (w & 1) AppUpdateStatus();                           /* dragging a selection */
        break;
    }
    if (StubWatch(m, w)) {
        StubSync(h);                                            /* the blocks for the selected line breaks */
        BandSync(h);                                            /* the partly visible rows at the bottom */
    }
    return r;
}

/* ---------------------------------------------------------------- create -- */
HWND EditCreate(HWND parent)
{
    HWND old = g_edit, e;
    WCHAR *text = NULL;
    DWORD s = 0, en = 0, st = WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    LONG_PTR ex = 0;
    int mod = 0, focus = 0, x = 0, y = 0, w = 0, h = 0;

    if (!g_pf.wrap) st |= WS_HSCROLL | ES_AUTOHSCROLL;
    if (old) {
        RECT rc;
        POINT a, b;
        text = EditGetDocText(NULL);
        if (!text) return NULL;
        SendMessageW(old, EM_GETSEL, (WPARAM)&s, (LPARAM)&en);
        mod = (int)SendMessageW(old, EM_GETMODIFY, 0, 0);
        ex = GetWindowLongPtrW(old, GWL_EXSTYLE) & RTL_BITS;
        focus = (GetFocus() == old);
        GetWindowRect(old, &rc);
        a.x = rc.left; a.y = rc.top; b.x = rc.right; b.y = rc.bottom;
        ScreenToClient(parent, &a);
        ScreenToClient(parent, &b);
        x = a.x; y = a.y; w = b.x - a.x; h = b.y - a.y;
    }

    e = CreateWindowExW((DWORD)ex, L"EDIT", NULL, st, x, y, w, h, parent, (HMENU)(ULONG_PTR)IDC_EDIT, g_hinst, NULL);
    if (!e) { mem_free(text); return NULL; }
    SendMessageW(e, EM_LIMITTEXT, 0, 0);
    /* Populate the hidden native control before committing any global state. */
    if (text && !SetWindowTextW(e, text)) { DestroyWindow(e); mem_free(text); return NULL; }
    g_edit = e;
    g_barsPending = 0;
    SetWindowPos(e, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);        /* (the overhang of the editor goes under the status bar, see Layout) */
    g_stub.n = 0;                                  /* nothing of the old control's selected line break blocks is on this one */
    g_orig = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SendMessageW(e, EM_LIMITTEXT, 0, 0);
    DarkScroll(e);
    SbarTrim(e, S(SBAR_TRIM), S(SBAR_TRIM));         /* (the editor's window overhangs the visible area by that much: thinner bars, see Layout) */
    EditApplyFont();

    if (text) {
        SendMessageW(e, WM_SETREDRAW, FALSE, 0);
        SendMessageW(e, EM_SETSEL, s, en);
        SendMessageW(e, EM_SCROLLCARET, 0, 0);
        SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(e, EM_SETMODIFY, (WPARAM)mod, 0);
        SendMessageW(e, WM_SETREDRAW, TRUE, 0);
        mem_free(text);
    }
    UpdateBars();                                   /* before it is shown: no flash of empty scrollbars */
    EditScrollSoon();
    ShowWindow(e, SW_SHOW);
    SbarSync(e);
    if (old) { SbarDetach(old); DestroyWindow(old); }
    if (focus || !old) SetFocus(e);
    return e;
}
