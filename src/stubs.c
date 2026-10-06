/* stubs.c - TEMPORARY placeholders for the dialogs that aren't written yet (delete each one as its real module lands).
 * every placeholder says so, instead of silently doing nothing. what is missing:
 *   find / replace / go to (find.c), open + save as + encoding pickers (filedlg.c), font & colors (fontdlg.c),
 *   print + page setup (print.c). see NOTES.md for the specs. */
#include "mp.h"

static void Todo(HWND owner, const WCHAR *msg)
{
    MpAsk(owner, APP_NAME, msg, L"ok", NULL, NULL, 1);
}

HWND FindDlgHwnd(void) { return NULL; }
BOOL FindHasText(void) { return FALSE; }
void FindNext(int dirUp) { (void)dirUp; }

void FindDlgShow(int replaceMode)
{
    Todo(g_hwnd, replaceMode ? L"replace isn't written yet." : L"find isn't written yet.");
}

void GotoDlg(HWND owner) { Todo(owner, L"go to line isn't written yet."); }
BOOL FontDlg(HWND owner) { Todo(owner, L"font & colors isn't written yet."); return FALSE; }

BOOL EncDlg(HWND owner, int *enc, int reopen)
{
    (void)enc;
    Todo(owner, reopen ? L"reopen with encoding isn't written yet." : L"other code page isn't written yet.");
    return FALSE;
}

BOOL FileDlgOpen(HWND owner, WCHAR *path, int cap)
{
    (void)path; (void)cap;
    Todo(owner, L"the open dialog isn't written yet.\n\nopen a file by dragging it onto the window or by giving its name on the command line.");
    return FALSE;
}

BOOL FileDlgSave(HWND owner, WCHAR *path, int cap, int *enc, int *eol)
{
    (void)path; (void)cap; (void)enc; (void)eol;
    Todo(owner, L"the save as dialog isn't written yet.\n\nfiles opened from the command line or by drag and drop can still be saved in place with ctrl+s.");
    return FALSE;
}

void PrintDoc(HWND owner) { Todo(owner, L"printing isn't written yet."); }
void PageSetup(HWND owner) { Todo(owner, L"page setup isn't written yet."); }
