#include "mp.h"

#ifndef FRAME_CUSTOM
#define FRAME_CUSTOM 1
#endif

int FrameEnabled(void) { return FRAME_CUSTOM; }

enum { FB_MIN, FB_MAX, FB_CLOSE, FB_COUNT };

static int   g_capH, g_hot = -1, g_down = -1, g_active = 1, g_track;
static HICON g_icon;

static int BtnW(void) { return S(38); }
static int Edge(void) { return UiMetric(SM_CYFRAME) + UiMetric(SM_CXPADDEDBORDER); }
static int Crop(void) { return S(1); }

static void Metrics(void) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW textMetrics;
    GetTextMetricsW(hdc, &textMetrics);
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
    g_capH = textMetrics.tmHeight + S(10) - Crop();
    if (g_capH < S(24) - Crop()) g_capH = S(24) - Crop();
}

int FrameHeight(void) {
    if (!FRAME_CUSTOM) return 0;
    if (!g_capH) Metrics();
    return g_capH;
}

static int ContentH(void) { return FrameHeight() + Crop(); }

void FrameRefont(HWND hwnd) {
    if (!FRAME_CUSTOM) return;
    if (g_icon) { DestroyIcon(g_icon); g_icon = NULL; }
    Metrics();
    if (hwnd) InvalidateRect(hwnd, NULL, FALSE);
}

void FrameInvalidate(HWND hwnd) {
    RECT client;
    if (!FRAME_CUSTOM || !hwnd) return;
    GetClientRect(hwnd, &client);
    client.bottom = FrameHeight();
    InvalidateRect(hwnd, &client, FALSE);
}

void FrameActive(HWND hwnd, int active) {
    g_active = active;
    if (!active && g_down < 0) g_hot = -1;
    FrameInvalidate(hwnd);
}

static void BtnRect(int button, int clientWidth, RECT *rect) {
    rect->right = clientWidth - (FB_COUNT - 1 - button) * BtnW();
    rect->left = rect->right - BtnW();
    rect->top = 0;
    rect->bottom = FrameHeight();
}

static int HitBtn(int x, int y, int clientWidth) {
    POINT pt;
    RECT rect;
    int button;
    pt.x = x; pt.y = y;
    for (button = 0; button < FB_COUNT; button++) {
        BtnRect(button, clientWidth, &rect);
        if (PtInRect(&rect, pt)) return button;
    }
    return -1;
}

static int IconLeft(void) { return S(8); }
static int IconRight(void) { return IconLeft() + S(16) + S(6); }
static int InIcon(int x, int y) { return x >= 0 && x < IconRight() && y >= 0 && y < FrameHeight(); }

static void Line(HDC hdc, int x1, int y1, int x2, int y2) {
    MoveToEx(hdc, x1, y1, NULL);
    LineTo(hdc, x2, y2);
}

static void DrawBtn(HDC hdc, int button, const RECT *rect, int hot, int down, int zoomed) {
    COLORREF bg = C_FACE, fg = g_active ? C_TEXT : C_DIM;
    HPEN pen;
    HGDIOBJ oldPen;
    int centerX = (rect->left + rect->right) / 2, centerY = (rect->top + rect->bottom) / 2, half = S(4), offset = S(2), penWidth = S(1);
    if (button == FB_CLOSE && (hot || down)) {
        bg = down ? RGB(0xf1, 0x70, 0x7a) : RGB(0xe8, 0x11, 0x23);
        fg = RGB(0xff, 0xff, 0xff);
    } else if (down) {
        bg = RGB(0x7c, 0xc4, 0x98); fg = C_ON_ACCENT;
    } else if (hot) {
        bg = C_ACCENT; fg = C_ON_ACCENT;
    }
    if (hot || down) FillC(hdc, rect, bg);
    pen = CreatePen(PS_SOLID, penWidth < 1 ? 1 : penWidth, fg);
    oldPen = SelectObject(hdc, pen);
    switch (button) {
    case FB_MIN:
        Line(hdc, centerX - half, centerY, centerX + half + 1, centerY);
        break;
    case FB_MAX:
        if (!zoomed) {
            Line(hdc, centerX - half, centerY - half, centerX + half, centerY - half);
            Line(hdc, centerX + half, centerY - half, centerX + half, centerY + half);
            Line(hdc, centerX + half, centerY + half, centerX - half, centerY + half);
            Line(hdc, centerX - half, centerY + half, centerX - half, centerY - half);
        } else {
            Line(hdc, centerX - half, centerY - half + offset, centerX + half - offset, centerY - half + offset);
            Line(hdc, centerX + half - offset, centerY - half + offset, centerX + half - offset, centerY + half);
            Line(hdc, centerX + half - offset, centerY + half, centerX - half, centerY + half);
            Line(hdc, centerX - half, centerY + half, centerX - half, centerY - half + offset);
            Line(hdc, centerX - half + offset, centerY - half + offset, centerX - half + offset, centerY - half);
            Line(hdc, centerX - half + offset, centerY - half, centerX + half, centerY - half);
            Line(hdc, centerX + half, centerY - half, centerX + half, centerY + half - offset);
            Line(hdc, centerX + half, centerY + half - offset, centerX + half - offset, centerY + half - offset);
        }
        break;
    case FB_CLOSE:
        Line(hdc, centerX - half, centerY - half, centerX + half + 1, centerY + half + 1);
        Line(hdc, centerX + half, centerY - half, centerX - half - 1, centerY + half + 1);
        break;
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

#define STRIP_TINT 9
#define STRIP_TINT_LIGHT_PLUS 12
#define STRIP_TINT_INACTIVE_MINUS 7
static COLORREF Tint(COLORREF face, COLORREF accent, int amount) {
    int red = GetRValue(face) + (GetRValue(accent) - GetRValue(face)) * amount / 255;
    int green = GetGValue(face) + (GetGValue(accent) - GetGValue(face)) * amount / 255;
    int blue = GetBValue(face) + (GetBValue(accent) - GetBValue(face)) * amount / 255;
    return RGB(red, green, blue);
}

static void FillStrip(HDC hdc, int width, int height) {
    RECT rect;
    int percent = STRIP_TINT + (ThemeGet() == THEME_LIGHT ? STRIP_TINT_LIGHT_PLUS : 0) - (g_active ? 0 : STRIP_TINT_INACTIVE_MINUS);
    if (percent < 0) percent = 0;
    rect.left = 0; rect.top = 0; rect.right = width; rect.bottom = height;
    FillC(hdc, &rect, Tint(C_FACE, C_ACCENT, percent * 255 / 100));
}

void FramePaint(HWND hwnd, HDC hdc) {
    RECT client, buttonRect, titleRect;
    HDC memDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap, oldFont;
    WCHAR title[PATH_CAP + 64];
    int width, height, button, iconSize = S(16), length, boldLength, boldWidth;
    COLORREF fg;
    if (!FRAME_CUSTOM) return;
    GetClientRect(hwnd, &client);
    width = client.right;
    height = FrameHeight();
    if (width <= 0) return;
    memDc = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, width, height);
    oldBitmap = SelectObject(memDc, bitmap);
    oldFont = SelectObject(memDc, g_fontMenu);
    FillStrip(memDc, width, height);
    if (!g_icon) g_icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
    if (g_icon) DrawIconEx(memDc, IconLeft(), (ContentH() - iconSize) / 2, g_icon, iconSize, iconSize, 0, NULL, DI_NORMAL);
    GetWindowTextW(hwnd, title, COUNTOF(title));
    BtnRect(FB_MIN, width, &buttonRect);
    titleRect.left = IconRight() + S(4); titleRect.right = buttonRect.left - S(8); titleRect.top = 0; titleRect.bottom = ContentH();
    fg = g_active ? (ThemeGet() == THEME_DARK ? RGB(0xff, 0xff, 0xff) : RGB(0, 0, 0)) : C_DIM;
    length = wlen(title);
    boldLength = length - wlen(TITLE_TAIL);
    if (boldLength < 0 || wcmp(title + boldLength, TITLE_TAIL) != 0) boldLength = length;
    SelectObject(memDc, g_fontMenuB);
    boldWidth = TextW(memDc, title, boldLength);
    TextC(memDc, title, boldLength, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, fg);
    SelectObject(memDc, g_fontMenu);
    if (boldLength < length && boldWidth < titleRect.right - titleRect.left) {
        titleRect.left += boldWidth;
        TextC(memDc, title + boldLength, length - boldLength, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, fg);
    }
    for (button = 0; button < FB_COUNT; button++) {
        BtnRect(button, width, &buttonRect);
        DrawBtn(memDc, button, &buttonRect, button == g_hot, button == g_down && button == g_hot, IsZoomed(hwnd) ? 1 : 0);
    }
    BitBlt(hdc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldFont);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
}

LRESULT FrameNcCalc(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    RECT *rect = (RECT *)lParam;
    RECT windowRect;
    LRESULT ret;
    if (!FRAME_CUSTOM || !rect) return DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
    windowRect = *rect;
    ret = DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
    if (!IsIconic(hwnd)) {
        rect->top = windowRect.top;
        if (IsZoomed(hwnd)) rect->top += rect->left - windowRect.left;
    }
    DBG(L"nccalc w/top-inset", wParam, rect->top - windowRect.top);
    return ret;
}

LRESULT FrameHitTest(HWND hwnd, LPARAM lParam) {
    POINT pt;
    RECT client;
    int button, border;
    LRESULT hit = DefWindowProcW(hwnd, WM_NCHITTEST, 0, lParam);
    if (!FRAME_CUSTOM || hit != HTCLIENT) return hit;
    pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
    ScreenToClient(hwnd, &pt);
    if (pt.y < 0 || pt.y >= FrameHeight()) return HTCLIENT;
    GetClientRect(hwnd, &client);
    button = HitBtn(pt.x, pt.y, client.right);
    border = button >= 0 ? Edge() / 2 : Edge();
    if (!IsZoomed(hwnd) && pt.y < border) {
        if (pt.x < S(16)) return HTTOPLEFT;
        if (pt.x >= client.right - S(16)) return HTTOPRIGHT;
        return HTTOP;
    }
    if (button >= 0 || InIcon(pt.x, pt.y)) return HTCLIENT;
    return HTCAPTION;
}

void FrameSysMenu(HWND hwnd, int x, int y) {
    POINT pt;
    if (x == -1 && y == -1) {
        pt.x = 0; pt.y = FrameHeight();
        ClientToScreen(hwnd, &pt);
        x = pt.x; y = pt.y;
    }
    MenuPopup(hwnd, &g_mdSys, x, y, 0);
}

unsigned FrameSysState(int id) {
    HWND hwnd = g_hwnd;
    int maximized = IsZoomed(hwnd) ? 1 : 0, minimized = IsIconic(hwnd) ? 1 : 0;
    switch (id) {
    case IDM_SYS_RESTORE: return (maximized || minimized) ? 0 : MS_GRAY;
    case IDM_SYS_MOVE:
    case IDM_SYS_SIZE:
    case IDM_SYS_MAX:     return maximized ? MS_GRAY : 0;
    }
    return 0;
}

void FrameSysCommand(HWND hwnd, int id) {
    UINT sysCommand = 0;
    switch (id) {
    case IDM_SYS_RESTORE: sysCommand = SC_RESTORE; break;
    case IDM_SYS_MOVE:    sysCommand = SC_MOVE; break;
    case IDM_SYS_SIZE:    sysCommand = SC_SIZE; break;
    case IDM_SYS_MIN:     sysCommand = SC_MINIMIZE; break;
    case IDM_SYS_MAX:     sysCommand = SC_MAXIMIZE; break;
    case IDM_SYS_CLOSE:   sysCommand = SC_CLOSE; break;
    }
    if (sysCommand) PostMessageW(hwnd, WM_SYSCOMMAND, sysCommand, 0);
}

BOOL FrameMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam), button;
    RECT client;
    POINT pt;
    if (!FRAME_CUSTOM) return FALSE;
    switch (msg) {
    case WM_MOUSEMOVE:
        GetClientRect(hwnd, &client);
        button = (y >= 0 && y < FrameHeight()) ? HitBtn(x, y, client.right) : -1;
        if (g_down >= 0 && button != g_down) button = -1;
        if (button >= 0 && !g_track) {
            TRACKMOUSEEVENT trackEvent;
            trackEvent.cbSize = sizeof trackEvent; trackEvent.dwFlags = TME_LEAVE; trackEvent.hwndTrack = hwnd; trackEvent.dwHoverTime = 0;
            g_track = TrackMouseEvent(&trackEvent) ? 1 : 0;
        }
        if (button != g_hot) { g_hot = button; FrameInvalidate(hwnd); }
        return TRUE;
    case WM_MOUSELEAVE:
        g_track = 0;
        if (g_hot >= 0 && g_down < 0) { g_hot = -1; FrameInvalidate(hwnd); }
        return TRUE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (y < 0 || y >= FrameHeight()) return FALSE;
        GetClientRect(hwnd, &client);
        button = HitBtn(x, y, client.right);
        if (button >= 0) {
            g_down = button; g_hot = button;
            SetCapture(hwnd);
            FrameInvalidate(hwnd);
            return TRUE;
        }
        if (InIcon(x, y)) { FrameSysMenu(hwnd, -1, -1); return TRUE; }
        return FALSE;
    case WM_LBUTTONUP:
        if (g_down >= 0) {
            int pressed = g_down;
            GetClientRect(hwnd, &client);
            button = (y >= 0 && y < FrameHeight()) ? HitBtn(x, y, client.right) : -1;
            g_down = -1;
            ReleaseCapture();
            g_hot = button;
            FrameInvalidate(hwnd);
            if (button == pressed) {
                UINT sysCommand = (pressed == FB_MIN) ? SC_MINIMIZE : (pressed == FB_CLOSE ? SC_CLOSE : (IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE));
                PostMessageW(hwnd, WM_SYSCOMMAND, sysCommand, 0);
            }
            return TRUE;
        }
        return FALSE;
    case WM_RBUTTONUP:
        if (y < 0 || y >= FrameHeight()) return FALSE;
        pt.x = x; pt.y = y;
        ClientToScreen(hwnd, &pt);
        FrameSysMenu(hwnd, pt.x, pt.y);
        return TRUE;
    case WM_NCRBUTTONDOWN:
        return wParam == HTCAPTION;
    case WM_CAPTURECHANGED:
        if (g_down >= 0) { g_down = -1; g_hot = -1; FrameInvalidate(hwnd); }
        return FALSE;
    }
    return FALSE;
}
