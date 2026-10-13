#include "edit_internal.h"

#define IDC_EDIT 100
#define SIZE_TIMER 0x4D50

HWND g_edit;

static WNDPROC g_orig;
static HFONT   g_font;
static HBRUSH  g_brEdit;
static int     g_wheel;
static int     g_paintDepth;
static int     g_shiftDir;
static int     g_stubW, g_stubMax;

#define WM_BARS (WM_APP + 7)
static int g_barsPending, g_inBars;
static int g_revealPending, g_sizeDeferred;
static int g_allowTallReveal, g_revealing, g_tallCaret;

void EditScrollSoon(void) {
    if (g_edit && !g_barsPending && !g_inBars) { g_barsPending = 1; PostMessageW(g_edit, WM_BARS, 0, 0); }
}

static int LineHeight(void) {
    static int lh;
    static HFONT lf;
    HDC dc;
    HGDIOBJ of;
    TEXTMETRICW tm;
    if (lh && lf == g_font) return lh;
    dc = GetDC(g_edit);
    if (!dc) return 1;
    memset(&tm, 0, sizeof tm);
    of = SelectObject(dc, g_font);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_edit, dc);
    lf = g_font;
    return lh = tm.tmHeight + tm.tmExternalLeading > 0 ? tm.tmHeight + tm.tmExternalLeading : 1;
}

static void FitTallCaret(HWND h) {
    if (!g_allowTallReveal || g_inBars || g_revealing) return;
    g_revealing = 1;
    EditCaretFit(h, g_font, g_orig, &g_tallCaret);
    g_revealing = 0;
}

static void RevealCaret(HWND h) {
    if (g_revealPending && !g_inBars && !g_sizeDeferred) {
        SendMessageW(h, EM_SCROLLCARET, 0, 0);
    }
}

static int VNeeded(void) {
    RECT r;
    int lines = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0), vis;
    SendMessageW(g_edit, EM_GETRECT, 0, (LPARAM)&r);
    vis = (r.bottom - r.top) / LineHeight();
    if (vis < 1) vis = 1;
    return lines > vis;
}

static int HNeeded(void) {
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE;
    if (!GetScrollInfo(g_edit, SB_HORZ, &si)) return 0;
    DBG(L"bar h range/page", si.nMax - si.nMin + 1, si.nPage);
    return si.nMax - si.nMin + 1 > (int)si.nPage;
}

#define BARS(e) (GetWindowLongPtrW(e, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL))

static void UpdateBars(void) {
    int pass;
    LONG_PTR before;
    if (!g_edit) return;
    before = BARS(g_edit);
    g_inBars = 1;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    for (pass = 0; pass < 4; pass++) {
        int changed = 0, v = VNeeded(), hadV = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_VSCROLL) != 0;
        if (v != hadV) {
            if (!v) {
                int first = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
                if (first) SendMessageW(g_edit, EM_LINESCROLL, 0, (LPARAM)-first);
            }
            ShowScrollBar(g_edit, SB_VERT, v);
            changed = 1;
        }
        if (!g_pf.wrap) {
            int hadH = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_HSCROLL) != 0, h;
            if (!hadH) ShowScrollBar(g_edit, SB_HORZ, TRUE);
            h = HNeeded();
            DBG(L"bars pass v/h", v, h);
            if (!h) {
                SendMessageW(g_edit, EM_LINESCROLL, (WPARAM)-100000, 0);
                ShowScrollBar(g_edit, SB_HORZ, FALSE);
            }
            if (h != hadH) changed = 1;
        }
        if (!changed) break;
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    if (BARS(g_edit) != before) RedrawWindow(g_edit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    g_inBars = 0;
    RevealCaret(g_edit);
    if (!g_sizeDeferred) g_revealPending = 0;
    SbarSync(g_edit);
}

static int CALLBACK FoundCb(const void *lf, const void *tm, DWORD type, LPARAM lp) {
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static BOOL FontInstalled(const WCHAR *face) {
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

void FontResolve(WCHAR *face) {
    static const WCHAR *const fb[] = { L"Consolas", L"Lucida Console", L"Courier New" };
    int i;
    if (face[0] && FontInstalled(face)) return;
    for (i = 0; i < COUNTOF(fb); i++) {
        if (FontInstalled(fb[i])) { wcopy(face, fb[i], 32); return; }
    }
    wcopy(face, L"Courier New", 32);
}

void EditViewRect(HWND h, RECT *r) {
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    GetClientRect(h, r);
    if (!(st & WS_VSCROLL)) {
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_LEFTSCROLLBAR) r->left += S(SBAR_TRIM);
        else r->right -= S(SBAR_TRIM);
    }
    if (!(st & WS_HSCROLL)) r->bottom -= S(SBAR_TRIM);
}

static void EditPad2(HWND h, int margins) {
    RECT r;
    int pad = S(EDIT_PAD), top = S(EDIT_PAD_TOP), height = LineHeight(), visibleBottom;
    if (margins) SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    EditViewRect(h, &r);
    visibleBottom = r.bottom;
    if (r.bottom - r.top > top + pad + 8 && r.right - r.left > 2 * pad + 8) {
        r.left += pad;
        if (g_pf.wrap || EditIsRtl()) r.right -= pad;
        r.top += top; r.bottom -= pad;
    }
    if (r.right <= r.left) r.right = r.left + 1;
    if (r.bottom <= r.top) r.bottom = r.top + 1;
    if (r.bottom - r.top < height) {
        r.top = 0;
        r.bottom = visibleBottom > height ? visibleBottom : height;
    }
    SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&r);
}

static void EditPad(HWND h) { EditPad2(h, 1); }

static int g_tabPx;

void EditApplyTabs(void) {
    HDC dc;
    HGDIOBJ of;
    TEXTMETRICW tm;
    UINT u = 4u * (UINT)g_pf.tab;
    if (!g_edit || !g_font) return;
    g_allowTallReveal = 1;
    EditCaretInvalidate();
    KillTimer(g_edit, SIZE_TIMER);
    g_sizeDeferred = 0;
    dc = GetDC(g_edit);
    of = SelectObject(dc, g_font);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_edit, dc);
    g_tabPx = g_pf.tab * tm.tmAveCharWidth;
    SendMessageW(g_edit, EM_SETTABSTOPS, 1, (LPARAM)&u);
    EditPad(g_edit);
    g_revealPending = 1;
    RevealCaret(g_edit);
    InvalidateRect(g_edit, NULL, TRUE);
    EditScrollSoon();
}

void EditApplyFont(void) {
    LOGFONTW lf;
    HFONT nf, old = g_font;
    int px = MulDiv(g_pf.cur, g_dpi, 720);
    if (px < 1) px = 1;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -px;
    lf.lfWeight = g_pf.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)(g_pf.italic ? 1 : 0);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcopy(lf.lfFaceName, g_pf.font, 32);
    nf = CreateFontIndirectW(&lf);
    if (!nf) return;
    if (g_edit) EditCaretRemember(g_edit, g_font);
    g_font = nf;
    g_stubW = 0;
    if (g_edit) {
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)nf, TRUE);
        EditApplyTabs();
    }
    if (old) DeleteObject(old);
}

HBRUSH EditBrush(void) {
    if (!g_brEdit) g_brEdit = CreateSolidBrush(C_EDIT_BG);
    return g_brEdit;
}

void EditApplyColors(void) {
    HBRUSH old = g_brEdit;
    g_brEdit = CreateSolidBrush(C_EDIT_BG);
    if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
    if (old) DeleteObject(old);
}

static const int g_ladder[] = { 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 220, 240, 260, 280, 300, 320, 340, 360,
                                400, 440, 480 };

static int Ladder(int cur, int dir) {
    int i;
    if (dir > 0) {
        for (i = 0; i < COUNTOF(g_ladder); i++) if (g_ladder[i] > cur) return g_ladder[i];
    } else {
        for (i = COUNTOF(g_ladder) - 1; i >= 0; i--) if (g_ladder[i] < cur) return g_ladder[i];
    }
    return cur;
}

BOOL EditZoomCan(int dir) {
    return Ladder(g_pf.cur, dir) != g_pf.cur;
}

void EditZoomStep(int dir) {
    int n = Ladder(g_pf.cur, dir);
    if (n == g_pf.cur) return;
    g_pf.cur = n;
    EditApplyFont();
    AppUpdateStatus();
}

void EditZoomReset(void) {
    if (g_pf.cur == g_pf.pt) return;
    g_pf.cur = g_pf.pt;
    EditApplyFont();
    AppUpdateStatus();
}

#define RTL_BITS (WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR)

static void StubSync(HWND h);
static void BandSync(HWND h);

BOOL EditIsRtl(void) {
    return (GetWindowLongPtrW(g_edit, GWL_EXSTYLE) & WS_EX_RTLREADING) != 0;
}

BOOL EditSetRtl(BOOL rtl) {
    LONG_PTR ex;
    if (!g_edit || !rtl == !EditIsRtl()) return FALSE;
    ex = GetWindowLongPtrW(g_edit, GWL_EXSTYLE);
    EditCaretInvalidate();
    SetWindowLongPtrW(g_edit, GWL_EXSTYLE, rtl ? (ex | RTL_BITS) : (ex & ~(LONG_PTR)RTL_BITS));
    SetWindowPos(g_edit, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    EditPad(g_edit);
    g_revealPending = 1;
    RevealCaret(g_edit);
    InvalidateRect(g_edit, NULL, TRUE);
    EditScrollSoon();
    StubSync(g_edit);
    BandSync(g_edit);
    return TRUE;
}

void EditDirKeyCancel(void) { g_shiftDir = 0; }

int EditStrongDir(int limit) {
    void *lock;
    const WCHAR *t;
    WORD ty[64];
    int n, i, j, k, dir = 0;
    t = EditLockText(&lock, &n);
    if (!t) return 0;
    if (n > limit) n = limit;
    for (i = 0; i < n && !dir; i += k) {
        k = n - i < COUNTOF(ty) ? n - i : COUNTOF(ty);
        if (!GetStringTypeW(2, t + i, k, ty)) break;
        for (j = 0; j < k; j++) if (ty[j] == 1 || ty[j] == 2) { dir = ty[j]; break; }
    }
    EditUnlockText(lock);
    return dir;
}

#define STUB_MAX 512
#ifdef SHOTDC
static int g_shotWiped, g_shotKept;
#endif

typedef struct StubGeom {
    RECT fr;
    int  first, total;
    int  y0, pitch, hoff;
    int  w;
    int  maxRow;
} StubGeom;

typedef struct Stub { int row, x, y, dir; } Stub;

static struct { int n, row[STUB_MAX], xd[STUB_MAX], dir[STUB_MAX]; } g_stub;

static LRESULT Ctl(HWND h, UINT m, WPARAM w, LPARAM l) {
    return CallWindowProcW(g_orig, h, m, w, l);
}

static int StubHasSel(HWND h) {
    DWORD s = 0, e = 0;
    Ctl(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    return e > s;
}

static void StubMetrics(HWND h) {
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

static int StubGeomGet(HWND h, StubGeom *g) {
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

static void StubRect(RECT *r, const StubGeom *g, int x, int y, int dir) {
    r->left = dir > 0 ? x : x - g->w;
    r->right = dir > 0 ? x + g->w : x;
    r->top = y;
    r->bottom = y + g->pitch;
}

static int StubList(HWND h, const StubGeom *g, Stub *out, int cap) {
    DWORD s = 0, e = 0;
    HLOCAL hl;
    const WCHAR *t;
    int n = 0, r, last, cur, nxt, rtl = EditIsRtl();
    Ctl(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s) return 0;
    hl = (HLOCAL)Ctl(h, EM_GETHANDLE, 0, 0);
    t = hl ? (const WCHAR *)LocalLock(hl) : NULL;
    if (!t) return 0;
    last = g->first + (g->fr.bottom - g->fr.top + g->pitch - 1) / g->pitch - 1;
    if (last > g->total - 2) last = g->total - 2;
    cur = (int)Ctl(h, EM_LINEINDEX, (WPARAM)g->first, 0);
    for (r = g->first; r <= last && n < cap; r++) {
        int b;
        nxt = (int)Ctl(h, EM_LINEINDEX, (WPARAM)(r + 1), 0);
        if (nxt < 2) break;
        b = nxt - 2;
        if (b >= cur && b - cur <= g->maxRow && t[b] == '\r' && t[b + 1] == '\n' && (int)s <= b && (int)e > b) {
            LRESULT p = Ctl(h, EM_POSFROMCHAR, (WPARAM)b, 0);
            out[n].row = r;
            out[n].x = (short)LOWORD(p);
            out[n].y = (short)HIWORD(p);
            out[n].dir = 1;
            if (rtl) {
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

static void StubDrawAll(HWND h, HDC dc, const StubGeom *g, const Stub *s, int n, const RECT *clip) {
    int i, hid = 0;
    HDC own = NULL;
    if (!dc) {
        own = dc = GetDC(h);
        if (!dc) return;
        hid = GetFocus() == h && HideCaret(h);
    }
    for (i = 0; i < n; i++) {
        RECT r;
        StubRect(&r, g, s[i].x, s[i].y, s[i].dir);
        if (!IntersectRect(&r, &r, &g->fr)) continue;
        if (clip && !IntersectRect(&r, &r, clip)) continue;
        FillRect(dc, &r, (HBRUSH)(COLOR_HIGHLIGHT + 1));
    }
    if (hid) ShowCaret(h);
    if (own) ReleaseDC(h, own);
}

static void StubPaint(HWND h, HDC dc, const RECT *clip) {
    StubGeom g;
    Stub cur[STUB_MAX];
    int n;
    if (h != g_edit || !StubHasSel(h) || !StubGeomGet(h, &g)) return;
    n = StubList(h, &g, cur, STUB_MAX);
    if (n) StubDrawAll(h, dc, &g, cur, n, clip);
}

static void StubSync(HWND h) {
    StubGeom g;
    Stub cur[STUB_MAX];
    int n = 0, i, j, any = 0, vis;
    if (h != g_edit || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) return;
    if (!StubHasSel(h) && !g_stub.n) return;
    if (!StubGeomGet(h, &g)) { g_stub.n = 0; return; }
    n = StubList(h, &g, cur, STUB_MAX);
    vis = (g.fr.bottom - g.fr.top + g.pitch - 1) / g.pitch;
    for (i = 0, j = 0; i < g_stub.n; i++) {
        int row = g_stub.row[i];
        RECT r;
        while (j < n && cur[j].row < row) j++;
        if (j < n && cur[j].row == row && cur[j].dir == g_stub.dir[i] && cur[j].x - g.fr.left + g.hoff == g_stub.xd[i]) {
#ifdef SHOTDC
            HDC pdc = GetDC(h);
            if (pdc) {
                int px = cur[j].dir > 0 ? cur[j].x + 2 : cur[j].x - 3, py = cur[j].y + g.pitch / 2;
                if (px < g.fr.right && GetPixel(pdc, px, py) != GetSysColor(COLOR_HIGHLIGHT)) g_shotWiped++;
                g_shotKept++;
                ReleaseDC(h, pdc);
            }
#endif
            continue;
        }
        if (row < g.first || row >= g.first + vis) continue;
        StubRect(&r, &g, g.fr.left - g.hoff + g_stub.xd[i], g.y0 + (row - g.first) * g.pitch, g_stub.dir[i]);
        if (IntersectRect(&r, &r, &g.fr)) { InvalidateRect(h, &r, TRUE); any = 1; }
    }
    if (any) UpdateWindow(h);
    if (n) StubDrawAll(h, NULL, &g, cur, n, NULL);
    g_stub.n = n;
    for (i = 0; i < n; i++) { g_stub.row[i] = cur[i].row; g_stub.xd[i] = cur[i].x - g.fr.left + g.hoff; g_stub.dir[i] = cur[i].dir; }
}

static int StubWatch(UINT m, WPARAM w) {
    switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
    case WM_SETFONT: case WM_SETTEXT:
    case EM_SETSEL: case EM_SETRECTNP: case EM_SCROLL: case EM_LINESCROLL: case EM_SCROLLCARET: case EM_REPLACESEL: case EM_UNDO: case EM_SETMARGINS:
        return 1;
    case WM_IME_COMPOSITION: case WM_IME_CHAR: case WM_IME_ENDCOMPOSITION:
        return 1;
    case WM_MOUSEMOVE:
        return (w & 1) != 0;
    }
    return 0;
}

#define BAND_SEL_EXTRA 1

typedef struct BandGeom {
    RECT fr, band;
    RECT top, left;
    int  first, total, rows;
    int  y0, x0, pitch;
    int  text;
    int  topText;
    int  leftText;
} BandGeom;

static int BandRect(HWND h, BandGeom *g) {
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

static int BandGeomGet(HWND h, BandGeom *g) {
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
    p1 = s1 >= 0 ? Ctl(h, EM_POSFROMCHAR, (WPARAM)s1, 0) : -1;
    g->y0 = (short)HIWORD(p0);
    g->x0 = (short)LOWORD(p0);
    g->pitch = (short)HIWORD(p1) - g->y0;
    if (g->pitch <= 0) g->pitch = LineHeight();
    g->rows = (g->fr.bottom - g->y0) / g->pitch;
    if (g->x0 > g->fr.left || g->fr.left - g->x0 > 30000) return 1;
    g->topText = g->first >= 1 && g->top.bottom > g->top.top;
    g->leftText = g->x0 < g->fr.left && g->left.right > g->left.left;
    if (g->rows < 1 || g->first + 1 >= g->total || g->first + g->rows >= g->total) return 1;
    g->text = 1;
    return 1;
}

static int BandKind(WCHAR c) {
    if ((c >= 0xFB1D && c < 0xFE00) || (c >= 0xFE70 && c < 0xFF00)) return 2;
    if ((c >= 0x0590 && c < 0x0900) || (c >= 0x200E && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) return 1;
    return 0;
}

static int BandWidth(HDC dc, const WCHAR *p, int n) {
    return n > 0 ? (int)LOWORD(GetTabbedTextExtentW(dc, p, n, g_tabPx > 0, &g_tabPx)) : 0;
}

static int BandText(HDC dc, int x, int y, const WCHAR *p, int n, int x0) {
    return n > 0 ? x + (int)LOWORD(TabbedTextOutW(dc, x, y, p, n, g_tabPx > 0, &g_tabPx, x0)) : x;
}

static void BandRows(HWND h, HDC dc, const BandGeom *g, int r0, int r1, const RECT *cut) {
    DWORD ss = 0, se = 0;
    HLOCAL hl;
    const WCHAR *t;
    HGDIOBJ of;
    COLORREF hit = GetSysColor(COLOR_HIGHLIGHTTEXT);
    int n, r, s, maxRow;
    hl = (HLOCAL)Ctl(h, EM_GETHANDLE, 0, 0);
    t = hl ? (const WCHAR *)LocalLock(hl) : NULL;
    if (!t) return;
    if (!g_stubW) StubMetrics(h);
    maxRow = 30000 / g_stubMax;
    n = (int)Ctl(h, WM_GETTEXTLENGTH, 0, 0);
    Ctl(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
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
        if (r + 1 < g->total && re - rs >= 2 && t[re - 2] == '\r' && t[re - 1] == '\n') { re -= 2; hard = 1; }
        if (re - rs > maxRow) continue;
        for (i = rs, cost = 0; i < re && kind < 2; i++) {
            int k = BandKind(t[i]);
            if (k > kind) kind = k;
            cost += t[i] == '\t' ? 8 : 1;
        }
        if (cost > maxRow || kind == 2) continue;
        if (kind == 1 && se > ss && (int)ss < e && (int)se > rs) continue;
        a = (int)ss < rs ? rs : ((int)ss > re ? re : (int)ss);
        b = (int)se < rs ? rs : ((int)se > re ? re : (int)se);
        q.top = y; q.bottom = y + g->pitch;
        if (b > a) {
            q.left = g->x0 + BandWidth(dc, t + rs, a - rs);
            q.right = g->x0 + BandWidth(dc, t + rs, b - rs) + BAND_SEL_EXTRA;
            FillRect(dc, &q, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        if (hard && (int)ss <= re && (int)se > re) {
            q.left = g->x0 + BandWidth(dc, t + rs, re - rs);
            q.right = q.left + g_stubW;
            FillRect(dc, &q, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        {
            int x = g->x0;
            SetTextColor(dc, C_EDIT_FG);
            x = BandText(dc, x, y, t + rs, a - rs, g->x0);
            SetTextColor(dc, hit);
            x = BandText(dc, x, y, t + a, b - a, g->x0);
            SetTextColor(dc, C_EDIT_FG);
            BandText(dc, x, y, t + b, re - b, g->x0);
        }
    }
    SelectObject(dc, of);
    LocalUnlock(hl);
}

static void BandStrip(HWND h, HDC dc, const BandGeom *g, const RECT *area, int text, int r0, int r1, const RECT *clip) {
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

static void BandDraw(HWND h, HDC dc, const BandGeom *g, const RECT *clip) {
    BandStrip(h, dc, g, &g->band, g->text, g->first + g->rows, g->total, clip);
    BandStrip(h, dc, g, &g->top, g->topText, g->first - 1, g->first, clip);
    BandStrip(h, dc, g, &g->left, g->leftText, g->first, g->first + g->rows, clip);
}

static void BandPaint(HWND h, HDC dc, const RECT *clip) {
    BandGeom g;
    RECT t;
    HDC own = NULL;
    if (h != g_edit || !BandRect(h, &g)) return;
    if (clip && !IntersectRect(&t, &g.band, clip) && !IntersectRect(&t, &g.top, clip) && !IntersectRect(&t, &g.left, clip)) return;
    if (!BandGeomGet(h, &g)) return;
    if (!dc) {
        own = dc = GetDC(h);
        if (!dc) return;
    }
    BandDraw(h, dc, &g, clip);
    if (own) ReleaseDC(h, own);
}

static void BandSync(HWND h) {
    if (h != g_edit || !(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) return;
    BandPaint(h, NULL, NULL);
}

static LRESULT BandErase(HWND h, WPARAM w, LPARAM l) {
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

static int EditPaintBuffered(HWND h, const RECT *dirty) {
    RECT cr;
    PAINTSTRUCT ps;
    HDC own = NULL, mdc = NULL, dc;
    HBITMAP bmp = NULL;
    HGDIOBJ ob = NULL;
    int saved = 0, depth, done = 0, copied = 0;
    if (!GetClientRect(h, &cr) || cr.right <= 0 || cr.bottom <= 0) return 0;
    own = GetDC(h);
    if (!own) return 0;
    mdc = CreateCompatibleDC(own);
    if (!mdc) goto cleanup;
    bmp = CreateCompatibleBitmap(own, cr.right, cr.bottom);
    if (!bmp) goto cleanup;
    ob = SelectObject(mdc, bmp);
    if (!ob) goto cleanup;
    IntersectClipRect(mdc, dirty->left, dirty->top, dirty->right, dirty->bottom);
    saved = SaveDC(mdc);
    if (!saved || !FillRect(mdc, &cr, EditBrush())) goto cleanup;
    ReleaseDC(h, own);
    own = NULL;
    memset(&ps, 0, sizeof ps);
    depth = g_paintDepth;
    g_paintDepth = 0;
    dc = BeginPaint(h, &ps);
    g_paintDepth = depth;
    if (!dc) {
        EndPaint(h, &ps);
        InvalidateRect(h, dirty, TRUE);
        goto cleanup;
    }
    g_paintDepth++;
    SendMessageW(h, WM_ERASEBKGND, (WPARAM)mdc, 0);
    Ctl(h, WM_PRINTCLIENT, (WPARAM)mdc, PRF_CLIENT | PRF_ERASEBKGND);
    g_paintDepth--;
    if (RestoreDC(mdc, saved)) {
        StubPaint(h, mdc, NULL);
        BandPaint(h, mdc, NULL);
        copied = BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, mdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    }
    saved = 0;
    if (!copied) {
        int state = SaveDC(dc);
        g_paintDepth++;
        SendMessageW(h, WM_ERASEBKGND, (WPARAM)dc, 0);
        Ctl(h, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT | PRF_ERASEBKGND);
        g_paintDepth--;
        if (state) RestoreDC(dc, state);
        StubPaint(h, dc, &ps.rcPaint);
        BandPaint(h, dc, &ps.rcPaint);
    }
    EndPaint(h, &ps);
    done = 1;
cleanup:
    if (saved) RestoreDC(mdc, saved);
    if (ob) SelectObject(mdc, ob);
    if (mdc) DeleteDC(mdc);
    if (bmp) DeleteObject(bmp);
    if (own) ReleaseDC(h, own);
    return done;
}

#ifdef SHOTDC
API DWORD WINAPI GetTempPathW(DWORD, LPWSTR);
static int g_shotPaintW, g_shotPaintH, g_shotPaintY, g_shotPaintHit;

static int ShotInt(char *d, int v) {
    char t[12];
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) d[i++] = t[--n];
    return i;
}

static void ShotDc(HWND h, int w, int ht, int y0) {
    WCHAR path[300];
    char hdr[40];
    int n = 0, x, y;
    BYTE *buf;
    HDC dc;
    HANDLE f;
    DWORD got;
    BOOL caretHidden;
    if (w < 1 || ht < 1) return;
    dc = GetDC(h);
    if (!dc) return;
    hdr[n++] = 'P'; hdr[n++] = '6'; hdr[n++] = '\n';
    n += ShotInt(hdr + n, w); hdr[n++] = ' '; n += ShotInt(hdr + n, ht);
    hdr[n++] = '\n'; hdr[n++] = '2'; hdr[n++] = '5'; hdr[n++] = '5'; hdr[n++] = '\n';
    buf = (BYTE *)mem_alloc((size_t)w * ht * 3);
    if (!buf) { ReleaseDC(h, dc); return; }
    caretHidden = HideCaret(h);
    for (y = 0; y < ht; y++) for (x = 0; x < w; x++) {
            COLORREF c = GetPixel(dc, x, y0 + y);
            BYTE *p = buf + ((size_t)y * w + x) * 3;
            p[0] = (BYTE)(c & 255); p[1] = (BYTE)((c >> 8) & 255); p[2] = (BYTE)((c >> 16) & 255);
        }
    if (caretHidden) ShowCaret(h);
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

static int CaretMoveMessage(UINT m, WPARAM w) {
    switch (m) {
    case WM_KEYDOWN: case WM_CHAR: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO: case WM_TIMER:
    case EM_SETSEL: case EM_REPLACESEL: case EM_UNDO:
        return 1;
    case WM_MOUSEMOVE:
        return (w & 1) != 0;
    }
    return 0;
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LRESULT r;
    int anchor = -1;
    int wasRevealing;
    if (CaretMoveMessage(m, w)) {
        DWORD ss = 0, se = 0;
        int active = EditCaretIndex();
        SendMessageW(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
        anchor = active == (int)ss ? (int)se : (int)ss;
        if (m != WM_TIMER) g_allowTallReveal = 1;
    }
    if (m == EM_SCROLLCARET) g_allowTallReveal = 1;
    switch (m) {
#ifdef SHOTDC
    case WM_APP + 90:
        ShotDc(h, (int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFFFF));
        return 0;
    case WM_APP + 97:
        return w ? ShowCaret(h) : HideCaret(h);
    case WM_APP + 91: {
        LRESULT v = (LRESULT)((g_shotWiped << 16) | g_shotKept);
        g_shotWiped = g_shotKept = 0;
        return v; }
    case WM_APP + 92: {
        HDC dc;
        if (!w) return PROBE_ID;
        dc = GetDC(h);
        if (dc) {
            SendMessageW(h, WM_ERASEBKGND, (WPARAM)dc, 0);
            ReleaseDC(h, dc);
        }
        ShotDc(h, (int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFFFF));
        return 0; }
    case WM_APP + 93: {
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
    case WM_APP + 94:
        if (!w) return PROBE_ID;
        g_shotPaintW = (int)w;
        g_shotPaintH = (int)(l & 0xFFFF);
        g_shotPaintY = (int)((l >> 16) & 0xFFFF);
        g_shotPaintHit = 0;
        InvalidateRect(h, NULL, TRUE);
        UpdateWindow(h);
        g_shotPaintW = 0;
        return g_shotPaintHit;
    case WM_APP + 95: {
        RECT dirty = { 17, 19, 157, 51 };
        HDC dc;
        int ht = (int)(l & 0xFFFF), y0 = (int)((l >> 16) & 0xFFFF);
        if (!w) return PROBE_ID;
        dc = GetDC(h);
        if (dc) {
            SetPixelV(dc, (int)w - 4, y0 + ht - 4, 0x00FF00FF);
            ReleaseDC(h, dc);
        }
        InvalidateRect(h, &dirty, TRUE);
        UpdateWindow(h);
        ShotDc(h, (int)w, ht, y0);
        return 0; }
    case WM_APP + 96: {
        DWORD ss = 0, se = 0, now, elapsed;
        int n, i, a, b, line, col;
        if (!w) return PROBE_ID;
        n = GetWindowTextLengthW(h);
        b = n > 16 ? n - 16 : n;
        a = b > (int)l ? b - (int)l : 0;
        Ctl(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
        SendMessageW(h, WM_SETREDRAW, FALSE, 0);
        Ctl(h, EM_SETSEL, (WPARAM)a, (LPARAM)a);
        EditCaretPos(&line, &col);
        now = GetTickCount();
        for (i = 0; i < (int)w; i++) {
            int idx = (i & 1) ? a : b;
            Ctl(h, EM_SETSEL, (WPARAM)idx, (LPARAM)idx);
            EditCaretPos(&line, &col);
        }
        elapsed = GetTickCount() - now;
        Ctl(h, EM_SETSEL, ss, se);
        SendMessageW(h, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(h, NULL, TRUE);
        AppUpdateStatus();
        return (LRESULT)elapsed; }
#endif
    case WM_ERASEBKGND:
        if (!g_paintDepth) return 0;
        r = BandErase(h, w, l);
#ifdef SHOTDC
        if (g_shotPaintW && !g_shotPaintHit) {
            g_shotPaintHit = 1;
            ShotDc(h, g_shotPaintW, g_shotPaintH, g_shotPaintY);
        }
#endif
#ifdef FLICKER_PROBE
        Sleep(8);
#endif
        return r;
    case WM_SIZE: {
        static HWND szH;
        static LPARAM szL;
        static DWORD lastAt, lastCost;
        DWORD now = GetTickCount();
        if (h == szH && l == szL) return 0;
        szH = h; szL = l;
        EditCaretRemember(h, g_font);
        if (!g_inBars) { g_revealPending = 1; g_allowTallReveal = 1; }
        if (!g_inBars && lastCost > 25 && now - lastAt < 2 * lastCost && EditCaretInView(h, LineHeight())) {
            g_sizeDeferred = 1;
            SetTimer(h, SIZE_TIMER, 40, NULL);
            return 0;
        }
        KillTimer(h, SIZE_TIMER);
        g_sizeDeferred = 0;
        EditPad2(h, 0);
        lastAt = GetTickCount(); lastCost = lastAt - now;
        RevealCaret(h);
        EditScrollSoon();
        SbarSync(h);
        StubSync(h);
        BandSync(h);
        return 0; }
    case WM_TIMER:
        if (w == SIZE_TIMER) {
            KillTimer(h, SIZE_TIMER);
            g_sizeDeferred = 0;
            EditPad2(h, 0);
            RevealCaret(h);
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
        if (have && EditPaintBuffered(h, &ur)) { FitTallCaret(h); return 0; }
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        if (w) { StubPaint(h, (HDC)w, NULL); BandPaint(h, (HDC)w, NULL); }
        else if (have) { StubPaint(h, NULL, &ur); BandPaint(h, NULL, &ur); }
        FitTallCaret(h);
        return r; }
    case WM_PRINTCLIENT:
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        StubPaint(h, (HDC)w, NULL);
        BandPaint(h, (HDC)w, NULL);
        return r;
    case WM_PRINT:
        g_paintDepth++;
        r = CallWindowProcW(g_orig, h, m, w, l);
        g_paintDepth--;
        return r;
    case WM_BARS:
        g_barsPending = 0;
        UpdateBars();
        if (g_tallCaret) FitTallCaret(h);
        return 0;
    case WM_MOUSEWHEEL:
        g_shiftDir = 0;
        if (w & MK_CONTROL) {
            g_wheel += GET_WHEEL_DELTA_WPARAM(w);
            while (g_wheel >= 120)  { g_wheel -= 120; EditZoomStep(1); }
            while (g_wheel <= -120) { g_wheel += 120; EditZoomStep(-1); }
            return 0;
        }
        g_revealPending = 0;
        g_allowTallReveal = 0;
        break;
    case WM_VSCROLL: case WM_HSCROLL:
        if (!g_inBars && !g_revealing) { g_revealPending = 0; g_allowTallReveal = 0; }
        break;
    case WM_CHAR:
        if (w == 0x7F) { EditDeleteWord(-1); return 0; }
        break;
    case WM_SYSCHAR:
        if (w != ' ' && w != VK_BACK && MenuBarMnemonic((WCHAR)w) >= 0) {
            SendMessageW(GetParent(h), WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)w);
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((w == VK_SHIFT || w == VK_CONTROL) && g_shiftDir) {
            PostMessageW(GetParent(h), WM_COMMAND, g_shiftDir == 2 ? IDM_DIR_RTL : IDM_DIR_LTR, 0);
            g_shiftDir = 0;
        }
        break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_SYSKEYDOWN:
        g_shiftDir = 0;
        break;
    case WM_KEYDOWN:
        if (w == VK_SHIFT && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) g_shiftDir = ((l >> 16) & 0xFF) == 0x36 ? 2 : 1;
        else if (w != VK_SHIFT && w != VK_CONTROL) g_shiftDir = 0;
        if (w == VK_DELETE && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            EditDeleteWord(1);
            return 0;
        }
        if (w >= VK_PRIOR && w <= VK_DOWN && !((w == VK_UP || w == VK_DOWN) && (GetKeyState(VK_CONTROL) & 0x8000)) && !EditCaretInView(h, LineHeight())) {
            SendMessageW(h, EM_SCROLLCARET, 0, 0);
        }
        break;
    case WM_CONTEXTMENU: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (x == -1 && y == -1) {
            POINT p;
            if (!GetCaretPos(&p)) { p.x = 8; p.y = 8; }
            ClientToScreen(h, &p);
            x = p.x; y = p.y + S(20);
        }
        MenuPopup(GetParent(h), &g_mdEditCtx, x, y, 0);
        return 0; }
    }
    wasRevealing = g_revealing;
    if (anchor >= 0 || m == EM_SCROLLCARET || m == WM_SETFONT || m == EM_SETRECTNP || m == EM_SETTABSTOPS || m == EM_SETMARGINS) g_revealing = 1;
    r = CallWindowProcW(g_orig, h, m, w, l);
    g_revealing = wasRevealing;
    if (m == WM_SETTEXT) { g_textRev++; g_stub.n = 0; }
    if (anchor >= 0) {
        DWORD ss = 0, se = 0;
        Ctl(h, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
        if (m == EM_SETSEL && (int)w >= 0) EditCaretHint(l >= 0 && (int)w > l ? (int)ss : (int)se);
        else if (ss == se || (int)se == anchor) EditCaretHint((int)ss);
        else if ((int)ss == anchor) EditCaretHint((int)se);
    }
    if (anchor >= 0 || m == WM_SETFOCUS || m == WM_KEYUP || m == EM_SCROLLCARET) FitTallCaret(h);
    switch (m) {
    case WM_SETFONT: case WM_SETTEXT:
        if (m != WM_SETTEXT) EditPad(h);
        EditScrollSoon();
        break;
    }
    if (!g_inBars) switch (m) {
    case WM_KEYDOWN: case WM_CHAR: case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL: case WM_SETTEXT:
    case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER: case WM_SETFONT:
        SbarSync(h);
        break;
    case WM_MOUSEMOVE:
        if (w & 1) SbarSync(h);
        break;
    default:
        if (m >= 0x00B0 && m <= 0x00D8) SbarSync(h);
        break;
    }
    switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case EM_SETSEL: case EM_REPLACESEL:
        AppUpdateStatus();
        break;
    case WM_MOUSEMOVE:
        if (w & 1) AppUpdateStatus();
        break;
    }
    if (StubWatch(m, w)) {
        StubSync(h);
        BandSync(h);
    }
    if (g_tallCaret) FitTallCaret(h);
    return r;
}

HWND EditCreate(HWND parent) {
    HWND old = g_edit, e;
    WCHAR *text = NULL;
    DWORD s = 0, en = 0, st = WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    LONG_PTR ex = 0;
    int mod = 0, focus = 0, x = 0, y = 0, w = 0, h = 0, back = 0;
    if (!g_pf.wrap) st |= WS_HSCROLL | ES_AUTOHSCROLL;
    if (old) {
        RECT rc;
        POINT a, b;
        text = EditGetDocText(NULL);
        if (!text) return NULL;
        SendMessageW(old, EM_GETSEL, (WPARAM)&s, (LPARAM)&en);
        back = en > s && EditCaretIndex() == (int)s;
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
    if (text && !SetWindowTextW(e, text)) { DestroyWindow(e); mem_free(text); return NULL; }
    g_edit = e;
    g_barsPending = 0;
    g_revealPending = g_sizeDeferred = 0;
    g_allowTallReveal = 1;
    g_tallCaret = 0;
    EditCaretInvalidate();
    EditCaretHint(0);
    EditCaretDisplayAdjusted(FALSE);
    SetWindowPos(e, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_stub.n = 0;
    g_orig = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditProc);
    DarkScroll(e);
    SbarTrim(e, S(SBAR_TRIM), S(SBAR_TRIM));
    EditApplyFont();
    if (text) {
        SendMessageW(e, WM_SETREDRAW, FALSE, 0);
        SendMessageW(e, EM_SETSEL, back ? en : s, back ? s : en);
        SendMessageW(e, EM_SCROLLCARET, 0, 0);
        SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(e, EM_SETMODIFY, (WPARAM)mod, 0);
        SendMessageW(e, WM_SETREDRAW, TRUE, 0);
        mem_free(text);
    }
    UpdateBars();
    EditScrollSoon();
    ShowWindow(e, SW_SHOW);
    SbarSync(e);
    if (old) { SbarDetach(old); DestroyWindow(old); }
    if (focus || !old) SetFocus(e);
    return e;
}
