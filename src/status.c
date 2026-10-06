/* status.c - classic status bar: ln/col | font size | line ending | encoding. no size grip: the window is resized by its frame.
 * the size / line-ending / encoding panels are clickable (they pop a menu). drawn in the chrome font (g_fontMenu, the same
 * font as the menu bar), so the panel widths are measured from the text they can show rather than fixed. */
#include "mp.h"

static HWND  g_sb;
static WCHAR g_txt[SB_COUNT][64];
static int   g_sbH, g_hotp = -1;
static int   g_pw[SB_COUNT];                    /* device-px widths of the clickable panels (SB_POS takes the rest) */

/* names the encoding panel can show besides g_encName[]: the widest code page labels */
static const WCHAR *const g_wide[] = { L"ks_c_5601-1987", L"x-mac-cyrillic", L"windows-1252", L"iso-8859-8-i" };

static void Measure(void)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    int i, w, m;

    GetTextMetricsW(dc, &tm);
    g_sbH = tm.tmHeight + S(8);
    g_pw[SB_POS] = 0;
    g_pw[SB_ZOOM] = TextW(dc, L"96 pt", -1) + S(14);
    for (m = 0, i = 0; i < EOL_COUNT; i++) { w = TextW(dc, g_eolName[i], -1); if (w > m) m = w; }
    g_pw[SB_EOL] = m + S(14);
    for (m = 0, i = 0; i < ENC_COUNT; i++) { w = TextW(dc, g_encName[i], -1); if (w > m) m = w; }
    for (i = 0; i < COUNTOF(g_wide); i++) { w = TextW(dc, g_wide[i], -1); if (w > m) m = w; }
    g_pw[SB_ENC] = m + S(14);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
}

int StatusHeight(void)
{
    if (!g_sbH) Measure();
    return g_sbH;
}

/* narrowest client width that still leaves room for the position panel */
int StatusMinWidth(void)
{
    if (!g_sbH) Measure();
    return g_pw[SB_ZOOM] + g_pw[SB_EOL] + g_pw[SB_ENC] + S(1) + S(110);
}

void StatusRefont(HWND sb)
{
    Measure();
    InvalidateRect(sb, NULL, FALSE);
}

void StatusSet(HWND sb, int idx, const WCHAR *text)
{
    if (idx < 0 || idx >= SB_COUNT) return;
    if (wcmp(g_txt[idx], text) == 0) return;
    wcopy(g_txt[idx], text, 64);
    InvalidateRect(sb, NULL, FALSE);
}

/* panel rectangles in client coords */
static void Panels(const RECT *rc, RECT out[SB_COUNT])
{
    int x = rc->right - S(1), i;                  /* the same 1px margin as on the left */
    if (!g_sbH) Measure();
    for (i = SB_COUNT - 1; i >= SB_ZOOM; i--) {
        out[i].right = x;
        x -= g_pw[i];
        out[i].left = x;
        out[i].top = S(2); out[i].bottom = rc->bottom - S(1);
    }
    out[SB_POS].left = S(1); out[SB_POS].right = x - S(2);
    out[SB_POS].top = S(2);  out[SB_POS].bottom = rc->bottom - S(1);
}

static int PanelAt(HWND h, int px, int py)
{
    RECT rc, p[SB_COUNT];
    POINT pt;
    int i;
    GetClientRect(h, &rc);
    Panels(&rc, p);
    pt.x = px; pt.y = py;
    for (i = 0; i < SB_COUNT; i++)
        if (PtInRect(&p[i], pt)) return i;
    return -1;
}

static void Paint(HWND h)
{
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
    r = rc; r.bottom = 1; FillC(mdc, &r, C_LO2);             /* etched line on top */
    r.top = 1; r.bottom = 2; FillC(mdc, &r, C_HI2);

    Panels(&rc, p);
    for (i = 0; i < SB_COUNT; i++) {
        BOOL click = (i != SB_POS);
        COLORREF c = (click && i == g_hotp) ? C_ACCENT_FG : C_TEXT;
        Bevel(mdc, &p[i], BV_FLAT_DN);
        r = p[i]; r.left += S(7); r.right -= S(3);
        TextC(mdc, g_txt[i], -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, c);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK StatusProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(h);
        return 0;
    case WM_MOUSEMOVE: {
        int i = PanelAt(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i == SB_POS) i = -1;
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
    case WM_SETCURSOR:
        if (g_hotp >= 0 && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        int i = PanelAt(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i >= SB_ZOOM) {
            RECT rc, p[SB_COUNT];
            POINT pt;
            GetClientRect(h, &rc);
            Panels(&rc, p);
            pt.x = p[i].left; pt.y = 0;
            ClientToScreen(h, &pt);
            g_hotp = -1;
            MenuPopup(GetParent(h), i == SB_ZOOM ? &g_mdZoom : (i == SB_EOL ? &g_mdEol : &g_mdEnc), pt.x, pt.y, 1);
        }
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

HWND StatusCreate(HWND parent)
{
    RegClass(L"mp_status", StatusProc, 0, NULL);
    StatusHeight();
    g_sb = CreateWindowExW(0, L"mp_status", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 100, g_sbH, parent, NULL, g_hinst, NULL);
    return g_sb;
}
