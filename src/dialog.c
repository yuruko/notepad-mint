#include "ui_internal.h"

HWND DialogHoldFind(void) {
    HWND hwnd = FindDlgHwnd();
    if (!hwnd || !IsWindow(hwnd) || !IsWindowEnabled(hwnd)) return NULL;
    EnableWindow(hwnd, FALSE);
    return hwnd;
}

void DialogReleaseFind(HWND held) {
    if (held && IsWindow(held)) EnableWindow(held, TRUE);
}

DlgBase *DlgFromHwnd(HWND hwnd, UINT msg, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        DlgBase *base = (DlgBase *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)base);
        base->hwnd = hwnd;
        return base;
    }
    return (DlgBase *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

void DlgBaseInit(DlgBase *base, HWND owner) {
    memset(base, 0, sizeof *base);
    base->owner = owner;
}

void DlgFrame(DlgBase *base, int x, int y, int width, int height, int kind) {
    if (base->nframes >= 24) return;
    base->fr[base->nframes].left = S(x);
    base->fr[base->nframes].top = S(y);
    base->fr[base->nframes].right = S(x) + S(width);
    base->fr[base->nframes].bottom = S(y) + S(height);
    base->frk[base->nframes++] = (BYTE)kind;
}

HWND DlgOpen(DlgBase *base, const WCHAR *className, const WCHAR *title, int clientWidth, int clientHeight, int modeless) {
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    RECT windowRect, anchorRect, workArea;
    MONITORINFO monitorInfo;
    POINT center;
    int width, height, x, y;
    base->modeless = modeless;
    windowRect.left = 0; windowRect.top = 0; windowRect.right = S(clientWidth); windowRect.bottom = S(clientHeight);
    UiAdjustRect(&windowRect, style, 0);
    width = windowRect.right - windowRect.left;
    height = windowRect.bottom - windowRect.top;
    if (base->owner && IsWindowVisible(base->owner) && !IsIconic(base->owner)) GetWindowRect(base->owner, &anchorRect);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchorRect, 0);
    x = anchorRect.left + ((anchorRect.right - anchorRect.left) - width) / 2;
    y = anchorRect.top + ((anchorRect.bottom - anchorRect.top) - height) / 2;
    center.x = x + width / 2; center.y = y + height / 2;
    monitorInfo.cbSize = sizeof monitorInfo;
    GetMonitorInfoW(MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    workArea = monitorInfo.rcWork;
    if (x + width > workArea.right)  x = workArea.right - width;
    if (y + height > workArea.bottom) y = workArea.bottom - height;
    if (x < workArea.left) x = workArea.left;
    if (y < workArea.top)  y = workArea.top;
    base->hwnd = CreateWindowExW(0, className, title, style, x, y, width, height, base->owner, NULL, g_hinst, base);
    if (base->hwnd) {
        static HICON big, small_;
        if (!big) big = LoadIconW(g_hinst, MAKEINTRESOURCEW(1));
        if (!small_) small_ = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, UiMetric(SM_CXSMICON), UiMetric(SM_CYSMICON), LR_DEFAULTCOLOR);
        SendMessageW(base->hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        SendMessageW(base->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_);
        DarkFrame(base->hwnd, 1);
        SendMessageW(base->hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_INITIALIZE, 0), 0);
    }
    return base->hwnd;
}

typedef struct { HWND list[8]; int count; HWND skip; } Disabled;

static BOOL CALLBACK DisableOne(HWND hwnd, LPARAM lParam) {
    Disabled *disabled = (Disabled *)lParam;
    WCHAR className[32];
    if (hwnd == disabled->skip || disabled->count >= COUNTOF(disabled->list) || !IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd)) return TRUE;
    GetClassNameW(hwnd, className, 32);
    if (wcmp(className, APP_CLASS) != 0 && !(className[0] == 'm' && className[1] == 'p' && className[2] == '_')) return TRUE;
    EnableWindow(hwnd, FALSE);
    disabled->list[disabled->count++] = hwnd;
    return TRUE;
}

void DlgRunModal(DlgBase *base) {
    MSG msg;
    Disabled disabled;
    int i;
    disabled.count = 0; disabled.skip = base->hwnd;
    EnumThreadWindows(GetCurrentThreadId(), DisableOne, (LPARAM)&disabled);
    if (base->owner && IsWindowEnabled(base->owner) && disabled.count < COUNTOF(disabled.list)) {
        EnableWindow(base->owner, FALSE);
        disabled.list[disabled.count++] = base->owner;
    }
    ShowWindow(base->hwnd, SW_SHOW);
    while (!base->done) {
        BOOL result = GetMessageW(&msg, NULL, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(base->hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    for (i = disabled.count - 1; i >= 0; i--) if (IsWindow(disabled.list[i])) EnableWindow(disabled.list[i], TRUE);
    DestroyWindow(base->hwnd);
    base->hwnd = NULL;
}

BOOL DlgCommon(DlgBase *base, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *ret) {
    HDC hdc;
    *ret = 0;
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT client;
        GetClientRect(base->hwnd, &client);
        FillC((HDC)wParam, &client, C_FACE);
        *ret = 1;
        return TRUE; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        int i;
        hdc = BeginPaint(base->hwnd, &ps);
        for (i = 0; i < base->nframes; i++) Bevel(hdc, &base->fr[i], base->frk[i]);
        EndPaint(base->hwnd, &ps);
        return TRUE; }
    case WM_CTLCOLORSTATIC:
        hdc = (HDC)wParam;
        SetTextColor(hdc, GetWindowLongPtrW((HWND)lParam, GWLP_ID) == IDC_DIM ? C_DIM : C_TEXT);
        SetBkColor(hdc, C_FACE);
        *ret = (LRESULT)g_brFace;
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        hdc = (HDC)wParam;
        SetTextColor(hdc, C_TEXT);
        SetBkColor(hdc, C_FIELD);
        *ret = (LRESULT)g_brField;
        return TRUE;
    case DM_GETDEFID:
        *ret = MAKELONG(IDOK, 0x534B);
        return TRUE;
    case WM_ACTIVATE:
        DarkFrame(base->hwnd, LOWORD(wParam) != WA_INACTIVE);
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND focus = GetFocus();
            if (focus && IsChild(base->hwnd, focus)) base->focus = focus;
        } else if (base->focus && IsWindow(base->focus)) {
            SetFocus(base->focus);
        }
        return TRUE;
    case WM_CLOSE:
        if (base->modeless) { DestroyWindow(base->hwnd); }
        else { base->result = IDCANCEL; base->done = 1; }
        return TRUE;
    }
    return FALSE;
}

HWND UiLabel(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD extra) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra, S(x), S(y), S(width), S(height), parent, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(label, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return label;
}

static WNDPROC g_stockEdit;

static LRESULT CALLBACK DlgEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CHAR && wParam == 0x7F) {
        WCHAR text[PATH_CAP + 1];
        DWORD selStart = 0, selEnd = 0;
        int length, wordStart;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selStart != selEnd) { SendMessageW(hwnd, WM_CLEAR, 0, 0); return 0; }
        length = GetWindowTextW(hwnd, text, PATH_CAP + 1);
        wordStart = (int)selStart;
        if (wordStart > length) wordStart = length;
        while (wordStart > 0 && WordClass(text[wordStart - 1]) == 0) wordStart--;
        if (wordStart > 0) {
            int wordClass = WordClass(text[wordStart - 1]);
            while (wordStart > 0 && WordClass(text[wordStart - 1]) == wordClass) wordStart--;
        }
        if (wordStart != (int)selStart) {
            SendMessageW(hwnd, EM_SETSEL, (WPARAM)wordStart, (LPARAM)selStart);
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"");
        }
        return 0;
    }
    return CallWindowProcW(g_stockEdit, hwnd, msg, wParam, lParam);
}

HWND UiEdit(DlgBase *base, const WCHAR *text, int x, int y, int width, int height, int id, DWORD extra) {
    HWND edit;
    DlgFrame(base, x, y, width, height, BV_SUNKEN);
    edit = CreateWindowExW(0, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra, S(x) + 2, S(y) + 2, S(width) - 4, S(height) - 4, base->hwnd, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(edit, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    if (edit) {
        if (!g_stockEdit) g_stockEdit = (WNDPROC)(LONG_PTR)GetWindowLongPtrW(edit, GWLP_WNDPROC);
        SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)DlgEditProc);
    }
    return edit;
}

typedef struct { DlgBase base; const WCHAR *message, *labels[3]; int buttonCount, escIdx, textWidth, textHeight; } MsgDlg;

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    MsgDlg *dlg = (MsgDlg *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        int clientWidth = dlg->textWidth + 32, buttonWidth = 88, gap = 8, totalWidth = dlg->buttonCount * buttonWidth + (dlg->buttonCount - 1) * gap, i, x, y;
        if (clientWidth < totalWidth + 32) clientWidth = totalWidth + 32;
        UiLabel(hwnd, dlg->message, 16, 16, dlg->textWidth, dlg->textHeight, 0, SS_NOPREFIX);
        x = clientWidth - 16 - totalWidth;
        y = 16 + dlg->textHeight + 18;
        for (i = 0; i < dlg->buttonCount; i++) {
            HWND button = UiButton(hwnd, dlg->labels[i], x + i * (buttonWidth + gap), y, buttonWidth, 24, i == 0 ? IDOK : 100 + i + 1, i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            if (i == 0) base->focus = button;
        }
        return 0; }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) { base->result = 1; base->done = 1; return 0; }
        if (id == 102 || id == 103) { base->result = id - 100; base->done = 1; return 0; }
        if (id == IDCANCEL) { base->result = dlg->escIdx; base->done = 1; return 0; }
        break; }
    case WM_CLOSE:
        base->result = dlg->escIdx; base->done = 1;
        return 0;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int MpAsk(HWND owner, const WCHAR *title, const WCHAR *message, const WCHAR *label1, const WCHAR *label2, const WCHAR *label3, int escIdx) {
    MsgDlg dlg;
    HDC hdc;
    HGDIOBJ oldFont;
    RECT textRect;
    int clientWidth, clientHeight, totalWidth, buttonWidth = 88, gap = 8;
    memset(&dlg, 0, sizeof dlg);
    DlgBaseInit(&dlg.base, owner);
    dlg.message = message;
    dlg.escIdx = escIdx;
    dlg.labels[0] = label1; dlg.labels[1] = label2; dlg.labels[2] = label3;
    dlg.buttonCount = label3 ? 3 : (label2 ? 2 : 1);
    hdc = GetDC(NULL);
    oldFont = SelectObject(hdc, g_fontUI);
    textRect.left = 0; textRect.top = 0; textRect.right = S(380); textRect.bottom = 0;
    DrawTextW(hdc, message, -1, &textRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
    dlg.textWidth = UnS(textRect.right) + 4;
    dlg.textHeight = UnS(textRect.bottom) + 2;
    if (dlg.textWidth < 200) dlg.textWidth = 200;
    totalWidth = dlg.buttonCount * buttonWidth + (dlg.buttonCount - 1) * gap;
    clientWidth = dlg.textWidth + 32;
    if (clientWidth < totalWidth + 32) clientWidth = totalWidth + 32;
    clientHeight = 16 + dlg.textHeight + 18 + 24 + 16;
    if (!DlgOpen(&dlg.base, L"mp_msg", title, clientWidth, clientHeight, 0)) return escIdx;
    DlgRunModal(&dlg.base);
    return dlg.base.result;
}

void MpNote(HWND owner, const WCHAR *title, const WCHAR *message) {
    MpAsk(owner, title, message, L"ok", NULL, NULL, 1);
}

void DialogInit(void) {
    RegClass(L"mp_msg", MsgProc, 0, NULL);
}
