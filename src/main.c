#include "app_internal.h"
#include "ui_probe.h"

HWND     g_hwnd, g_status;
DocState g_doc;
DWORD    g_textRev;

static HWND   g_bar;
static HACCEL g_accel;
static WCHAR  g_title[PATH_CAP + 64];

#define MIN_WIN_W 320
#define MIN_WIN_H 140
#define DROP_ASK  10
#define DND_TIMER 9

static void Say(const WCHAR *message) {
    MpNote(g_hwnd, APP_NAME, message);
}

static void FileError(DWORD err, const WCHAR *path, BOOL saving) {
    WCHAR reason[256], message[PATH_CAP + 320];
    DWORD length;
    if (err == ERR_BADCP) {
        wcopy(reason, L"that code page isn't installed on this computer.", 256);
    } else {
        length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, reason, 256, NULL);
        if (!length) {
            wsprintfW(reason, L"error %lu", err);
        } else {
            while (length > 0 && (reason[length - 1] == '\r' || reason[length - 1] == '\n' || reason[length - 1] == ' ')) reason[--length] = 0;
            CharLowerW(reason);
        }
    }
    wcopy(message, saving ? L"cannot save the " : L"cannot open the ", COUNTOF(message));
    wcat(message, path, COUNTOF(message));
    wcat(message, L" file.\n\n", COUNTOF(message));
    wcat(message, reason, COUNTOF(message));
    Say(message);
}

static void FocusEdit(void) {
    if (g_edit && IsWindowEnabled(g_hwnd)) SetFocus(g_edit);
}

static HWND OtherFocus(void) {
    HWND focus = GetFocus();
    return focus && focus != g_edit && focus != g_hwnd ? focus : NULL;
}

static void NewDocName(void) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    DefaultDocName(PrefsTakeDocName(DocNameClock(&now)), g_doc.name, COUNTOF(g_doc.name));
}

const WCHAR *AppDocName(void) {
    return g_doc.path[0] ? PathName(g_doc.path) : g_doc.name;
}

void AppUpdateTitle(void) {
    WCHAR title[PATH_CAP + 64];
    title[0] = 0;
    if (AppIsDirty()) wcopy(title, L"*", COUNTOF(title));
    wcat(title, AppDocName(), COUNTOF(title));
    wcat(title, TITLE_TAIL, COUNTOF(title));
    if (wcmp(title, g_title) != 0) {
        wcopy(g_title, title, COUNTOF(g_title));
        SetWindowTextW(g_hwnd, title);
    }
}

static void EditArea(RECT *area) {
    RECT client;
    int statusHeight = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &client);
    area->left = 0;
    area->top = FrameHeight() + MenuBarHeight();
    area->right = client.right;
    area->bottom = client.bottom - statusHeight;
    if (area->bottom < area->top + 4) area->bottom = area->top + 4;
    if (EditIsRtl()) area->left -= S(SBAR_TRIM);
    else area->right += S(SBAR_TRIM);
    area->bottom += S(SBAR_TRIM);
}

static void Layout(void) {
    RECT client, editRect;
    HDWP defer;
    int statusHeight = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &client);
    EditArea(&editRect);
    defer = BeginDeferWindowPos(3);
    if (defer && g_bar) defer = DeferWindowPos(defer, g_bar, NULL, 0, FrameHeight(), client.right, MenuBarHeight(), SWP_NOZORDER | SWP_NOACTIVATE);
    if (defer && g_edit) defer = DeferWindowPos(defer, g_edit, HWND_BOTTOM, editRect.left, editRect.top, editRect.right - editRect.left, editRect.bottom - editRect.top, SWP_NOACTIVATE);
    if (defer && g_status && statusHeight) defer = DeferWindowPos(defer, g_status, NULL, 0, client.bottom - statusHeight, client.right, statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!defer || !EndDeferWindowPos(defer)) {
        if (g_bar) MoveWindow(g_bar, 0, FrameHeight(), client.right, MenuBarHeight(), TRUE);
        if (g_edit) { SetWindowPos(g_edit, HWND_BOTTOM, editRect.left, editRect.top, editRect.right - editRect.left, editRect.bottom - editRect.top, SWP_NOACTIVATE); InvalidateRect(g_edit, NULL, TRUE); }
        if (g_status && statusHeight) MoveWindow(g_status, 0, client.bottom - statusHeight, client.right, statusHeight, TRUE);
    }
    if (g_status) ShowWindow(g_status, statusHeight ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

#define DIR_SCAN 4096
static int g_dirManual;

static void SetRtl(BOOL rtl) {
    if (!EditSetRtl(rtl)) return;
    Layout();
    SbarSync(g_edit);
}

static void AutoDir(void) {
    int direction;
    if (g_dirManual) return;
    direction = EditStrongDir(DIR_SCAN);
    if (direction) SetRtl(direction == 2);
}

static void MarkChanged(void) {
    AppUpdateTitle();
    AppUpdateStatus();
}

static void NewWindow(const WCHAR *file) {
    WCHAR exe[PATH_CAP], cmd[PATH_CAP * 2 + 8];
    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION processInfo;
    GetModuleFileNameW(NULL, exe, PATH_CAP);
    wcopy(cmd, L"\"", COUNTOF(cmd));
    wcat(cmd, exe, COUNTOF(cmd));
    wcat(cmd, L"\"", COUNTOF(cmd));
    if (file) {
        wcat(cmd, L" \"", COUNTOF(cmd));
        wcat(cmd, file, COUNTOF(cmd));
        wcat(cmd, L"\"", COUNTOF(cmd));
    }
    memset(&startupInfo, 0, sizeof startupInfo);
    startupInfo.cb = sizeof startupInfo;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

static void AppendLogStamp(void);

typedef struct { __int64 size; FILETIME time; int valid; } FileStamp;
static FileStamp g_diskStamp;

static FileStamp DiskStamp(const WCHAR *path) {
    FileStamp stamp = { 0 };
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        stamp.valid = GetFileSizeEx(file, &stamp.size) && GetFileTime(file, NULL, NULL, &stamp.time);
        CloseHandle(file);
    }
    return stamp;
}

static BOOL ConfirmOverwrite(const WCHAR *path) {
    FileStamp current;
    if (!g_diskStamp.valid || !g_doc.path[0] || wcmpi(path, g_doc.path) != 0) return TRUE;
    current = DiskStamp(path);
    if (current.valid && current.size == g_diskStamp.size && current.time.dwLowDateTime == g_diskStamp.time.dwLowDateTime && current.time.dwHighDateTime == g_diskStamp.time.dwHighDateTime) return TRUE;
    return MpAsk(g_hwnd, APP_NAME, L"this file has changed outside notepad mint or is no longer available.\n\noverwrite it with the text in this window?", L"&overwrite", L"cancel", NULL, 2) == 1;
}

static BOOL OpenDoc(const WCHAR *path, int force, BOOL logEntry) {
    WCHAR *text;
    int length, enc, eol, logFile;
    FileStamp stamp = DiskStamp(path);
    DWORD err = DocRead(path, &text, &length, &enc, &eol, force);
    if (err == ERR_LOSSY) {
        WCHAR message[PATH_CAP + 320];
        wcopy(message, path, COUNTOF(message));
        wcat(message, L"\n\nsome bytes of this file are not valid in its encoding (a broken utf-8 sequence, or an odd number of bytes for utf-16). open it anyway? they are shown as \xFFFD, and saving writes them that way. format > encoding > reopen with encoding can pick another encoding.", COUNTOF(message));
        if (MpAsk(g_hwnd, APP_NAME, message, L"&open", L"cancel", NULL, 2) != 1) return FALSE;
        DocAllowLossy(TRUE);
        err = DocRead(path, &text, &length, &enc, &eol, force);
        DocAllowLossy(FALSE);
    }
    if (err) { FileError(err, path, FALSE); return FALSE; }
    if (!EditSetDocText(text)) { mem_free(text); FileError(ERR_NOMEM, path, FALSE); return FALSE; }
    g_dirManual = 0;
    SetRtl(EditStrongDir(DIR_SCAN) == 2);
    logFile = length >= 4 && text[0] == '.' && text[1] == 'L' && text[2] == 'O' && text[3] == 'G';
    mem_free(text);
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

static BOOL WriteDoc(const WCHAR *path, int enc, int eol) {
    void *lock = NULL;
    int length = 0;
    const WCHAR *text;
    BOOL lossy = FALSE;
    DWORD err;
    if (!ConfirmOverwrite(path)) return FALSE;
    text = EditLockText(&lock, &length);
    err = text ? DocWrite(path, text, length, enc, eol, &lossy) : ERR_NOMEM;
    EditUnlockText(lock);
    if (err == ERR_LOSSY) {
        if (MpAsk(g_hwnd, APP_NAME, L"this file contains characters in unicode format which will be lost if you save this file in this encoding. to keep the unicode information, click cancel below and then pick one of the unicode options in the encoding menu.\n\ncontinue?", L"ok", L"cancel", NULL, 2) != 1) return FALSE;
        lossy = TRUE;
        text = EditLockText(&lock, &length);
        err = text ? DocWrite(path, text, length, enc, eol, &lossy) : ERR_NOMEM;
        EditUnlockText(lock);
    }
    if (err) { FileError(err, path, TRUE); return FALSE; }
    wcopy(g_doc.path, path, PATH_CAP);
    g_doc.enc = enc;
    g_doc.eol = eol;
    g_diskStamp = DiskStamp(path);
    AppMarkClean();
    RecentAdd(g_doc.path);
    AppUpdateTitle();
    AppUpdateStatus();
    return TRUE;
}

static BOOL FileSave(BOOL saveAs) {
    WCHAR path[PATH_CAP];
    wcopy(path, g_doc.path, PATH_CAP);
    if (saveAs || !path[0]) {
        if (!path[0]) wcopy(path, g_doc.name, PATH_CAP);
        if (!FileDlgSave(g_hwnd, path, PATH_CAP)) return FALSE;
    }
    return WriteDoc(path, g_doc.enc, g_doc.eol);
}

static BOOL Confirm(void) {
    WCHAR message[PATH_CAP + 64];
    if (!AppIsDirty()) return TRUE;
    wcopy(message, L"do you want to save changes to ", COUNTOF(message));
    wcat(message, AppDocName(), COUNTOF(message));
    wcat(message, L"?", COUNTOF(message));
    switch (MpAsk(g_hwnd, APP_NAME, message, L"&save", L"do&n't save", L"cancel", 3)) {
    case 1:  return FileSave(FALSE);
    case 2:  return TRUE;
    default: return FALSE;
    }
}

static void FileNew(void) {
    if (!Confirm()) return;
    if (!EditSetDocText(L"")) { Say(L"not enough memory available to complete this operation."); return; }
    g_dirManual = 0;
    SetRtl(FALSE);
    g_doc.path[0] = 0;
    g_diskStamp.valid = 0;
    NewDocName();
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;
    AppMarkClean();
    AppUpdateTitle();
    AppUpdateStatus();
}

static void FileOpen(void) {
    WCHAR path[PATH_CAP];
    if (!Confirm()) return;
    wcopy(path, g_doc.path, PATH_CAP);
    if (!FileDlgOpen(g_hwnd, path, PATH_CAP)) return;
    OpenDoc(path, -1, TRUE);
}

static void OpenCmdFile(const WCHAR *arg, int force) {
    WCHAR full[PATH_CAP], message[PATH_CAP + 128];
    const WCHAR *name;
    BOOL dot = FALSE;
    int result;
    result = (int)GetFullPathNameW(arg, PATH_CAP, full, NULL);
    if (!result || result >= PATH_CAP) { FileError(result >= PATH_CAP ? 206 : GetLastError(), arg, FALSE); return; }
    if (IsDir(full)) return;
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) { OpenDoc(full, force, TRUE); return; }
    for (name = PathName(arg); *name; name++) if (*name == '.') dot = TRUE;
    if (!dot) {
        WCHAR txtPath[PATH_CAP];
        wcopy(txtPath, full, PATH_CAP);
        wcat(txtPath, L".txt", PATH_CAP);
        if (GetFileAttributesW(txtPath) != INVALID_FILE_ATTRIBUTES) { OpenDoc(txtPath, force, TRUE); return; }
        if (wlen(txtPath) < PATH_CAP - 1) wcopy(full, txtPath, PATH_CAP);
    }
    wcopy(message, L"cannot find the ", COUNTOF(message));
    wcat(message, full, COUNTOF(message));
    wcat(message, L" file.\n\ndo you want to create a new file?", COUNTOF(message));
    result = MpAsk(g_hwnd, APP_NAME, message, L"&yes", L"&no", L"cancel", 3);
    if (result == 1) {
        HANDLE file = CreateFileW(full, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) { FileError(GetLastError(), full, TRUE); return; }
        CloseHandle(file);
        wcopy(g_doc.path, full, PATH_CAP);
        g_diskStamp = DiskStamp(full);
        AppUpdateTitle();
        AppUpdateStatus();
    } else if (result == 3) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
}

static void FormatTimeDate(WCHAR *stamp) {
    SYSTEMTIME now;
    int length;
    stamp[0] = 0;
    GetLocalTime(&now);
    length = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, NULL, stamp, 64);
    if (length > 0) stamp[length - 1] = ' '; else { stamp[0] = ' '; length = 1; }
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &now, NULL, stamp + length, 64);
}

static void InsertTimeDate(void) {
    WCHAR stamp[160] = { 0 };
    FormatTimeDate(stamp);
    EditInsert(stamp);
}

static void AppendLogStamp(void) {
    WCHAR stamp[168] = L"\r\n";
    int end = GetWindowTextLengthW(g_edit);
    FormatTimeDate(stamp + 2);
    wcat(stamp, L"\r\n", COUNTOF(stamp));
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)end, (LPARAM)end);
    EditInsert(stamp);
}

static void ToggleWrap(void) {
    g_pf.wrap = !g_pf.wrap;
    if (!EditCreate(g_hwnd)) {
        g_pf.wrap = !g_pf.wrap;
        Say(L"not enough memory available to complete this operation.");
        return;
    }
    Layout();
    if (g_bar) InvalidateRect(g_bar, NULL, FALSE);
    AppUpdateTitle();
    AppUpdateStatus();
}

static void SetEol(int eol) {
    if (g_doc.eol == eol) return;
    g_doc.eol = eol;
    MarkChanged();
}

static void SetEnc(int enc) {
    if (g_doc.enc == enc) return;
    g_doc.enc = enc;
    MarkChanged();
}

static void Reopen(void) {
    int enc = g_doc.enc;
    if (!g_doc.path[0]) return;
    if (AppIsDirty() && MpAsk(g_hwnd, APP_NAME, L"reopening the file will discard your unsaved changes.\n\ndo you want to continue?", L"&yes", L"&no", NULL, 2) != 1) return;
    if (!EncDlg(g_hwnd, &enc, 1)) return;
    OpenDoc(g_doc.path, enc, FALSE);
}

static BOOL CALLBACK RethemeWnd(HWND hwnd, LPARAM lParam) {
    WCHAR className[32];
    (void)lParam;
    if (!GetClassNameW(hwnd, className, 32)) return TRUE;
    if (wcmp(className, APP_CLASS) != 0 && !(className[0] == 'm' && className[1] == 'p' && className[2] == '_')) return TRUE;
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) DarkFrame(hwnd, hwnd == GetActiveWindow());
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

static void ApplyTheme(int theme) {
    if (theme == g_pf.theme) return;
    ThemeUse(theme);
    SetClassLongW(g_hwnd, GCL_HBRBACKGROUND, (LONG)(LONG_PTR)g_brFace);
    EditApplyColors();
    DarkScroll(g_edit);
    EnumThreadWindows(GetCurrentThreadId(), RethemeWnd, 0);
    AppSavePrefs();
}

static const WCHAR g_ucc[17] = {
    0x200E, 0x200F, 0x200D, 0x200C, 0x202A, 0x202B, 0x202D, 0x202E, 0x202C,
    0x206E, 0x206F, 0x206B, 0x206A, 0x206D, 0x206C, 0x001E, 0x001F
};

static void SetDefaultEditor(void) {
    typedef HINSTANCE (WINAPI *ShellExecFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
    typedef void (WINAPI *ChangeFn)(LONG, UINT, const void *, const void *);
    WCHAR exe[PATH_CAP];
    HMODULE shell32;
    BOOL machine;
    DWORD length = GetModuleFileNameW(NULL, exe, PATH_CAP);
    if (!length || length >= PATH_CAP) return;
    machine = AssocMachineHas(exe);
    if (!machine && !AssocRegisterUser(exe)) {
        Say(L"windows didn't let notepad mint register itself for text files.");
        return;
    }
    shell32 = LoadLibraryW(L"shell32.dll");
    if (!shell32) return;
    {
        ChangeFn changeNotify = (ChangeFn)GetProcAddress(shell32, "SHChangeNotify");
        ShellExecFn shellExecute = (ShellExecFn)GetProcAddress(shell32, "ShellExecuteW");
        if (changeNotify && !machine) changeNotify(0x08000000, 0, NULL, NULL);
        if (shellExecute) shellExecute(g_hwnd, L"open", machine ? L"ms-settings:defaultapps?registeredAppMachine=" ASSOC_REGAPP : L"ms-settings:defaultapps?registeredAppUser=" ASSOC_REGAPP, NULL, NULL, SW_SHOWNORMAL);
    }
}

static void Cmd(int id) {
    EditDirKeyCancel();
    switch (id) {
    case IDM_FILE_NEW:      FileNew(); break;
    case IDM_FILE_NEWWIN:   NewWindow(NULL); break;
    case IDM_FILE_OPEN:     FileOpen(); break;
    case IDM_FILE_SAVE:     FileSave(FALSE); break;
    case IDM_FILE_SAVEAS:   FileSave(TRUE); break;
    case IDM_FILE_PAGESETUP: PageSetup(g_hwnd); break;
    case IDM_FILE_PRINT:    PrintDoc(g_hwnd, 0, NULL); break;
    case IDM_FILE_EXIT:     SendMessageW(g_hwnd, WM_CLOSE, 0, 0); return;
    case IDM_EDIT_UNDO: {
        HWND focus = OtherFocus();
        SendMessageW(focus ? focus : g_edit, EM_UNDO, 0, 0);
        if (focus) return;
        break; }
    case IDM_EDIT_REDO:     if (OtherFocus()) return; SendMessageW(g_edit, EM_REDO, 0, 0); break;
    case IDM_EDIT_CUT:     SendMessageW(g_edit, WM_CUT, 0, 0); break;
    case IDM_EDIT_COPY:     SendMessageW(g_edit, WM_COPY, 0, 0); break;
    case IDM_EDIT_PASTE:    SendMessageW(g_edit, WM_PASTE, 0, 0); break;
    case IDM_EDIT_DELETE:   SendMessageW(g_edit, WM_CLEAR, 0, 0); break;
    case IDM_EDIT_FIND:     FindDlgShow(0); return;
    case IDM_EDIT_REPLACE:  FindDlgShow(1); return;
    case IDM_EDIT_FINDNEXT: FindNext(0); return;
    case IDM_EDIT_FINDPREV: FindNext(1); return;
    case IDM_EDIT_GOTO:     GotoDlg(g_hwnd); break;
    case IDM_EDIT_SELALL: {
        HWND focus = OtherFocus();
        if (focus) { SendMessageW(focus, EM_SETSEL, 0, (LPARAM)-1); return; }
        SendMessageW(g_edit, EM_SETSEL, 0, (LPARAM)-1);
        break; }
    case IDM_EDIT_TIMEDATE: if (OtherFocus()) return; InsertTimeDate(); break;
    case IDM_EDIT_CLEARLINE: if (OtherFocus()) return; EditClearLine(); break;
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
        int enc = g_doc.enc;
        if (EncDlg(g_hwnd, &enc, 0)) SetEnc(enc);
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
    case IDM_RECENT_CLEAR:  RecentClear(); break;
    default:
        if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + RECENT_MAX) {
            WCHAR path[PATH_CAP];
            if (RecentPath(id - IDM_RECENT_BASE, path, COUNTOF(path))) {
                if (Confirm() && !OpenDoc(path, -1, TRUE) && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) RecentRemove(path);
            }
        } else if (id >= IDM_UCC_BASE && id < IDM_UCC_BASE + 17) {
            WCHAR text[2];
            text[0] = g_ucc[id - IDM_UCC_BASE]; text[1] = 0;
            EditInsert(text);
        }
        break;
    }
    if (!FindDlgHwnd() || GetActiveWindow() != FindDlgHwnd()) FocusEdit();
}

static void ApplyChrome(void) {
    UiSetChromeFont(g_pf.font);
    if (g_bar) MenuBarRefont(g_bar);
    if (g_status) StatusRefont(g_status);
    FrameRefont(g_hwnd);
}

void AppApplyPrefs(void) {
    FontResolve(g_pf.font);
    if (g_pf.pt < FONT_MIN) g_pf.pt = FONT_MIN;
    if (g_pf.pt > FONT_MAX) g_pf.pt = FONT_MAX;
    g_pf.cur = g_pf.pt;
    ApplyChrome();
    EditApplyFont();
    EditApplyColors();
    Layout();
    AppUpdateStatus();
    AppSavePrefs();
}

static unsigned MenuState(int id) {
    unsigned radio = MS_CHECK | MS_RADIO;
    switch (id) {
    case IDM_EDIT_UNDO:     return HistCanUndo() ? 0 : MS_GRAY;
    case IDM_EDIT_REDO:     return HistCanRedo() ? 0 : MS_GRAY;
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
    case IDM_TAB_2:         return g_pf.tab == 2 ? radio : 0;
    case IDM_TAB_4:         return g_pf.tab == 4 ? radio : 0;
    case IDM_TAB_8:         return g_pf.tab == 8 ? radio : 0;
    case IDM_EOL_CRLF:      return g_doc.eol == EOL_CRLF ? radio : 0;
    case IDM_EOL_LF:        return g_doc.eol == EOL_LF ? radio : 0;
    case IDM_EOL_CR:        return g_doc.eol == EOL_CR ? radio : 0;
    case IDM_ENC_UTF8:      return g_doc.enc == ENC_UTF8 ? radio : 0;
    case IDM_ENC_UTF8BOM:   return g_doc.enc == ENC_UTF8BOM ? radio : 0;
    case IDM_ENC_UTF16LE:   return g_doc.enc == ENC_UTF16LE ? radio : 0;
    case IDM_ENC_UTF16BE:   return g_doc.enc == ENC_UTF16BE ? radio : 0;
    case IDM_ENC_ANSI:      return g_doc.enc == ENC_ANSI ? radio : 0;
    case IDM_ENC_OTHER:     return g_doc.enc >= ENC_CP_MIN ? radio : 0;
    case IDM_ZOOM_IN:       return EditZoomCan(1) ? 0 : MS_GRAY;
    case IDM_ZOOM_OUT:      return EditZoomCan(-1) ? 0 : MS_GRAY;
    case IDM_ZOOM_RESET:    return g_pf.cur != g_pf.pt ? 0 : MS_GRAY;
    case IDM_ENC_REOPEN:    return g_doc.path[0] ? 0 : MS_GRAY;
    case IDM_THEME_DARK:    return g_pf.theme == THEME_DARK ? radio : 0;
    case IDM_THEME_LIGHT:   return g_pf.theme == THEME_LIGHT ? radio : 0;
    case IDM_SYS_RESTORE: case IDM_SYS_MOVE: case IDM_SYS_SIZE:
    case IDM_SYS_MIN: case IDM_SYS_MAX: case IDM_SYS_CLOSE:
        return FrameSysState(id);
    }
    return 0;
}

void AppOpenFiles(const WCHAR *paths) {
    static int busy;
    const WCHAR *path;
    WCHAR message[128];
    int files = 0, opened = 0;
    if (busy) return;
    for (path = paths; *path; path += wlen(path) + 1) if (!IsDir(path)) files++;
    if (!files) return;
    busy = 1;
    if (files > DROP_ASK) {
        wsprintfW(message, L"open these %d files? the first one opens in this window, the others in new windows.", files);
        if (MpAsk(g_hwnd, APP_NAME, message, L"&open", L"cancel", NULL, 2) != 1) { busy = 0; return; }
    }
    for (path = paths; *path; path += wlen(path) + 1) {
        if (IsDir(path)) continue;
        if (!opened) {
            opened = 1;
            if (!Confirm()) break;
            OpenDoc(path, -1, TRUE);
        } else {
            NewWindow(path);
        }
    }
    busy = 0;
}

static LRESULT OnCreate(HWND hwnd) {
    g_hwnd = hwnd;
    UiSetDpi(UiDpiForWindow(hwnd));
    DarkFrame(hwnd, 1);
    FontResolve(g_pf.font);
    UiSetChromeFont(g_pf.font);
    g_bar = MenuBarCreate(hwnd, MenuState);
    g_status = StatusCreate(hwnd);
    if (!EditCreate(hwnd)) return -1;
    AppMarkClean();
    Layout();
    AppUpdateTitle();
    AppUpdateStatus();
    return 0;
}

static void OnDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    const RECT *suggested = (const RECT *)lParam;
    UiSetDpi((int)HIWORD(wParam));
    MenuBarRefont(g_bar);
    StatusRefont(g_status);
    FrameRefont(hwnd);
    EditApplyFont();
    SbarTrim(g_edit, S(SBAR_TRIM), S(SBAR_TRIM));
    SetWindowPos(hwnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
#ifdef SHOTDC
    case WM_APP + 60: {
        RECT probeRect;
        int dpi = (int)wParam;
        if (dpi < 96 || dpi > 768 || !GetWindowRect(hwnd, &probeRect)) return 0;
        OnDpiChanged(hwnd, MAKEWPARAM(dpi, dpi), (LPARAM)&probeRect);
        return g_dpi; }
    case WM_APP + 61:
        return UiProbeFontAndDialogs(wParam, lParam);
    case WM_COPYDATA:
        return DndProbe((const COPYDATASTRUCT *)lParam);
#endif
    case WM_CREATE:
        return OnCreate(hwnd);
    case WM_SIZE:
        MenuCancel();
        FrameInvalidate(hwnd);
        if (wParam != SIZE_MINIMIZED && g_edit) Layout();
        return 0;
    case WM_MOVE:
        MenuCancel();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT rect;
        MINMAXINFO *minMaxInfo = (MINMAXINFO *)lParam;
        rect.left = 0; rect.top = 0; rect.right = StatusMinWidth(); rect.bottom = 0;
        if (rect.right < MenuBarMinWidth()) rect.right = MenuBarMinWidth();
        UiAdjustRect(&rect, WS_OVERLAPPEDWINDOW, 0);
        minMaxInfo->ptMinTrackSize.x = rect.right - rect.left;
        if (minMaxInfo->ptMinTrackSize.x < S(MIN_WIN_W)) minMaxInfo->ptMinTrackSize.x = S(MIN_WIN_W);
        minMaxInfo->ptMinTrackSize.y = S(MIN_WIN_H);
        return 0; }
    case WM_SETFOCUS:
        FocusEdit();
        return 0;
    case WM_ACTIVATE:
        DBG(L"activate", LOWORD(wParam), lParam);
        DarkFrame(hwnd, LOWORD(wParam) != WA_INACTIVE);
        FrameActive(hwnd, LOWORD(wParam) != WA_INACTIVE);
        if (LOWORD(wParam) != WA_INACTIVE) RecentLoad();
        break;
    case WM_NCACTIVATE:
        if (FrameEnabled()) return DefWindowProcW(hwnd, msg, wParam, (LPARAM)-1);
        break;
    case WM_NCCALCSIZE:
        return FrameNcCalc(hwnd, wParam, lParam);
    case WM_NCHITTEST:
        return FrameHitTest(hwnd, lParam);
    case WM_NCRBUTTONUP:
        if (FrameEnabled() && wParam == HTCAPTION) { FrameSysMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0; }
        break;
    case WM_SETTEXT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        FrameInvalidate(hwnd);
        return result; }
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_NCRBUTTONDOWN:
    case WM_CAPTURECHANGED:
        if (FrameMsg(hwnd, msg, wParam, lParam)) return 0;
        break;
    case WM_ACTIVATEAPP:
        if (!wParam) MenuCancel();
        break;
    case WM_CANCELMODE:
        MenuCancel();
        break;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT client;
        int saved = SaveDC(hdc);
        GetClientRect(hwnd, &client);
        ExcludeClipRect(hdc, 0, 0, client.right, FrameHeight());
        FillRect(hdc, &client, g_brFace);
        RestoreDC(hdc, saved);
        return 1; }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        FramePaint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_CTLCOLOREDIT:
        if ((HWND)lParam == g_edit) {
            SetTextColor((HDC)wParam, C_EDIT_FG);
            SetBkColor((HDC)wParam, C_EDIT_BG);
            return (LRESULT)EditBrush();
        }
        break;
    case WM_TIMER:
        if (wParam == DND_TIMER) { KillTimer(hwnd, DND_TIMER); DndInit(hwnd); return 0; }
        AppStatusTimer(hwnd, wParam);
        return 0;
    case WM_COMMAND:
        if ((HWND)lParam == g_edit) {
            switch (HIWORD(wParam)) {
            case EN_CHANGE:
                g_textRev++;
                AutoDir();
                AppUpdateTitle();
                AppUpdateStatus();
                EditScrollSoon();
                break;
            case EN_ERRSPACE:
            case EN_MAXTEXT:
                Say(L"not enough memory available to complete this operation.");
                break;
            }
            return 0;
        }
        Cmd(LOWORD(wParam));
        return 0;
    case WM_SYSCOMMAND:
        DBG(L"syscmd", wParam, lParam);
        if (FrameEnabled() && (wParam & 0xFFF0) == SC_KEYMENU && lParam == ' ') {
            MenuShowCues();
            FrameSysMenu(hwnd, -1, -1);
            return 0;
        }
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam != ' ') {
            if (lParam == 0) {
                MenuBarActivate(0, 0);
            } else {
                int index = MenuBarMnemonic((WCHAR)lParam);
                if (index >= 0) MenuBarActivate(index, 1);
            }
            return 0;
        }
        break;
    case WM_DPICHANGED:
        OnDpiChanged(hwnd, wParam, lParam);
        return 0;
    case WM_DROPFILES:
        DndDropFiles((HANDLE)wParam);
        return 0;
    case WM_APP_OPENFILES:
        AppOpenFiles((const WCHAR *)lParam);
        mem_free((void *)lParam);
        return 0;
    case WM_QUERYENDSESSION:
        return Confirm() ? TRUE : FALSE;
    case WM_ENDSESSION:
        if (wParam) AppSavePrefs();
        return 0;
    case WM_CLOSE:
        if (!Confirm()) return 0;
        AppSavePrefs();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        DndShutdown();
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static ACCEL g_acc[] = {
    { FVIRTKEY | FCONTROL,           'N',            IDM_FILE_NEW },
    { FVIRTKEY | FCONTROL | FSHIFT,  'N',            IDM_FILE_NEWWIN },
    { FVIRTKEY | FCONTROL,           'O',            IDM_FILE_OPEN },
    { FVIRTKEY | FCONTROL,           'S',            IDM_FILE_SAVE },
    { FVIRTKEY | FCONTROL | FSHIFT,  'S',            IDM_FILE_SAVEAS },
    { FVIRTKEY | FCONTROL,           'P',            IDM_FILE_PRINT },
    { FVIRTKEY | FCONTROL,           'W',            IDM_FILE_EXIT },
    { FVIRTKEY | FALT,               'X',            IDM_THEME_TOGGLE },
    { FVIRTKEY | FCONTROL,           'U',            IDM_VIEW_STATUS },
    { FVIRTKEY | FALT,               'Z',            IDM_FMT_WRAP },
    { FVIRTKEY | FCONTROL,           'Z',            IDM_EDIT_UNDO },
    { FVIRTKEY | FCONTROL,           'Y',            IDM_EDIT_REDO },
    { FVIRTKEY | FCONTROL | FSHIFT,  'Z',            IDM_EDIT_REDO },
    { FVIRTKEY | FCONTROL,           'A',            IDM_EDIT_SELALL },
    { FVIRTKEY | FCONTROL,           'F',            IDM_EDIT_FIND },
    { FVIRTKEY | FCONTROL,           'H',            IDM_EDIT_REPLACE },
    { FVIRTKEY | FCONTROL,           'G',            IDM_EDIT_GOTO },
    { FVIRTKEY | FCONTROL,           'K',            IDM_EDIT_CLEARLINE },
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

static void CmdLineFile(WCHAR *out, int cap) {
    const WCHAR *text = GetCommandLineW();
    int length = 0;
    out[0] = 0;
    if (*text == '"') { text++; while (*text && *text != '"') text++; if (*text) text++; }
    else while (*text && *text != ' ' && *text != '\t') text++;
    while (*text == ' ' || *text == '\t') text++;
    if (*text == '"') {
        text++;
        while (*text && *text != '"' && length < cap - 1) out[length++] = *text++;
    } else {
        while (*text && length < cap - 1) out[length++] = *text++;
        while (length > 0 && (out[length - 1] == ' ' || out[length - 1] == '\t')) length--;
    }
    out[length] = 0;
}

static WCHAR *CmdToken(WCHAR *text, WCHAR **next) {
    WCHAR *start;
    while (*text == ' ' || *text == '\t') text++;
    if (*text == '"') { start = ++text; while (*text && *text != '"') text++; }
    else { start = text; while (*text && *text != ' ' && *text != '\t') text++; }
    if (*text) *text++ = 0;
    *next = text;
    return start;
}

static int CmdSwitch(WCHAR *arg, WCHAR *printer, int cap) {
    WCHAR option, *file, *rest;
    int length;
    printer[0] = 0;
    if (arg[0] == '/' && wlow(arg[1]) == 'p' && wlow(arg[2]) == 't' && (arg[3] == ' ' || arg[3] == '\t')) {
        file = CmdToken(arg + 4, &rest);
        wcopy(printer, CmdToken(rest, &rest), cap);
        memmove(arg, file, ((size_t)wlen(file) + 1) * sizeof(WCHAR));
        return 't';
    }
    if (arg[0] != '/' || !arg[1] || (arg[2] != ' ' && arg[2] != '\t')) return 0;
    option = wlow(arg[1]);
    if (option != 'p' && option != 'a' && option != 'w') return 0;
    file = arg + 3;
    while (*file == ' ' || *file == '\t') file++;
    if (*file == '"') { file++; for (length = 0; file[length] && file[length] != '"'; length++) ; file[length] = 0; }
    memmove(arg, file, ((size_t)wlen(file) + 1) * sizeof(WCHAR));
    return option;
}

static void Cascade(int *x, int *y) {
    int moved = 1, tries = 0, tolerance = S(12);
    while (moved && tries++ < 12) {
        HWND other = NULL;
        moved = 0;
        while ((other = FindWindowExW(NULL, other, APP_CLASS, NULL)) != NULL) {
            RECT rect;
            if (other == g_hwnd || IsZoomed(other) || !GetWindowRect(other, &rect)) continue;
            if (rect.left - *x < tolerance && *x - rect.left < tolerance && rect.top - *y < tolerance && *y - rect.top < tolerance) {
                *x += S(32); *y += S(32);
                moved = 1;
                break;
            }
        }
    }
}

static void ShowMain(void) {
    int hasPlacement = (g_pf.winw >= 200 && g_pf.winh >= 120);
    POINT pt;
    if (hasPlacement) {
        pt.x = g_pf.winx + 40; pt.y = g_pf.winy + 12;
        if (!MonitorFromPoint(pt, MONITOR_DEFAULTTONULL)) hasPlacement = 0;
    }
    if (hasPlacement) {
        WINDOWPLACEMENT placement;
        int x = g_pf.winx, y = g_pf.winy;
        Cascade(&x, &y);
        memset(&placement, 0, sizeof placement);
        placement.length = sizeof placement;
        placement.showCmd = g_pf.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        placement.rcNormalPosition.left = x;
        placement.rcNormalPosition.top = y;
        placement.rcNormalPosition.right = x + g_pf.winw;
        placement.rcNormalPosition.bottom = y + g_pf.winh;
        SetWindowPlacement(g_hwnd, &placement);
    } else {
        ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    }
}

int mp_main(void) {
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    WNDCLASSEXW windowClass;
    MSG message;
    WCHAR arg[PATH_CAP], printer[PATH_CAP];
    int option;
    UiInit(hInstance);
    PrefsInit();
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;
    NewDocName();
    memset(&windowClass, 0, sizeof windowClass);
    windowClass.cbSize = sizeof windowClass;
    windowClass.lpfnWndProc = MainProc;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = g_brFace;
    windowClass.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&windowClass)) return 1;
    g_accel = CreateAcceleratorTableW(g_acc, COUNTOF(g_acc));
    if (!CreateWindowExW(WS_EX_ACCEPTFILES, APP_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, 0, S(900), S(620), NULL, NULL, hInstance, NULL)) return 1;
    if (FrameEnabled()) SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    CmdLineFile(arg, PATH_CAP);
    option = CmdSwitch(arg, printer, PATH_CAP);
    if (option == 'p' || option == 't') {
        WCHAR full[PATH_CAP];
        DWORD length = arg[0] ? GetFullPathNameW(arg, PATH_CAP, full, NULL) : 0;
        if (length && length < PATH_CAP && GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES && OpenDoc(full, -1, FALSE)) PrintDoc(g_hwnd, 1, option == 't' ? printer : NULL);
        return 0;
    }
    ShowMain();
    SetTimer(g_hwnd, DND_TIMER, 1, NULL);
    if (arg[0]) OpenCmdFile(arg, option == 'a' ? ENC_ANSI : option == 'w' ? ENC_UTF16LE : -1);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        HWND findDlg = FindDlgHwnd();
        if (findDlg && IsDialogMessageW(findDlg, &message)) continue;
        if (g_hwnd && message.hwnd && (message.hwnd == g_hwnd || IsChild(g_hwnd, message.hwnd) || (findDlg && (message.hwnd == findDlg || IsChild(findDlg, message.hwnd)))) && TranslateAcceleratorW(g_hwnd, g_accel, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}

void start(void) {
    ExitProcess((UINT)mp_main());
}
