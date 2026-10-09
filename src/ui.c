/* ui.c - shared palette, fonts, DPI, drawing primitives and optional Win32 theming APIs.
 * Button state and input live in button.c; dialog lifetime and controls live in dialog.c. */
#include "ui_internal.h"

#define UI_FACE  L"Segoe UI"
#define UI_PT    9

HINSTANCE g_hinst;
int       g_dpi = 96;
HFONT     g_fontUI, g_fontUIB, g_fontMenu, g_fontMenuB;
HBRUSH    g_brFace, g_brField;
Palette   g_pal;

/* ------------------------------------------------------------ themes ---- */
/* Dark chrome uses #161418 with a mint accent and a black editor. Light chrome
 * uses #dfdee1 with a white editor; its green fill (#2f7d58) uses white text,
 * while green text and thin lines use the darker #1f6a43 for contrast. */
static const Palette g_themes[2] = {
    { RGB(0x9d, 0xf5, 0xbd), RGB(0x9d, 0xf5, 0xbd), RGB(0x16, 0x14, 0x18),
      RGB(0x16, 0x14, 0x18), RGB(0x21, 0x1e, 0x24), RGB(0x10, 0x0f, 0x12),
      RGB(0x55, 0x4f, 0x5c), RGB(0x36, 0x32, 0x3c), RGB(0x05, 0x04, 0x06), RGB(0x0c, 0x0b, 0x0e),
      RGB(0xff, 0xff, 0xff), RGB(0x80, 0x7a, 0x88),
      RGB(0xff, 0xff, 0xff), RGB(0x00, 0x00, 0x00) },
    { RGB(0x2f, 0x7d, 0x58), RGB(0x1f, 0x6a, 0x43), RGB(0xff, 0xff, 0xff),
      RGB(0xdf, 0xde, 0xe1), RGB(0xf0, 0xee, 0xe9), RGB(0xff, 0xff, 0xff),
      RGB(0xff, 0xff, 0xff), RGB(0xf3, 0xf1, 0xed), RGB(0x40, 0x40, 0x40), RGB(0x73, 0x73, 0x73),
      RGB(0x00, 0x00, 0x00), RGB(0x59, 0x59, 0x59),
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

static WCHAR g_chromeFace[32] = DEFAULT_FACE;   /* menu bar / popups / status bar / title strip font: the editor font face, CHROME_PX pixels */

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

static HFONT FacePx(const WCHAR *face, int px, int weight)       /* px = 96-dpi pixels (em height), scaled by the dpi */
{
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

/* Controls borrow their dialog fonts, and a modeless dialog can retain the pair
 * from an earlier monitor. Own one pair per DPI for the process lifetime: returning
 * to a monitor reuses its handles instead of leaking a new pair on every visit. */
typedef struct DialogFonts {
    struct DialogFonts *next;
    int dpi;
    HFONT normal, bold;
} DialogFonts;
static DialogFonts *g_dialogFonts;

static void MakeDialogFonts(void)
{
    DialogFonts *f;
    for (f = g_dialogFonts; f; f = f->next) if (f->dpi == g_dpi) break;
    if (!f) {
        f = (DialogFonts *)mem_zalloc(sizeof *f);
        if (!f) return;
        f->normal = Face(UI_FACE, UI_PT, FW_NORMAL);
        f->bold = Face(UI_FACE, UI_PT, FW_BOLD);
        if (!f->normal || !f->bold) {
            if (f->normal) DeleteObject(f->normal);
            if (f->bold) DeleteObject(f->bold);
            mem_free(f);
            return;                                   /* preserve the last usable pair */
        }
        f->dpi = g_dpi;
        f->next = g_dialogFonts;
        g_dialogFonts = f;
    }
    g_fontUI = f->normal;
    g_fontUIB = f->bold;
}

typedef struct { HFONT old, oldBold, font, bold; } FontChange;

static BOOL CALLBACK RefontChild(HWND h, LPARAM l)
{
    const FontChange *c = (const FontChange *)l;
    HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0), replacement = NULL;
    if (c->old && font == c->old) replacement = c->font;
    else if (c->oldBold && font == c->oldBold) replacement = c->bold;
    if (replacement) {
        WCHAR cls[16];
        RECT r;
        BOOL edit;
        GetClassNameW(h, cls, COUNTOF(cls));
        edit = wcmpi(cls, L"EDIT") == 0;
        if (edit) SendMessageW(h, EM_GETRECT, 0, (LPARAM)&r);
        SendMessageW(h, WM_SETFONT, (WPARAM)replacement, TRUE);
        if (edit) SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&r);  /* retain the help editor's inset */
    }
    return TRUE;
}

static BOOL CALLBACK RefontWindow(HWND h, LPARAM l)
{
    RefontChild(h, l);
    EnumChildWindows(h, RefontChild, l);
    return TRUE;
}

static void MakeMenuFont(void)
{
    FontChange c;
    c.old = g_fontMenu; c.oldBold = g_fontMenuB;
    c.font = FacePx(g_chromeFace, CHROME_PX, FW_NORMAL);
    c.bold = FacePx(g_chromeFace, CHROME_PX, FW_BOLD);
    if (!c.font || !c.bold) {
        if (c.font) DeleteObject(c.font);
        if (c.bold) DeleteObject(c.bold);
        return;
    }
    g_fontMenu = c.font;
    g_fontMenuB = c.bold;
    /* The help editor also borrows the chrome font. Reassign live controls
     * before deleting either old handle; ordinary chrome reads the globals. */
    if (c.old || c.oldBold) EnumThreadWindows(GetCurrentThreadId(), RefontWindow, (LPARAM)&c);
    if (c.old) DeleteObject(c.old);
    if (c.oldBold) DeleteObject(c.oldBold);
}

static void MakeFonts(void)
{
    MakeDialogFonts();
    MakeMenuFont();
}

/* chrome font = the editor's font face at a static size (CHROME_PX): zooming the editor does not touch the menu bar, status bar or title */
void UiSetChromeFont(const WCHAR *face)
{
    wcopy(g_chromeFace, face, 32);
    MakeMenuFont();
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
void UiFrame(HDC dc, const RECT *r, COLORREF tl, COLORREF br)
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
        UiFrame(dc, &a, C_HI, C_LO);   InflateRect(&a, -1, -1); UiFrame(dc, &a, C_HI2, C_LO2); break;
    case BV_SUNKEN:
        UiFrame(dc, &a, C_LO2, C_HI);  InflateRect(&a, -1, -1); UiFrame(dc, &a, C_LO, C_HI2);  break;
    case BV_ETCHED:
        UiFrame(dc, &a, C_LO2, C_HI2); InflateRect(&a, -1, -1); UiFrame(dc, &a, C_HI2, C_LO2); break;
    case BV_FLAT_UP:
        UiFrame(dc, &a, C_HI2, C_LO2); break;
    case BV_FLAT_DN:
        UiFrame(dc, &a, C_LO2, C_HI2); break;
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

/* the scrollbars of a control: visual styles off for it (empty theme names) = the classic native bar with a fixed 17 px strip and its
 * arrow buttons always visible (windows 11's themed bars are thin and hide their arrows), and sbar.c covers that strip with our own
 * classic bar in the palette: the native one keeps doing the work, ours is what you see. safe to call again (a theme switch) */
void DarkScroll(HWND h)
{
    if (pSetTheme) pSetTheme(h, L"", L"");
    SbarAttach(h);
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

/* ------------------------------------------------------------- init ---- */
void UiInit(HINSTANCE hi)
{
    HMODULE m;
    g_hinst = hi;
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);   /* no system "there is no disk in the drive" box when a dialog probes an empty card reader */
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

    ButtonInit();
    DialogInit();
}
