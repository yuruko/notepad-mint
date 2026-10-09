/* main.c - process entry point, main-window layout, commands and document flow.
 * Layout: [title strip][menu bar][editor][status bar]. The editor has internal
 * margins and clipped scrollbar overhangs; the other child windows draw chrome.
 * Persistence, dirty-state tracking and status calculations live in dedicated
 * modules, leaving command sequencing and window ownership here. */
#include "app_internal.h"
#include "ui_probe.h"

HWND     g_hwnd, g_status;
DocState g_doc;
DWORD    g_textRev;

static HWND   g_bar;
static HACCEL g_accel;
static WCHAR  g_title[PATH_CAP + 64];

#define MIN_WIN_W 320                                  /* smallest size of the ENTIRE window (frame included), px at 96 dpi: the width yields to the status panels if they need more */
#define MIN_WIN_H 140

/* ===================================================== small helpers ===== */
static void Say(const WCHAR *msg)
{
    MpAsk(g_hwnd, APP_NAME, msg, L"ok", NULL, NULL, 1);
}

static void FileError(DWORD er, const WCHAR *path, BOOL saving)
{
    WCHAR sys[256], msg[PATH_CAP + 320];
    DWORD n;
    if (er == ERR_BADCP) {
        wcopy(sys, L"that code page isn't installed on this computer.", 256);
    } else {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, er, 0, sys, 256, NULL);
        if (!n) {
            wsprintfW(sys, L"error %lu", er);
        } else {
            while (n > 0 && (sys[n - 1] == '\r' || sys[n - 1] == '\n' || sys[n - 1] == ' ')) sys[--n] = 0;
            CharLowerW(sys);
        }
    }
    wcopy(msg, saving ? L"cannot save the " : L"cannot open the ", COUNTOF(msg));
    wcat(msg, path, COUNTOF(msg));
    wcat(msg, L" file.\n\n", COUNTOF(msg));
    wcat(msg, sys, COUNTOF(msg));
    Say(msg);
}

static void FocusEdit(void)
{
    if (g_edit && IsWindowEnabled(g_hwnd)) SetFocus(g_edit);
}

/* a fresh default name for a document that has no file yet ("mint-XXXX": from the local date and time, and always higher than the last one) */
static void NewDocName(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    DefaultDocName(PrefsTakeDocName(DocNameClock(&st)), g_doc.name, COUNTOF(g_doc.name));
}

const WCHAR *AppDocName(void)
{
    return g_doc.path[0] ? PathName(g_doc.path) : g_doc.name;
}

void AppUpdateTitle(void)
{
    WCHAR t[PATH_CAP + 64];
    t[0] = 0;
    if (AppIsDirty()) wcopy(t, L"*", COUNTOF(t));
    wcat(t, AppDocName(), COUNTOF(t));
    wcat(t, TITLE_TAIL, COUNTOF(t));
    if (wcmp(t, g_title) != 0) {
        wcopy(g_title, t, COUNTOF(g_title));
        SetWindowTextW(g_hwnd, t);                      /* (taskbar / alt-tab; the strip repaints from WM_SETTEXT) */
    }
}

/* ========================================================== layout ======= */
static void EditArea(RECT *r)                       /* the area the edit fills (no frame: the padding is inside the control) */
{
    RECT cr;
    int sh = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &cr);
    r->left = 0;
    r->top = FrameHeight() + MenuBarHeight();         /* [title strip][menu bar][edit][status bar] */
    r->right = cr.right;
    r->bottom = cr.bottom - sh;
    if (r->bottom < r->top + 4) r->bottom = r->top + 4;
    if (EditIsRtl()) r->left -= S(SBAR_TRIM);         /* the overhang that makes the scrollbars thinner (sbar.c, EditViewRect): the parent clips it away. */
    else r->right += S(SBAR_TRIM);                    /* it is on the side of the vertical bar: the left one in a right to left editor */
    r->bottom += S(SBAR_TRIM);
}

static void Layout(void)
{
    RECT cr, f;
    HDWP dp;
    int sh = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &cr);
    EditArea(&f);
    dp = BeginDeferWindowPos(3);                    /* the three children move in one go (one pass over the update regions, nothing repainted in between): a resize drags a lot of pixels around */
    if (dp && g_bar) dp = DeferWindowPos(dp, g_bar, NULL, 0, FrameHeight(), cr.right, MenuBarHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
    if (dp && g_edit) dp = DeferWindowPos(dp, g_edit, HWND_BOTTOM, f.left, f.top, f.right - f.left, f.bottom - f.top, SWP_NOACTIVATE);   /* (at the bottom: the overhang goes under the status bar) */
    if (dp && g_status && sh) dp = DeferWindowPos(dp, g_status, NULL, 0, cr.bottom - sh, cr.right, sh, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!dp || !EndDeferWindowPos(dp)) {            /* (no memory for the batch: the old way, one window at a time) */
        if (g_bar) MoveWindow(g_bar, 0, FrameHeight(), cr.right, MenuBarHeight(), TRUE);
        if (g_edit) { SetWindowPos(g_edit, HWND_BOTTOM, f.left, f.top, f.right - f.left, f.bottom - f.top, SWP_NOACTIVATE); InvalidateRect(g_edit, NULL, TRUE); }
        if (g_status && sh) MoveWindow(g_status, 0, cr.bottom - sh, cr.right, sh, TRUE);
    }
    if (g_status) ShowWindow(g_status, sh ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_hwnd, NULL, FALSE);            /* the frame lines move with the edit */
}

/* ===================================================== reading order ===== */
#define DIR_SCAN 4096                               /* how far the automatic reading order looks for the first letter with a direction */
static int g_dirManual;                             /* the user picked the reading order of this document: nothing automatic until the next new / open */

static void SetRtl(BOOL rtl)
{
    if (!EditSetRtl(rtl)) return;
    Layout();                                       /* (the overhang of the editor's window moves to the side of its scrollbar) */
    SbarSync(g_edit);
}

/* like dir="auto" in html: the reading order follows the first letter that has a direction (hebrew, arabic ... = right to left) until the user
 * picks one (context menu, ctrl+right shift / ctrl+left shift). a document without such a letter keeps the order it has */
static void AutoDir(void)
{
    int d;
    if (g_dirManual) return;
    d = EditStrongDir(DIR_SCAN);
    if (d) SetRtl(d == 2);
}

/* ===================================================== document state ==== */
static void MarkChanged(void)                       /* encoding / line ending changed: AppIsDirty compares them with the clean state */
{
    AppUpdateTitle();
    AppUpdateStatus();
}

static void NewWindow(const WCHAR *file)
{
    WCHAR exe[PATH_CAP], cmd[PATH_CAP * 2 + 8];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, exe, PATH_CAP);
    wcopy(cmd, L"\"", COUNTOF(cmd));
    wcat(cmd, exe, COUNTOF(cmd));
    wcat(cmd, L"\"", COUNTOF(cmd));
    if (file) {
        wcat(cmd, L" \"", COUNTOF(cmd));
        wcat(cmd, file, COUNTOF(cmd));
        wcat(cmd, L"\"", COUNTOF(cmd));
    }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void AppendLogStamp(void);

typedef struct { __int64 size; FILETIME time; int valid; } FileStamp;
static FileStamp g_diskStamp;

static FileStamp DiskStamp(const WCHAR *path)
{
    FileStamp s = { 0 };
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        s.valid = GetFileSizeEx(f, &s.size) && GetFileTime(f, NULL, NULL, &s.time);
        CloseHandle(f);
    }
    return s;
}

static BOOL ConfirmOverwrite(const WCHAR *path)
{
    FileStamp s;
    if (!g_diskStamp.valid || !g_doc.path[0] || wcmpi(path, g_doc.path) != 0) return TRUE;
    s = DiskStamp(path);
    if (s.valid && s.size == g_diskStamp.size &&
        s.time.dwLowDateTime == g_diskStamp.time.dwLowDateTime &&
        s.time.dwHighDateTime == g_diskStamp.time.dwHighDateTime) return TRUE;
    return MpAsk(g_hwnd, APP_NAME,
                 L"this file has changed outside notepad mint or is no longer available.\n\n"
                 L"overwrite it with the text in this window?",
                 L"overwrite", L"cancel", NULL, 2) == 1;
}

/* load `path` into the edit control. force = -1 detects the encoding */
static BOOL OpenDoc(const WCHAR *path, int force, BOOL logEntry)
{
    WCHAR *t;
    int len, enc, eol, logFile;
    FileStamp stamp = DiskStamp(path);
    DWORD er = DocRead(path, &t, &len, &enc, &eol, force);
    if (er == ERR_LOSSY) {                          /* not valid in its encoding: never opened by accident, but it can be opened */
        WCHAR msg[PATH_CAP + 320];
        wcopy(msg, path, COUNTOF(msg));
        wcat(msg, L"\n\nsome bytes of this file are not valid in its encoding (a broken utf-8 sequence, or an odd number of bytes for utf-16). "
                  L"open it anyway? they are shown as \xFFFD, and saving writes them that way. format > encoding > reopen with encoding can pick another encoding.", COUNTOF(msg));
        if (MpAsk(g_hwnd, APP_NAME, msg, L"open", L"cancel", NULL, 2) != 1) return FALSE;
        DocAllowLossy(TRUE);
        er = DocRead(path, &t, &len, &enc, &eol, force);
        DocAllowLossy(FALSE);
    }
    if (er) { FileError(er, path, FALSE); return FALSE; }
    if (!EditSetDocText(t)) { mem_free(t); FileError(ERR_NOMEM, path, FALSE); return FALSE; }
    g_dirManual = 0;
    SetRtl(EditStrongDir(DIR_SCAN) == 2);
    /* classic .LOG files get a new entry each time they are opened for editing. */
    logFile = len >= 4 && t[0] == '.' && t[1] == 'L' && t[2] == 'O' && t[3] == 'G';
    mem_free(t);
    wcopy(g_doc.path, path, PATH_CAP);
    g_doc.enc = enc;
    g_doc.eol = eol;
    g_diskStamp = stamp;
    AppMarkClean();
    RecentAdd(g_doc.path);
    if (logFile && logEntry) AppendLogStamp();
    AppUpdateTitle();
    AppUpdateStatus();
    return TRUE;
}

/* write the document. returns TRUE when it reached the disk */
static BOOL WriteDoc(const WCHAR *path, int enc, int eol)
{
    void *h = NULL;
    int len = 0;
    const WCHAR *t;
    BOOL lossy = FALSE;
    DWORD er;

    if (!ConfirmOverwrite(path)) return FALSE;
    t = EditLockText(&h, &len);                   /* never pump dialog messages while the edit buffer is locked */
    er = t ? DocWrite(path, t, len, enc, eol, &lossy) : ERR_NOMEM;
    EditUnlockText(h);
    if (er == ERR_LOSSY) {
        if (MpAsk(g_hwnd, APP_NAME,
                  L"this file contains characters in unicode format which will be lost if you save this file "
                  L"in this encoding. to keep the unicode information, click cancel below and then pick one of "
                  L"the unicode options in the encoding menu.\n\ncontinue?",
                  L"ok", L"cancel", NULL, 2) != 1) return FALSE;
        lossy = TRUE;
        t = EditLockText(&h, &len);
        er = t ? DocWrite(path, t, len, enc, eol, &lossy) : ERR_NOMEM;
        EditUnlockText(h);
    }
    if (er) { FileError(er, path, TRUE); return FALSE; }
    wcopy(g_doc.path, path, PATH_CAP);
    g_doc.enc = enc;
    g_doc.eol = eol;
    g_diskStamp = DiskStamp(path);
    AppMarkClean();                                     /* what is on disk is the new clean state */
    RecentAdd(g_doc.path);
    AppUpdateTitle();
    AppUpdateStatus();
    return TRUE;
}

static BOOL FileSave(BOOL saveAs)
{
    WCHAR path[PATH_CAP];
    wcopy(path, g_doc.path, PATH_CAP);
    if (saveAs || !path[0]) {
        if (!path[0]) wcopy(path, g_doc.name, PATH_CAP);     /* an unsaved document: the dialog proposes its default name (".txt" is added) */
        if (!FileDlgSave(g_hwnd, path, PATH_CAP)) return FALSE;
    }
    return WriteDoc(path, g_doc.enc, g_doc.eol);             /* the encoding / line ending are the document's: format menu or the status bar panels */
}

/* TRUE when the current document may be replaced / closed (saved, or the user said don't save) */
static BOOL Confirm(void)
{
    WCHAR msg[PATH_CAP + 64];
    if (!AppIsDirty()) return TRUE;
    wcopy(msg, L"do you want to save changes to ", COUNTOF(msg));
    wcat(msg, AppDocName(), COUNTOF(msg));
    wcat(msg, L"?", COUNTOF(msg));
    switch (MpAsk(g_hwnd, APP_NAME, msg, L"save", L"don't save", L"cancel", 3)) {
    case 1:  return FileSave(FALSE);
    case 2:  return TRUE;
    default: return FALSE;
    }
}

static void FileNew(void)
{
    if (!Confirm()) return;
    if (!EditSetDocText(L"")) { Say(L"not enough memory available to complete this operation."); return; }
    g_dirManual = 0;
    SetRtl(FALSE);
    g_doc.path[0] = 0;
    g_diskStamp.valid = 0;
    NewDocName();                                            /* every new document gets its own default name */
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;
    AppMarkClean();                                             /* blank is the clean state: typing and deleting again is not a change */
    AppUpdateTitle();
    AppUpdateStatus();
}

static void FileOpen(void)
{
    WCHAR path[PATH_CAP];
    if (!Confirm()) return;
    wcopy(path, g_doc.path, PATH_CAP);
    if (!FileDlgOpen(g_hwnd, path, PATH_CAP)) return;
    OpenDoc(path, -1, TRUE);
}

void AppOpenPath(const WCHAR *path)
{
    if (Confirm()) OpenDoc(path, -1, TRUE);
}

/* the file named on the command line. like notepad: "foo" falls back to "foo.txt", a missing file can be created. force = the encoding of /a or /w (-1 = detect) */
static void OpenCmdFile(const WCHAR *arg, int force)
{
    WCHAR full[PATH_CAP], msg[PATH_CAP + 128];
    const WCHAR *nm;
    BOOL dot = FALSE;
    int r;

    r = (int)GetFullPathNameW(arg, PATH_CAP, full, NULL);
    if (!r || r >= PATH_CAP) { FileError(r >= PATH_CAP ? 206 : GetLastError(), arg, FALSE); return; }
    if (IsDir(full)) return;
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) { OpenDoc(full, force, TRUE); return; }

    for (nm = PathName(arg); *nm; nm++) if (*nm == '.') dot = TRUE;
    if (!dot) {
        WCHAR t[PATH_CAP];
        wcopy(t, full, PATH_CAP);
        wcat(t, L".txt", PATH_CAP);
        if (GetFileAttributesW(t) != INVALID_FILE_ATTRIBUTES) { OpenDoc(t, force, TRUE); return; }
    }

    wcopy(msg, L"cannot find the ", COUNTOF(msg));
    wcat(msg, full, COUNTOF(msg));
    wcat(msg, L" file.\n\ndo you want to create a new file?", COUNTOF(msg));
    r = MpAsk(g_hwnd, APP_NAME, msg, L"yes", L"no", L"cancel", 3);
    if (r == 1) {
        HANDLE f = CreateFileW(full, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { FileError(GetLastError(), full, TRUE); return; }
        CloseHandle(f);
        wcopy(g_doc.path, full, PATH_CAP);
        g_diskStamp = DiskStamp(full);
        AppUpdateTitle();
        AppUpdateStatus();
    } else if (r == 3) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
}

/* ====================================================== edit commands ==== */
static void FormatTimeDate(WCHAR *b)
{
    SYSTEMTIME st;
    int n;
    b[0] = 0;
    GetLocalTime(&st);
    n = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, b, 64);
    if (n > 0) b[n - 1] = ' '; else { b[0] = ' '; n = 1; }
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, b + n, 64);
}

static void InsertTimeDate(void)
{
    WCHAR b[160] = { 0 };
    FormatTimeDate(b);
    EditInsert(b);
}

static void AppendLogStamp(void)
{
    WCHAR b[168] = L"\r\n";
    int end = GetWindowTextLengthW(g_edit);
    FormatTimeDate(b + 2);
    wcat(b, L"\r\n", COUNTOF(b));
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)end, (LPARAM)end);
    EditInsert(b);
}

static void ToggleWrap(void)
{
    g_pf.wrap = !g_pf.wrap;
    if (!EditCreate(g_hwnd)) {                      /* keep the original control and preference on failure */
        g_pf.wrap = !g_pf.wrap;
        Say(L"not enough memory available to complete this operation.");
        return;
    }
    Layout();
    if (g_bar) InvalidateRect(g_bar, NULL, FALSE);  /* the word wrap button of the menu bar shows the state */
    AppUpdateTitle();                               /* reloading the text raised the modified flag for a moment */
    AppUpdateStatus();
}

static void SetEol(int e)
{
    if (g_doc.eol == e) return;
    g_doc.eol = e;
    MarkChanged();
}

static void SetEnc(int e)
{
    if (g_doc.enc == e) return;
    g_doc.enc = e;
    MarkChanged();
}

static void Reopen(void)
{
    int e = g_doc.enc;
    if (!g_doc.path[0]) return;                              /* (grayed: nothing saved yet) */
    if (AppIsDirty() &&                                      /* asked before the encoding is picked, not after */
        MpAsk(g_hwnd, APP_NAME, L"reopening the file will discard your unsaved changes.\n\ndo you want to continue?",
              L"yes", L"no", NULL, 2) != 1) return;
    if (!EncDlg(g_hwnd, &e, 1)) return;
    OpenDoc(g_doc.path, e, FALSE);                           /* (a .LOG file got its entry when it was opened) */
}

/* our top-level windows on this thread (the main window, a modeless find dialog): frame colours + a full repaint */
static BOOL CALLBACK RethemeWnd(HWND h, LPARAM l)
{
    WCHAR cn[32];
    (void)l;
    if (!GetClassNameW(h, cn, 32)) return TRUE;
    if (wcmp(cn, APP_CLASS) != 0 && !(cn[0] == 'm' && cn[1] == 'p' && cn[2] == '_')) return TRUE;   /* ime windows etc. */
    if ((GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) DarkFrame(h, h == GetActiveWindow());
    RedrawWindow(h, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

/* view > theme. everything we draw reads the palette at paint time, so a repaint is enough after this */
static void ApplyTheme(int theme)
{
    if (theme == g_pf.theme) return;                  /* clicking the checked item again: nothing to repaint or save */
    ThemeUse(theme);
    SetClassLongW(g_hwnd, GCL_HBRBACKGROUND, (LONG)(LONG_PTR)g_brFace);   /* the class brush erases the main window */
    EditApplyColors();
    DarkScroll(g_edit);
    EnumThreadWindows(GetCurrentThreadId(), RethemeWnd, 0);           /* menu bar + status bar repaint as children */
    AppSavePrefs();
}

static const WCHAR g_ucc[17] = {
    0x200E, 0x200F, 0x200D, 0x200C, 0x202A, 0x202B, 0x202D, 0x202E, 0x202C,
    0x206E, 0x206F, 0x206B, 0x206A, 0x206D, 0x206C, 0x001E, 0x001F
};

/* help > set as default text editor (assoc.c): notepad mint becomes a choice for the plain text extensions, then windows' default apps settings
 * open on it. windows lets only the user pick a default, so the last click is theirs. an installed copy was registered for all users by its
 * installer: then only the page opens (one entry in settings, not two) */
static void SetDefaultEditor(void)
{
    typedef HINSTANCE (WINAPI *ShellExecFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
    typedef void (WINAPI *ChangeFn)(LONG, UINT, const void *, const void *);
    WCHAR exe[PATH_CAP];
    HMODULE sh;
    BOOL machine;
    DWORD n = GetModuleFileNameW(NULL, exe, PATH_CAP);
    if (!n || n >= PATH_CAP) return;
    machine = AssocMachineHas(exe);
    if (!machine && !AssocRegisterUser(exe)) {
        Say(L"windows didn't let notepad mint register itself for text files.");
        return;
    }
    sh = LoadLibraryW(L"shell32.dll");
    if (!sh) return;
    {
        ChangeFn ch = (ChangeFn)GetProcAddress(sh, "SHChangeNotify");
        ShellExecFn ex = (ShellExecFn)GetProcAddress(sh, "ShellExecuteW");
        if (ch && !machine) ch(0x08000000, 0, NULL, NULL);      /* SHCNE_ASSOCCHANGED: explorer reads the new choices */
        if (ex) ex(g_hwnd, L"open", machine ? L"ms-settings:defaultapps?registeredAppMachine=" ASSOC_REGAPP
                                            : L"ms-settings:defaultapps?registeredAppUser=" ASSOC_REGAPP, NULL, NULL, SW_SHOWNORMAL);
    }
}

static void Cmd(int id)
{
    EditDirKeyCancel();                                      /* an accelerator ate the key after ctrl+shift (ctrl+shift+n ...): no reading order on the key release */
    switch (id) {
    case IDM_FILE_NEW:      FileNew(); break;
    case IDM_FILE_NEWWIN:   NewWindow(NULL); break;
    case IDM_FILE_OPEN:     FileOpen(); break;
    case IDM_FILE_SAVE:     FileSave(FALSE); break;
    case IDM_FILE_SAVEAS:   FileSave(TRUE); break;
    case IDM_FILE_PAGESETUP: PageSetup(g_hwnd); break;
    case IDM_FILE_PRINT:    PrintDoc(g_hwnd, 0); break;
    case IDM_FILE_EXIT:     SendMessageW(g_hwnd, WM_CLOSE, 0, 0); return;

    case IDM_EDIT_UNDO:     SendMessageW(g_edit, EM_UNDO, 0, 0); break;
    case IDM_EDIT_CUT:      SendMessageW(g_edit, WM_CUT, 0, 0); break;
    case IDM_EDIT_COPY:     SendMessageW(g_edit, WM_COPY, 0, 0); break;
    case IDM_EDIT_PASTE:    SendMessageW(g_edit, WM_PASTE, 0, 0); break;
    case IDM_EDIT_DELETE:   SendMessageW(g_edit, WM_CLEAR, 0, 0); break;
    case IDM_EDIT_FIND:     FindDlgShow(0); return;
    case IDM_EDIT_REPLACE:  FindDlgShow(1); return;
    case IDM_EDIT_FINDNEXT: FindNext(0); return;          /* may open the find dialog: don't take its focus */
    case IDM_EDIT_FINDPREV: FindNext(1); return;
    case IDM_EDIT_GOTO:     GotoDlg(g_hwnd); break;
    case IDM_EDIT_SELALL: {
        HWND f = GetFocus();
        if (f && f != g_edit && f != g_hwnd) { SendMessageW(f, EM_SETSEL, 0, (LPARAM)-1); return; }   /* ctrl+a inside a text box of the find dialog */
        SendMessageW(g_edit, EM_SETSEL, 0, (LPARAM)-1);
        break; }
    case IDM_EDIT_TIMEDATE: {
        HWND f = GetFocus();
        if (f && f != g_edit && f != g_hwnd) return;         /* f5 inside a text box of the find dialog: not the editor's text */
        InsertTimeDate();
        break; }
    case IDM_EDIT_CLEARLINE: {
        HWND f = GetFocus();
        if (f && f != g_edit && f != g_hwnd) return;         /* ctrl+k inside a text box of the find dialog: not the editor's line to clear */
        EditClearLine();
        break; }

    case IDM_FMT_WRAP:      ToggleWrap(); break;
    case IDM_FMT_FONT:      if (FontDlg(g_hwnd)) AppApplyPrefs(); break;
    case IDM_TAB_2: case IDM_TAB_4: case IDM_TAB_8:
        g_pf.tab = id == IDM_TAB_2 ? 2 : id == IDM_TAB_4 ? 4 : 8;
        EditApplyTabs();
        AppSavePrefs();
        break;
    case IDM_EOL_CRLF:      SetEol(EOL_CRLF); break;
    case IDM_EOL_LF:        SetEol(EOL_LF); break;
    case IDM_EOL_CR:        SetEol(EOL_CR); break;
    case IDM_ENC_UTF8:      SetEnc(ENC_UTF8); break;
    case IDM_ENC_UTF8BOM:   SetEnc(ENC_UTF8BOM); break;
    case IDM_ENC_UTF16LE:   SetEnc(ENC_UTF16LE); break;
    case IDM_ENC_UTF16BE:   SetEnc(ENC_UTF16BE); break;
    case IDM_ENC_ANSI:      SetEnc(ENC_ANSI); break;
    case IDM_ENC_OTHER: {
        int e = g_doc.enc;
        if (EncDlg(g_hwnd, &e, 0)) SetEnc(e);
        break; }
    case IDM_ENC_REOPEN:    Reopen(); break;
    case IDM_RTL:           g_dirManual = 1; SetRtl(!EditIsRtl()); break;
    case IDM_DIR_LTR:       g_dirManual = 1; SetRtl(FALSE); break;
    case IDM_DIR_RTL:       g_dirManual = 1; SetRtl(TRUE); break;

    case IDM_VIEW_STATUS:
        g_pf.statusbar = !g_pf.statusbar;
        Layout();
        AppUpdateStatus();
        break;
    case IDM_ZOOM_IN:       EditZoomStep(1); break;
    case IDM_ZOOM_OUT:      EditZoomStep(-1); break;
    case IDM_ZOOM_RESET:    EditZoomReset(); break;
    case IDM_THEME_DARK:    ApplyTheme(THEME_DARK); break;
    case IDM_THEME_LIGHT:   ApplyTheme(THEME_LIGHT); break;
    case IDM_THEME_TOGGLE:  ApplyTheme(g_pf.theme == THEME_DARK ? THEME_LIGHT : THEME_DARK); break;

    case IDM_HELP_TOPICS:   HelpDlg(g_hwnd); break;
    case IDM_HELP_ABOUT:    AboutDlg(g_hwnd); break;
    case IDM_HELP_DEFAULT:  SetDefaultEditor(); break;

    case IDM_SYS_RESTORE: case IDM_SYS_MOVE: case IDM_SYS_SIZE:
    case IDM_SYS_MIN: case IDM_SYS_MAX: case IDM_SYS_CLOSE:
        FrameSysCommand(g_hwnd, id);
        return;

    case IDM_RECENT_CLEAR:  RecentClear(); break;           /* file > recent > clear list (the other windows read the empty list when they are activated) */

    default:
        if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + RECENT_MAX) {          /* file > recent */
            WCHAR p[PATH_CAP];
            if (RecentPath(id - IDM_RECENT_BASE, p, COUNTOF(p))) {
                if (Confirm() && !OpenDoc(p, -1, TRUE) && GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) RecentRemove(p);   /* (only a file that is gone leaves the list) */
            }
        } else if (id >= IDM_UCC_BASE && id < IDM_UCC_BASE + 17) {
            WCHAR s[2];
            s[0] = g_ucc[id - IDM_UCC_BASE]; s[1] = 0;
            EditInsert(s);
        }
        break;
    }
    if (!FindDlgHwnd() || GetActiveWindow() != FindDlgHwnd()) FocusEdit();   /* (alt+z, ctrl+plus ... pressed in the find dialog keep it active) */
}

/* the chrome (menu bar, popups, status bar, title strip) uses the editor font face at a static size (CHROME_PX) */
static void ApplyChrome(void)
{
    UiSetChromeFont(g_pf.font);
    if (g_bar) MenuBarRefont(g_bar);
    if (g_status) StatusRefont(g_status);
    FrameRefont(g_hwnd);
}

void AppApplyPrefs(void)
{
    FontResolve(g_pf.font);
    if (g_pf.pt < FONT_MIN) g_pf.pt = FONT_MIN;
    if (g_pf.pt > FONT_MAX) g_pf.pt = FONT_MAX;
    g_pf.cur = g_pf.pt;                             /* a new pick also resets any ctrl+plus / minus offset */
    ApplyChrome();
    EditApplyFont();
    EditApplyColors();
    Layout();
    AppUpdateStatus();
    AppSavePrefs();
}

/* menu item states: check marks, radio dots, grayed items */
static unsigned MenuState(int id)
{
    unsigned on = MS_CHECK | MS_RADIO;
    switch (id) {
    case IDM_EDIT_UNDO:     return SendMessageW(g_edit, EM_CANUNDO, 0, 0) ? 0 : MS_GRAY;
    case IDM_EDIT_CUT:
    case IDM_EDIT_COPY:
    case IDM_EDIT_DELETE:   return EditHasSel() ? 0 : MS_GRAY;
    case IDM_EDIT_PASTE:    return IsClipboardFormatAvailable(CF_UNICODETEXT) ? 0 : MS_GRAY;
    case IDM_EDIT_FINDNEXT:
    case IDM_EDIT_FINDPREV: return FindHasText() ? 0 : MS_GRAY;
    case IDM_FMT_WRAP:      return g_pf.wrap ? MS_CHECK : 0;
    case IDM_VIEW_STATUS:   return g_pf.statusbar ? MS_CHECK : 0;
    case IDM_RTL:           return EditIsRtl() ? MS_CHECK : 0;
    case IDM_RECENT_NONE:   return MS_GRAY;
    case IDM_TAB_2:         return g_pf.tab == 2 ? on : 0;
    case IDM_TAB_4:         return g_pf.tab == 4 ? on : 0;
    case IDM_TAB_8:         return g_pf.tab == 8 ? on : 0;
    case IDM_EOL_CRLF:      return g_doc.eol == EOL_CRLF ? on : 0;
    case IDM_EOL_LF:        return g_doc.eol == EOL_LF ? on : 0;
    case IDM_EOL_CR:        return g_doc.eol == EOL_CR ? on : 0;
    case IDM_ENC_UTF8:      return g_doc.enc == ENC_UTF8 ? on : 0;
    case IDM_ENC_UTF8BOM:   return g_doc.enc == ENC_UTF8BOM ? on : 0;
    case IDM_ENC_UTF16LE:   return g_doc.enc == ENC_UTF16LE ? on : 0;
    case IDM_ENC_UTF16BE:   return g_doc.enc == ENC_UTF16BE ? on : 0;
    case IDM_ENC_ANSI:      return g_doc.enc == ENC_ANSI ? on : 0;
    case IDM_ENC_OTHER:     return g_doc.enc >= ENC_CP_MIN ? on : 0;
    case IDM_ZOOM_IN:       return EditZoomCan(1) ? 0 : MS_GRAY;
    case IDM_ZOOM_OUT:      return EditZoomCan(-1) ? 0 : MS_GRAY;
    case IDM_ZOOM_RESET:    return g_pf.cur != g_pf.pt ? 0 : MS_GRAY;
    case IDM_ENC_REOPEN:    return g_doc.path[0] ? 0 : MS_GRAY;
    case IDM_THEME_DARK:    return g_pf.theme == THEME_DARK ? on : 0;
    case IDM_THEME_LIGHT:   return g_pf.theme == THEME_LIGHT ? on : 0;
    case IDM_SYS_RESTORE: case IDM_SYS_MOVE: case IDM_SYS_SIZE:
    case IDM_SYS_MIN: case IDM_SYS_MAX: case IDM_SYS_CLOSE:
        return FrameSysState(id);
    }
    return 0;
}

/* ======================================================= main window ===== */
typedef UINT (WINAPI *DragQueryFn)(HANDLE, UINT, LPWSTR, UINT);
typedef void (WINAPI *DragFinishFn)(HANDLE);

/* files and / or folders dropped on the window: the full path of each goes into the text at the caret (replacing the selection, one undo step,
 * like a paste), one path per line, plain (no quotes) and no line break after the last one. shift held while
 * dropping opens the files instead (not the folders): the first here and the rest in windows of their own */
static void OnDropFiles(HANDLE drop)
{
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    DragQueryFn q = sh ? (DragQueryFn)GetProcAddress(sh, "DragQueryFileW") : NULL;
    DragFinishFn fin = sh ? (DragFinishFn)GetProcAddress(sh, "DragFinish") : NULL;
    UINT n, i;
    if (!q || !fin) return;
    n = q(drop, 0xFFFFFFFF, NULL, 0);
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        int first = 1;
        for (i = 0; i < n; i++) {
            WCHAR p[PATH_CAP];
            if (!q(drop, i, p, PATH_CAP) || IsDir(p)) continue;
            if (first) { first = 0; AppOpenPath(p); }
            else NewWindow(p);
        }
    } else if (n) {
        size_t cap = 1;                                    /* the paths can be long (up to 32k): measure them, never truncate */
        WCHAR *text, *d;
        for (i = 0; i < n; i++) cap += (size_t)q(drop, i, NULL, 0) + 2;
        text = (WCHAR *)mem_alloc(cap * sizeof(WCHAR));
        if (text) {
            d = text;
            for (i = 0; i < n; i++) {
                if (i) { *d++ = '\r'; *d++ = '\n'; }
                d += q(drop, i, d, q(drop, i, NULL, 0) + 1);
            }
            *d = 0;
            EditInsert(text);
            mem_free(text);
        }
    }
    fin(drop);
}

static LRESULT OnCreate(HWND h)
{
    g_hwnd = h;
    UiSetDpi(UiDpiForWindow(h));
    DarkFrame(h, 1);
    FontResolve(g_pf.font);
    UiSetChromeFont(g_pf.font);                     /* before the bars exist: their heights come from this font */
    g_bar = MenuBarCreate(h, MenuState);
    g_status = StatusCreate(h);
    if (!EditCreate(h)) return -1;
    AppMarkClean();                                    /* the new blank document (enc / eol were set in mp_main) is the clean state */
    Layout();
    AppUpdateTitle();
    AppUpdateStatus();
    return 0;
}

static void OnDpiChanged(HWND h, WPARAM w, LPARAM l)
{
    const RECT *r = (const RECT *)l;
    UiSetDpi((int)HIWORD(w));                       /* rebuilds every ui font at the new dpi */
    MenuBarRefont(g_bar);
    StatusRefont(g_status);
    FrameRefont(h);
    EditApplyFont();
    SbarTrim(g_edit, S(SBAR_TRIM), S(SBAR_TRIM));    /* (the overhang is in pixels of the new dpi too: EditArea, EditViewRect) */
    SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
#ifdef SHOTDC
    case WM_APP + 60: {
        RECT probeRect;
        int dpi = (int)w;
        if (dpi < 96 || dpi > 768 || !GetWindowRect(h, &probeRect)) return 0;
        OnDpiChanged(h, MAKEWPARAM(dpi, dpi), (LPARAM)&probeRect);
        return g_dpi; }
    case WM_APP + 61:
        return UiProbeFontAndDialogs(w, l);
#endif
    case WM_CREATE:
        return OnCreate(h);
    case WM_SIZE:
        MenuCancel();
        FrameInvalidate(h);                              /* the title strip's fill and buttons depend on the width: repaint all of it */
        if (w != SIZE_MINIMIZED && g_edit) Layout();
        return 0;
    case WM_MOVE:
        MenuCancel();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT r;
        MINMAXINFO *mm = (MINMAXINFO *)l;
        r.left = 0; r.top = 0; r.right = StatusMinWidth(); r.bottom = 0;        /* client width the status panels need */
        if (r.right < MenuBarMinWidth()) r.right = MenuBarMinWidth();           /* ... or the menu bar: its menus and the two buttons at the right end */
        UiAdjustRect(&r, WS_OVERLAPPEDWINDOW, 0);
        mm->ptMinTrackSize.x = r.right - r.left;                                  /* ... as a window width (nothing is ever cut off) */
        if (mm->ptMinTrackSize.x < S(MIN_WIN_W)) mm->ptMinTrackSize.x = S(MIN_WIN_W);
        mm->ptMinTrackSize.y = S(MIN_WIN_H);                                      /* the window's own height: title strip, menu bar, editor and status bar included */
        return 0; }
    case WM_SETFOCUS:
        FocusEdit();
        return 0;
    case WM_ACTIVATE:
        DBG(L"activate", LOWORD(w), l);
        DarkFrame(h, LOWORD(w) != WA_INACTIVE);
        FrameActive(h, LOWORD(w) != WA_INACTIVE);
        if (LOWORD(w) != WA_INACTIVE) RecentLoad();           /* (another window may have changed the list) */
        break;
    case WM_NCACTIVATE:
        if (FrameEnabled()) return DefWindowProcW(h, m, w, (LPARAM)-1);   /* custom strip: no default caption repaint */
        break;
    case WM_NCCALCSIZE:
        return FrameNcCalc(h, w, l);
    case WM_NCHITTEST:
        return FrameHitTest(h, l);
    case WM_NCRBUTTONUP:
        if (FrameEnabled() && w == HTCAPTION) { FrameSysMenu(h, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0; }
        break;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(h, m, w, l);
        FrameInvalidate(h);
        return r; }
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_NCRBUTTONDOWN:
    case WM_CAPTURECHANGED:
        if (FrameMsg(h, m, w, l)) return 0;
        break;
    case WM_ACTIVATEAPP:
        if (!w) MenuCancel();
        break;
    case WM_CANCELMODE:
        MenuCancel();
        break;
    case WM_ERASEBKGND: {                                  /* the children cover the whole client area but the title strip, and FramePaint puts the strip there in one buffered blit: no flat face colour under it first (the class brush would paint that) */
        HDC dc = (HDC)w;
        RECT rc;
        int saved = SaveDC(dc);
        GetClientRect(h, &rc);
        ExcludeClipRect(dc, 0, 0, rc.right, FrameHeight());
        FillRect(dc, &rc, g_brFace);
        RestoreDC(dc, saved);
        return 1; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        FramePaint(h, dc);                                 /* the title strip */
        EndPaint(h, &ps);
        return 0; }
    case WM_CTLCOLOREDIT:
        if ((HWND)l == g_edit) {
            SetTextColor((HDC)w, g_pf.fg);
            SetBkColor((HDC)w, g_pf.bg);
            return (LRESULT)EditBrush();
        }
        break;
    case WM_TIMER:
        AppStatusTimer(h, w);
        return 0;
    case WM_COMMAND:
        if ((HWND)l == g_edit) {                               /* notifications from the edit control */
            switch (HIWORD(w)) {
            case EN_CHANGE:
                g_textRev++;
                AutoDir();                                     /* (typing or pasting the first letter of a document) */
                AppUpdateTitle();
                AppUpdateStatus();
                EditScrollSoon();                              /* scrollbars only when the text needs them */
                break;
            case EN_ERRSPACE:
            case EN_MAXTEXT:
                Say(L"not enough memory available to complete this operation.");
                break;
            }
            return 0;
        }
        Cmd(LOWORD(w));
        return 0;
    case WM_SYSCOMMAND:
        DBG(L"syscmd", w, l);
        if (FrameEnabled() && (w & 0xFFF0) == SC_KEYMENU && l == ' ') {   /* alt+space: the window menu, ours */
            FrameSysMenu(h, -1, -1);
            return 0;
        }
        if ((w & 0xFFF0) == SC_KEYMENU && l != ' ') {          /* alt / f10 / alt+letter: our own menu bar */
            if (l == 0) {
                MenuBarActivate(g_bar, 0, 0);
            } else {
                int i = MenuBarMnemonic((WCHAR)l);
                if (i >= 0) MenuBarActivate(g_bar, i, 1);
            }
            return 0;
        }
        break;
    case WM_DPICHANGED:
        OnDpiChanged(h, w, l);
        return 0;
    case WM_DROPFILES:
        OnDropFiles((HANDLE)w);
        return 0;
    case WM_QUERYENDSESSION:
        return Confirm() ? TRUE : FALSE;
    case WM_ENDSESSION:                                        /* logoff / shutdown: no WM_CLOSE comes, keep the settings */
        if (w) AppSavePrefs();
        return 0;
    case WM_CLOSE:
        if (!Confirm()) return 0;
        AppSavePrefs();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ======================================================= entry point ===== */
static ACCEL g_acc[] = {
    { FVIRTKEY | FCONTROL,           'N',            IDM_FILE_NEW },
    { FVIRTKEY | FCONTROL | FSHIFT,  'N',            IDM_FILE_NEWWIN },
    { FVIRTKEY | FCONTROL,           'O',            IDM_FILE_OPEN },
    { FVIRTKEY | FCONTROL,           'S',            IDM_FILE_SAVE },
    { FVIRTKEY | FCONTROL | FSHIFT,  'S',            IDM_FILE_SAVEAS },
    { FVIRTKEY | FCONTROL,           'P',            IDM_FILE_PRINT },
    { FVIRTKEY | FCONTROL,           'W',            IDM_FILE_EXIT },          /* close the window = file > exit (asks about unsaved changes first) */
    { FVIRTKEY | FALT,               'X',            IDM_THEME_TOGGLE },       /* dark <-> light (like alt+z below it has no menu bar mnemonic: no bar title has an x) */
    { FVIRTKEY | FCONTROL,           'U',            IDM_VIEW_STATUS },        /* show / hide the status bar */
    { FVIRTKEY | FALT,               'Z',            IDM_FMT_WRAP },           /* word wrap on / off (alt+z has no menu bar mnemonic; AltGr reports ctrl+alt, so it never matches here) */
    { FVIRTKEY | FCONTROL,           'A',            IDM_EDIT_SELALL },        /* the stock multiline edit has no ctrl+a of its own */
    { FVIRTKEY | FCONTROL,           'F',            IDM_EDIT_FIND },
    { FVIRTKEY | FCONTROL,           'H',            IDM_EDIT_REPLACE },
    { FVIRTKEY | FCONTROL,           'G',            IDM_EDIT_GOTO },
    { FVIRTKEY | FCONTROL,           'K',            IDM_EDIT_CLEARLINE },     /* empty the caret's line */
    { FVIRTKEY,                      VK_F3,          IDM_EDIT_FINDNEXT },
    { FVIRTKEY | FSHIFT,             VK_F3,          IDM_EDIT_FINDPREV },
    { FVIRTKEY,                      VK_F5,          IDM_EDIT_TIMEDATE },
    { FVIRTKEY,                      VK_F1,          IDM_HELP_TOPICS },
    { FVIRTKEY | FCONTROL,           VK_OEM_PLUS,    IDM_ZOOM_IN },
    { FVIRTKEY | FCONTROL | FSHIFT,  VK_OEM_PLUS,    IDM_ZOOM_IN },
    { FVIRTKEY | FCONTROL,           VK_ADD,         IDM_ZOOM_IN },
    { FVIRTKEY | FCONTROL,           VK_OEM_MINUS,   IDM_ZOOM_OUT },
    { FVIRTKEY | FCONTROL,           VK_SUBTRACT,    IDM_ZOOM_OUT },
    { FVIRTKEY | FCONTROL,           '0',            IDM_ZOOM_RESET },
    { FVIRTKEY | FCONTROL,           VK_NUMPAD0,     IDM_ZOOM_RESET },
};

/* the command line after the exe name; notepad treats all of it as one file name */
static void CmdLineFile(WCHAR *out, int cap)
{
    const WCHAR *p = GetCommandLineW();
    int n = 0;
    out[0] = 0;
    if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; }
    else while (*p && *p != ' ' && *p != '\t') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && n < cap - 1) out[n++] = *p++;
    } else {
        while (*p && n < cap - 1) out[n++] = *p++;
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    }
    out[n] = 0;
}

/* notepad's switches: "/p file" prints it, "/a file" opens it as ansi, "/w file" as utf-16. returns the letter (0 = none) and leaves only the
 * file in arg, without its quotes */
static int CmdSwitch(WCHAR *arg)
{
    WCHAR sw, *f;
    int n;
    if (arg[0] != '/' || !arg[1] || (arg[2] != ' ' && arg[2] != '\t')) return 0;
    sw = wlow(arg[1]);
    if (sw != 'p' && sw != 'a' && sw != 'w') return 0;
    f = arg + 3;
    while (*f == ' ' || *f == '\t') f++;
    if (*f == '"') { f++; for (n = 0; f[n] && f[n] != '"'; n++) ; f[n] = 0; }
    memmove(arg, f, ((size_t)wlen(f) + 1) * sizeof(WCHAR));
    return sw;
}

/* a second window opened from the first one shouldn't sit exactly on top of it */
static void Cascade(int *x, int *y)
{
    int moved = 1, tries = 0, tol = S(12);
    while (moved && tries++ < 12) {
        HWND o = NULL;
        moved = 0;
        while ((o = FindWindowExW(NULL, o, APP_CLASS, NULL)) != NULL) {
            RECT r;
            if (o == g_hwnd || IsZoomed(o) || !GetWindowRect(o, &r)) continue;
            if (r.left - *x < tol && *x - r.left < tol && r.top - *y < tol && *y - r.top < tol) {
                *x += S(32); *y += S(32);
                moved = 1;
                break;
            }
        }
    }
}

static void ShowMain(void)
{
    int have = (g_pf.winw >= 200 && g_pf.winh >= 120);
    POINT pt;
    if (have) {
        pt.x = g_pf.winx + 40; pt.y = g_pf.winy + 12;
        if (!MonitorFromPoint(pt, MONITOR_DEFAULTTONULL)) have = 0;          /* saved on a monitor that's gone */
    }
    if (have) {
        WINDOWPLACEMENT wp;
        int x = g_pf.winx, y = g_pf.winy;
        Cascade(&x, &y);
        memset(&wp, 0, sizeof wp);
        wp.length = sizeof wp;
        wp.showCmd = g_pf.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        wp.rcNormalPosition.left = x;
        wp.rcNormalPosition.top = y;
        wp.rcNormalPosition.right = x + g_pf.winw;
        wp.rcNormalPosition.bottom = y + g_pf.winh;
        SetWindowPlacement(g_hwnd, &wp);
    } else {
        ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    }
}

int mp_main(void)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    MSG msg;
    WCHAR arg[PATH_CAP];
    int sw;

    UiInit(hi);
    PrefsInit();
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;
    NewDocName();                                            /* before the window exists: its first title already uses it */

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_brFace;
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    g_accel = CreateAcceleratorTableW(g_acc, COUNTOF(g_acc));
    if (!CreateWindowExW(WS_EX_ACCEPTFILES, APP_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         CW_USEDEFAULT, 0, S(900), S(620), NULL, NULL, hi, NULL)) return 1;
    if (FrameEnabled())                                        /* title strip: one more WM_NCCALCSIZE, the wParam TRUE kind */
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    CmdLineFile(arg, PATH_CAP);
    sw = CmdSwitch(arg);
    if (sw == 'p') {                                         /* notepad /p file: print it on the default printer and quit (the window never shows) */
        WCHAR full[PATH_CAP];
        DWORD r = arg[0] ? GetFullPathNameW(arg, PATH_CAP, full, NULL) : 0;
        if (r && r < PATH_CAP && GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES && OpenDoc(full, -1, FALSE)) PrintDoc(g_hwnd, 1);
        return 0;
    }
    ShowMain();
    if (arg[0]) OpenCmdFile(arg, sw == 'a' ? ENC_ANSI : sw == 'w' ? ENC_UTF16LE : -1);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND fd = FindDlgHwnd();
        if (fd && IsDialogMessageW(fd, &msg)) continue;        /* the modeless find / replace dialog */
        if (g_hwnd && msg.hwnd &&
            (msg.hwnd == g_hwnd || IsChild(g_hwnd, msg.hwnd) || (fd && (msg.hwnd == fd || IsChild(fd, msg.hwnd)))) &&   /* f3, ctrl+g ... work from the find dialog too, like notepad */
            TranslateAcceleratorW(g_hwnd, g_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

/* process entry point (/ENTRY:start): there is no crt to run first */
void start(void)
{
    ExitProcess((UINT)mp_main());
}
