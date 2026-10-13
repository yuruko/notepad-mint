#include "mp.h"

enum { ID_WHAT = 1001, ID_CASE, ID_WRAP, ID_UP, ID_DOWN, ID_WITH, ID_REPLACE, ID_REPLACEALL, ID_WORD };
#define ID_LINE 1051

typedef struct { DlgBase base; int replaceMode; } FindSt;
typedef struct { DlgBase base; HWND edit; } GotoSt;

static FindSt g_fd;
static int    g_switching;
static RECT   g_pos;
static int    g_havePos;

HWND FindDlgHwnd(void) { return g_fd.base.hwnd; }
BOOL FindHasText(void) { return g_pf.findWhat[0] != 0; }

static void NotFound(HWND owner) {
    WCHAR msg[300];
    wcopy(msg, L"cannot find \"", COUNTOF(msg));
    wcat(msg, g_pf.findWhat, COUNTOF(msg));
    wcat(msg, L"\"", COUNTOF(msg));
    MpNote(owner, APP_NAME, msg);
}

static void Dodge(HWND dlg, int index) {
    MONITORINFO monitorInfo;
    POINT pt;
    RECT dlgRect;
    LRESULT charPos;
    int lineHeight = MulDiv(g_pf.cur, g_dpi, 480), height, y;
    if (!dlg) return;
    charPos = SendMessageW(g_edit, EM_POSFROMCHAR, (WPARAM)index, 0);
    if (charPos == -1) return;
    pt.x = GET_X_LPARAM(charPos); pt.y = GET_Y_LPARAM(charPos);
    ClientToScreen(g_edit, &pt);
    GetWindowRect(dlg, &dlgRect);
    if (pt.x < dlgRect.left || pt.x >= dlgRect.right || pt.y + lineHeight <= dlgRect.top || pt.y >= dlgRect.bottom) return;
    height = dlgRect.bottom - dlgRect.top;
    monitorInfo.cbSize = sizeof monitorInfo;
    if (!GetMonitorInfoW(MonitorFromWindow(dlg, MONITOR_DEFAULTTONEAREST), &monitorInfo)) return;
    y = pt.y - height - S(8);
    if (y < monitorInfo.rcWork.top) y = pt.y + lineHeight + S(8);
    if (y + height > monitorInfo.rcWork.bottom) return;
    SetWindowPos(dlg, NULL, dlgRect.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void Search(HWND owner, int up) {
    DWORD selStart = 0, selEnd = 0;
    void *lock = NULL;
    int length = 0, patLen = wlen(g_pf.findWhat), found = -1;
    const WCHAR *text;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    text = EditLockText(&lock, &length);
    if (text) {
        found = FindInTextEx(text, length, g_pf.findWhat, patLen, (int)(up ? selStart : selEnd), up, g_pf.matchCase, g_pf.wholeWord);
        if (found < 0 && g_pf.wrapAround) found = FindInTextEx(text, length, g_pf.findWhat, patLen, up ? length : 0, up, g_pf.matchCase, g_pf.wholeWord);
    }
    EditUnlockText(lock);
    if (found < 0) { NotFound(owner); return; }
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)found, (LPARAM)(found + patLen));
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    Dodge(g_fd.base.hwnd, found);
}

void FindNext(int reverse) {
    if (!g_pf.findWhat[0]) { FindDlgShow(g_fd.base.hwnd ? g_fd.replaceMode : 0); return; }
    Search(g_hwnd, reverse ? !g_pf.findUp : g_pf.findUp);
}

static void Replace(HWND owner) {
    DWORD selStart = 0, selEnd = 0;
    void *lock = NULL;
    int length = 0, patLen = wlen(g_pf.findWhat), hit = 0;
    const WCHAR *text;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    text = EditLockText(&lock, &length);
    if (text && (int)(selEnd - selStart) == patLen && (int)selEnd <= length) hit = FindInTextEx(text, length, g_pf.findWhat, patLen, (int)selStart, 0, g_pf.matchCase, g_pf.wholeWord) == (int)selStart;
    EditUnlockText(lock);
    if (hit) {
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)g_pf.findWith);
        if (g_pf.findUp) SendMessageW(g_edit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selStart);
    }
    Search(owner, g_pf.findUp);
}

static void ReplaceAll(HWND owner) {
    DWORD selStart = 0, selEnd = 0;
    void *lock = NULL;
    int length = 0, patLen = wlen(g_pf.findWhat), replLen = wlen(g_pf.findWith), outLen = 0, count = 0, before = 0, i, pos, caret, found = 0;
    const WCHAR *text;
    WCHAR *out = NULL;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    text = EditLockText(&lock, &length);
    if (text) {
        found = FindInTextEx(text, length, g_pf.findWhat, patLen, 0, 0, g_pf.matchCase, g_pf.wholeWord) >= 0;
        if (found) {
            out = ReplaceAllTextEx(text, length, g_pf.findWhat, patLen, g_pf.findWith, replLen, g_pf.matchCase, g_pf.wholeWord, &outLen, &count);
            if ((int)selStart > length) selStart = (DWORD)length;
            for (i = 0; (pos = FindInTextEx(text, length, g_pf.findWhat, patLen, i, 0, g_pf.matchCase, g_pf.wholeWord)) >= 0 && pos + patLen <= (int)selStart; i = pos + patLen) before++;
        }
    }
    EditUnlockText(lock);
    if (found && !out) {
        MpNote(owner, APP_NAME, L"not enough memory available to complete this operation.");
        return;
    }
    if (!found || !count) { mem_free(out); NotFound(owner); return; }
    caret = (int)selStart + before * (replLen - patLen);
    if (caret < 0) caret = 0;
    if (caret > outLen) caret = outLen;
    SendMessageW(g_edit, EM_SETSEL, 0, (LPARAM)-1);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)out);
    mem_free(out);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

static void Flag(HWND dlg, int id, int *value) {
    HWND control = GetDlgItem(dlg, id);
    if (control) *value = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void Sync(HWND dlg) {
    HWND control;
    if ((control = GetDlgItem(dlg, ID_WHAT)) != NULL) GetWindowTextW(control, g_pf.findWhat, FIND_CAP);
    if ((control = GetDlgItem(dlg, ID_WITH)) != NULL) GetWindowTextW(control, g_pf.findWith, FIND_CAP);
    Flag(dlg, ID_CASE, &g_pf.matchCase);
    Flag(dlg, ID_WRAP, &g_pf.wrapAround);
    Flag(dlg, ID_WORD, &g_pf.wholeWord);
    Flag(dlg, ID_UP, &g_pf.findUp);
}

static void Enable(HWND dlg) {
    static const int ids[3] = { IDOK, ID_REPLACE, ID_REPLACEALL };
    BOOL enabled = GetWindowTextLengthW(GetDlgItem(dlg, ID_WHAT)) > 0;
    int i;
    for (i = 0; i < 3; i++) {
        HWND control = GetDlgItem(dlg, ids[i]);
        if (control) EnableWindow(control, enabled);
    }
}

static void Opt(HWND dlg, const WCHAR *text, int x, int y, int width, int id, DWORD style, int checked) {
    HWND button = UiButton(dlg, text, x, y, width, 20, id, style);
    SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void Group(DlgBase *base, const WCHAR *text, int x, int y, int width, int height) {
    HWND label = UiLabel(base->hwnd, text, x + 6, y, 8, 16, 0, SS_CENTER | SS_NOPREFIX);
    HDC hdc = GetDC(label);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontUI);
    int textWidth = TextW(hdc, text, -1) + S(6);
    SelectObject(hdc, oldFont);
    ReleaseDC(label, hdc);
    SetWindowPos(label, NULL, 0, 0, textWidth, S(16), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    DlgFrame(base, x, y + 8, width, height - 8, BV_ETCHED);
}

static void FindLayout(DlgBase *base, int replaceMode) {
    HWND dlg = base->hwnd, edit;
    int dy = replaceMode ? 32 : 0;
    UiLabel(dlg, L"fi&nd what:", 12, 16, 78, 16, 0, 0);
    edit = UiEdit(base, g_pf.findWhat, 92, 12, 212, 23, ID_WHAT, 0);
    SendMessageW(edit, EM_LIMITTEXT, 255, 0);
    base->focus = edit;
    if (replaceMode) {
        UiLabel(dlg, L"re&place with:", 12, 48, 78, 16, 0, 0);
        edit = UiEdit(base, g_pf.findWith, 92, 44, 212, 23, ID_WITH, 0);
        SendMessageW(edit, EM_LIMITTEXT, 255, 0);
    }
    Opt(dlg, L"match &case", 12, 66 + dy, 150, ID_CASE, BS_AUTOCHECKBOX, g_pf.matchCase);
    Opt(dlg, L"&wrap around", 12, 90 + dy, 150, ID_WRAP, BS_AUTOCHECKBOX, g_pf.wrapAround);
    Opt(dlg, L"whole w&ord", 12, 114 + dy, 150, ID_WORD, BS_AUTOCHECKBOX, g_pf.wholeWord);
    Group(base, L"direction", 176, 50 + dy, 128, 52);
    Opt(dlg, L"&up", 188, 70 + dy, 46, ID_UP, BS_AUTORADIOBUTTON | WS_GROUP, g_pf.findUp);
    Opt(dlg, L"&down", 240, 70 + dy, 58, ID_DOWN, BS_AUTORADIOBUTTON, !g_pf.findUp);
    UiButton(dlg, L"&find next", 316, 12, 82, 24, IDOK, BS_DEFPUSHBUTTON | WS_GROUP);
    if (replaceMode) {
        UiButton(dlg, L"&replace", 316, 42, 82, 24, ID_REPLACE, BS_PUSHBUTTON);
        UiButton(dlg, L"replace &all", 316, 72, 82, 24, ID_REPLACEALL, BS_PUSHBUTTON);
    }
    UiButton(dlg, L"cancel", 316, replaceMode ? 102 : 42, 82, 24, IDCANCEL, BS_PUSHBUTTON);
    Enable(dlg);
}

static void FindCmd(HWND dlg, int id, int code) {
    if (id == IDCANCEL) { SendMessageW(dlg, WM_CLOSE, 0, 0); return; }
    if ((id == ID_WHAT || id == ID_WITH) && code != EN_CHANGE) return;
    Sync(dlg);
    if (id == ID_WHAT) Enable(dlg);
    if (!g_pf.findWhat[0]) return;
    if (id == IDOK) Search(dlg, g_pf.findUp);
    else if (id == ID_REPLACE) Replace(dlg);
    else if (id == ID_REPLACEALL) ReplaceAll(dlg);
}

static LRESULT CALLBACK FindProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE:
        FindLayout(base, ((FindSt *)base)->replaceMode);
        return 0;
    case WM_COMMAND:
        FindCmd(hwnd, LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_DESTROY:
        GetWindowRect(hwnd, &g_pos);
        g_havePos = 1;
        break;
    case WM_NCDESTROY:
        base->hwnd = NULL;
        if (!g_switching && g_edit && g_hwnd && IsWindowVisible(g_hwnd) && IsWindowEnabled(g_hwnd)) SetFocus(g_edit);
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL SelText(WCHAR *out) {
    DWORD selStart = 0, selEnd = 0;
    void *lock = NULL;
    int length = 0, selLen, i = 0;
    const WCHAR *text;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    selLen = (int)(selEnd - selStart);
    if (selLen <= 0 || selLen > 255) return FALSE;
    text = EditLockText(&lock, &length);
    if (text && (int)selEnd <= length) {
        text += selStart;
        for (; i < selLen && text[i] != '\r' && text[i] != '\n'; i++) out[i] = text[i];
    }
    EditUnlockText(lock);
    out[i] = 0;
    return i == selLen;
}

static void PlaceAgain(HWND dlg) {
    MONITORINFO monitorInfo;
    POINT pt;
    RECT dlgRect;
    HMONITOR monitor;
    int width, height;
    if (!g_havePos) return;
    pt.x = g_pos.left; pt.y = g_pos.top;
    monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    monitorInfo.cbSize = sizeof monitorInfo;
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) return;
    GetWindowRect(dlg, &dlgRect);
    width = dlgRect.right - dlgRect.left; height = dlgRect.bottom - dlgRect.top;
    if (pt.x + width > monitorInfo.rcWork.right)  pt.x = monitorInfo.rcWork.right - width;
    if (pt.y + height > monitorInfo.rcWork.bottom) pt.y = monitorInfo.rcWork.bottom - height;
    if (pt.x < monitorInfo.rcWork.left) pt.x = monitorInfo.rcWork.left;
    if (pt.y < monitorInfo.rcWork.top)  pt.y = monitorInfo.rcWork.top;
    SetWindowPos(dlg, NULL, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FindDlgShow(int replaceMode) {
    static BOOL reg;
    WCHAR selection[256];
    HWND dlg = g_fd.base.hwnd, edit;
    int replacing = replaceMode ? 1 : 0, prefill;
    if (!reg) { RegClass(L"mp_find", FindProc, 0, NULL); reg = TRUE; }
    if (dlg && !IsWindowEnabled(dlg)) return;
    if (dlg) Sync(dlg);
    prefill = SelText(selection);
    if (prefill) wcopy(g_pf.findWhat, selection, FIND_CAP);
    if (dlg && g_fd.replaceMode == replacing) {
        if (prefill) SetWindowTextW(GetDlgItem(dlg, ID_WHAT), g_pf.findWhat);
    } else {
        if (dlg) { g_switching = 1; DestroyWindow(dlg); g_switching = 0; }
        DlgBaseInit(&g_fd.base, g_hwnd);
        g_fd.replaceMode = replacing;
        dlg = DlgOpen(&g_fd.base, L"mp_find", replacing ? L"replace" : L"find", 410, replacing ? 178 : 146, 1);
        if (!dlg) return;
        PlaceAgain(dlg);
        ShowWindow(dlg, SW_SHOW);
    }
    SetActiveWindow(dlg);
    edit = GetDlgItem(dlg, ID_WHAT);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, (LPARAM)-1);
}

static LRESULT CALLBACK GotoProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    GotoSt *state = (GotoSt *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        WCHAR num[16];
        int line, col;
        EditCaretPos(&line, &col);
        wsprintfW(num, L"%d", line);
        UiLabel(hwnd, L"&line number:", 12, 14, 276, 16, 0, 0);
        state->edit = UiEdit(base, num, 12, 34, 276, 23, ID_LINE, ES_NUMBER);
        SendMessageW(state->edit, EM_LIMITTEXT, 9, 0);
        SendMessageW(state->edit, EM_SETSEL, 0, (LPARAM)-1);
        base->focus = state->edit;
        UiButton(hwnd, L"go to", 116, 72, 82, 24, IDOK, BS_DEFPUSHBUTTON);
        UiButton(hwnd, L"cancel", 206, 72, 82, 24, IDCANCEL, BS_PUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            WCHAR text[16];
            int line;
            GetWindowTextW(state->edit, text, 16);
            line = wtoi(text);
            if (EditGotoLine(line < 1 ? 1 : line)) { base->result = 1; base->done = 1; return 0; }
            MpNote(hwnd, L"go to line", L"the line number is beyond the total number of lines");
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { base->result = 0; base->done = 1; return 0; }
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void GotoDlg(HWND owner) {
    static BOOL reg;
    GotoSt state;
    if (!reg) { RegClass(L"mp_goto", GotoProc, 0, NULL); reg = TRUE; }
    memset(&state, 0, sizeof state);
    DlgBaseInit(&state.base, owner);
    if (!DlgOpen(&state.base, L"mp_goto", L"go to line", 300, 110, 0)) return;
    DlgRunModal(&state.base);
    if (g_edit) SetFocus(g_edit);
}
