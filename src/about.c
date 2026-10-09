/* about.c - "about notepad mint" box and the help topics (a keyboard / features cheat sheet) */
#include "mp.h"

#define SS_ICON     0x03
#define STM_SETICON 0x0170

typedef struct { DlgBase b; HICON icon; HWND edit; } InfoDlg;

/* ------------------------------------------------------------------- link -- */
/* a text link: accent coloured and underlined, accent background while hovered, opens its address in the default browser
 * (shell32 is loaded at run time like everywhere else). the address is the control's text ("yuru.be" -> https://yuru.be) */
#define ID_LINK 1060
typedef struct { int hot, focus, down; } LinkSt;

static void LinkOpen(HWND h)
{
    typedef HINSTANCE (WINAPI *ShellExecFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
    WCHAR url[160];
    HMODULE m = LoadLibraryW(L"shell32.dll");
    ShellExecFn f = m ? (ShellExecFn)GetProcAddress(m, "ShellExecuteW") : NULL;
    wcopy(url, L"https://", COUNTOF(url));
    GetWindowTextW(h, url + 8, COUNTOF(url) - 8);
    if (f) f(GetParent(h), L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK LinkProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LinkSt *s = (LinkSt *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        s = (LinkSt *)mem_zalloc(sizeof *s);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        return DefWindowProcW(h, m, w, l);                          /* this is what stores the caption text */
    }
    if (!s) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_NCDESTROY:
        mem_free(s);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
    case WM_SETFOCUS:  s->focus = 1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_KILLFOCUS: s->focus = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    case WM_MOUSEMOVE:
        if (!s->hot) {
            TRACKMOUSEEVENT te;
            s->hot = 1;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        s->hot = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        SetCapture(h);
        s->down = 1;
        return 0;
    case WM_LBUTTONUP:
        if (s->down) {
            RECT rc;
            POINT pt;
            s->down = 0;
            ReleaseCapture();
            pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
            GetClientRect(h, &rc);
            if (PtInRect(&rc, pt)) LinkOpen(h);
        }
        return 0;
    case WM_KEYUP:
        if (w == VK_SPACE) { LinkOpen(h); return 0; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc, tr, cr, ul;
        WCHAR t[96];
        int n, th;
        HGDIOBJ of = SelectObject(dc, g_fontUI);
        GetClientRect(h, &rc);
        FillC(dc, &rc, s->hot ? C_ACCENT : C_FACE);
        n = GetWindowTextW(h, t, COUNTOF(t));
        tr = rc; tr.left += S(2);
        cr = tr;
        DrawTextW(dc, t, n, &cr, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        th = cr.bottom - cr.top;
        TextC(dc, t, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, s->hot ? C_ON_ACCENT : C_ACCENT_FG);
        ul.left = tr.left; ul.right = cr.right; ul.top = (rc.bottom + th) / 2 - 1; ul.bottom = ul.top + 1;      /* the underline */
        FillC(dc, &ul, s->hot ? C_ON_ACCENT : C_ACCENT_FG);
        if (s->focus) {
            RECT f = rc;
            InflateRect(&f, -1, -1);
            DrawFocusRect(dc, &f);
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ about -- */
static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    InfoDlg *d = (InfoDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        HWND c;
        d->icon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(32), S(32), LR_DEFAULTCOLOR);
        c = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON, S(20), S(20), S(32), S(32),
                            h, NULL, g_hinst, NULL);
        if (d->icon) SendMessageW(c, STM_SETICON, (WPARAM)d->icon, 0);
        c = UiLabel(h, L"notepad mint", 68, 18, 290, 20, 0, SS_NOPREFIX);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUIB, FALSE);
        UiLabel(h, L"version " APP_VERSION L"  (32-bit)", 68, 40, 290, 16, IDC_DIM, SS_NOPREFIX);
        DlgFrame(b, 20, 68, 340, 2, BV_ETCHED);
        UiLabel(h, L"a free replacement for notepad.exe.\n"
                   L"plain text, dark or light, and nothing else:\n"
                   L"no ai, no sign-in, no telemetry, no cloud.", 20, 80, 340, 52, 0, SS_NOPREFIX);
        UiLabel(h, L"written in c and assembly with no runtime libraries. one small 32-bit exe runs on "
                   L"32-bit, 64-bit and arm windows.", 20, 138, 340, 34, IDC_DIM, SS_NOPREFIX);
        UiLabel(h, L"settings: %appdata%\\notepad-mint\\settings.ini", 20, 176, 340, 16, IDC_DIM, SS_NOPREFIX);
        c = CreateWindowExW(0, L"mp_link", L"yuru.be", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(20), S(206), S(76), S(20),
                            h, (HMENU)(ULONG_PTR)ID_LINK, g_hinst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
        b->focus = UiButton(h, L"ok", 272, 204, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { b->result = 1; b->done = 1; return 0; }
        if (LOWORD(w) == ID_LINK) { LinkOpen(GetDlgItem(h, ID_LINK)); return 0; }   /* enter on the focused link (the dialog manager sends it as a button click) */
        break;
    case WM_NCDESTROY:
        if (d->icon) { DestroyIcon(d->icon); d->icon = NULL; }
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

void AboutDlg(HWND owner)
{
    static BOOL reg;
    InfoDlg d;
    if (!reg) { RegClass(L"mp_about", AboutProc, 0, NULL); RegClass(L"mp_link", LinkProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    if (!DlgOpen(&d.b, L"mp_about", L"about notepad mint", 380, 244, 0)) return;
    DlgRunModal(&d.b);
}

/* ------------------------------------------------------------------- help -- */
/* keys are a table (monospace: the columns line up), everything else is one paragraph per line: the edit wraps it, so no line is broken by hand */
static const WCHAR g_help[] =
    L"notepad mint - keyboard shortcuts and tips\r\n"
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
    L"  ctrl+z            undo\r\n"
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
    L"match case and wrap around apply to find next and previous as well.\r\n"
    L"\r\n"
    L"log files: an uppercase .log marker (a dot followed by capital l, o and g) at the start of the text "
    L"appends the current time and date each time the file is opened for editing. save to keep the new entry.\r\n"
    L"\r\n"
    L"printing: select text before opening print to enable the selection option.\r\n"
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
    L"file > recent keeps the last 9 files you opened or saved, and clear list (its last item) empties it. "
    L"drop files or folders on the window to insert their paths at the caret, one per line; hold shift "
    L"while dropping to open the files instead.\r\n"
    L"\r\n"
    L"menus: alt or f10 selects the menu bar, alt + a letter opens that menu, esc closes it. right click "
    L"in the text for the edit menu, which also has the right to left reading order and the unicode "
    L"control characters.\r\n"
    L"\r\n"
    L"right to left: a document whose first letter is hebrew, arabic or another right to left script "
    L"opens (or, typed into an empty document, switches) right to left: the text is right aligned and the "
    L"scroll bar moves to the left. the edit menu's right to left reading order, ctrl+right shift and "
    L"ctrl+left shift set it by hand until the next new or open.\r\n";

/* the text fills the whole window: no frame, no padding around it and no button (esc or enter closes) */
#define HELP_W 560
#define HELP_H 420

static LRESULT CALLBACK HelpProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    InfoDlg *d = (InfoDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        RECT tr;
        d->edit = CreateWindowExW(0, L"EDIT", g_help,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                  0, 0, S(HELP_W), S(HELP_H), h, (HMENU)(ULONG_PTR)1001, g_hinst, NULL);
        SendMessageW(d->edit, WM_SETFONT, (WPARAM)g_fontMenu, FALSE);       /* monospace: the columns line up */
        GetClientRect(d->edit, &tr);
        InflateRect(&tr, -S(10), -S(8));                                    /* the text sits inside the window, not against its edges */
        SendMessageW(d->edit, EM_SETRECTNP, 0, (LPARAM)&tr);
        DarkScroll(d->edit);
        b->focus = d->edit;
        return 0; }
    case WM_CTLCOLORSTATIC:                                  /* a read-only edit asks for the "static" colors */
        if ((HWND)l == d->edit) {
            SetTextColor((HDC)w, C_TEXT);
            SetBkColor((HDC)w, C_FIELD);
            return (LRESULT)g_brField;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { b->result = 1; b->done = 1; return 0; }
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

void HelpDlg(HWND owner)
{
    static BOOL reg;
    InfoDlg d;
    if (!reg) { RegClass(L"mp_help", HelpProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    if (!DlgOpen(&d.b, L"mp_help", L"help topics", HELP_W, HELP_H, 0)) return;
    DlgRunModal(&d.b);
}
