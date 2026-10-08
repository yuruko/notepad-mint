/* find.c - find / replace (one modeless dialog, two modes) and go to line. every search goes through search.c */
#include "mp.h"

enum { ID_WHAT = 1001, ID_CASE, ID_WRAP, ID_UP, ID_DOWN, ID_WITH, ID_REPLACE, ID_REPLACEALL, ID_WORD };
#define ID_LINE 1051

typedef struct { DlgBase b; int rep; } FindSt;
typedef struct { DlgBase b; HWND edit; } GotoSt;

static WCHAR  g_findWhat[256], g_findWith[256];
static int    g_findUp;
static FindSt g_fd;                         /* the modeless dialog (b.hwnd = NULL while it is closed) */
static int    g_switching;                  /* recreating it for the other mode: don't hand the focus back */
static RECT   g_pos;                        /* where it was last: it reopens there */
static int    g_havePos;

HWND FindDlgHwnd(void) { return g_fd.b.hwnd; }
BOOL FindHasText(void) { return g_findWhat[0] != 0; }

/* ------------------------------------------------------------- search -- */
static void NotFound(HWND owner)
{
    WCHAR msg[300];
    wcopy(msg, L"cannot find \"", COUNTOF(msg));
    wcat(msg, g_findWhat, COUNTOF(msg));
    wcat(msg, L"\"", COUNTOF(msg));
    MpAsk(owner, APP_NAME, msg, L"ok", NULL, NULL, 1);
}

/* like notepad: move the dialog above (else below) the match when it covers it */
static void Dodge(HWND dlg, int at)
{
    MONITORINFO mi;
    POINT pt;
    RECT r;
    LRESULT p;
    int lh = MulDiv(g_pf.cur, g_dpi, 48), h, y;
    if (!dlg) return;
    p = SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)at, 0);
    if (p == -1) return;
    pt.x = GET_X_LPARAM(p); pt.y = GET_Y_LPARAM(p);
    ClientToScreen(g_edit, &pt);
    GetWindowRect(dlg, &r);
    if (pt.x < r.left || pt.x >= r.right || pt.y + lh <= r.top || pt.y >= r.bottom) return;
    h = r.bottom - r.top;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST), &mi)) return;
    y = pt.y - h - S(8);
    if (y < mi.rcWork.top) y = pt.y + lh + S(8);
    if (y + h > mi.rcWork.bottom) return;
    SetWindowPos(dlg, NULL, r.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* down from the end of the selection / up from its start, then once more from the other end (wrap around) */
static void Search(HWND owner, int up)
{
    DWORD s = 0, e = 0;
    void *hl = NULL;
    int n = 0, m = wlen(g_findWhat), at = -1;
    const WCHAR *t;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    t = EditLockText(&hl, &n);
    if (t) {
        at = FindInTextEx(t, n, g_findWhat, m, (int)(up ? s : e), up, g_pf.matchCase, g_pf.wholeWord);
        if (at < 0 && g_pf.wrapAround) at = FindInTextEx(t, n, g_findWhat, m, up ? n : 0, up, g_pf.matchCase, g_pf.wholeWord);
    }
    EditUnlockText(hl);
    if (at < 0) { NotFound(owner); return; }
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)at, (LPARAM)(at + m));
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    Dodge(g_fd.b.hwnd, at);
}

/* f3 / shift+f3: the remembered text + options, the direction from the key */
void FindNext(int dirUp)
{
    if (!g_findWhat[0]) { FindDlgShow(g_fd.b.hwnd ? g_fd.rep : 0); return; }     /* nothing remembered: ask */
    Search(g_hwnd, dirUp);
}

/* the selection is replaced only when it is a match (honouring match case); then find next */
static void Replace(HWND owner)
{
    DWORD s = 0, e = 0;
    void *hl = NULL;
    int n = 0, m = wlen(g_findWhat), hit = 0;
    const WCHAR *t;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    t = EditLockText(&hl, &n);
    if (t && (int)(e - s) == m && (int)e <= n)
        hit = FindInTextEx(t, n, g_findWhat, m, (int)s, 0, g_pf.matchCase, g_pf.wholeWord) == (int)s;
    EditUnlockText(hl);
    if (hit) {
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)g_findWith);
        if (g_findUp) SendMessageW(g_edit, EM_SETSEL, (WPARAM)s, (LPARAM)s);    /* going up: continue before it */
    }
    Search(owner, g_findUp);
}

/* one pass over the zero-copy view, then the whole text is swapped in one EM_REPLACESEL (= one undo step) */
static void ReplaceAll(HWND owner)
{
    DWORD s = 0, e = 0;
    void *hl = NULL;
    int n = 0, m = wlen(g_findWhat), wn = wlen(g_findWith), len = 0, cnt = 0, k = 0, i, p, caret, found = 0;
    const WCHAR *t;
    WCHAR *out = NULL;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    t = EditLockText(&hl, &n);
    if (t) {
        found = FindInTextEx(t, n, g_findWhat, m, 0, 0, g_pf.matchCase, g_pf.wholeWord) >= 0; /* avoid copying the whole text for zero matches */
        if (found) {
            out = ReplaceAllTextEx(t, n, g_findWhat, m, g_findWith, wn, g_pf.matchCase, g_pf.wholeWord, &len, &cnt);
            if ((int)s > n) s = (DWORD)n;
            for (i = 0; (p = FindInTextEx(t, n, g_findWhat, m, i, 0, g_pf.matchCase, g_pf.wholeWord)) >= 0 && p + m <= (int)s; i = p + m) k++; /* full document boundaries, matches before the caret */
        }
    }
    EditUnlockText(hl);
    if (found && !out) {
        MpAsk(owner, APP_NAME, L"not enough memory available to complete this operation.", L"ok", NULL, NULL, 1);
        return;
    }
    if (!found || !cnt) { mem_free(out); NotFound(owner); return; }
    caret = (int)s + k * (wn - m);                  /* the caret stays at the same place in the text */
    if (caret < 0) caret = 0;
    if (caret > len) caret = len;
    SendMessageW(g_edit, EM_SETSEL, 0, (LPARAM)-1);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)out);
    mem_free(out);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

/* ------------------------------------------------------------- dialog -- */
static void Flag(HWND h, int id, int *v)
{
    HWND c = GetDlgItem(h, id);
    if (c) *v = SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

/* the dialog's texts and options -> the remembered state (main saves the search options) */
static void Sync(HWND h)
{
    HWND c;
    if ((c = GetDlgItem(h, ID_WHAT)) != NULL) GetWindowTextW(c, g_findWhat, 256);
    if ((c = GetDlgItem(h, ID_WITH)) != NULL) GetWindowTextW(c, g_findWith, 256);
    Flag(h, ID_CASE, &g_pf.matchCase);
    Flag(h, ID_WRAP, &g_pf.wrapAround);
    Flag(h, ID_WORD, &g_pf.wholeWord);
    Flag(h, ID_UP, &g_findUp);
}

/* find next / replace / replace all only while "find what" has text */
static void Enable(HWND h)
{
    static const int ids[3] = { IDOK, ID_REPLACE, ID_REPLACEALL };
    BOOL on = GetWindowTextLengthW(GetDlgItem(h, ID_WHAT)) > 0;
    int i;
    for (i = 0; i < 3; i++) {
        HWND c = GetDlgItem(h, ids[i]);
        if (c) EnableWindow(c, on);
    }
}

static void Opt(HWND h, const WCHAR *text, int x, int y, int w, int id, DWORD style, int on)
{
    HWND c = UiButton(h, text, x, y, w, 20, id, style);
    SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* etched frame whose caption sits on its top line (the label hides the line behind the text) */
static void Group(DlgBase *b, const WCHAR *text, int x, int y, int w, int h)
{
    HWND c = UiLabel(b->hwnd, text, x + 6, y, 8, 16, 0, SS_CENTER | SS_NOPREFIX);
    HDC dc = GetDC(c);
    HGDIOBJ of = SelectObject(dc, g_fontUI);
    int tw = TextW(dc, text, -1) + S(6);
    SelectObject(dc, of);
    ReleaseDC(c, dc);
    SetWindowPos(c, NULL, 0, 0, tw, S(16), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    DlgFrame(b, x, y + 8, w, h - 8, BV_ETCHED);
}

static void FindLayout(DlgBase *b, int rep)
{
    HWND h = b->hwnd, e;
    int dy = rep ? 32 : 0;
    UiLabel(h, L"find what:", 12, 16, 78, 16, 0, SS_NOPREFIX);
    e = UiEdit(b, g_findWhat, 92, 12, 212, 23, ID_WHAT, 0);
    SendMessageW(e, EM_LIMITTEXT, 255, 0);
    b->focus = e;
    if (rep) {
        UiLabel(h, L"replace with:", 12, 48, 78, 16, 0, SS_NOPREFIX);
        e = UiEdit(b, g_findWith, 92, 44, 212, 23, ID_WITH, 0);
        SendMessageW(e, EM_LIMITTEXT, 255, 0);
    }
    Opt(h, L"match case", 12, 66 + dy, 150, ID_CASE, BS_AUTOCHECKBOX, g_pf.matchCase);
    Opt(h, L"wrap around", 12, 90 + dy, 150, ID_WRAP, BS_AUTOCHECKBOX, g_pf.wrapAround);
    Opt(h, L"whole word", 12, 114 + dy, 150, ID_WORD, BS_AUTOCHECKBOX, g_pf.wholeWord);
    Group(b, L"direction", 176, 50 + dy, 128, 52);
    Opt(h, L"up", 188, 70 + dy, 46, ID_UP, BS_AUTORADIOBUTTON | WS_GROUP, g_findUp);
    Opt(h, L"down", 240, 70 + dy, 58, ID_DOWN, BS_AUTORADIOBUTTON, !g_findUp);
    UiButton(h, L"find next", 316, 12, 82, 24, IDOK, BS_DEFPUSHBUTTON | WS_GROUP);
    if (rep) {
        UiButton(h, L"replace", 316, 42, 82, 24, ID_REPLACE, BS_PUSHBUTTON);
        UiButton(h, L"replace all", 316, 72, 82, 24, ID_REPLACEALL, BS_PUSHBUTTON);
    }
    UiButton(h, L"cancel", 316, rep ? 102 : 42, 82, 24, IDCANCEL, BS_PUSHBUTTON);
    Enable(h);
}

static void FindCmd(HWND h, int id, int code)
{
    if (id == IDCANCEL) { SendMessageW(h, WM_CLOSE, 0, 0); return; }     /* DlgCommon destroys a modeless dialog */
    if ((id == ID_WHAT || id == ID_WITH) && code != EN_CHANGE) return;
    Sync(h);                                                             /* typed text, checks, radios */
    if (id == ID_WHAT) Enable(h);
    if (!g_findWhat[0]) return;
    if (id == IDOK) Search(h, g_findUp);
    else if (id == ID_REPLACE) Replace(h);
    else if (id == ID_REPLACEALL) ReplaceAll(h);
}

static LRESULT CALLBACK FindProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE:
        FindLayout(b, ((FindSt *)b)->rep);
        return 0;
    case WM_COMMAND:
        FindCmd(h, LOWORD(w), HIWORD(w));
        return 0;
    case WM_DESTROY:
        GetWindowRect(h, &g_pos);
        g_havePos = 1;
        break;
    case WM_NCDESTROY:
        b->hwnd = NULL;
        if (!g_switching && g_edit && g_hwnd && IsWindowVisible(g_hwnd) && IsWindowEnabled(g_hwnd)) SetFocus(g_edit);
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

/* the main edit's selection when it is non-empty, single line and <= 255 chars (never the clipboard) */
static BOOL SelText(WCHAR *out)
{
    DWORD s = 0, e = 0;
    void *hl = NULL;
    int n = 0, len, i = 0;
    const WCHAR *t;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    len = (int)(e - s);
    if (len <= 0 || len > 255) return FALSE;
    t = EditLockText(&hl, &n);
    if (t && (int)e <= n) {
        t += s;
        for (; i < len && t[i] != '\r' && t[i] != '\n'; i++) out[i] = t[i];
    }
    EditUnlockText(hl);
    out[i] = 0;
    return i == len;
}

/* put the new (hidden) dialog where the last one was, kept inside that monitor's work area */
static void PlaceAgain(HWND h)
{
    MONITORINFO mi;
    POINT pt;
    RECT r;
    HMONITOR mon;
    int w, ht;
    if (!g_havePos) return;
    pt.x = g_pos.left; pt.y = g_pos.top;
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoW(mon, &mi)) return;
    GetWindowRect(h, &r);
    w = r.right - r.left; ht = r.bottom - r.top;
    if (pt.x + w > mi.rcWork.right)  pt.x = mi.rcWork.right - w;
    if (pt.y + ht > mi.rcWork.bottom) pt.y = mi.rcWork.bottom - ht;
    if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
    if (pt.y < mi.rcWork.top)  pt.y = mi.rcWork.top;
    SetWindowPos(h, NULL, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FindDlgShow(int replaceMode)
{
    static BOOL reg;
    WCHAR sel[256];
    HWND h = g_fd.b.hwnd, e;
    int rep = replaceMode ? 1 : 0, pre;

    if (!reg) { RegClass(L"mp_find", FindProc, 0, NULL); reg = TRUE; }
    if (h && !IsWindowEnabled(h)) return;               /* its "cannot find" box is up */
    if (h) Sync(h);                                     /* keep what was typed */
    pre = SelText(sel);
    if (pre) wcopy(g_findWhat, sel, 256);
    if (h && g_fd.rep == rep) {
        if (pre) SetWindowTextW(GetDlgItem(h, ID_WHAT), g_findWhat);
    } else {
        if (h) { g_switching = 1; DestroyWindow(h); g_switching = 0; }
        DlgBaseInit(&g_fd.b, g_hwnd);
        g_fd.rep = rep;
        h = DlgOpen(&g_fd.b, L"mp_find", rep ? L"replace" : L"find", 410, rep ? 178 : 146, 1);
        if (!h) return;
        PlaceAgain(h);
        ShowWindow(h, SW_SHOW);
    }
    SetActiveWindow(h);
    e = GetDlgItem(h, ID_WHAT);
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, 0, (LPARAM)-1);
}

/* -------------------------------------------------------------- go to -- */
static LRESULT CALLBACK GotoProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    GotoSt *d = (GotoSt *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        WCHAR num[16];
        int line, col;
        EditCaretPos(&line, &col);
        wsprintfW(num, L"%d", line);
        UiLabel(h, L"line number:", 12, 14, 276, 16, 0, SS_NOPREFIX);
        d->edit = UiEdit(b, num, 12, 34, 276, 23, ID_LINE, ES_NUMBER);
        SendMessageW(d->edit, EM_LIMITTEXT, 9, 0);
        SendMessageW(d->edit, EM_SETSEL, 0, (LPARAM)-1);
        b->focus = d->edit;
        UiButton(h, L"go to", 116, 72, 82, 24, IDOK, BS_DEFPUSHBUTTON);
        UiButton(h, L"cancel", 206, 72, 82, 24, IDCANCEL, BS_PUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            WCHAR t[16];
            int n;
            GetWindowTextW(d->edit, t, 16);
            n = wtoi(t);
            if (EditGotoLine(n < 1 ? 1 : n)) { b->result = 1; b->done = 1; return 0; }
            MpAsk(h, L"go to line", L"the line number is beyond the total number of lines", L"ok", NULL, NULL, 1);
            SetFocus(d->edit);
            SendMessageW(d->edit, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) { b->result = 0; b->done = 1; return 0; }
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

void GotoDlg(HWND owner)
{
    static BOOL reg;
    GotoSt d;
    if (!reg) { RegClass(L"mp_goto", GotoProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    if (!DlgOpen(&d.b, L"mp_goto", L"go to line", 300, 110, 0)) return;
    DlgRunModal(&d.b);
    if (g_edit) SetFocus(g_edit);
}
