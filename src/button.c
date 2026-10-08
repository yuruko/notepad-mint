/* button.c - themed push buttons, check boxes and radio buttons.
 * Each window owns its state; fonts are borrowed from the UI font cache.
 * Notifications follow native BM_* / WM_COMMAND conventions. */
#include "ui_internal.h"

/* ------------------------------------------------------- button class -- */
typedef struct { int check, hot, down, cap, kbd, focus, isdef; HFONT font; } BtnSt;

static int BtnType(HWND h) { return (int)(GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK); }

static BOOL IsMpRadio(HWND h)
{
    WCHAR cn[16];
    GetClassNameW(h, cn, 16);
    return wcmp(cn, L"mp_btn") == 0 && BtnType(h) == BS_AUTORADIOBUTTON;
}

static void BtnClick(HWND h, BtnSt *s)
{
    int t = BtnType(h);
    if (t == BS_AUTOCHECKBOX) {
        s->check = !s->check;
    } else if (t == BS_AUTORADIOBUTTON) {
        HWND a = h, c;
        while (!(GetWindowLongPtrW(a, GWL_STYLE) & WS_GROUP)) {      /* walk back to the group head */
            c = GetWindow(a, GW_HWNDPREV);
            if (!c) break;
            a = c;
        }
        for (c = a; c; c = GetWindow(c, GW_HWNDNEXT)) {              /* clear the other radios in it */
            if (c != a && (GetWindowLongPtrW(c, GWL_STYLE) & WS_GROUP)) break;
            if (c != h && IsMpRadio(c)) SendMessageW(c, BM_SETCHECK, 0, 0);
        }
        s->check = 1;
    }
    InvalidateRect(h, NULL, FALSE);
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetWindowLongPtrW(h, GWLP_ID), BN_CLICKED), (LPARAM)h);
}

static void BtnDraw(HWND h, BtnSt *s, HDC dc, RECT rc)
{
    WCHAR text[128];
    int n = GetWindowTextW(h, text, 128), t = BtnType(h);
    BOOL en = IsWindowEnabled(h) ? TRUE : FALSE;
    BOOL hot = s->hot && en;                         /* hover = the accent as the BACKGROUND with on-accent text (never just a coloured text) */
    COLORREF tc = !en ? C_DIM : C_TEXT;
    HGDIOBJ of = SelectObject(dc, s->font ? s->font : g_fontUI);

    FillC(dc, &rc, C_FACE);
    if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON) {
        RECT r = rc, in, tr;
        if (s->isdef && en) { UiFrame(dc, &r, C_ACCENT_FG, C_ACCENT_FG); InflateRect(&r, -1, -1); }   /* a disabled default (empty find box) shows no outline */
        Bevel(dc, &r, s->down ? BV_SUNKEN : BV_RAISED);
        in = r;
        InflateRect(&in, -2, -2);
        FillC(dc, &in, s->down ? C_FACE : (hot ? C_ACCENT : C_FACE2));
        tr = in;
        if (s->down) OffsetRect(&tr, 1, 1);
        TextC(dc, text, n, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, (hot && !s->down) ? C_ON_ACCENT : tc);
        if (s->focus && en) {
            RECT f = in;
            InflateRect(&f, -S(3), -S(3));
            DrawFocusRect(dc, &f);
        }
    } else {
        int bx = S(13), by = (rc.bottom - bx) / 2;
        RECT box, tr, cr, in;
        box.left = 0; box.top = by; box.right = bx; box.bottom = by + bx;
        if (t == BS_AUTORADIOBUTTON) {
            HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
            HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCPenColor(dc, ThemeGet() == THEME_DARK ? C_HI : C_LO2);      /* the ring: the darker edge in the light theme */
            SetDCBrushColor(dc, C_FIELD);
            Ellipse(dc, box.left, box.top, box.right, box.bottom);
            if (s->check) {
                SetDCPenColor(dc, en ? C_ACCENT_FG : C_DIM);
                SetDCBrushColor(dc, en ? C_ACCENT_FG : C_DIM);
                Ellipse(dc, box.left + S(4), box.top + S(4), box.right - S(3), box.bottom - S(3));
            }
            SelectObject(dc, op);
            SelectObject(dc, ob);
        } else {
            Bevel(dc, &box, BV_SUNKEN);
            in = box;
            InflateRect(&in, -2, -2);
            FillC(dc, &in, C_FIELD);
            if (s->check) CheckGlyph(dc, box.left, box.top, en ? C_ACCENT_FG : C_DIM);
        }
        tr.left = bx + S(7); tr.top = 0; tr.right = rc.right; tr.bottom = rc.bottom;
        if (hot) {                                   /* the label gets the accent background, like a menu item */
            RECT hl = tr;
            DrawTextW(dc, text, n, &hl, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
            hl.left = tr.left - S(4); hl.right = hl.right + S(4);
            hl.top = (rc.bottom - (hl.bottom - hl.top)) / 2 - S(2); hl.bottom = hl.top + (hl.bottom - hl.top) + S(4);
            FillC(dc, &hl, C_ACCENT);
        }
        TextC(dc, text, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, hot ? C_ON_ACCENT : tc);
        if (s->focus && en) {
            cr = tr;
            DrawTextW(dc, text, n, &cr, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
            cr.left = tr.left - S(2); cr.right = cr.left + (cr.right - tr.left) + S(4);
            cr.top = (rc.bottom - (cr.bottom - cr.top)) / 2 - S(1);
            cr.bottom = cr.top + (cr.bottom - cr.top) + S(2);
            DrawFocusRect(dc, &cr);
        }
    }
    SelectObject(dc, of);
}

/* Buffer a complete button when resources allow it. A failed allocation or
 * presentation still draws into the paint DC, so controls remain readable. */
static void BtnPaint(HWND h, BtnSt *s)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(h, &ps), memory = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ old = NULL;
    BOOL presented = FALSE;

    GetClientRect(h, &rc);
    if (dc && rc.right > 0 && rc.bottom > 0) {
        memory = CreateCompatibleDC(dc);
        if (memory) bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        if (bitmap) old = SelectObject(memory, bitmap);
        if (old && old != (HGDIOBJ)(INT_PTR)-1) {
            BtnDraw(h, s, memory, rc);
            presented = BitBlt(dc, 0, 0, rc.right, rc.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
        }
        if (!presented) BtnDraw(h, s, dc, rc);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    BtnSt *s = (BtnSt *)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT rc;
    POINT pt;
    int t;

    if (m == WM_NCCREATE) {
        s = (BtnSt *)mem_zalloc(sizeof *s);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        if (s) s->isdef = (((CREATESTRUCTW *)l)->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
        return DefWindowProcW(h, m, w, l);                 /* this is what stores the caption text */
    }
    if (!s) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_NCDESTROY:
        mem_free(s);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        BtnPaint(h, s);
        return 0;
    case WM_GETDLGCODE:
        t = BtnType(h);
        if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON)
            return DLGC_BUTTON | (s->isdef ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
        if (t == BS_AUTORADIOBUTTON) return DLGC_BUTTON | DLGC_RADIOBUTTON;
        return DLGC_BUTTON;
    case WM_SETFONT:
        s->font = (HFONT)w;
        if (l) InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)s->font;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        InvalidateRect(h, NULL, FALSE);
        return r; }
    case WM_ENABLE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETFOCUS:
        s->focus = 1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        s->focus = 0; s->kbd = 0; s->down = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case BM_GETCHECK:
        return s->check;
    case BM_SETCHECK:
        s->check = w ? 1 : 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case BM_GETSTATE:
        return (s->check ? BST_CHECKED : BST_UNCHECKED) | (s->down ? 4 : 0) | (s->focus ? 8 : 0);
    case BM_SETSTYLE:
        t = (int)(w & BS_TYPEMASK);
        if (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON) {
            s->isdef = (t == BS_DEFPUSHBUTTON);
            if (l) InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case BM_CLICK:
        BtnClick(h, s);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (IsWindowEnabled(h)) {
            SetFocus(h);
            s->cap = 1; s->down = 1;
            SetCapture(h);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        GetClientRect(h, &rc);
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        if (!s->hot) {
            TRACKMOUSEEVENT te;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            s->hot = 1;
            InvalidateRect(h, NULL, FALSE);
        }
        if (s->cap) {
            int in = PtInRect(&rc, pt) ? 1 : 0;
            if (in != s->down) { s->down = in; InvalidateRect(h, NULL, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        s->hot = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s->cap) {
            int fire = s->down;
            s->cap = 0; s->down = 0;
            ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
            if (fire) BtnClick(h, s);
        }
        return 0;
    case WM_CAPTURECHANGED:
        s->cap = 0; s->down = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_SPACE && !s->kbd) { s->kbd = 1; s->down = 1; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_KEYUP:
        if (w == VK_SPACE && s->kbd) {
            s->kbd = 0; s->down = 0;
            InvalidateRect(h, NULL, FALSE);
            BtnClick(h, s);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

HWND UiButton(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD style)
{
    HWND c = CreateWindowExW(0, L"mp_btn", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                             S(x), S(y), S(w), S(h), p, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return c;
}

void ButtonInit(void)
{
    RegClass(L"mp_btn", BtnProc, CS_DBLCLKS, NULL);
}
