#include "mp.h"

#define SS_ICON     0x03
#define STM_SETICON 0x0170

typedef struct { DlgBase base; HICON icon; HWND edit; } InfoDlg;

#define ID_LINK 1060
typedef struct { int hot, focus, down; } LinkSt;

static void LinkOpen(HWND hwnd) {
    typedef HINSTANCE (WINAPI *ShellExecFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
    WCHAR url[160];
    HMODULE module = LoadLibraryW(L"shell32.dll");
    ShellExecFn shellExecute = module ? (ShellExecFn)GetProcAddress(module, "ShellExecuteW") : NULL;
    wcopy(url, L"https://", COUNTOF(url));
    GetWindowTextW(hwnd, url + 8, COUNTOF(url) - 8);
    if (shellExecute) shellExecute(GetParent(hwnd), L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK LinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LinkSt *state = (LinkSt *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        state = (LinkSt *)mem_zalloc(sizeof *state);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_NCDESTROY:
        mem_free(state);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
    case WM_SETFOCUS:  state->focus = 1; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_KILLFOCUS: state->focus = 0; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    case WM_MOUSEMOVE:
        if (!state->hot) {
            TRACKMOUSEEVENT trackEvent;
            state->hot = 1;
            trackEvent.cbSize = sizeof trackEvent; trackEvent.dwFlags = TME_LEAVE; trackEvent.hwndTrack = hwnd; trackEvent.dwHoverTime = 0;
            TrackMouseEvent(&trackEvent);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        state->hot = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        SetCapture(hwnd);
        state->down = 1;
        return 0;
    case WM_LBUTTONUP:
        if (state->down) {
            RECT client;
            POINT pt;
            state->down = 0;
            ReleaseCapture();
            pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
            GetClientRect(hwnd, &client);
            if (PtInRect(&client, pt)) LinkOpen(hwnd);
        }
        return 0;
    case WM_KEYUP:
        if (wParam == VK_SPACE) { LinkOpen(hwnd); return 0; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client, textRect, measured, underline;
        WCHAR text[96];
        int length, textHeight;
        HGDIOBJ oldFont = SelectObject(hdc, g_fontUI);
        GetClientRect(hwnd, &client);
        FillC(hdc, &client, state->hot ? C_ACCENT : C_FACE);
        length = GetWindowTextW(hwnd, text, COUNTOF(text));
        textRect = client; textRect.left += S(2);
        measured = textRect;
        DrawTextW(hdc, text, length, &measured, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        textHeight = measured.bottom - measured.top;
        TextC(hdc, text, length, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, state->hot ? C_ON_ACCENT : C_ACCENT_FG);
        underline.left = textRect.left; underline.right = measured.right; underline.top = (client.bottom + textHeight) / 2 - 1; underline.bottom = underline.top + 1;
        FillC(hdc, &underline, state->hot ? C_ON_ACCENT : C_ACCENT_FG);
        if (state->focus) {
            RECT focusRect = client;
            InflateRect(&focusRect, -1, -1);
            DrawFocusRect(hdc, &focusRect);
        }
        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK AboutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    InfoDlg *dlg = (InfoDlg *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        HWND control;
        dlg->icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(32), S(32), LR_DEFAULTCOLOR);
        control = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON, S(20), S(20), S(32), S(32), hwnd, NULL, g_hinst, NULL);
        if (dlg->icon) SendMessageW(control, STM_SETICON, (WPARAM)dlg->icon, 0);
        control = UiLabel(hwnd, L"notepad mint", 68, 18, 290, 20, 0, SS_NOPREFIX);
        SendMessageW(control, WM_SETFONT, (WPARAM)g_fontUIB, FALSE);
        UiLabel(hwnd, L"version " APP_VERSION L"  (32-bit)", 68, 40, 290, 16, IDC_DIM, SS_NOPREFIX);
        DlgFrame(base, 20, 68, 340, 2, BV_ETCHED);
        UiLabel(hwnd, L"a free replacement for notepad.exe.\nplain text, dark or light, and nothing else:\nno ai, no sign-in, no telemetry, no cloud.", 20, 80, 340, 52, 0, SS_NOPREFIX);
        UiLabel(hwnd, L"written in c and assembly with no runtime libraries. one small 32-bit exe runs on 32-bit, 64-bit and arm windows.", 20, 138, 340, 34, IDC_DIM, SS_NOPREFIX);
        UiLabel(hwnd, L"settings: %appdata%\\notepad-mint\\settings.ini", 20, 176, 340, 16, IDC_DIM, SS_NOPREFIX);
        control = CreateWindowExW(0, L"mp_link", L"yuru.be", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(20), S(206), S(76), S(20), hwnd, (HMENU)(ULONG_PTR)ID_LINK, g_hinst, NULL);
        SendMessageW(control, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
        base->focus = UiButton(hwnd, L"ok", 272, 204, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) { base->result = 1; base->done = 1; return 0; }
        if (LOWORD(wParam) == ID_LINK) { LinkOpen(GetDlgItem(hwnd, ID_LINK)); return 0; }
        break;
    case WM_NCDESTROY:
        if (dlg->icon) { DestroyIcon(dlg->icon); dlg->icon = NULL; }
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void AboutDlg(HWND owner) {
    static BOOL reg;
    InfoDlg dlg;
    if (!reg) { RegClass(L"mp_about", AboutProc, 0, NULL); RegClass(L"mp_link", LinkProc, 0, NULL); reg = TRUE; }
    memset(&dlg, 0, sizeof dlg);
    DlgBaseInit(&dlg.base, owner);
    if (!DlgOpen(&dlg.base, L"mp_about", L"about notepad mint", 380, 244, 0)) return;
    DlgRunModal(&dlg.base);
}

static const WCHAR g_help[] = L"notepad mint - keyboard shortcuts and tips\r\n"
    L"\r\n"
    L"file\r\n"
    L"  ctrl+n            new\r\n"
    L"  ctrl+shift+n      new window\r\n"
    L"  ctrl+o            open\r\n"
    L"  ctrl+s            save\r\n"
    L"  ctrl+shift+s      save as\r\n"
    L"  ctrl+p            print\r\n"
    L"  ctrl+w            close the window (exit)\r\n"
    L"\r\n"
    L"edit\r\n"
    L"  ctrl+z            undo (up to 128 steps)\r\n"
    L"  ctrl+y            redo (also ctrl+shift+z)\r\n"
    L"  ctrl+x / c / v    cut / copy / paste\r\n"
    L"  ctrl+a            select all\r\n"
    L"  ctrl+f            find\r\n"
    L"  f3 / shift+f3     find next / previous\r\n"
    L"  ctrl+h            replace\r\n"
    L"  ctrl+g            go to line\r\n"
    L"  f5                insert the time and date\r\n"
    L"  ctrl+backspace    delete the word before the caret\r\n"
    L"  ctrl+delete       delete the word after the caret\r\n"
    L"  ctrl+k            clear the current line (ctrl+z brings it back)\r\n"
    L"  ctrl+right shift  right to left reading order\r\n"
    L"  ctrl+left shift   left to right reading order\r\n"
    L"\r\n"
    L"format\r\n"
    L"  alt+z             word wrap on / off\r\n"
    L"\r\n"
    L"view\r\n"
    L"  alt+x             switch between the dark and light theme\r\n"
    L"  ctrl+u            show / hide the status bar\r\n"
    L"  ctrl+plus         bigger font (or ctrl + mouse wheel up)\r\n"
    L"  ctrl+minus        smaller font (or ctrl + mouse wheel down)\r\n"
    L"  ctrl+0            back to the size picked in the font dialog\r\n"
    L"\r\n"
    L"help\r\n"
    L"  f1                this window\r\n"
    L"\r\n"
    L"tips\r\n"
    L"\r\n"
    L"find and replace: whole word matches complete words, including letters, numbers and underscores. "
    L"match case, wrap around and the direction (up / down) apply to f3 as well; shift+f3 searches the other way. "
    L"the search text and the options are remembered for the next time.\r\n"
    L"\r\n"
    L"log files: an uppercase .log marker (a dot followed by capital l, o and g) at the start of the text "
    L"appends the current time and date each time the file is opened for editing. save to keep the new entry.\r\n"
    L"\r\n"
    L"printing: select text before opening print to enable the selection option. file > page setup sets the "
    L"margins and the header and footer: &f is the file name, &p the page number, &d the date, &t the time, "
    L"&& an ampersand, and &l, &c, &r align what follows left, centre or right.\r\n"
    L"\r\n"
    L"status bar: the line and column, the number of lines, the size in bytes, the line ending and the encoding. "
    L"with a selection the position also shows the lines and bytes it covers, like \"162:54 [5 L 54 B]\". "
    L"after ctrl+plus / minus the zoom shows next to the position (\"120%\"). click the position to go to a line, "
    L"the zoom to go back to 100%, the line ending or the encoding to change it.\r\n"
    L"\r\n"
    L"font: format > font picks the face and the size, 7 to 100 pt. the menus, title bar and status bar use "
    L"the same face at a fixed 11 px. tab size (2, 4 or 8 columns), line ending and encoding are in the format menu.\r\n"
    L"\r\n"
    L"theme: view > theme switches between dark (the default) and light. the sun / moon button at the right "
    L"end of the menu bar does the same with one click; the button left of it turns word wrap on / off.\r\n"
    L"\r\n"
    L"files: utf-8, utf-16 (with or without a bom) and the legacy code pages open; format > encoding > "
    L"reopen with encoding picks one by hand. give a file's name on the command line or use file > open. "
    L"file > recent keeps the last 9 files you opened or saved, and clear list (its last item) empties it.\r\n"
    L"\r\n"
    L"drag and drop: drop a file on the window to open it, like notepad (with several files the others open "
    L"in new windows); hold shift while dropping to insert their paths at the drop point instead. drag "
    L"selected text to move it, hold ctrl to copy it; text can also be dragged in from other programs and "
    L"out to them.\r\n"
    L"\r\n"
    L"default editor: help > set as default text editor makes notepad mint a choice for .txt .log .ini .cfg "
    L".conf .md .csv .nfo .diz and .text files and opens windows' default apps settings on it: pick it there "
    L"(windows lets only you choose a default program). the installer does the same for all users.\r\n"
    L"\r\n"
    L"menus: alt or f10 selects the menu bar, alt + a letter opens that menu, esc closes it. right click "
    L"in the text for the edit menu, which also has the right to left reading order and the unicode "
    L"control characters.\r\n"
    L"\r\n"
    L"right to left: a document whose first letter is hebrew, arabic or another right to left script "
    L"opens (or, typed into an empty document, switches) right to left: the text is right aligned and the "
    L"scroll bar moves to the left. the edit menu's right to left reading order, ctrl+right shift and "
    L"ctrl+left shift set it by hand until the next new or open.\r\n";

#define HELP_W 560
#define HELP_H 420

static LRESULT CALLBACK HelpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    InfoDlg *dlg = (InfoDlg *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        RECT textRect;
        dlg->edit = CreateWindowExW(0, L"EDIT", g_help, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, S(HELP_W), S(HELP_H), hwnd, (HMENU)(ULONG_PTR)1001, g_hinst, NULL);
        SendMessageW(dlg->edit, WM_SETFONT, (WPARAM)g_fontMenu, FALSE);
        GetClientRect(dlg->edit, &textRect);
        InflateRect(&textRect, -S(10), -S(8));
        SendMessageW(dlg->edit, EM_SETRECTNP, 0, (LPARAM)&textRect);
        DarkScroll(dlg->edit);
        base->focus = dlg->edit;
        return 0; }
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == dlg->edit) {
            SetTextColor((HDC)wParam, C_TEXT);
            SetBkColor((HDC)wParam, C_FIELD);
            return (LRESULT)g_brField;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) { base->result = 1; base->done = 1; return 0; }
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void HelpDlg(HWND owner) {
    static BOOL reg;
    InfoDlg dlg;
    if (!reg) { RegClass(L"mp_help", HelpProc, 0, NULL); reg = TRUE; }
    memset(&dlg, 0, sizeof dlg);
    DlgBaseInit(&dlg.base, owner);
    if (!DlgOpen(&dlg.base, L"mp_help", L"help topics", HELP_W, HELP_H, 0)) return;
    DlgRunModal(&dlg.base);
}
