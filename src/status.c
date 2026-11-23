#include "mp.h"

#define BW        2
#define TIP_TIMER 1
#define ICON_SS   8
#define ICON_MAX  64

enum { TB_LIGHT, TB_DARK, TB_WRAP, TB_ONTOP, TB_INVIS, TB_COUNT };
enum { BT_ONTOP, BT_INVIS, BT_WRAP, BT_COUNT };
static const struct { int id; const WCHAR *tip; } g_btn[BT_COUNT] = {
    [BT_ONTOP] = { IDM_VIEW_ONTOP,      L"toggle always on top" },
    [BT_INVIS] = { IDM_VIEW_INVISIBLES, L"show / hide invisible characters" },
    [BT_WRAP]  = { IDM_FMT_WRAP,     L"toggle word wrap" },
};

static HWND  g_sb, g_tip, g_tipHost;
static RECT  g_tipAnchor;
static WCHAR g_tipText[96];
static int   g_tipBelow;
static WCHAR g_txt[SB_COUNT][64];
static int   g_sbH, g_hotp = -1, g_hotBtn = -1;
static int   g_floor[SB_COUNT];
static int   g_need[SB_COUNT];

static int BtnCount(void) { return BT_COUNT; }
static int BtnW(int button) { return S(BAR_BTN_W); }
static int BtnsW(void) {
    int button, width = 0;
    for (button = 0; button < BT_COUNT; button++) width += BtnW(button);
    return width;
}
static int PanelW(int index);
static void Measure(void);

static int BtnsRight(int clientWidth) {
    if (!g_sbH) Measure();
    return clientWidth - S(2) - PanelW(SB_ENC);
}

static void BtnRect(int clientWidth, int clientHeight, int button, RECT *rect) {
    int next;
    rect->left = BtnsRight(clientWidth);
    for (next = button; next < BT_COUNT; next++) rect->left -= BtnW(next);
    rect->right = rect->left + BtnW(button);
    rect->top = S(2);
    rect->bottom = clientHeight - S(1);
}

static int BtnHit(HWND hwnd, int x, int y) {
    RECT client, rect;
    int button;
    GetClientRect(hwnd, &client);
    if (y < 0 || y >= client.bottom) return -1;
    for (button = 0; button < BtnCount(); button++) {
        BtnRect(client.right, client.bottom, button, &rect);
        if (x >= rect.left && x < rect.right) return button;
    }
    return -1;
}

static int BtnOpacity(int button) {
    if (button == BT_WRAP) return g_pf.wrap ? BAR_BTN_OPACITY_ON : BAR_BTN_OPACITY;
    if (button == BT_INVIS) return EditInvisiblesShown() ? BAR_BTN_OPACITY_ON : BAR_BTN_OPACITY;
    if (button == BT_ONTOP) return (GetWindowLongPtrW(GetParent(g_sb), GWL_EXSTYLE) & WS_EX_TOPMOST) ? BAR_BTN_OPACITY_ON : BAR_BTN_OPACITY;
    return BAR_BTN_OPACITY;
}

#define D(v) ((v) * 64)
#define PX(k) ((k) * 299 - 149)
static int IconInside(int kind, int x, int y) {
    int dx = x - D(28), dy = y - D(28), ax, ay, bx, by;
    if (kind == TB_WRAP) {
        if (y >= PX(1) && y < PX(2) && x >= PX(1) && x < PX(11)) return 1;
        if (y >= PX(3) && y < PX(4) && x >= PX(1) && x < PX(10)) return 1;
        if (x >= PX(9) && x < PX(10) && y >= PX(3) && y < PX(7)) return 1;
        if (y >= PX(6) && y < PX(7) && x >= PX(3) && x < PX(10)) return 1;
        ax = x - PX(2); ay = y - (PX(6) + 150); if (ay < 0) ay = -ay;
        if (ax >= 0 && x < PX(5) && ay * 2 <= ax + 150) return 1;
        return y >= PX(9) && y < PX(10) && x >= PX(1) && x < PX(6);
    }
    if (kind == TB_INVIS) {
        if (y >= PX(1) && y < PX(2) && x >= PX(3) && x < PX(10)) return 1;
        if (y >= PX(1) && y < PX(11) && ((x >= PX(6) && x < PX(7)) || (x >= PX(8) && x < PX(9)))) return 1;
        ax = x - PX(6); ay = y - (PX(1) + 3 * 299);
        return x < PX(6) && ax * ax + ay * ay < 897 * 897;
    }
    if (kind == TB_ONTOP) {
        ax = x - PX(6); if (ax < 0) ax = -ax;
        if (y >= PX(1) && y < PX(2)) return ax < 3 * 299;
        if (y >= PX(2) && y < PX(6)) return ax < 2 * 299;
        if (y >= PX(6) && y < PX(7)) return ax < 4 * 299;
        return y >= PX(7) && y < PX(11) && ax < 150;
    }
    if (kind == TB_LIGHT) {
        if (dx * dx + dy * dy < D(11) * D(11)) return 1;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 154 && dy >= 1043 && dy < D(25)) return 1;
        if (dy < 154 && dx >= 1043 && dx < D(25)) return 1;
        return dx - dy < 217 && dy - dx < 217 && dx + dy >= 1448 && dx + dy < 2037;
    }
    ax = x - D(26); ay = y - D(30); bx = x - D(38); by = y - D(22);
    return ax * ax + ay * ay < D(22) * D(22) && bx * bx + by * by >= D(19) * D(19);
}
#undef PX
#undef D

static BYTE g_icCov[TB_COUNT][ICON_MAX * ICON_MAX];
static int  g_icSize[TB_COUNT];

static void IconCoverage(int kind, int size, BYTE *out) {
    int grid[ICON_MAX * ICON_SS], count, i, j, subX, subY, coverage, shift;
    count = size * ICON_SS;
    shift = (size & 1) ? 0 : 1792 / size;
    for (i = 0; i < count; i++) grid[i] = (2 * i + 1) * 56 * 32 / count - shift;
    for (j = 0; j < size; j++) for (i = 0; i < size; i++) {
            coverage = 0;
            for (subY = 0; subY < ICON_SS; subY++) for (subX = 0; subX < ICON_SS; subX++) coverage += IconInside(kind, grid[i * ICON_SS + subX], grid[j * ICON_SS + subY]);
            out[j * size + i] = (BYTE)coverage;
        }
}

static void ThemeIcon(HDC hdc, int kind, int x, int y, int size, COLORREF fg, COLORREF bg, int opacity) {
    int i, j, coverage, num, den = ICON_SS * ICON_SS * 100;
    const BYTE *coverageMap;
    if (size > ICON_MAX) size = ICON_MAX;
    if (g_icSize[kind] != size) { IconCoverage(kind, size, g_icCov[kind]); g_icSize[kind] = size; }
    coverageMap = g_icCov[kind];
    for (j = 0; j < size; j++) {
        for (i = 0; i < size; i++) {
            coverage = coverageMap[j * size + i];
            if (!coverage) continue;
            num = coverage * opacity;
            SetPixelV(hdc, x + i, y + j, num >= den ? fg : RGB(GetRValue(bg) + (GetRValue(fg) - GetRValue(bg)) * num / den, GetGValue(bg) + (GetGValue(fg) - GetGValue(bg)) * num / den, GetBValue(bg) + (GetBValue(fg) - GetBValue(bg)) * num / den));
        }
    }
}

void ThemeIconDraw(HDC hdc, int x, int y, int size, COLORREF fg, COLORREF bg, int opacity) {
    ThemeIcon(hdc, ThemeGet() == THEME_LIGHT ? TB_LIGHT : TB_DARK, x, y, size, fg, bg, opacity);
}

static void CALLBACK TipTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    TipTimer();
}

void TipHide(void) {
    if (g_tipHost) KillTimer(g_tipHost, TIP_TIMER);
    if (g_tip) DestroyWindow(g_tip);
    g_tip = NULL;
}

static void TipShow(void) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW tm;
    MONITORINFO mi;
    POINT pt;
    int width, height;
    GetTextMetricsW(hdc, &tm);
    width = TextW(hdc, g_tipText, -1) + 2 * (BW + S(7));
    height = tm.tmHeight + 2 * (BW + S(3));
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
    pt.x = g_tipAnchor.right - width;
    pt.y = g_tipBelow ? g_tipAnchor.bottom + S(2) : g_tipAnchor.top - height - S(2);
    ClientToScreen(g_tipHost, &pt);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    if (pt.x + width > mi.rcWork.right) pt.x = mi.rcWork.right - width;
    if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
    g_tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"mp_tip", g_tipText, WS_POPUP, pt.x, pt.y, width, height, g_hwnd, NULL, g_hinst, NULL);
    ShowWindow(g_tip, SW_SHOWNOACTIVATE);
    SetTimer(g_tipHost, TIP_TIMER, BAR_TIP_SHOW, TipTimerProc);
}

void TipTimer(void) {
    KillTimer(g_tipHost, TIP_TIMER);
    if (g_tip) TipHide();
    else TipShow();
}

void TipTrack(HWND host, const RECT *anchor, const WCHAR *tip, int id, int below) {
    BOOL wasShown = g_tip != NULL;
    const WCHAR *accel = id ? MenuAccelOf(id) : NULL;
    TipHide();
    if (!anchor) return;
    g_tipHost = host; g_tipAnchor = *anchor; g_tipBelow = below;
    wcopy(g_tipText, tip, 96);
    if (accel) { wcat(g_tipText, L" (", 96); wcat(g_tipText, accel, 96); wcat(g_tipText, L")", 96); }
    if (wasShown) TipShow();
    else SetTimer(host, TIP_TIMER, BAR_TIP_DELAY, TipTimerProc);
}

static LRESULT CALLBACK TipProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return (LRESULT)-1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), memDc;
        HBITMAP bitmap;
        HGDIOBJ oldBitmap, oldFont;
        RECT client, textRect;
        WCHAR text[96];
        int length = GetWindowTextW(hwnd, text, 96);
        GetClientRect(hwnd, &client);
        memDc = CreateCompatibleDC(hdc);
        bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        oldBitmap = SelectObject(memDc, bitmap);
        oldFont = SelectObject(memDc, g_fontMenu);
        FillC(memDc, &client, C_FACE2);
        Bevel(memDc, &client, BV_RAISED);
        textRect = client; textRect.left = BW + S(7);
        TextC(memDc, text, length, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, C_TEXT);
        BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldFont);
        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd, &ps);
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int PadL(void) { return S(8); }
static int PadR(void) { return S(8); }
static int TextWidth(HDC hdc, const WCHAR *text) { return TextW(hdc, text, -1) + PadL() + PadR(); }
static int PanelW(int index) { return !g_txt[index][0] && index != SB_POS ? 0 : g_need[index] > g_floor[index] ? g_need[index] : g_floor[index]; }

static void Measure(void) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW metrics;
    int i;
    GetTextMetricsW(hdc, &metrics);
    g_sbH = metrics.tmHeight + S(8);
    g_floor[SB_POS] = TextWidth(hdc, L"99999999:99999");
    for (i = 0; i < SB_COUNT; i++) g_need[i] = TextWidth(hdc, g_txt[i]);
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
}

int StatusHeight(void) {
    if (!g_sbH) Measure();
    return g_sbH;
}

int StatusMinWidth(void) {
    int i, width;
    if (!g_sbH) Measure();
    width = S(4) + BtnsW();
    for (i = 0; i < SB_COUNT; i++) width += PanelW(i);
    return width;
}

void StatusRefont(HWND hwnd) {
    Measure();
    InvalidateRect(hwnd, NULL, FALSE);
}

void StatusSet(HWND hwnd, int index, const WCHAR *text) {
    if (index < 0 || index >= SB_COUNT) return;
    if (wcmp(g_txt[index], text) == 0) return;
    wcopy(g_txt[index], text, 64);
    if (g_sbH) {
        HDC hdc = GetDC(NULL);
        HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
        g_need[index] = TextWidth(hdc, g_txt[index]);
        SelectObject(hdc, oldFont);
        ReleaseDC(NULL, hdc);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void Panels(const RECT *client, RECT out[SB_COUNT]) {
    int x = BtnsRight(client->right) - BtnsW(), i;
    out[SB_ENC].right = client->right - S(2);
    out[SB_ENC].left = out[SB_ENC].right - PanelW(SB_ENC);
    out[SB_ENC].top = S(2); out[SB_ENC].bottom = client->bottom - S(1);
    for (i = SB_ENC - 1; i > SB_POS; i--) {
        out[i].right = x;
        x -= PanelW(i);
        out[i].left = x;
        out[i].top = S(2); out[i].bottom = client->bottom - S(1);
    }
    out[SB_POS].left = S(1); out[SB_POS].right = x - S(2);
    out[SB_POS].top = S(2);  out[SB_POS].bottom = client->bottom - S(1);
}

static int PanelAt(HWND hwnd, int px, int py) {
    RECT client, panels[SB_COUNT];
    POINT pt;
    int i;
    GetClientRect(hwnd, &client);
    Panels(&client, panels);
    pt.x = px; pt.y = py;
    for (i = 0; i < SB_COUNT; i++) if (PtInRect(&panels[i], pt)) return i;
    return -1;
}

static void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps), memDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap, oldFont;
    RECT client, panels[SB_COUNT], rect;
    int i, iconSize = S(BAR_BTN_ICON);
    GetClientRect(hwnd, &client);
    memDc = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    oldBitmap = SelectObject(memDc, bitmap);
    oldFont = SelectObject(memDc, g_fontMenu);
    FillC(memDc, &client, C_FACE);
    Panels(&client, panels);
    for (i = 0; i < SB_COUNT; i++) {
        BOOL hot = (i != SB_POS && i == g_hotp);
        if (panels[i].right <= panels[i].left) continue;
        Bevel(memDc, &panels[i], BV_FLAT_DN);
        if (hot) { rect = panels[i]; InflateRect(&rect, -1, -1); FillC(memDc, &rect, C_ACCENT); }
        rect = panels[i]; rect.left += PadL(); rect.right -= PadR();
        TextC(memDc, g_txt[i], -1, &rect, (i != SB_POS ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, hot ? C_ON_ACCENT : C_TEXT);
    }
    for (i = 0; i < BtnCount(); i++) {
        BOOL hot = i == g_hotBtn;
        int kind = i == BT_ONTOP ? TB_ONTOP : i == BT_INVIS ? TB_INVIS : TB_WRAP;
        BtnRect(client.right, client.bottom, i, &rect);
        Bevel(memDc, &rect, BV_FLAT_DN);
        if (hot) { RECT fill = rect; InflateRect(&fill, -1, -1); FillC(memDc, &fill, C_ACCENT); }
        ThemeIcon(memDc, kind, (rect.left + rect.right - iconSize) / 2, (rect.top + rect.bottom - iconSize) / 2 + (i == BT_WRAP ? S(BAR_WRAP_ICON_DY) : i == BT_ONTOP ? S(BAR_PIN_ICON_DY) : S(BAR_INVIS_ICON_DY)), iconSize, hot ? C_ON_ACCENT : C_TEXT, hot ? C_ACCENT : C_FACE, hot ? 100 : BtnOpacity(i));
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldFont);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd, &ps);
}

static int AllText(WCHAR *out, int cap) {
    static const WCHAR sep[] = L" | ";
    int i, k, length = 0;
    for (i = 0; i < SB_COUNT; i++) {
        if (i && !g_txt[i][0]) continue;
        if (i) for (k = 0; sep[k]; k++, length++) if (out && length < cap - 1) out[length] = sep[k];
        for (k = 0; g_txt[i][k]; k++, length++) if (out && length < cap - 1) out[length] = g_txt[i][k];
    }
    if (out && cap > 0) out[length < cap ? length : cap - 1] = 0;
    return length;
}

static LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_GETTEXTLENGTH:
        return AllText(NULL, 0);
    case WM_GETTEXT: {
        int length = AllText((WCHAR *)lParam, (int)wParam);
        return length < (int)wParam ? length : ((int)wParam > 0 ? (int)wParam - 1 : 0); }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
#ifdef MENU_NO_TRACK
    case WM_APP + 91:
        return PROBE_ID;
#endif
    case WM_MOUSEMOVE: {
        int button = BtnHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        int panel = button < 0 ? PanelAt(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
        if (panel != SB_ENC) panel = -1;
        if (panel != g_hotp || button != g_hotBtn) {
            TRACKMOUSEEVENT trackEvent;
            if (button != g_hotBtn) {
                RECT client, rect;
                GetClientRect(hwnd, &client);
                if (button >= 0) BtnRect(client.right, client.bottom, button, &rect);
                TipTrack(hwnd, button >= 0 ? &rect : NULL, button >= 0 ? g_btn[button].tip : NULL, button >= 0 ? g_btn[button].id : 0, 0);
            }
            g_hotp = panel; g_hotBtn = button;
            trackEvent.cbSize = sizeof trackEvent; trackEvent.dwFlags = TME_LEAVE; trackEvent.hwndTrack = hwnd; trackEvent.dwHoverTime = 0;
#ifndef MENU_NO_TRACK
            TrackMouseEvent(&trackEvent);
#endif
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0; }
    case WM_MOUSELEAVE:
        TipTrack(hwnd, NULL, NULL, 0, 0);
        g_hotp = -1; g_hotBtn = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int button = BtnHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        int panel = button < 0 ? PanelAt(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
        TipHide();
        if (button >= 0) { PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(g_btn[button].id, 0), 0); return 0; }
        if (panel == SB_POS) {
            g_hotp = -1;
            PostMessageW(GetParent(hwnd), WM_COMMAND, IDM_EDIT_GOTO, 0);
        } else if (panel == SB_ENC) {
            RECT client, panels[SB_COUNT];
            POINT pt;
            GetClientRect(hwnd, &client);
            Panels(&client, panels);
            pt.x = panels[panel].left; pt.y = 0;
            ClientToScreen(hwnd, &pt);
            g_hotp = -1;
            MenuPopup(GetParent(hwnd), &g_mdEncEol, pt.x, pt.y, 1);
        }
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND StatusCreate(HWND parent) {
    RegClass(L"mp_tip", TipProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_status", StatusProc, CS_HREDRAW | CS_VREDRAW, NULL);
    StatusHeight();
    g_sb = CreateWindowExW(0, L"mp_status", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, g_sbH, parent, NULL, g_hinst, NULL);
    return g_sb;
}
