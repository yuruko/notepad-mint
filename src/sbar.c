#include "mp.h"

#define SBT_REPEAT   1
#define SBT_POLL     2
#define POLL_MS      40
#define REPEAT_FIRST 400
#define REPEAT_NEXT  50

enum { P_NONE, P_ARROW1, P_ARROW2, P_PAGE1, P_THUMB, P_PAGE2, P_CORNER };

typedef struct { HWND target; int horz; } SbInit;

typedef struct Sb {
    HWND self, target;
    int horz;
    int shown;
    RECT rc;
    int corner;
    SCROLLINFO si;
    int part, over;
    int grab, dragA;
    int px, py;
} Sb;

typedef struct { int len, btn, t0, t1, a0, a1, on; } Geo;

static struct { HWND target; Sb *v, *h; int trimR, trimB; } g_tab[8];
static HBRUSH g_dither;

static int ReadSi(const Sb *s, SCROLLINFO *si) {
    memset(si, 0, sizeof *si);
    si->cbSize = sizeof *si;
    si->fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    return GetScrollInfo(s->target, s->horz ? SB_HORZ : SB_VERT, si);
}

static void Measure(const Sb *s, int w, int h, Geo *g) {
    int th = s->horz ? h : w;
    int range = s->si.nMax - s->si.nMin + 1, page = (int)s->si.nPage;
    int tl, tlen, maxp, pos;
    g->len = s->horz ? w : h - s->corner;
    g->btn = th;
    if (g->len < 2 * g->btn) g->btn = g->len / 2;
    g->t0 = g->btn;
    g->t1 = g->len - g->btn;
    tl = g->t1 - g->t0;
    g->a0 = g->a1 = 0;
    g->on = (page > 0 ? range > page : range > 1) && tl > 0;
    if (!g->on) return;
    tlen = page > 0 ? MulDiv(tl, page, range) : th;
    if (tlen < th) tlen = th;
    if (tlen > tl) tlen = tl;
    maxp = range - (page > 0 ? page : 0);
    pos = ClampInt(s->si.nPos - s->si.nMin, 0, maxp);
    g->a0 = g->t0 + (maxp > 0 ? MulDiv(tl - tlen, pos, maxp) : 0);
    g->a1 = g->a0 + tlen;
}

static int Hit(const Sb *s, int x, int y, const Geo *g) {
    int a = s->horz ? x : y;
    if (!s->horz && y >= g->len) return P_CORNER;
    if (!g->on) return P_NONE;
    if (a < g->btn) return P_ARROW1;
    if (a >= g->len - g->btn) return P_ARROW2;
    if (a < g->a0) return P_PAGE1;
    if (a >= g->a1) return P_PAGE2;
    return P_THUMB;
}

static void Seg(const Sb *s, int a0, int a1, int w, int h, RECT *r) {
    if (s->horz) { r->left = a0; r->right = a1; r->top = 0; r->bottom = h; }
    else         { r->left = 0; r->right = w; r->top = a0; r->bottom = a1; }
}

static void Glyph(HDC dc, const RECT *r, int dir, COLORREF c, int off) {
    POINT p[3];
    int t = r->right - r->left < r->bottom - r->top ? r->right - r->left : r->bottom - r->top;
    int hh = (t * 3 + 8) / 17, hw, cx, cy, x0, y0;
    HGDIOBJ op, ob;
    if (hh < 2) hh = 2;
    hw = hh;
    cx = (r->left + r->right) / 2 + off;
    cy = (r->top + r->bottom) / 2 + off;
    x0 = cx - (hh + 1) / 2;
    y0 = cy - (hh + 1) / 2;
    switch (dir) {
    case 0: p[0].x = cx; p[0].y = y0;      p[1].x = cx - hw; p[1].y = y0 + hh; p[2].x = cx + hw; p[2].y = y0 + hh; break;
    case 1: p[0].x = cx; p[0].y = y0 + hh; p[1].x = cx - hw; p[1].y = y0;      p[2].x = cx + hw; p[2].y = y0;      break;
    case 2: p[0].x = x0; p[0].y = cy;      p[1].x = x0 + hh; p[1].y = cy - hw; p[2].x = x0 + hh; p[2].y = cy + hw; break;
    default: p[0].x = x0 + hh; p[0].y = cy; p[1].x = x0; p[1].y = cy - hw;      p[2].x = x0;      p[2].y = cy + hw; break;
    }
    op = SelectObject(dc, GetStockObject(DC_PEN));
    ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, c);
    SetDCBrushColor(dc, c);
    Polygon(dc, p, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

static void Dither(HDC dc, const RECT *r) {
    static const WORD bits[8] = { 0x00AA, 0x0055, 0x00AA, 0x0055, 0x00AA, 0x0055, 0x00AA, 0x0055 };
    if (!g_dither) {
        HBITMAP bm = CreateBitmap(8, 8, 1, 1, bits);
        g_dither = bm ? CreatePatternBrush(bm) : NULL;
        if (bm) DeleteObject(bm);
    }
    if (!g_dither) { FillC(dc, r, C_FACE); return; }
    SetTextColor(dc, C_FACE);
    SetBkColor(dc, ThemeGet() == THEME_LIGHT ? C_HI : C_FACE2);
    FillRect(dc, r, g_dither);
}

static void Button(HDC dc, const RECT *r, int pressed) {
    FillC(dc, r, C_FACE);
    Bevel(dc, r, pressed ? BV_FLAT_DN : BV_RAISED);
}

static void Paint(Sb *s, HDC dc, int w, int h) {
    Geo g;
    RECT r, all;
    int dirA = s->horz ? 2 : 0, dirB = s->horz ? 3 : 1, off;
    COLORREF gc;
    all.left = 0; all.top = 0; all.right = w; all.bottom = h;
    FillC(dc, &all, C_FACE);
    Measure(s, w, h, &g);
    gc = g.on ? C_TEXT : C_DIM;
    Seg(s, g.t0, g.t1, w, h, &r);
    if (r.right > r.left && r.bottom > r.top) Dither(dc, &r);
    Seg(s, 0, g.btn, w, h, &r);
    off = (g.on && s->part == P_ARROW1 && s->over) ? 1 : 0;
    Button(dc, &r, off);
    Glyph(dc, &r, dirA, gc, off);
    Seg(s, g.len - g.btn, g.len, w, h, &r);
    off = (g.on && s->part == P_ARROW2 && s->over) ? 1 : 0;
    Button(dc, &r, off);
    Glyph(dc, &r, dirB, gc, off);
    if (g.on) {
        int a0 = g.a0, a1 = g.a1;
        if (s->part == P_THUMB) { a1 = s->dragA + (g.a1 - g.a0); a0 = s->dragA; }
        Seg(s, a0, a1, w, h, &r);
        Button(dc, &r, 0);
    }
}

static void PaintTo(Sb *s, HDC dc) {
    RECT rc;
    HDC mdc;
    HBITMAP bmp;
    HGDIOBJ ob;
    GetClientRect(s->self, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    ob = SelectObject(mdc, bmp);
    Paint(s, mdc, rc.right, rc.bottom);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

static int IsClass(HWND h, const WCHAR *name) {
    WCHAR c[24];
    return GetClassNameW(h, c, COUNTOF(c)) && wcmpi(c, name) == 0;
}

static void Step(Sb *s, int part) {
    UINT code = part == P_ARROW1 ? SB_LINEUP : part == P_ARROW2 ? SB_LINEDOWN : part == P_PAGE1 ? SB_PAGEUP : SB_PAGEDOWN;
    SendMessageW(s->target, s->horz ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(code, 0), 0);
}

static void ScrollTo(Sb *s, int pos) {
    SCROLLINFO si;
    int maxp, cur;
    if (!ReadSi(s, &si)) return;
    maxp = si.nMax - (int)si.nPage + 1;
    pos = ClampInt(pos, si.nMin, maxp > si.nMin ? maxp : si.nMin);
    if (!s->horz && IsClass(s->target, L"Edit")) {
        cur = (int)SendMessageW(s->target, EM_GETFIRSTVISIBLELINE, 0, 0);
        if (pos != cur) SendMessageW(s->target, EM_LINESCROLL, 0, (LPARAM)(pos - cur));
    } else if (!s->horz && IsClass(s->target, L"ListBox")) {
        SendMessageW(s->target, LB_SETTOPINDEX, (WPARAM)pos, 0);
    } else if (pos != si.nPos) {
        SendMessageW(s->target, s->horz ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (WORD)ClampInt(pos, 0, 65535)), 0);
    }
}

static void Pointer(Sb *s, LPARAM l) {
    s->px = (short)LOWORD(l);
    s->py = (short)HIWORD(l);
}

static void Drag(Sb *s) {
    RECT rc;
    Geo g;
    int a, tlen, tl, na, range, page, maxp;
    GetClientRect(s->self, &rc);
    Measure(s, rc.right, rc.bottom, &g);
    if (!g.on) return;
    a = s->horz ? s->px : s->py;
    tlen = g.a1 - g.a0;
    tl = g.t1 - g.t0;
    na = ClampInt(a - s->grab, g.t0, g.t1 - tlen);
    s->dragA = na;
    range = s->si.nMax - s->si.nMin + 1;
    page = (int)s->si.nPage;
    maxp = range - (page > 0 ? page : 0);
    if (tl > tlen) ScrollTo(s, s->si.nMin + MulDiv(na - g.t0, maxp, tl - tlen));
    InvalidateRect(s->self, NULL, FALSE);
}

static void Release(Sb *s) {
    KillTimer(s->self, SBT_REPEAT);
    s->part = P_NONE;
    s->over = 0;
    InvalidateRect(s->self, NULL, FALSE);
}

static void Sync(Sb *s);

static LRESULT CALLBACK SbProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Sb *s = (Sb *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        const SbInit *in = (const SbInit *)((CREATESTRUCTW *)l)->lpCreateParams;
        s = (Sb *)mem_zalloc(sizeof *s);
        if (!s) return FALSE;
        s->self = h;
        s->target = in->target;
        s->horz = in->horz;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        return DefWindowProcW(h, m, w, l);
    }
    if (!s) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_NCDESTROY: {
        int i;
        KillTimer(h, SBT_REPEAT);
        KillTimer(h, SBT_POLL);
        for (i = 0; i < COUNTOF(g_tab); i++) {
            if (g_tab[i].v == s) g_tab[i].v = NULL;
            if (g_tab[i].h == s) g_tab[i].h = NULL;
            if (g_tab[i].target && !g_tab[i].v && !g_tab[i].h) g_tab[i].target = NULL;
        }
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        mem_free(s);
        break; }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintTo(s, dc);
        EndPaint(h, &ps);
        return 0; }
    case WM_PRINTCLIENT:
        PaintTo(s, (HDC)w);
        return 0;
    case WM_MOUSEWHEEL:
        return SendMessageW(s->target, m, w, l);
    case WM_LBUTTONDOWN: {
        RECT rc;
        Geo g;
        int part;
        GetClientRect(h, &rc);
        Sync(s);
        Measure(s, rc.right, rc.bottom, &g);
        Pointer(s, l);
        part = Hit(s, s->px, s->py, &g);
        if (part == P_NONE || part == P_CORNER) return 0;
        SetCapture(h);
        s->part = part;
        s->over = 1;
        if (part == P_THUMB) {
            s->grab = (s->horz ? s->px : s->py) - g.a0;
            s->dragA = g.a0;
        } else {
            Step(s, part);
            SetTimer(h, SBT_REPEAT, REPEAT_FIRST, NULL);
        }
        InvalidateRect(h, NULL, FALSE);
        return 0; }
    case WM_MOUSEMOVE:
        Pointer(s, l);
        if (s->part == P_THUMB) {
            Drag(s);
        } else if (s->part) {
            RECT rc;
            Geo g;
            int over;
            GetClientRect(h, &rc);
            Measure(s, rc.right, rc.bottom, &g);
            over = Hit(s, s->px, s->py, &g) == s->part;
            if (over != s->over) { s->over = over; InvalidateRect(h, NULL, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        Release(s);
        return 0;
    case WM_CAPTURECHANGED:
        Release(s);
        return 0;
    case WM_TIMER:
        if (w == SBT_POLL) {
            Sync(s);
        } else if (w == SBT_REPEAT) {
            RECT rc;
            Geo g;
            SetTimer(h, SBT_REPEAT, REPEAT_NEXT, NULL);
            if (s->part && s->part != P_THUMB) {
                Sync(s);
                GetClientRect(h, &rc);
                Measure(s, rc.right, rc.bottom, &g);
                s->over = Hit(s, s->px, s->py, &g) == s->part;
                if (s->over) Step(s, s->part);
                InvalidateRect(h, NULL, FALSE);
            }
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void Strips(HWND t, RECT *wr, int *l, int *r, int *b, int trimR, int trimB) {
    RECT cr;
    POINT o;
    GetWindowRect(t, wr);
    GetClientRect(t, &cr);
    o.x = 0; o.y = 0;
    ClientToScreen(t, &o);
    *l = o.x - wr->left;
    *r = wr->right - (o.x + cr.right);
    *b = wr->bottom - (o.y + cr.bottom);
    if (GetWindowLongPtrW(t, GWL_EXSTYLE) & WS_EX_LEFTSCROLLBAR) { wr->left += trimR; *l -= trimR; }
    else { wr->right -= trimR; *r -= trimR; }
    wr->bottom -= trimB; *b -= trimB;
    if (*l < 0) *l = 0;
    if (*r < 0) *r = 0;
    if (*b < 0) *b = 0;
}

static void Place(Sb *s, int visible, const RECT *scr) {
    SCROLLINFO si;
    RECT rc;
    POINT a, c;
    int changed = 0;
    if (!visible) {
        if (s->shown) { ShowWindow(s->self, SW_HIDE); s->shown = 0; }
        return;
    }
    a.x = scr->left; a.y = scr->top; c.x = scr->right; c.y = scr->bottom;
    ScreenToClient(GetParent(s->self), &a);
    ScreenToClient(GetParent(s->self), &c);
    rc.left = a.x; rc.top = a.y; rc.right = c.x; rc.bottom = c.y;
    if (!s->shown || rc.left != s->rc.left || rc.top != s->rc.top || rc.right != s->rc.right || rc.bottom != s->rc.bottom) {
        SetWindowPos(s->self, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s->rc = rc;
        s->shown = 1;
        changed = 1;
    }
    if (ReadSi(s, &si) && (si.nMin != s->si.nMin || si.nMax != s->si.nMax || si.nPage != s->si.nPage || si.nPos != s->si.nPos)) {
        s->si = si;
        changed = 1;
    }
    if (changed) InvalidateRect(s->self, NULL, FALSE);
}

static void SyncEntry(int i) {
    HWND t = g_tab[i].target;
    Sb *v = g_tab[i].v, *hz = g_tab[i].h;
    RECT wr, r;
    int l, rt, b, vt, left, vis;
    if (!t || !IsWindow(t)) return;
    Strips(t, &wr, &l, &rt, &b, g_tab[i].trimR, g_tab[i].trimB);
    vis = (GetWindowLongPtrW(t, GWL_STYLE) & WS_VISIBLE) != 0;
    vt = l > rt ? l : rt;
    left = l > rt;
    if (v) {
        r.left = left ? wr.left : wr.right - vt; r.right = r.left + vt;
        r.top = wr.top; r.bottom = wr.bottom;
        if (v->corner != b) { v->corner = b; InvalidateRect(v->self, NULL, FALSE); }
        Place(v, vis && vt > 0, &r);
    }
    if (hz) {
        r.left = wr.left + (left ? vt : 0); r.right = wr.right - (left ? 0 : vt);
        r.top = wr.bottom - b; r.bottom = wr.bottom;
        Place(hz, vis && b > 0, &r);
    }
}

static void Sync(Sb *s) {
    int i;
    for (i = 0; i < COUNTOF(g_tab); i++) if (g_tab[i].target == s->target) { SyncEntry(i); return; }
}

static int Find(HWND t) {
    int i;
    for (i = 0; i < COUNTOF(g_tab); i++) if (g_tab[i].target == t) return i;
    return -1;
}

static Sb *Make(HWND t, int horz) {
    SbInit in;
    HWND w;
    in.target = t;
    in.horz = horz;
    w = CreateWindowExW(0, L"mp_sbar", NULL, WS_CHILD, 0, 0, 0, 0, GetParent(t), NULL, g_hinst, &in);
    return w ? (Sb *)GetWindowLongPtrW(w, GWLP_USERDATA) : NULL;
}

void SbarAttach(HWND t) {
    static int reg;
    int i;
    LONG_PTR st;
    if (!t || Find(t) >= 0) return;
    if (!reg) { RegClass(L"mp_sbar", SbProc, 0, NULL); reg = 1; }
    st = GetWindowLongPtrW(t, GWL_STYLE);
    if (!(st & WS_CLIPSIBLINGS)) {
        SetWindowLongPtrW(t, GWL_STYLE, st | WS_CLIPSIBLINGS);
        SetWindowPos(t, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    for (i = 0; i < COUNTOF(g_tab) && g_tab[i].target; i++) {}
    if (i == COUNTOF(g_tab)) return;
    g_tab[i].target = t;
    g_tab[i].trimR = g_tab[i].trimB = 0;
    g_tab[i].v = Make(t, 0);
    g_tab[i].h = Make(t, 1);
    if (g_tab[i].v) SetTimer(g_tab[i].v->self, SBT_POLL, POLL_MS, NULL);
    SyncEntry(i);
}

void SbarTrim(HWND t, int right, int bottom) {
    int i = Find(t);
    if (i < 0) return;
    g_tab[i].trimR = right;
    g_tab[i].trimB = bottom;
    SyncEntry(i);
}

void SbarSync(HWND t) {
    int i = Find(t);
    if (i >= 0) SyncEntry(i);
}

void SbarDetach(HWND t) {
    int i = Find(t);
    Sb *v, *h;
    if (i < 0) return;
    v = g_tab[i].v;
    h = g_tab[i].h;
    g_tab[i].target = NULL;
    g_tab[i].v = g_tab[i].h = NULL;
    if (v) DestroyWindow(v->self);
    if (h) DestroyWindow(h->self);
}
