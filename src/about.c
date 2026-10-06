/* about.c - "about notepad mint" box and the help topics (a keyboard / features cheat sheet) */
#include "mp.h"

#define SS_ICON     0x03
#define STM_SETICON 0x0170

typedef struct { DlgBase b; HICON icon; HWND edit; } InfoDlg;

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
                   L"plain text, dark, and nothing else:\n"
                   L"no ai, no sign-in, no telemetry, no cloud.", 20, 80, 340, 52, 0, SS_NOPREFIX);
        UiLabel(h, L"written in c and assembly with no runtime libraries. one small 32-bit exe runs on "
                   L"32-bit, 64-bit and arm windows.", 20, 138, 340, 34, IDC_DIM, SS_NOPREFIX);
        UiLabel(h, L"settings: %appdata%\\notepad mint\\settings.ini", 20, 176, 340, 16, IDC_DIM, SS_NOPREFIX);
        b->focus = UiButton(h, L"ok", 272, 204, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { b->result = 1; b->done = 1; return 0; }
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
    if (!reg) { RegClass(L"mp_about", AboutProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    if (!DlgOpen(&d.b, L"mp_about", L"about notepad mint", 380, 244, 0)) return;
    DlgRunModal(&d.b);
}

/* ------------------------------------------------------------------- help -- */
static const WCHAR g_help[] =
    L"notepad mint - keyboard shortcuts and tips\r\n"
    L"\r\n"
    L"file\r\n"
    L"  ctrl+n            new\r\n"
    L"  ctrl+shift+n      new window\r\n"
    L"  ctrl+o            open...\r\n"
    L"  ctrl+s            save\r\n"
    L"  ctrl+shift+s      save as...\r\n"
    L"  ctrl+p            print...\r\n"
    L"\r\n"
    L"edit\r\n"
    L"  ctrl+z            undo\r\n"
    L"  ctrl+x / c / v    cut / copy / paste\r\n"
    L"  ctrl+a            select all\r\n"
    L"  ctrl+f            find\r\n"
    L"  f3 / shift+f3     find next / find previous\r\n"
    L"  ctrl+h            replace\r\n"
    L"  ctrl+g            go to line\r\n"
    L"  f5                insert the time and date\r\n"
    L"  ctrl+backspace    delete the word before the caret\r\n"
    L"  ctrl+delete       delete the word after the caret\r\n"
    L"\r\n"
    L"font size\r\n"
    L"  ctrl+plus         bigger  (also ctrl + mouse wheel up)\r\n"
    L"  ctrl+minus        smaller (also ctrl + mouse wheel down)\r\n"
    L"  ctrl+0            back to the size you picked in font & colors\r\n"
    L"  sizes run from 10 to 96 pt. the menus follow the editor font:\r\n"
    L"  the same face, 2 pt smaller, never above 14 pt.\r\n"
    L"\r\n"
    L"format and status bar\r\n"
    L"  word wrap, font & colors, line ending and encoding are in the\r\n"
    L"  format menu. the status bar shows line and column, the font size,\r\n"
    L"  the line ending and the encoding - click the last three to change them.\r\n"
    L"\r\n"
    L"files\r\n"
    L"  any text file opens: utf-8, utf-16 (with or without a bom) and the\r\n"
    L"  legacy code pages (format > encoding > reopen with encoding...).\r\n"
    L"  drag a file onto the window, or give its name on the command line.\r\n"
    L"\r\n"
    L"menus\r\n"
    L"  alt or f10 selects the menu bar, alt + a letter opens that menu,\r\n"
    L"  the arrow keys and enter work as usual, esc closes.\r\n"
    L"  right click in the text for the edit menu (it also has the\r\n"
    L"  right to left reading order and unicode control characters).\r\n";

static LRESULT CALLBACK HelpProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    InfoDlg *d = (InfoDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE:
        DlgFrame(b, 16, 16, 528, 368, BV_SUNKEN);
        d->edit = CreateWindowExW(0, L"EDIT", g_help,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                  S(16) + 2, S(16) + 2, S(528) - 4, S(368) - 4, h, (HMENU)(ULONG_PTR)1001, g_hinst, NULL);
        SendMessageW(d->edit, WM_SETFONT, (WPARAM)g_fontMenu, FALSE);       /* monospace: the columns line up */
        SendMessageW(d->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(8), S(8)));
        DarkScroll(d->edit);
        UiButton(h, L"ok", 456, 396, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        b->focus = d->edit;
        return 0;
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
    if (!DlgOpen(&d.b, L"mp_help", L"help topics", 560, 436, 0)) return;
    DlgRunModal(&d.b);
}
