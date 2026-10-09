/* dialog.c - shared dialog lifetime, controls and themed message boxes.
 * DlgBase must be the first member of each dialog state. Native-dialog helpers
 * also hold the modeless find window so it cannot edit a document mid-operation. */
#include "ui_internal.h"

HWND DialogHoldFind(void)
{
    HWND h = FindDlgHwnd();
    if (!h || !IsWindow(h) || !IsWindowEnabled(h)) return NULL;
    EnableWindow(h, FALSE);
    return h;
}

void DialogReleaseFind(HWND held)
{
    if (held && IsWindow(held)) EnableWindow(held, TRUE);
}

/* ------------------------------------------------------- dialog base --- */
DlgBase *DlgFromHwnd(HWND h, UINT m, LPARAM l)
{
    if (m == WM_NCCREATE) {
        DlgBase *b = (DlgBase *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)b);
        b->hwnd = h;
        return b;
    }
    return (DlgBase *)GetWindowLongPtrW(h, GWLP_USERDATA);
}

void DlgBaseInit(DlgBase *b, HWND owner)
{
    memset(b, 0, sizeof *b);
    b->owner = owner;
}

void DlgFrame(DlgBase *b, int x, int y, int w, int h, int kind)
{
    if (b->nframes >= 24) return;
    b->fr[b->nframes].left = S(x);
    b->fr[b->nframes].top = S(y);
    b->fr[b->nframes].right = S(x) + S(w);
    b->fr[b->nframes].bottom = S(y) + S(h);
    b->frk[b->nframes++] = (BYTE)kind;
}

/* creates (hidden) a centred caption dialog whose client area is cw x ch (96-dpi px) */
HWND DlgOpen(DlgBase *b, const WCHAR *cls, const WCHAR *title, int cw, int ch, int modeless)
{
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT rc, ow, wa;
    MONITORINFO mi;
    POINT cp;
    int w, h, x, y;

    b->modeless = modeless;
    rc.left = 0; rc.top = 0; rc.right = S(cw); rc.bottom = S(ch);
    UiAdjustRect(&rc, st, 0);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    if (b->owner && IsWindowVisible(b->owner) && !IsIconic(b->owner)) GetWindowRect(b->owner, &ow);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &ow, 0);
    x = ow.left + ((ow.right - ow.left) - w) / 2;
    y = ow.top + ((ow.bottom - ow.top) - h) / 2;

    cp.x = x + w / 2; cp.y = y + h / 2;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(cp, MONITOR_DEFAULTTONEAREST), &mi);
    wa = mi.rcWork;
    if (x + w > wa.right)  x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (x < wa.left) x = wa.left;
    if (y < wa.top)  y = wa.top;

    b->hwnd = CreateWindowExW(0, cls, title, st, x, y, w, h, b->owner, NULL, g_hinst, b);
    if (b->hwnd) {
        static HICON big, small_;                       /* every dialog wears the app icon (resource 1) */
        if (!big) big = LoadIconW(g_hinst, MAKEINTRESOURCEW(1));
        if (!small_) small_ = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, UiMetric(SM_CXSMICON), UiMetric(SM_CYSMICON), LR_DEFAULTCOLOR);
        SendMessageW(b->hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        SendMessageW(b->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_);
        DarkFrame(b->hwnd, 1);
    }
    return b->hwnd;
}

/* a modal dialog disables every other window of ours that is live (the owner, and the modeless find dialog:
 * otherwise replace all could still edit the document under an open / save as dialog) and enables them again after */
typedef struct { HWND list[8]; int n; HWND skip; } Disabled;

static BOOL CALLBACK DisableOne(HWND h, LPARAM l)
{
    Disabled *d = (Disabled *)l;
    WCHAR cn[32];
    if (h == d->skip || d->n >= COUNTOF(d->list) || !IsWindowVisible(h) || !IsWindowEnabled(h)) return TRUE;
    GetClassNameW(h, cn, 32);
    if (wcmp(cn, APP_CLASS) != 0 && !(cn[0] == 'm' && cn[1] == 'p' && cn[2] == '_')) return TRUE;   /* ime windows etc. */
    EnableWindow(h, FALSE);
    d->list[d->n++] = h;
    return TRUE;
}

void DlgRunModal(DlgBase *b)
{
    MSG m;
    Disabled dis;
    int i;
    dis.n = 0; dis.skip = b->hwnd;
    EnumThreadWindows(GetCurrentThreadId(), DisableOne, (LPARAM)&dis);
    if (b->owner && IsWindowEnabled(b->owner) && dis.n < COUNTOF(dis.list)) {   /* an owner the walk skipped (not visible) */
        EnableWindow(b->owner, FALSE);
        dis.list[dis.n++] = b->owner;
    }
    ShowWindow(b->hwnd, SW_SHOW);
    while (!b->done) {
        BOOL r = GetMessageW(&m, NULL, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage((int)m.wParam);        /* let the app loop see WM_QUIT too */
            break;
        }
        if (!IsDialogMessageW(b->hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    for (i = dis.n - 1; i >= 0; i--)                           /* before the dialog goes away, so the activation returns to our own windows */
        if (IsWindow(dis.list[i])) EnableWindow(dis.list[i], TRUE);
    DestroyWindow(b->hwnd);
    b->hwnd = NULL;
}

/* shared themed behaviour for every dialog window. returns TRUE if handled */
BOOL DlgCommon(DlgBase *b, UINT m, WPARAM w, LPARAM l, LRESULT *ret)
{
    HDC dc;
    *ret = 0;
    switch (m) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(b->hwnd, &rc);
        FillC((HDC)w, &rc, C_FACE);
        *ret = 1;
        return TRUE; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        int i;
        dc = BeginPaint(b->hwnd, &ps);
        for (i = 0; i < b->nframes; i++) Bevel(dc, &b->fr[i], b->frk[i]);
        EndPaint(b->hwnd, &ps);
        return TRUE; }
    case WM_CTLCOLORSTATIC:
        dc = (HDC)w;
        SetTextColor(dc, GetWindowLongPtrW((HWND)l, GWLP_ID) == IDC_DIM ? C_DIM : C_TEXT);
        SetBkColor(dc, C_FACE);
        *ret = (LRESULT)g_brFace;
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        dc = (HDC)w;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        *ret = (LRESULT)g_brField;
        return TRUE;
    case DM_GETDEFID:
        *ret = MAKELONG(IDOK, 0x534B);                           /* DC_HASDEFID */
        return TRUE;
    case WM_ACTIVATE:
        DarkFrame(b->hwnd, LOWORD(w) != WA_INACTIVE);
        if (LOWORD(w) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(b->hwnd, f)) b->focus = f;
        } else if (b->focus && IsWindow(b->focus)) {
            SetFocus(b->focus);
        }
        return TRUE;
    case WM_CLOSE:
        if (b->modeless) { DestroyWindow(b->hwnd); }
        else { b->result = IDCANCEL; b->done = 1; }
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------- control helpers -- */
HWND UiLabel(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra,
                             S(x), S(y), S(w), S(h), p, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return c;
}

/* the stock single-line edit inserts a DEL character for ctrl+backspace (a box in the text, and a find pattern that can
 * never match): delete the word before the caret instead, like the main editor does (edit_text.c) */
static WNDPROC g_stockEdit;

static LRESULT CALLBACK DlgEditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CHAR && w == 0x7F) {
        WCHAR t[PATH_CAP + 1];
        DWORD s = 0, e = 0;
        int n, a;
        SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
        if (s != e) { SendMessageW(h, WM_CLEAR, 0, 0); return 0; }
        n = GetWindowTextW(h, t, PATH_CAP + 1);
        a = (int)s;
        if (a > n) a = n;
        while (a > 0 && WordClass(t[a - 1]) == 0) a--;                 /* blanks, then one run of a kind */
        if (a > 0) {
            int k = WordClass(t[a - 1]);
            while (a > 0 && WordClass(t[a - 1]) == k) a--;
        }
        if (a != (int)s) {
            SendMessageW(h, EM_SETSEL, (WPARAM)a, (LPARAM)s);
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)L"");
        }
        return 0;
    }
    return CallWindowProcW(g_stockEdit, h, m, w, l);
}

/* single-line edit inside a drawn 2px sunken frame */
HWND UiEdit(DlgBase *b, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra)
{
    HWND c;
    DlgFrame(b, x, y, w, h, BV_SUNKEN);
    c = CreateWindowExW(0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                        S(x) + 2, S(y) + 2, S(w) - 4, S(h) - 4, b->hwnd, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    if (c) {
        if (!g_stockEdit) g_stockEdit = (WNDPROC)(LONG_PTR)GetWindowLongPtrW(c, GWLP_WNDPROC);
        SetWindowLongPtrW(c, GWLP_WNDPROC, (LONG_PTR)DlgEditProc);
    }
    return c;
}

/* ------------------------------------------------------ message dialog -- */
typedef struct { DlgBase b; const WCHAR *msg, *lab[3]; int nb, esc, tw, th; } MsgDlg;

static LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    MsgDlg *d = (MsgDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        int cw = d->tw + 32, bw = 88, gap = 8, total = d->nb * bw + (d->nb - 1) * gap, i, x, y;
        if (cw < total + 32) cw = total + 32;
        UiLabel(h, d->msg, 16, 16, d->tw, d->th, 0, SS_NOPREFIX);
        x = cw - 16 - total;
        y = 16 + d->th + 18;
        for (i = 0; i < d->nb; i++) {
            HWND c = UiButton(h, d->lab[i], x + i * (bw + gap), y, bw, 24, i == 0 ? IDOK : 100 + i + 1,
                              i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            if (i == 0) b->focus = c;
        }
        return 0; }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK) { b->result = 1; b->done = 1; return 0; }
        if (id == 102 || id == 103) { b->result = id - 100; b->done = 1; return 0; }
        if (id == IDCANCEL) { b->result = d->esc; b->done = 1; return 0; }
        break; }
    case WM_CLOSE:
        b->result = d->esc; b->done = 1;
        return 0;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

/* themed message box. buttons are 1-based; returns the pressed index (escIdx for esc / close) */
int MpAsk(HWND owner, const WCHAR *title, const WCHAR *msg, const WCHAR *b1, const WCHAR *b2, const WCHAR *b3, int escIdx)
{
    MsgDlg d;
    HDC dc;
    HGDIOBJ old;
    RECT r;
    int cw, ch, total, bw = 88, gap = 8;

    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    d.msg = msg;
    d.esc = escIdx;
    d.lab[0] = b1; d.lab[1] = b2; d.lab[2] = b3;
    d.nb = b3 ? 3 : (b2 ? 2 : 1);

    dc = GetDC(NULL);
    old = SelectObject(dc, g_fontUI);
    r.left = 0; r.top = 0; r.right = S(380); r.bottom = 0;
    DrawTextW(dc, msg, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    d.tw = UnS(r.right) + 4;
    d.th = UnS(r.bottom) + 2;
    if (d.tw < 200) d.tw = 200;

    total = d.nb * bw + (d.nb - 1) * gap;
    cw = d.tw + 32;
    if (cw < total + 32) cw = total + 32;
    ch = 16 + d.th + 18 + 24 + 16;

    if (!DlgOpen(&d.b, L"mp_msg", title, cw, ch, 0)) return escIdx;
    DlgRunModal(&d.b);
    return d.b.result;
}

void MpNote(HWND owner, const WCHAR *title, const WCHAR *msg)
{
    MpAsk(owner, title, msg, L"ok", NULL, NULL, 1);
}

void DialogInit(void)
{
    RegClass(L"mp_msg", MsgProc, 0, NULL);
}
