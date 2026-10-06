/* frame.c - the main window's title strip, drawn by us in the chrome font (g_fontMenu: the editor font face, CHROME_PX pixels).
 *
 * the window keeps the system's thick frame on the left / right / bottom (resizing, aero snap, shadow and animations
 * stay native); only the caption moves into the client area: WM_NCCALCSIZE hands the client the whole top edge, we
 * paint the icon, the title (white / black while active) and our own minimize / maximize / close buttons there, and
 * WM_NCHITTEST answers HTTOP along the top edge and HTCAPTION over the rest of the strip, so resizing, dragging,
 * aero snap and double-click-to-maximize keep working. the window menu is our popup (menu.c). */
#include "mp.h"

/* on: the title bar text uses the chrome font (CHROME_PX), like the menu bar and the status bar.
 * the first try drew two title bars: the only WM_NCCALCSIZE sent by CreateWindowExW has wParam FALSE, FrameNcCalc handed
 * that one to DefWindowProc untouched, and nothing recalculated the frame before the first resize. fixed by FrameNcCalc
 * treating both forms and mp_main forcing one more recalculation (SWP_FRAMECHANGED) right after the window is created.
 * verified with tools\frame_test.ps1 (hit tests, maximize / restore, double click, drag, aero snap, minimize, close).
 * build with /DFRAME_CUSTOM=0 (tools\probe.bat) to get the native caption back: windows then draws it in the system font. */
#ifndef FRAME_CUSTOM
#define FRAME_CUSTOM 1
#endif

int FrameEnabled(void) { return FRAME_CUSTOM; }

enum { FB_MIN, FB_MAX, FB_CLOSE, FB_COUNT };

static int   g_capH, g_hot = -1, g_down = -1, g_active = 1, g_track;
static HICON g_icon;

static int BtnW(void) { return S(46); }
static int Edge(void) { return UiMetric(SM_CYFRAME) + UiMetric(SM_CXPADDEDBORDER); }     /* the system's sizing border */

static void Metrics(void)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    g_capH = tm.tmHeight + S(10);
    if (g_capH < S(25)) g_capH = S(25);                      /* (was 30: the strip is 5 px lower than it was) */
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
    if (!FRAME_CUSTOM) return;
    if (g_icon) { DestroyIcon(g_icon); g_icon = NULL; }
    Metrics();
    if (h) InvalidateRect(h, NULL, FALSE);
}

static void Invalidate(HWND h)
{
    RECT rc;
    if (!FRAME_CUSTOM || !h) return;
    GetClientRect(h, &rc);
    rc.bottom = FrameHeight();
    InvalidateRect(h, &rc, FALSE);
}

void FrameInvalidate(HWND h) { Invalidate(h); }

void FrameActive(HWND h, int active)
{
    g_active = active;
    if (!active && g_down < 0) g_hot = -1;
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
    COLORREF bg = C_FACE, fg = g_active ? C_TEXT : C_DIM;
    HPEN pen;
    HGDIOBJ op;
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2, g = S(5), d = S(2), pw = S(1);

    if (b == FB_CLOSE && (hot || down)) {                            /* the system's close red, in both themes */
        bg = down ? RGB(0xf1, 0x70, 0x7a) : RGB(0xe8, 0x11, 0x23);
        fg = RGB(0xff, 0xff, 0xff);
    } else if (down) {
        bg = RGB(0x7c, 0xc4, 0x98); fg = C_ON_ACCENT;                 /* pressed: a deeper mint */
    } else if (hot) {
        bg = C_ACCENT; fg = C_ON_ACCENT;
    }
    if (hot || down) FillC(dc, r, bg);                               /* a button at rest is transparent: the strip's fade shows through */

    pen = CreatePen(PS_SOLID, pw < 1 ? 1 : pw, fg);
    op = SelectObject(dc, pen);
    switch (b) {
    case FB_MIN:
        Line(dc, cx - g, cy, cx + g + 1, cy);
        break;
    case FB_MAX:
        if (!zoomed) {
            Line(dc, cx - g, cy - g, cx + g, cy - g);
            Line(dc, cx + g, cy - g, cx + g, cy + g);
            Line(dc, cx + g, cy + g, cx - g, cy + g);
            Line(dc, cx - g, cy + g, cx - g, cy - g);
        } else {                                                     /* restore: two overlapping squares */
            Line(dc, cx - g, cy - g + d, cx + g - d, cy - g + d);
            Line(dc, cx + g - d, cy - g + d, cx + g - d, cy + g);
            Line(dc, cx + g - d, cy + g, cx - g, cy + g);
            Line(dc, cx - g, cy + g, cx - g, cy - g + d);
            Line(dc, cx - g + d, cy - g + d, cx - g + d, cy - g);
            Line(dc, cx - g + d, cy - g, cx + g, cy - g);
            Line(dc, cx + g, cy - g, cx + g, cy + g - d);
            Line(dc, cx + g, cy + g - d, cx + g - d, cy + g - d);
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

/* the strip's background: the face colour with a fade of the accent over it, FADE_FROM % at the left edge down to FADE_TO % where the window
 * buttons start, and FADE_TO % under the buttons (their normal state is transparent, so the strip reads as one piece). the light theme
 * has FADE_LIGHT_PLUS points more at both ends (the dark accent needs more to show on the light face). one fill per distinct colour */
#define FADE_FROM 14
#define FADE_TO   4
#define FADE_LIGHT_PLUS 5
static COLORREF Tint(COLORREF face, COLORREF acc, int a)         /* a = 0..255: how much of the accent */
{
    int r = GetRValue(face) + (GetRValue(acc) - GetRValue(face)) * a / 255;
    int g = GetGValue(face) + (GetGValue(acc) - GetGValue(face)) * a / 255;
    int b = GetBValue(face) + (GetBValue(acc) - GetBValue(face)) * a / 255;
    return RGB(r, g, b);
}

static void FillStrip(HDC dc, int cw, int ch, int fadeEnd)
{
    RECT r;
    int pf = FADE_FROM + (ThemeGet() == THEME_LIGHT ? FADE_LIGHT_PLUS : 0), pt = FADE_TO + (ThemeGet() == THEME_LIGHT ? FADE_LIGHT_PLUS : 0);
    COLORREF base = Tint(C_FACE, C_ACCENT, pt * 255 / 100), last = base, c;
    int x, from = 0;
    r.left = 0; r.top = 0; r.right = cw; r.bottom = ch;
    FillC(dc, &r, base);
    if (fadeEnd > cw) fadeEnd = cw;
    for (x = 0; x <= fadeEnd; x++) {
        c = x < fadeEnd ? Tint(C_FACE, C_ACCENT, 255 * (pt * fadeEnd + (pf - pt) * (fadeEnd - x)) / (100 * fadeEnd)) : base;
        if (c != last || x == fadeEnd) {
            if (last != base) { r.left = from; r.right = x; FillC(dc, &r, last); }
            from = x;
            last = c;
        }
    }
}

/* called from the main window's WM_PAINT: the colours are read here, so a theme switch only needs a repaint */
void FramePaint(HWND h, HDC dc)
{
    RECT rc, r, tr;
    HDC mdc;
    HBITMAP bmp;
    HGDIOBJ ob, of;
    WCHAR t[PATH_CAP + 64];
    int cw, ch, b, isz = S(16);

    if (!FRAME_CUSTOM) return;
    GetClientRect(h, &rc);
    cw = rc.right;
    ch = FrameHeight();
    if (cw <= 0) return;
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cw, ch);
    ob = SelectObject(mdc, bmp);
    of = SelectObject(mdc, g_fontMenu);

    BtnRect(FB_MIN, cw, &r);
    FillStrip(mdc, cw, ch, r.left);
    if (!g_icon) g_icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, isz, isz, LR_DEFAULTCOLOR);
    if (g_icon) DrawIconEx(mdc, S(10), (ch - isz) / 2, g_icon, isz, isz, 0, NULL, DI_NORMAL);

    GetWindowTextW(h, t, COUNTOF(t));
    BtnRect(FB_MIN, cw, &r);
    tr.left = IconRight() + S(4); tr.right = r.left - S(8); tr.top = 0; tr.bottom = ch;
    TextC(mdc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
          g_active ? (ThemeGet() == THEME_DARK ? RGB(0xff, 0xff, 0xff) : RGB(0, 0, 0)) : C_DIM);      /* plain white / black: readable on the accent fade */

    for (b = 0; b < FB_COUNT; b++) {
        BtnRect(b, cw, &r);
        DrawBtn(mdc, b, &r, b == g_hot, b == g_down && b == g_hot, IsZoomed(h) ? 1 : 0);
    }
    BitBlt(dc, 0, 0, cw, ch, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

/* ------------------------------------------------------------ messages --- */
/* the caption becomes client area (our strip); the left / right / bottom frame stays the system's.
 * wParam TRUE: l is an NCCALCSIZE_PARAMS whose rgrc[0] goes from the new window rect to the new client rect.
 * wParam FALSE: l is that one RECT. CreateWindowExW sends only this form, so it needs the same treatment:
 * passing it through was the "two title bars" bug (the native caption stayed until the first resize) */
LRESULT FrameNcCalc(HWND h, WPARAM w, LPARAM l)
{
    RECT *r = (RECT *)l;
    RECT win;
    LRESULT ret;
    if (!FRAME_CUSTOM || !r) return DefWindowProcW(h, WM_NCCALCSIZE, w, l);
    win = *r;
    ret = DefWindowProcW(h, WM_NCCALCSIZE, w, l);           /* the standard frame (and caption) */
    if (!IsIconic(h)) {
        r->top = win.top;                                    /* no caption: the strip starts at the top edge */
        if (IsZoomed(h)) r->top += r->left - win.left;       /* maximized windows hang over the screen by the frame thickness */
    }
    DBG(L"nccalc w/top-inset", w, r->top - win.top);
    return ret;
}

LRESULT FrameHitTest(HWND h, LPARAM l)
{
    POINT pt;
    RECT rc;
    int b, e;
    LRESULT r = DefWindowProcW(h, WM_NCHITTEST, 0, l);      /* the system's frame: left / right / bottom edges */
    if (!FRAME_CUSTOM || r != HTCLIENT) return r;
    pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
    ScreenToClient(h, &pt);
    if (pt.y < 0 || pt.y >= FrameHeight()) return HTCLIENT;
    GetClientRect(h, &rc);
    b = HitBtn(pt.x, pt.y, rc.right);
    e = b >= 0 ? Edge() / 2 : Edge();                        /* thinner over the buttons */
    if (!IsZoomed(h) && pt.y < e) {                          /* the top sizing edge lives inside the strip */
        if (pt.x < S(16)) return HTTOPLEFT;
        if (pt.x >= rc.right - S(16)) return HTTOPRIGHT;
        return HTTOP;
    }
    if (b >= 0 || InIcon(pt.x, pt.y)) return HTCLIENT;       /* ours: plain mouse messages (FrameMsg) */
    return HTCAPTION;
}

void FrameSysMenu(HWND h, int x, int y)
{
    POINT pt;
    if (x == -1 && y == -1) {                                /* keyboard (alt+space) / icon click: under the icon */
        pt.x = 0; pt.y = FrameHeight();
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

/* the main window's own mouse messages: button hover / press / click, the icon, right clicks in the strip */
BOOL FrameMsg(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l), b;
    RECT rc;
    POINT pt;

    if (!FRAME_CUSTOM) return FALSE;
    switch (m) {
    case WM_MOUSEMOVE:
        GetClientRect(h, &rc);
        b = (y >= 0 && y < FrameHeight()) ? HitBtn(x, y, rc.right) : -1;
        if (g_down >= 0 && b != g_down) b = -1;                  /* while one is pressed the others stay quiet */
        if (b >= 0 && !g_track) {
            TRACKMOUSEEVENT te;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            g_track = TrackMouseEvent(&te) ? 1 : 0;
        }
        if (b != g_hot) { g_hot = b; Invalidate(h); }
        return TRUE;
    case WM_MOUSELEAVE:                                          /* also when the pointer moves on to the caption (non-client) */
        g_track = 0;
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
        if (InIcon(x, y)) { FrameSysMenu(h, -1, -1); return TRUE; }
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
    case WM_RBUTTONUP:                                           /* icon / buttons (client area): the window menu at the pointer */
        if (y < 0 || y >= FrameHeight()) return FALSE;
        pt.x = x; pt.y = y;
        ClientToScreen(h, &pt);
        FrameSysMenu(h, pt.x, pt.y);
        return TRUE;
    case WM_NCRBUTTONDOWN:                                       /* caption: DefWindowProc would run its own loop and eat the button up */
        return w == HTCAPTION;
    case WM_CAPTURECHANGED:
        if (g_down >= 0) { g_down = -1; g_hot = -1; Invalidate(h); }
        return FALSE;
    }
    return FALSE;
}
