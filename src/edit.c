/* edit.c - the native EDIT control: (re)creation, subclass, font + zoom, text transfer, caret helpers.
 *
 * the control is a plain multiline EDIT holding utf-16 text with CRLF breaks. everything
 * the stock control can't do is done here around it: ctrl+wheel zoom (font rescale),
 * ctrl+backspace / ctrl+delete word delete, our own context menu, logical line numbers
 * (EM_LINEFROMCHAR counts wrapped rows) via a direct scan of the control's text buffer. */
#include "mp.h"

#define IDC_EDIT 100

HWND g_edit;

static WNDPROC g_orig;
static HFONT   g_font;
static HBRUSH  g_brEdit;
static int     g_wheel;

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
    HLOCAL h;
    int n, idx = CaretIndex(), i;
    const WCHAR *p = TextLock(&h, &n);
    *line = 1; *col = 1;
    if (!p) return;
    if (idx > n) idx = n;
    *line = 1 + (int)mp_count_lf(p, (size_t)idx);
    for (i = idx; i > 0 && p[i - 1] != '\n'; i--) {}
    *col = idx - i + 1;
    LocalUnlock(h);
}

int EditLineCount(void)
{
    HLOCAL h;
    int n, r = 1;
    const WCHAR *p = TextLock(&h, &n);
    if (p) { r = 1 + (int)mp_count_lf(p, (size_t)n); LocalUnlock(h); }
    return r;
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

/* ------------------------------------------------------------ text i/o ---- */
void EditSetDocText(const WCHAR *t)
{
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(g_edit, t ? t : L"");
    SendMessageW(g_edit, EM_SETSEL, 0, 0);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
}

WCHAR *EditGetDocText(int *len)
{
    int n = GetWindowTextLengthW(g_edit), got;
    WCHAR *buf = (WCHAR *)mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!buf) return NULL;
    got = GetWindowTextW(g_edit, buf, n + 1);
    if (got < 0) got = 0;
    buf[got] = 0;
    if (len) *len = got;
    return buf;
}

void EditInsert(const WCHAR *s)
{
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)s);
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
    if (g_edit) {
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)nf, TRUE);
        SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(4), S(4)));
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
static const int g_ladder[] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22, 24, 26, 28, 30, 32, 34, 36,
                                40, 44, 48, 54, 60, 66, 72, 80, 88, 96 };

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
/* 0 blank, 1 word, 2 punctuation */
static int WClass(WCHAR c)
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
            while (a > 0 && WClass(p[a - 1]) == 0) a--;        /* blanks, then one run of a kind */
            if (a > 0 && p[a - 1] != '\n') {
                int k = WClass(p[a - 1]);
                while (a > 0 && p[a - 1] != '\n' && WClass(p[a - 1]) == k) a--;
            }
        }
    } else {
        if (b < n && p[b] == '\r') {
            b++;
            if (b < n && p[b] == '\n') b++;
        } else if (b < n && p[b] == '\n') {
            b++;
        } else {
            if (b < n && WClass(p[b]) != 0) {
                int k = WClass(p[b]);
                while (b < n && p[b] != '\r' && WClass(p[b]) == k) b++;
            }
            while (b < n && WClass(p[b]) == 0) b++;
        }
    }
    LocalUnlock(h);
    if (a != b) {
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    }
}

/* ------------------------------------------------------------ subclass ---- */
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;

    if (m == WM_KEYDOWN || m == WM_CHAR || m == WM_SYSCHAR || m == WM_SYSKEYDOWN || m == WM_KEYUP) DBG(L"edit", m, w);
    switch (m) {
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
    return r;
}

/* ---------------------------------------------------------------- create -- */
HWND EditCreate(HWND parent)
{
    HWND old = g_edit, e;
    WCHAR *text = NULL;
    DWORD s = 0, en = 0, st = WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    LONG_PTR ex = 0;
    int mod = 0, focus = 0, x = 0, y = 0, w = 0, h = 0;

    if (!g_pf.wrap) st |= WS_HSCROLL | ES_AUTOHSCROLL;
    if (old) {
        RECT rc;
        POINT a, b;
        text = EditGetDocText(NULL);
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
    g_edit = e;
    g_orig = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SendMessageW(e, EM_LIMITTEXT, 0, 0);
    DarkScroll(e);
    EditApplyFont();

    if (text) {
        SendMessageW(e, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(e, text);
        SendMessageW(e, EM_SETSEL, s, en);
        SendMessageW(e, EM_SCROLLCARET, 0, 0);
        SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(e, EM_SETMODIFY, (WPARAM)mod, 0);
        SendMessageW(e, WM_SETREDRAW, TRUE, 0);
        mem_free(text);
    }
    ShowWindow(e, SW_SHOW);
    if (old) DestroyWindow(old);
    if (focus || !old) SetFocus(e);
    return e;
}
