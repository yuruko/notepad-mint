#include "mp.h"

static HWND  g_sb;
static WCHAR g_txt[SB_COUNT][64];
static int   g_sbH, g_hotp = -1;
static int   g_floor[SB_COUNT];
static int   g_need[SB_COUNT];

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
    int i, width = S(4);
    if (!g_sbH) Measure();
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
    int x = client->right - S(1), i;
    if (!g_sbH) Measure();
    for (i = SB_COUNT - 1; i > SB_POS; i--) {
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
    int i;
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
        TextC(memDc, g_txt[i], -1, &rect, (i != SB_POS && i != SB_EOL ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, hot ? C_ON_ACCENT : C_TEXT);
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
    case WM_MOUSEMOVE: {
        int panel = PanelAt(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (panel != SB_ZOOM && panel < SB_EOL) panel = -1;
        if (panel != g_hotp) {
            TRACKMOUSEEVENT trackEvent;
            g_hotp = panel;
            trackEvent.cbSize = sizeof trackEvent; trackEvent.dwFlags = TME_LEAVE; trackEvent.hwndTrack = hwnd; trackEvent.dwHoverTime = 0;
            TrackMouseEvent(&trackEvent);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0; }
    case WM_MOUSELEAVE:
        g_hotp = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int panel = PanelAt(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (panel == SB_POS || panel == SB_ZOOM) {
            g_hotp = -1;
            PostMessageW(GetParent(hwnd), WM_COMMAND, panel == SB_POS ? IDM_EDIT_GOTO : IDM_ZOOM_RESET, 0);
        } else if (panel >= SB_EOL) {
            RECT client, panels[SB_COUNT];
            POINT pt;
            GetClientRect(hwnd, &client);
            Panels(&client, panels);
            pt.x = panels[panel].left; pt.y = 0;
            ClientToScreen(hwnd, &pt);
            g_hotp = -1;
            MenuPopup(GetParent(hwnd), panel == SB_EOL ? &g_mdEol : &g_mdEnc, pt.x, pt.y, 1);
        }
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND StatusCreate(HWND parent) {
    RegClass(L"mp_status", StatusProc, CS_HREDRAW | CS_VREDRAW, NULL);
    StatusHeight();
    g_sb = CreateWindowExW(0, L"mp_status", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, g_sbH, parent, NULL, g_hinst, NULL);
    return g_sb;
}
