/* ui.c - classic look in two themes: palettes, bevels, dwm frame, custom button, dialog scaffolding */
#include "mp.h"

#define UI_FACE  L"Segoe UI"
#define UI_PT    9

HINSTANCE g_hinst;
int       g_dpi = 96;
HFONT     g_fontUI, g_fontUIB, g_fontMenu;
HBRUSH    g_brFace, g_brField;
Palette   g_pal;

/* ------------------------------------------------------------ themes ---- */
/* dark: everything derived from the #161418 face + the mint; the editor is black.
 * light: the classic win2000 gray (#d4d0c8 face, white fields); the mint stays the highlight fill, a dark
 * mint is used where the accent is text or a thin line on a light surface; the editor is white.
 * light contrast (wcag): text >= 13.6, dark mint text on face 5.0, dim on face 3.4, shadow #737373 on face 3.1 */
static const Palette g_themes[2] = {
    { RGB(0x9d, 0xf5, 0xbd), RGB(0x9d, 0xf5, 0xbd), RGB(0x16, 0x14, 0x18),
      RGB(0x16, 0x14, 0x18), RGB(0x21, 0x1e, 0x24), RGB(0x10, 0x0f, 0x12),
      RGB(0x55, 0x4f, 0x5c), RGB(0x36, 0x32, 0x3c), RGB(0x05, 0x04, 0x06), RGB(0x0c, 0x0b, 0x0e),
      RGB(0xff, 0xff, 0xff), RGB(0x80, 0x7a, 0x88),
      RGB(0xff, 0xff, 0xff), RGB(0x00, 0x00, 0x00) },
    { RGB(0x9d, 0xf5, 0xbd), RGB(0x0b, 0x60, 0x33), RGB(0x00, 0x00, 0x00),
      RGB(0xd4, 0xd0, 0xc8), RGB(0xe4, 0xe1, 0xda), RGB(0xff, 0xff, 0xff),
      RGB(0xff, 0xff, 0xff), RGB(0xe9, 0xe7, 0xe2), RGB(0x40, 0x40, 0x40), RGB(0x73, 0x73, 0x73),
      RGB(0x00, 0x00, 0x00), RGB(0x6d, 0x6d, 0x6d),
      RGB(0x00, 0x00, 0x00), RGB(0xff, 0xff, 0xff) },
};
static int    g_theme;
static HBRUSH g_brTheme[2][2];              /* [theme][face, field]: made once, never deleted (window classes keep them) */

void ThemeSet(int theme)
{
    g_theme = theme == THEME_LIGHT ? THEME_LIGHT : THEME_DARK;
    g_pal = g_themes[g_theme];
    if (!g_brTheme[g_theme][0]) g_brTheme[g_theme][0] = CreateSolidBrush(C_FACE);
    if (!g_brTheme[g_theme][1]) g_brTheme[g_theme][1] = CreateSolidBrush(C_FIELD);
    g_brFace = g_brTheme[g_theme][0];
    g_brField = g_brTheme[g_theme][1];
}

int ThemeGet(void) { return g_theme; }

typedef long (WINAPI *DwmSetFn)(HWND, DWORD, const void *, DWORD);
typedef long (WINAPI *ThemeFn)(HWND, LPCWSTR, LPCWSTR);
typedef UINT (WINAPI *DpiWinFn)(HWND);
typedef UINT (WINAPI *DpiSysFn)(void);
typedef BOOL (WINAPI *AdjDpiFn)(RECT *, DWORD, BOOL, DWORD, UINT);
typedef int  (WINAPI *MetricFn)(int, UINT);
static DwmSetFn pDwmSet;
static ThemeFn  pSetTheme;
static DpiWinFn pDpiWin;                    /* windows 10+; looked up at run time so the exe still starts on older windows */
static DpiSysFn pDpiSys;
static AdjDpiFn pAdjDpi;
static MetricFn pMetric;

static WCHAR g_chromeFace[32] = L"Consolas";   /* menu bar / popups / status bar font: the editor font, 2pt smaller */
static int   g_chromePt = 11;

int S(int v)    { return MulDiv(v, g_dpi, 96); }
int UnS(int px) { return MulDiv(px, 96, g_dpi); }

int UiSystemDpi(void)
{
    if (pDpiSys) return (int)pDpiSys();
    {
        HDC dc = GetDC(NULL);
        int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(NULL, dc);
        return d ? d : 96;
    }
}

int UiDpiForWindow(HWND h)
{
    UINT d = pDpiWin ? pDpiWin(h) : 0;
    return d ? (int)d : g_dpi;
}

/* GetSystemMetrics at the current dpi (windows 10 has a per-dpi version) */
int UiMetric(int idx)
{
    if (pMetric) return pMetric(idx, (UINT)g_dpi);
    return GetSystemMetrics(idx);
}

/* client size -> window size for the current dpi */
BOOL UiAdjustRect(RECT *r, DWORD style, DWORD ex)
{
    if (pAdjDpi) return pAdjDpi(r, style, FALSE, ex, (UINT)g_dpi);
    return AdjustWindowRectEx(r, style, FALSE, ex);
}

/* ------------------------------------------------------------ fonts ---- */
/* DEFAULT_CHARSET on purpose (measured with tools\fonttest.c on a ja-JP system): with ANSI_CHARSET segoe ui and
 * arial draw '\' as a yen sign there; tahoma and microsoft sans serif do it in every charset, so the dialogs use
 * segoe ui. consolas / verdana / courier new / lucida console are fine either way. */
static HFONT Face(const WCHAR *face, int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static void MakeFonts(void)
{
    if (g_fontUI)   DeleteObject(g_fontUI);
    if (g_fontUIB)  DeleteObject(g_fontUIB);
    if (g_fontMenu) DeleteObject(g_fontMenu);
    g_fontUI   = Face(UI_FACE, UI_PT, FW_NORMAL);              /* dialogs */
    g_fontUIB  = Face(UI_FACE, UI_PT, FW_BOLD);
    g_fontMenu = Face(g_chromeFace, g_chromePt, FW_NORMAL);    /* main window chrome */
}

/* chrome font = the editor's font face, 2pt smaller than its size, never above 14pt */
void UiSetChromeFont(const WCHAR *face, int editorPt)
{
    int pt = editorPt - 2;
    if (pt > 14) pt = 14;
    if (pt < 8) pt = 8;
    wcopy(g_chromeFace, face, 32);
    g_chromePt = pt;
    MakeFonts();
}

void UiSetDpi(int dpi)
{
    g_dpi = dpi < 96 ? 96 : dpi;
    MakeFonts();
}

/* --------------------------------------------------------- primitives -- */
void FillC(HDC dc, const RECT *r, COLORREF c)
{
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, r, NULL, 0, NULL);
}

/* 1px frame: top/left in tl, bottom/right in br (classic edge corner rules) */
static void Frame(HDC dc, const RECT *r, COLORREF tl, COLORREF br)
{
    RECT a;
    a.left = r->left;      a.top = r->top;        a.right = r->right - 1; a.bottom = r->top + 1;    FillC(dc, &a, tl);
    a.left = r->left;      a.top = r->top + 1;    a.right = r->left + 1;  a.bottom = r->bottom - 1; FillC(dc, &a, tl);
    a.left = r->left;      a.top = r->bottom - 1; a.right = r->right;     a.bottom = r->bottom;     FillC(dc, &a, br);
    a.left = r->right - 1; a.top = r->top;        a.right = r->right;     a.bottom = r->bottom - 1; FillC(dc, &a, br);
}

void Bevel(HDC dc, const RECT *r, int kind)
{
    RECT a = *r;
    switch (kind) {
    case BV_RAISED:
        Frame(dc, &a, C_HI, C_LO);   InflateRect(&a, -1, -1); Frame(dc, &a, C_HI2, C_LO2); break;
    case BV_SUNKEN:
        Frame(dc, &a, C_LO2, C_HI);  InflateRect(&a, -1, -1); Frame(dc, &a, C_LO, C_HI2);  break;
    case BV_ETCHED:
        Frame(dc, &a, C_LO2, C_HI2); InflateRect(&a, -1, -1); Frame(dc, &a, C_HI2, C_LO2); break;
    case BV_FLAT_UP:
        Frame(dc, &a, C_HI2, C_LO2); break;
    case BV_FLAT_DN:
        Frame(dc, &a, C_LO2, C_HI2); break;
    }
}

void TextC(HDC dc, const WCHAR *s, int n, RECT *r, UINT fmt, COLORREF c)
{
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, n, r, fmt);
}

int TextW(HDC dc, const WCHAR *s, int n)
{
    SIZE sz;
    sz.cx = 0;
    GetTextExtentPoint32W(dc, s, n < 0 ? wlen(s) : n, &sz);
    return sz.cx;
}

/* small filled triangle. dir 0 = pointing right, 1 = pointing down. sz = half height */
void Tri(HDC dc, int x, int y, int sz, int dir, COLORREF c)
{
    POINT p[3];
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, c);
    if (dir == 0) {
        p[0].x = x;      p[0].y = y;
        p[1].x = x;      p[1].y = y + sz * 2;
        p[2].x = x + sz; p[2].y = y + sz;
    } else {
        p[0].x = x;          p[0].y = y;
        p[1].x = x + sz * 2; p[1].y = y;
        p[2].x = x + sz;     p[2].y = y + sz;
    }
    Polygon(dc, p, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

/* ------------------------------------------------------ dwm / uxtheme -- */
/* the frame follows the theme. title text = the accent while active, dim otherwise; the border stays neutral gray */
void DarkFrame(HWND h, int active)
{
    BOOL dark = g_theme == THEME_DARK;
    COLORREF cap = C_FACE, txt = active ? C_ACCENT_FG : C_DIM;
    COLORREF bor = dark ? (active ? C_HI : C_HI2) : (active ? C_LO2 : RGB(0xa8, 0xa5, 0x9e));
    int corner = DWMWCP_DONOTROUND;
    if (!pDwmSet) return;
    pDwmSet(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    pDwmSet(h, DWMWA_CAPTION_COLOR, &cap, sizeof cap);
    pDwmSet(h, DWMWA_TEXT_COLOR, &txt, sizeof txt);
    pDwmSet(h, DWMWA_BORDER_COLOR, &bor, sizeof bor);
    pDwmSet(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
}

void DarkScroll(HWND h)
{
    if (pSetTheme) pSetTheme(h, g_theme == THEME_DARK ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

void RegClass(const WCHAR *name, WNDPROC proc, UINT style, HBRUSH bg)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = bg;
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

/* ------------------------------------------------------- button class -- */
typedef struct { int check, hot, down, cap, kbd, focus, isdef; HFONT font; } BtnSt;

static int BtnType(HWND h) { return (int)(GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK); }

static BOOL IsMpRadio(HWND h)
{
    WCHAR cn[16];
    GetClassNameW(h, cn, 16);
    return wcmp(cn, L"mp_btn") == 0 && BtnType(h) == BS_AUTORADIOBUTTON;
}

static void BtnClick(HWND h, BtnSt *s)
{
    int t = BtnType(h);
    if (t == BS_AUTOCHECKBOX) {
        s->check = !s->check;
    } else if (t == BS_AUTORADIOBUTTON) {
        HWND a = h, c;
        while (!(GetWindowLongPtrW(a, GWL_STYLE) & WS_GROUP)) {      /* walk back to the group head */
            c = GetWindow(a, GW_HWNDPREV);
            if (!c) break;
            a = c;
        }
        for (c = a; c; c = GetWindow(c, GW_HWNDNEXT)) {              /* clear the other radios in it */
            if (c != a && (GetWindowLongPtrW(c, GWL_STYLE) & WS_GROUP)) break;
            if (c != h && IsMpRadio(c)) SendMessageW(c, BM_SETCHECK, 0, 0);
        }
        s->check = 1;
    }
    InvalidateRect(h, NULL, FALSE);
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetWindowLongPtrW(h, GWLP_ID), BN_CLICKED), (LPARAM)h);
}

/* check mark drawn inside a 13x13 (96-dpi) box whose top-left is x,y */
void CheckGlyph(HDC dc, int x, int y, COLORREF c)
{
    static const POINT pt[6] = { {2, 6}, {3, 5}, {5, 7}, {9, 3}, {10, 4}, {5, 9} };
    POINT p[6];
    int i;
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    for (i = 0; i < 6; i++) { p[i].x = x + S(pt[i].x); p[i].y = y + S(pt[i].y); }
    SetDCBrushColor(dc, c);
    Polygon(dc, p, 6);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

static void BtnDraw(HWND h, BtnSt *s, HDC dc, RECT rc)
{
    WCHAR text[128];
    int n = GetWindowTextW(h, text, 128), t = BtnType(h);
    BOOL en = IsWindowEnabled(h) ? TRUE : FALSE;
    COLORREF tc = !en ? C_DIM : (s->hot ? C_ACCENT_FG : C_TEXT);
    HGDIOBJ of = SelectObject(dc, s->font ? s->font : g_fontUI);

    FillC(dc, &rc, C_FACE);
    if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON) {
        RECT r = rc, in, tr;
        if (s->isdef) { Frame(dc, &r, C_ACCENT_FG, C_ACCENT_FG); InflateRect(&r, -1, -1); }
        Bevel(dc, &r, s->down ? BV_SUNKEN : BV_RAISED);
        in = r;
        InflateRect(&in, -2, -2);
        FillC(dc, &in, s->down ? C_FACE : C_FACE2);
        tr = in;
        if (s->down) OffsetRect(&tr, 1, 1);
        TextC(dc, text, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, tc);
        if (s->focus && en) {
            RECT f = in;
            InflateRect(&f, -S(3), -S(3));
            DrawFocusRect(dc, &f);
        }
    } else {
        int bx = S(13), by = (rc.bottom - bx) / 2;
        RECT box, tr, cr, in;
        box.left = 0; box.top = by; box.right = bx; box.bottom = by + bx;
        if (t == BS_AUTORADIOBUTTON) {
            HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
            HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCPenColor(dc, g_theme == THEME_DARK ? C_HI : C_LO2);      /* the ring: the darker edge in the light theme */
            SetDCBrushColor(dc, C_FIELD);
            Ellipse(dc, box.left, box.top, box.right, box.bottom);
            if (s->check) {
                SetDCPenColor(dc, en ? C_ACCENT_FG : C_DIM);
                SetDCBrushColor(dc, en ? C_ACCENT_FG : C_DIM);
                Ellipse(dc, box.left + S(4), box.top + S(4), box.right - S(3), box.bottom - S(3));
            }
            SelectObject(dc, op);
            SelectObject(dc, ob);
        } else {
            Bevel(dc, &box, BV_SUNKEN);
            in = box;
            InflateRect(&in, -2, -2);
            FillC(dc, &in, C_FIELD);
            if (s->check) CheckGlyph(dc, box.left, box.top, en ? C_ACCENT_FG : C_DIM);
        }
        tr.left = bx + S(7); tr.top = 0; tr.right = rc.right; tr.bottom = rc.bottom;
        TextC(dc, text, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, tc);
        if (s->focus && en) {
            cr = tr;
            DrawTextW(dc, text, n, &cr, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
            cr.left = tr.left - S(2); cr.right = cr.left + (cr.right - tr.left) + S(4);
            cr.top = (rc.bottom - (cr.bottom - cr.top)) / 2 - S(1);
            cr.bottom = cr.top + (cr.bottom - cr.top) + S(2);
            DrawFocusRect(dc, &cr);
        }
    }
    SelectObject(dc, of);
}

static LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    BtnSt *s = (BtnSt *)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT rc;
    POINT pt;
    int t;

    if (m == WM_NCCREATE) {
        s = (BtnSt *)mem_zalloc(sizeof *s);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        if (s) s->isdef = (((CREATESTRUCTW *)l)->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
        return DefWindowProcW(h, m, w, l);                 /* this is what stores the caption text */
    }
    if (!s) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_NCDESTROY:
        mem_free(s);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps), mdc;
        HBITMAP bmp;
        HGDIOBJ old;
        GetClientRect(h, &rc);
        mdc = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = SelectObject(mdc, bmp);
        BtnDraw(h, s, mdc, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0; }
    case WM_GETDLGCODE:
        t = BtnType(h);
        if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON)
            return DLGC_BUTTON | (s->isdef ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
        if (t == BS_AUTORADIOBUTTON) return DLGC_BUTTON | DLGC_RADIOBUTTON;
        return DLGC_BUTTON;
    case WM_SETFONT:
        s->font = (HFONT)w;
        if (l) InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)s->font;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        InvalidateRect(h, NULL, FALSE);
        return r; }
    case WM_ENABLE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETFOCUS:
        s->focus = 1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        s->focus = 0; s->kbd = 0; s->down = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case BM_GETCHECK:
        return s->check;
    case BM_SETCHECK:
        s->check = w ? 1 : 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case BM_GETSTATE:
        return (s->check ? 3 : 0) | (s->down ? 4 : 0) | (s->focus ? 8 : 0);
    case BM_SETSTYLE:
        t = (int)(w & BS_TYPEMASK);
        if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON) {
            s->isdef = (t == BS_DEFPUSHBUTTON);
            if (l) InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case BM_CLICK:
        BtnClick(h, s);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (IsWindowEnabled(h)) {
            SetFocus(h);
            s->cap = 1; s->down = 1;
            SetCapture(h);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        GetClientRect(h, &rc);
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        if (!s->hot) {
            TRACKMOUSEEVENT te;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            s->hot = 1;
            InvalidateRect(h, NULL, FALSE);
        }
        if (s->cap) {
            int in = PtInRect(&rc, pt) ? 1 : 0;
            if (in != s->down) { s->down = in; InvalidateRect(h, NULL, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        s->hot = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s->cap) {
            int fire = s->down;
            s->cap = 0; s->down = 0;
            ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
            if (fire) BtnClick(h, s);
        }
        return 0;
    case WM_CAPTURECHANGED:
        s->cap = 0; s->down = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE && !s->kbd) { s->kbd = 1; s->down = 1; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_KEYUP:
        if (w == VK_SPACE && s->kbd) {
            s->kbd = 0; s->down = 0;
            InvalidateRect(h, NULL, FALSE);
            BtnClick(h, s);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------- dialog base --- */
DlgBase *DlgFromHwnd(HWND h, UINT m, LPARAM l)
{
    if (m == WM_NCCREATE) {
        DlgBase *b = (DlgBase *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)b);
        b->hwnd = h;
        return b;
    }
    return (DlgBase *)GetWindowLongPtrW(h, GWLP_USERDATA);
}

void DlgBaseInit(DlgBase *b, HWND owner)
{
    memset(b, 0, sizeof *b);
    b->owner = owner;
}

void DlgFrame(DlgBase *b, int x, int y, int w, int h, int kind)
{
    if (b->nframes >= 24) return;
    b->fr[b->nframes].left = S(x);
    b->fr[b->nframes].top = S(y);
    b->fr[b->nframes].right = S(x) + S(w);
    b->fr[b->nframes].bottom = S(y) + S(h);
    b->frk[b->nframes++] = (BYTE)kind;
}

/* creates (hidden) a centred caption dialog whose client area is cw x ch (96-dpi px) */
HWND DlgOpen(DlgBase *b, const WCHAR *cls, const WCHAR *title, int cw, int ch, int modeless)
{
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT rc, ow, wa;
    MONITORINFO mi;
    POINT cp;
    int w, h, x, y;

    b->modeless = modeless;
    rc.left = 0; rc.top = 0; rc.right = S(cw); rc.bottom = S(ch);
    UiAdjustRect(&rc, st, 0);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    if (b->owner && IsWindowVisible(b->owner) && !IsIconic(b->owner)) GetWindowRect(b->owner, &ow);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &ow, 0);
    x = ow.left + ((ow.right - ow.left) - w) / 2;
    y = ow.top + ((ow.bottom - ow.top) - h) / 2;

    cp.x = x + w / 2; cp.y = y + h / 2;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(cp, MONITOR_DEFAULTTONEAREST), &mi);
    wa = mi.rcWork;
    if (x + w > wa.right)  x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (x < wa.left) x = wa.left;
    if (y < wa.top)  y = wa.top;

    b->hwnd = CreateWindowExW(0, cls, title, st, x, y, w, h, b->owner, NULL, g_hinst, b);
    if (b->hwnd) DarkFrame(b->hwnd, 1);
    return b->hwnd;
}

void DlgRunModal(DlgBase *b)
{
    MSG m;
    HWND owner = b->owner;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(b->hwnd, SW_SHOW);
    while (!b->done) {
        BOOL r = GetMessageW(&m, NULL, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage((int)m.wParam);        /* let the app loop see WM_QUIT too */
            break;
        }
        if (!IsDialogMessageW(b->hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(b->hwnd);
    b->hwnd = NULL;
}

/* shared themed behaviour for every dialog window. returns TRUE if handled */
BOOL DlgCommon(DlgBase *b, UINT m, WPARAM w, LPARAM l, LRESULT *ret)
{
    HDC dc;
    *ret = 0;
    switch (m) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(b->hwnd, &rc);
        FillC((HDC)w, &rc, C_FACE);
        *ret = 1;
        return TRUE; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        int i;
        dc = BeginPaint(b->hwnd, &ps);
        for (i = 0; i < b->nframes; i++) Bevel(dc, &b->fr[i], b->frk[i]);
        EndPaint(b->hwnd, &ps);
        return TRUE; }
    case WM_CTLCOLORSTATIC:
        dc = (HDC)w;
        SetTextColor(dc, GetWindowLongPtrW((HWND)l, GWLP_ID) == IDC_DIM ? C_DIM : C_TEXT);
        SetBkColor(dc, C_FACE);
        *ret = (LRESULT)g_brFace;
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        dc = (HDC)w;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        *ret = (LRESULT)g_brField;
        return TRUE;
    case DM_GETDEFID:
        *ret = MAKELONG(IDOK, 0x534B);                           /* DC_HASDEFID */
        return TRUE;
    case WM_ACTIVATE:
        DarkFrame(b->hwnd, LOWORD(w) != WA_INACTIVE);
        if (LOWORD(w) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(b->hwnd, f)) b->focus = f;
        } else if (b->focus && IsWindow(b->focus)) {
            SetFocus(b->focus);
        }
        return TRUE;
    case WM_CLOSE:
        if (b->modeless) { DestroyWindow(b->hwnd); }
        else { b->result = IDCANCEL; b->done = 1; }
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------- control helpers -- */
HWND UiLabel(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra,
                             S(x), S(y), S(w), S(h), p, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return c;
}

/* single-line edit inside a drawn 2px sunken frame */
HWND UiEdit(DlgBase *b, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra)
{
    HWND c;
    DlgFrame(b, x, y, w, h, BV_SUNKEN);
    c = CreateWindowExW(0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                        S(x) + 2, S(y) + 2, S(w) - 4, S(h) - 4, b->hwnd, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return c;
}

HWND UiButton(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD style)
{
    HWND c = CreateWindowExW(0, L"mp_btn", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                             S(x), S(y), S(w), S(h), p, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return c;
}

/* ------------------------------------------------------ message dialog -- */
typedef struct { DlgBase b; const WCHAR *msg, *lab[3]; int nb, esc, tw, th; } MsgDlg;

static LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    MsgDlg *d = (MsgDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        int cw = d->tw + 32, bw = 88, gap = 8, total = d->nb * bw + (d->nb - 1) * gap, i, x, y;
        if (cw < total + 32) cw = total + 32;
        UiLabel(h, d->msg, 16, 16, d->tw, d->th, 0, SS_NOPREFIX);
        x = cw - 16 - total;
        y = 16 + d->th + 18;
        for (i = 0; i < d->nb; i++) {
            HWND c = UiButton(h, d->lab[i], x + i * (bw + gap), y, bw, 24, i == 0 ? IDOK : 100 + i + 1,
                              i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            if (i == 0) b->focus = c;
        }
        return 0; }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK) { b->result = 1; b->done = 1; return 0; }
        if (id == 102 || id == 103) { b->result = id - 100; b->done = 1; return 0; }
        if (id == IDCANCEL) { b->result = d->esc; b->done = 1; return 0; }
        break; }
    case WM_CLOSE:
        b->result = d->esc; b->done = 1;
        return 0;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

/* themed message box. buttons are 1-based; returns the pressed index (escIdx for esc / close) */
int MpAsk(HWND owner, const WCHAR *title, const WCHAR *msg, const WCHAR *b1, const WCHAR *b2, const WCHAR *b3, int escIdx)
{
    MsgDlg d;
    HDC dc;
    HGDIOBJ old;
    RECT r;
    int cw, ch, total, bw = 88, gap = 8;

    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    d.msg = msg;
    d.esc = escIdx;
    d.lab[0] = b1; d.lab[1] = b2; d.lab[2] = b3;
    d.nb = b3 ? 3 : (b2 ? 2 : 1);

    dc = GetDC(NULL);
    old = SelectObject(dc, g_fontUI);
    r.left = 0; r.top = 0; r.right = S(380); r.bottom = 0;
    DrawTextW(dc, msg, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    d.tw = UnS(r.right) + 4;
    d.th = UnS(r.bottom) + 2;
    if (d.tw < 200) d.tw = 200;

    total = d.nb * bw + (d.nb - 1) * gap;
    cw = d.tw + 32;
    if (cw < total + 32) cw = total + 32;
    ch = 16 + d.th + 18 + 24 + 16;

    if (!DlgOpen(&d.b, L"mp_msg", title, cw, ch, 0)) return escIdx;
    DlgRunModal(&d.b);
    return d.b.result;
}

/* ------------------------------------------------------------- init ---- */
void UiInit(HINSTANCE hi)
{
    HMODULE m;
    g_hinst = hi;
    m = LoadLibraryW(L"dwmapi.dll");
    if (m) pDwmSet = (DwmSetFn)GetProcAddress(m, "DwmSetWindowAttribute");
    m = LoadLibraryW(L"uxtheme.dll");
    if (m) pSetTheme = (ThemeFn)GetProcAddress(m, "SetWindowTheme");
    m = GetModuleHandleW(L"user32.dll");
    if (m) {
        pDpiWin = (DpiWinFn)GetProcAddress(m, "GetDpiForWindow");
        pDpiSys = (DpiSysFn)GetProcAddress(m, "GetDpiForSystem");
        pAdjDpi = (AdjDpiFn)GetProcAddress(m, "AdjustWindowRectExForDpi");
        pMetric = (MetricFn)GetProcAddress(m, "GetSystemMetricsForDpi");
    }

    g_dpi = UiSystemDpi();
    MakeFonts();
    ThemeSet(g_theme);                                  /* main calls ThemeSet again once the settings are loaded */

    RegClass(L"mp_btn", BtnProc, CS_DBLCLKS, NULL);
    RegClass(L"mp_msg", MsgProc, 0, NULL);
}
