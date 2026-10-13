#include "ui_internal.h"

#define UI_FACE  L"Segoe UI"
#define UI_PT    9

HINSTANCE g_hinst;
int       g_dpi = 96;
HFONT     g_fontUI, g_fontUIB, g_fontMenu, g_fontMenuB;
HBRUSH    g_brFace, g_brField;
Palette   g_pal;

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
static HBRUSH g_brTheme[2][2];

void ThemeSet(int theme) {
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
static DpiWinFn pDpiWin;
static DpiSysFn pDpiSys;
static AdjDpiFn pAdjDpi;
static MetricFn pMetric;

static WCHAR g_chromeFace[32] = DEFAULT_FACE;

int S(int v)    { return MulDiv(v, g_dpi, 96); }
int UnS(int px) { return MulDiv(px, 96, g_dpi); }

int UiSystemDpi(void) {
    if (pDpiSys) return (int)pDpiSys();
    {
        HDC hdc = GetDC(NULL);
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc) ReleaseDC(NULL, hdc);
        return dpi ? dpi : 96;
    }
}

int UiDpiForWindow(HWND hwnd) {
    UINT dpi = pDpiWin ? pDpiWin(hwnd) : 0;
    return dpi ? (int)dpi : g_dpi;
}

int UiMetric(int index) {
    if (pMetric) return pMetric(index, (UINT)g_dpi);
    return GetSystemMetrics(index);
}

BOOL UiAdjustRect(RECT *rect, DWORD style, DWORD exStyle) {
    if (pAdjDpi) return pAdjDpi(rect, style, FALSE, exStyle, (UINT)g_dpi);
    return AdjustWindowRectEx(rect, style, FALSE, exStyle);
}

static HFONT Face(const WCHAR *face, int points, int weight) {
    return CreateFontW(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static HFONT FacePx(const WCHAR *face, int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

typedef struct DialogFonts {
    struct DialogFonts *next;
    int dpi;
    HFONT normal, bold;
} DialogFonts;
static DialogFonts *g_dialogFonts;

static void MakeDialogFonts(void) {
    DialogFonts *fonts;
    for (fonts = g_dialogFonts; fonts; fonts = fonts->next) if (fonts->dpi == g_dpi) break;
    if (!fonts) {
        fonts = (DialogFonts *)mem_zalloc(sizeof *fonts);
        if (!fonts) return;
        fonts->normal = Face(UI_FACE, UI_PT, FW_NORMAL);
        fonts->bold = Face(UI_FACE, UI_PT, FW_BOLD);
        if (!fonts->normal || !fonts->bold) {
            if (fonts->normal) DeleteObject(fonts->normal);
            if (fonts->bold) DeleteObject(fonts->bold);
            mem_free(fonts);
            return;
        }
        fonts->dpi = g_dpi;
        fonts->next = g_dialogFonts;
        g_dialogFonts = fonts;
    }
    g_fontUI = fonts->normal;
    g_fontUIB = fonts->bold;
}

typedef struct { HFONT old, oldBold, font, bold; } FontChange;

static BOOL CALLBACK RefontChild(HWND hwnd, LPARAM lParam) {
    const FontChange *change = (const FontChange *)lParam;
    HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0), replacement = NULL;
    if (change->old && font == change->old) replacement = change->font;
    else if (change->oldBold && font == change->oldBold) replacement = change->bold;
    if (replacement) {
        WCHAR className[16];
        RECT rect;
        BOOL edit;
        GetClassNameW(hwnd, className, COUNTOF(className));
        edit = wcmpi(className, L"EDIT") == 0;
        if (edit) SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&rect);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)replacement, TRUE);
        if (edit) SendMessageW(hwnd, EM_SETRECTNP, 0, (LPARAM)&rect);
    }
    return TRUE;
}

static BOOL CALLBACK RefontWindow(HWND hwnd, LPARAM lParam) {
    RefontChild(hwnd, lParam);
    EnumChildWindows(hwnd, RefontChild, lParam);
    return TRUE;
}

static void MakeMenuFont(void) {
    FontChange change;
    change.old = g_fontMenu; change.oldBold = g_fontMenuB;
    change.font = FacePx(g_chromeFace, CHROME_PX, FW_NORMAL);
    change.bold = FacePx(g_chromeFace, CHROME_PX, FW_BOLD);
    if (!change.font || !change.bold) {
        if (change.font) DeleteObject(change.font);
        if (change.bold) DeleteObject(change.bold);
        return;
    }
    g_fontMenu = change.font;
    g_fontMenuB = change.bold;
    if (change.old || change.oldBold) EnumThreadWindows(GetCurrentThreadId(), RefontWindow, (LPARAM)&change);
    if (change.old) DeleteObject(change.old);
    if (change.oldBold) DeleteObject(change.oldBold);
}

static void MakeFonts(void) {
    MakeDialogFonts();
    MakeMenuFont();
}

void UiSetChromeFont(const WCHAR *face) {
    wcopy(g_chromeFace, face, 32);
    MakeMenuFont();
}

void UiSetDpi(int dpi) {
    g_dpi = dpi < 96 ? 96 : dpi;
    MakeFonts();
}

void FillC(HDC hdc, const RECT *rect, COLORREF color) {
    SetBkColor(hdc, color);
    ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, rect, NULL, 0, NULL);
}

void UiFrame(HDC hdc, const RECT *rect, COLORREF topLeft, COLORREF bottomRight) {
    RECT edge;
    edge.left = rect->left;      edge.top = rect->top;        edge.right = rect->right - 1; edge.bottom = rect->top + 1;    FillC(hdc, &edge, topLeft);
    edge.left = rect->left;      edge.top = rect->top + 1;    edge.right = rect->left + 1;  edge.bottom = rect->bottom - 1; FillC(hdc, &edge, topLeft);
    edge.left = rect->left;      edge.top = rect->bottom - 1; edge.right = rect->right;     edge.bottom = rect->bottom;     FillC(hdc, &edge, bottomRight);
    edge.left = rect->right - 1; edge.top = rect->top;        edge.right = rect->right;     edge.bottom = rect->bottom - 1; FillC(hdc, &edge, bottomRight);
}

void Bevel(HDC hdc, const RECT *rect, int kind) {
    RECT area = *rect;
    switch (kind) {
    case BV_RAISED:
        UiFrame(hdc, &area, C_HI, C_LO);   InflateRect(&area, -1, -1); UiFrame(hdc, &area, C_HI2, C_LO2); break;
    case BV_SUNKEN:
        UiFrame(hdc, &area, C_LO2, C_HI);  InflateRect(&area, -1, -1); UiFrame(hdc, &area, C_LO, C_HI2);  break;
    case BV_ETCHED:
        UiFrame(hdc, &area, C_LO2, C_HI2); InflateRect(&area, -1, -1); UiFrame(hdc, &area, C_HI2, C_LO2); break;
    case BV_FLAT_DN:
        UiFrame(hdc, &area, C_LO2, C_HI2); break;
    }
}

void TextC(HDC hdc, const WCHAR *text, int length, RECT *rect, UINT format, COLORREF color) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, length, rect, format);
}

int TextW(HDC hdc, const WCHAR *text, int length) {
    SIZE size;
    size.cx = 0;
    GetTextExtentPoint32W(hdc, text, length < 0 ? wlen(text) : length, &size);
    return size.cx;
}

void Tri(HDC hdc, int x, int y, int size, COLORREF color) {
    POINT points[3];
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, color);
    points[0].x = x;        points[0].y = y;
    points[1].x = x;        points[1].y = y + size * 2;
    points[2].x = x + size; points[2].y = y + size;
    Polygon(hdc, points, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
}

void CheckGlyph(HDC hdc, int x, int y, COLORREF color) {
    static const POINT pt[6] = { {2, 6}, {3, 5}, {5, 7}, {9, 3}, {10, 4}, {5, 9} };
    POINT points[6];
    int i;
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    for (i = 0; i < 6; i++) { points[i].x = x + S(pt[i].x); points[i].y = y + S(pt[i].y); }
    SetDCBrushColor(hdc, color);
    Polygon(hdc, points, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
}

void DarkFrame(HWND hwnd, int active) {
    BOOL dark = g_theme == THEME_DARK;
    COLORREF captionColor = C_FACE, textColor = active ? C_ACCENT_FG : C_DIM;
    COLORREF borderColor = dark ? (active ? C_HI : C_HI2) : (active ? C_LO2 : RGB(0xa8, 0xa5, 0x9e));
    int corner = DWMWCP_DONOTROUND;
    if (!pDwmSet) return;
    pDwmSet(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    pDwmSet(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof captionColor);
    pDwmSet(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof textColor);
    pDwmSet(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof borderColor);
    pDwmSet(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
}

void DarkScroll(HWND hwnd) {
    if (pSetTheme) pSetTheme(hwnd, L"", L"");
    SbarAttach(hwnd);
}

void RegClass(const WCHAR *name, WNDPROC proc, UINT style, HBRUSH bg) {
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

void UiInit(HINSTANCE hInstance) {
    HMODULE module;
    g_hinst = hInstance;
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);
    module = LoadLibraryW(L"dwmapi.dll");
    if (module) pDwmSet = (DwmSetFn)GetProcAddress(module, "DwmSetWindowAttribute");
    module = LoadLibraryW(L"uxtheme.dll");
    if (module) pSetTheme = (ThemeFn)GetProcAddress(module, "SetWindowTheme");
    module = GetModuleHandleW(L"user32.dll");
    if (module) {
        pDpiWin = (DpiWinFn)GetProcAddress(module, "GetDpiForWindow");
        pDpiSys = (DpiSysFn)GetProcAddress(module, "GetDpiForSystem");
        pAdjDpi = (AdjDpiFn)GetProcAddress(module, "AdjustWindowRectExForDpi");
        pMetric = (MetricFn)GetProcAddress(module, "GetSystemMetricsForDpi");
    }
    g_dpi = UiSystemDpi();
    MakeFonts();
    ThemeSet(g_theme);
    ButtonInit();
    DialogInit();
}
