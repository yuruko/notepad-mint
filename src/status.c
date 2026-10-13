#include "mp.h"

static HWND  g_sb;
static WCHAR g_txt[SB_COUNT][64];
static int   g_sbH, g_hotp = -1;
static int   g_floor[SB_COUNT];
static int   g_need[SB_COUNT];

static int PadL(void) { return S(8); }
static int PadR(void) { return S(8); }
static int TextWidth(HDC dc, const WCHAR *t) { return TextW(dc, t, -1) + PadL() + PadR(); }
static int PanelW(int i) { return !g_txt[i][0] && i != SB_POS ? 0 : g_need[i] > g_floor[i] ? g_need[i] : g_floor[i]; }

static void Measure(void) {
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    int i;
    GetTextMetricsW(dc, &tm);
    g_sbH = tm.tmHeight + S(8);
    g_floor[SB_POS] = TextWidth(dc, L"99999999:99999");
    for (i = 0; i < SB_COUNT; i++) g_need[i] = TextWidth(dc, g_txt[i]);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
}

int StatusHeight(void) {
    if (!g_sbH) Measure();
    return g_sbH;
}

int StatusMinWidth(void) {
    int i, w = S(4);
    if (!g_sbH) Measure();
    for (i = 0; i < SB_COUNT; i++) w += PanelW(i);
    return w;
}

void StatusRefont(HWND sb) {
    Measure();
    InvalidateRect(sb, NULL, FALSE);
}

void StatusSet(HWND sb, int idx, const WCHAR *text) {
    if (idx < 0 || idx >= SB_COUNT) return;
    if (wcmp(g_txt[idx], text) == 0) return;
    wcopy(g_txt[idx], text, 64);
    if (g_sbH) {
        HDC dc = GetDC(NULL);
        HGDIOBJ of = SelectObject(dc, g_fontMenu);
        g_need[idx] = TextWidth(dc, g_txt[idx]);
        SelectObject(dc, of);
        ReleaseDC(NULL, dc);
    }
    InvalidateRect(sb, NULL, FALSE);
}

static void Panels(const RECT *rc, RECT out[SB_COUNT]) {
    int x = rc->right - S(1), i;
    if (!g_sbH) Measure();
    for (i = SB_COUNT - 1; i > SB_POS; i--) {
        out[i].right = x;
        x -= PanelW(i);
        out[i].left = x;
        out[i].top = S(2); out[i].bottom = rc->bottom - S(1);
    }
    out[SB_POS].left = S(1); out[SB_POS].right = x - S(2);
    out[SB_POS].top = S(2);  out[SB_POS].bottom = rc->bottom - S(1);
}

static int PanelAt(HWND h, int px, int py) {
    RECT rc, p[SB_COUNT];
    POINT pt;
    int i;
    GetClientRect(h, &rc);
    Panels(&rc, p);
    pt.x = px; pt.y = py;
    for (i = 0; i < SB_COUNT; i++) if (PtInRect(&p[i], pt)) return i;
    return -1;
}

static void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mdc;
    HBITMAP bmp;
    HGDIOBJ old, of;
    RECT rc, p[SB_COUNT], r;
    int i;
    GetClientRect(h, &rc);
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old = SelectObject(mdc, bmp);
    of = SelectObject(mdc, g_fontMenu);
    FillC(mdc, &rc, C_FACE);
    Panels(&rc, p);
    for (i = 0; i < SB_COUNT; i++) {
        BOOL hot = (i != SB_POS && i == g_hotp);
        if (p[i].right <= p[i].left) continue;
        Bevel(mdc, &p[i], BV_FLAT_DN);
        if (hot) { r = p[i]; InflateRect(&r, -1, -1); FillC(mdc, &r, C_ACCENT); }
        r = p[i]; r.left += PadL(); r.right -= PadR();
        TextC(mdc, g_txt[i], -1, &r, (i != SB_POS && i != SB_EOL ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, hot ? C_ON_ACCENT : C_TEXT);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    EndPaint(h, &ps);
}

static int AllText(WCHAR *out, int cap) {
    static const WCHAR sep[] = L" | ";
    int i, k, n = 0;
    for (i = 0; i < SB_COUNT; i++) {
        if (i && !g_txt[i][0]) continue;
        if (i) for (k = 0; sep[k]; k++, n++) if (out && n < cap - 1) out[n] = sep[k];
        for (k = 0; g_txt[i][k]; k++, n++) if (out && n < cap - 1) out[n] = g_txt[i][k];
    }
    if (out && cap > 0) out[n < cap ? n : cap - 1] = 0;
    return n;
}

static LRESULT CALLBACK StatusProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_GETTEXTLENGTH:
        return AllText(NULL, 0);
    case WM_GETTEXT: {
        int n = AllText((WCHAR *)l, (int)w);
        return n < (int)w ? n : ((int)w > 0 ? (int)w - 1 : 0); }
    case WM_SIZE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_PAINT:
        Paint(h);
        return 0;
    case WM_MOUSEMOVE: {
        int i = PanelAt(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i != SB_ZOOM && i < SB_EOL) i = -1;
        if (i != g_hotp) {
            TRACKMOUSEEVENT te;
            g_hotp = i;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0; }
    case WM_MOUSELEAVE:
        g_hotp = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int i = PanelAt(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i == SB_POS || i == SB_ZOOM) {
            g_hotp = -1;
            PostMessageW(GetParent(h), WM_COMMAND, i == SB_POS ? IDM_EDIT_GOTO : IDM_ZOOM_RESET, 0);
        } else if (i >= SB_EOL) {
            RECT rc, p[SB_COUNT];
            POINT pt;
            GetClientRect(h, &rc);
            Panels(&rc, p);
            pt.x = p[i].left; pt.y = 0;
            ClientToScreen(h, &pt);
            g_hotp = -1;
            MenuPopup(GetParent(h), i == SB_EOL ? &g_mdEol : &g_mdEnc, pt.x, pt.y, 1);
        }
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

HWND StatusCreate(HWND parent) {
    RegClass(L"mp_status", StatusProc, CS_HREDRAW | CS_VREDRAW, NULL);
    StatusHeight();
    g_sb = CreateWindowExW(0, L"mp_status", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, g_sbH, parent, NULL, g_hinst, NULL);
    return g_sb;
}
