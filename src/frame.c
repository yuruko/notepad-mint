/* frame.c - the main window's title strip, drawn by us in the chrome font (the editor font, 2pt smaller).
 *
 * the window keeps its native thick frame (resizing, aero snap, shadow, animations all stay the system's); only
 * the caption moves into the client area: WM_NCCALCSIZE gives the client the whole top edge, we paint the icon,
 * the title and our own minimize / maximize / close buttons there, and WM_NCHITTEST answers HTCAPTION over the
 * rest of the strip so dragging and double-click-to-maximize keep working. the window menu is our dark popup. */
#include "mp.h"

/* EXPERIMENTAL, off by default: when it was switched on, windows still drew its own caption above our strip
 * (the WM_NCCALCSIZE override didn't take effect and nothing was logged from FrameNcCalc), so the window showed two
 * title bars. with it off the native dark title bar is used (drawn in the system caption font). the rest of the
 * chrome (menu bar, popups, status bar) follows the editor font either way. flip to 1 to continue the work. */
#define FRAME_CUSTOM 0

int FrameEnabled(void) { return FRAME_CUSTOM; }

enum { FB_MIN, FB_MAX, FB_CLOSE, FB_COUNT };

static int   g_capH, g_hot = -1, g_down = -1, g_active = 1;
static HICON g_icon;

static int BtnW(void) { return S(46); }

static void Metrics(void)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    g_capH = tm.tmHeight + S(12);
    if (g_capH < S(30)) g_capH = S(30);
}

int FrameHeight(void)
{
    if (!FRAME_CUSTOM) return 0;
    if (!g_capH) Metrics();
    return g_capH;
}

/* fonts or dpi changed */
void FrameRefont(HWND h)
{
    if (g_icon) { DestroyIcon(g_icon); g_icon = NULL; }
    Metrics();
    InvalidateRect(h, NULL, FALSE);
}

static void Invalidate(HWND h)
{
    RECT rc;
    if (!FRAME_CUSTOM) return;
    GetClientRect(h, &rc);
    rc.bottom = FrameHeight();
    InvalidateRect(h, &rc, FALSE);
}

void FrameInvalidate(HWND h) { Invalidate(h); }

void FrameActive(HWND h, int active)
{
    g_active = active;
    Invalidate(h);
}

/* ------------------------------------------------------------ geometry --- */
static void BtnRect(int b, int cw, RECT *r)                 /* right aligned: close is the last one */
{
    r->right = cw - (FB_COUNT - 1 - b) * BtnW();
    r->left = r->right - BtnW();
    r->top = 0;
    r->bottom = FrameHeight();
}

static int HitBtn(int x, int y, int cw)
{
    POINT p;
    RECT r;
    int b;
    p.x = x; p.y = y;
    for (b = 0; b < FB_COUNT; b++) {
        BtnRect(b, cw, &r);
        if (PtInRect(&r, p)) return b;
    }
    return -1;
}

static int IconRight(void) { return S(10) + S(16) + S(6); }
static int InIcon(int x, int y) { return x >= 0 && x < IconRight() && y >= 0 && y < FrameHeight(); }

/* ------------------------------------------------------------- painting --- */
static void Line(HDC dc, int x1, int y1, int x2, int y2)
{
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
}

static void DrawBtn(HDC dc, int b, const RECT *r, int hot, int down, int zoomed)
{
    COLORREF bg = C_FACE, fg = C_TEXT;
    HPEN pen;
    HGDIOBJ op;
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2, g = S(5), pw = S(1);

    if (down)     { bg = RGB(0x7c, 0xc4, 0x98); fg = C_FACE; }        /* pressed: a deeper mint */
    else if (hot) { bg = C_ACCENT; fg = C_FACE; }
    FillC(dc, r, bg);

    pen = CreatePen(PS_SOLID, pw < 1 ? 1 : pw, fg);
    op = SelectObject(dc, pen);
    switch (b) {
    case FB_MIN:
        Line(dc, cx - g, cy + g / 2, cx + g + 1, cy + g / 2);
        break;
    case FB_MAX:
        if (!zoomed) {
            Line(dc, cx - g, cy - g, cx + g, cy - g);
            Line(dc, cx + g, cy - g, cx + g, cy + g);
            Line(dc, cx + g, cy + g, cx - g, cy + g);
            Line(dc, cx - g, cy + g, cx - g, cy - g);
        } else {                                                        /* two overlapping squares */
            Line(dc, cx - g, cy - g + 3, cx + g - 3, cy - g + 3);
            Line(dc, cx + g - 3, cy - g + 3, cx + g - 3, cy + g);
            Line(dc, cx + g - 3, cy + g, cx - g, cy + g);
            Line(dc, cx - g, cy + g, cx - g, cy - g + 3);
            Line(dc, cx - g + 3, cy - g + 3, cx - g + 3, cy - g);
            Line(dc, cx - g + 3, cy - g, cx + g, cy - g);
            Line(dc, cx + g, cy - g, cx + g, cy + g - 3);
            Line(dc, cx + g, cy + g - 3, cx + g - 3, cy + g - 3);
        }
        break;
    case FB_CLOSE:
        Line(dc, cx - g, cy - g, cx + g + 1, cy + g + 1);
        Line(dc, cx + g, cy - g, cx - g - 1, cy + g + 1);
        break;
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

void FramePaint(HWND h, HDC dc)
{
    RECT rc, r, tr;
    HDC mdc;
    HBITMAP bmp;
    HGDIOBJ ob, of;
    WCHAR t[PATH_CAP + 64];
    int cw, b, isz = S(16);

    if (!FRAME_CUSTOM) return;
    GetClientRect(h, &rc);
    cw = rc.right;
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cw, FrameHeight());
    ob = SelectObject(mdc, bmp);
    of = SelectObject(mdc, g_fontMenu);

    r.left = 0; r.top = 0; r.right = cw; r.bottom = FrameHeight();
    FillC(mdc, &r, C_FACE);
    if (!g_icon) g_icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, isz, isz, LR_DEFAULTCOLOR);
    if (g_icon) DrawIconEx(mdc, S(10), (FrameHeight() - isz) / 2, g_icon, isz, isz, 0, NULL, DI_NORMAL);

    GetWindowTextW(h, t, COUNTOF(t));
    BtnRect(FB_MIN, cw, &r);
    tr.left = IconRight() + S(4); tr.right = r.left - S(8); tr.top = 0; tr.bottom = FrameHeight();
    TextC(mdc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, g_active ? C_TEXT : C_DIM);

    for (b = 0; b < FB_COUNT; b++) {
        BtnRect(b, cw, &r);
        DrawBtn(mdc, b, &r, b == g_hot, b == g_down && b == g_hot, IsZoomed(h) ? 1 : 0);
    }
    BitBlt(dc, 0, 0, cw, FrameHeight(), mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

/* ------------------------------------------------------------ messages --- */
/* the client area starts at the very top of the window: the caption strip is ours */
LRESULT FrameNcCalc(HWND h, WPARAM w, LPARAM l)
{
    NCCALCSIZE_PARAMS *p;
    LONG top;
    LRESULT r;
    if (!FRAME_CUSTOM || !w) return DefWindowProcW(h, WM_NCCALCSIZE, w, l);
    p = (NCCALCSIZE_PARAMS *)l;
    top = p->rgrc[0].top;
    r = DefWindowProcW(h, WM_NCCALCSIZE, w, l);              /* standard frame on the left / right / bottom */
    DBG(L"nccalc in-top/out-top", top, p->rgrc[0].top);
    DBG(L"nccalc ret/zoomed", r, IsZoomed(h));
    if (r) return r;
    p->rgrc[0].top = top;
    if (IsZoomed(h)) p->rgrc[0].top += UiMetric(SM_CYFRAME) + UiMetric(SM_CXPADDEDBORDER);   /* maximized windows overhang the screen */
    return 0;
}

LRESULT FrameHitTest(HWND h, LPARAM l)
{
    POINT pt;
    RECT rc;
    LRESULT r = DefWindowProcW(h, WM_NCHITTEST, 0, l);      /* frame edges (left / right / bottom) */
    if (!FRAME_CUSTOM || r != HTCLIENT) return r;
    pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
    ScreenToClient(h, &pt);
    GetClientRect(h, &rc);
    if (!IsZoomed(h) && pt.y < S(4)) {                       /* top resize edge: it lives inside our strip */
        if (pt.x < S(12)) return HTTOPLEFT;
        if (pt.x >= rc.right - S(12)) return HTTOPRIGHT;
        return HTTOP;
    }
    if (pt.y >= 0 && pt.y < FrameHeight()) {
        if (HitBtn(pt.x, pt.y, rc.right) >= 0 || InIcon(pt.x, pt.y)) return HTCLIENT;   /* ours: ordinary mouse messages */
        return HTCAPTION;
    }
    return HTCLIENT;
}

void FrameSysMenu(HWND h, int x, int y)
{
    POINT pt;
    if (x == -1 && y == -1) {                                /* keyboard (alt+space): under the icon */
        pt.x = S(6); pt.y = FrameHeight();
        ClientToScreen(h, &pt);
        x = pt.x; y = pt.y;
    }
    MenuPopup(h, &g_mdSys, x, y, 0);
}

unsigned FrameSysState(int id)
{
    HWND h = g_hwnd;
    int max = IsZoomed(h) ? 1 : 0, min = IsIconic(h) ? 1 : 0;
    switch (id) {
    case IDM_SYS_RESTORE: return (max || min) ? 0 : MS_GRAY;
    case IDM_SYS_MOVE:
    case IDM_SYS_SIZE:
    case IDM_SYS_MAX:     return max ? MS_GRAY : 0;
    }
    return 0;
}

void FrameSysCommand(HWND h, int id)
{
    UINT sc = 0;
    switch (id) {
    case IDM_SYS_RESTORE: sc = SC_RESTORE; break;
    case IDM_SYS_MOVE:    sc = SC_MOVE; break;
    case IDM_SYS_SIZE:    sc = SC_SIZE; break;
    case IDM_SYS_MIN:     sc = SC_MINIMIZE; break;
    case IDM_SYS_MAX:     sc = SC_MAXIMIZE; break;
    case IDM_SYS_CLOSE:   sc = SC_CLOSE; break;
    }
    if (sc) PostMessageW(h, WM_SYSCOMMAND, sc, 0);
}

/* mouse messages the main window gets for its own client area: button hover / press / click, the icon */
BOOL FrameMsg(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l), b;
    RECT rc;
    (void)w;

    if (!FRAME_CUSTOM) return FALSE;
    switch (m) {
    case WM_MOUSEMOVE:
        GetClientRect(h, &rc);
        b = (y >= 0 && y < FrameHeight()) ? HitBtn(x, y, rc.right) : -1;
        if (b != g_hot) {
            if (g_hot < 0 && b >= 0) {
                TRACKMOUSEEVENT te;
                te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
                TrackMouseEvent(&te);
            }
            g_hot = b;
            Invalidate(h);
        }
        return TRUE;
    case WM_MOUSELEAVE:
        if (g_hot >= 0 && g_down < 0) { g_hot = -1; Invalidate(h); }
        return TRUE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (y < 0 || y >= FrameHeight()) return FALSE;
        GetClientRect(h, &rc);
        b = HitBtn(x, y, rc.right);
        if (b >= 0) {
            g_down = b; g_hot = b;
            SetCapture(h);
            Invalidate(h);
            return TRUE;
        }
        if (InIcon(x, y)) {
            if (m == WM_LBUTTONDBLCLK) PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, 0);       /* classic: double click the icon */
            else FrameSysMenu(h, -1, -1);
            return TRUE;
        }
        return FALSE;
    case WM_LBUTTONUP:
        if (g_down >= 0) {
            int d = g_down;
            GetClientRect(h, &rc);
            b = (y >= 0 && y < FrameHeight()) ? HitBtn(x, y, rc.right) : -1;
            g_down = -1;
            ReleaseCapture();
            g_hot = b;
            Invalidate(h);
            if (b == d) {
                UINT sc = (d == FB_MIN) ? SC_MINIMIZE : (d == FB_CLOSE ? SC_CLOSE : (IsZoomed(h) ? SC_RESTORE : SC_MAXIMIZE));
                PostMessageW(h, WM_SYSCOMMAND, sc, 0);
            }
            return TRUE;
        }
        return FALSE;
    case WM_CAPTURECHANGED:
        if (g_down >= 0) { g_down = -1; g_hot = -1; Invalidate(h); }
        return FALSE;
    }
    return FALSE;
}
