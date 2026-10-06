/* main.c - notepad mint: entry point, main window, commands, file flow, settings.
 *
 * layout: [menu bar][sunken frame around the edit][status bar]. the menu bar, status bar
 * and edit control are child windows; everything except the edit control is drawn by us. */
#include "mp.h"

Prefs    g_pf;
HWND     g_hwnd, g_status;
DocState g_doc;

static HWND   g_bar;
static HACCEL g_accel;
static WCHAR  g_iniDir[PATH_CAP], g_ini[PATH_CAP];
static WCHAR  g_title[PATH_CAP + 64];

/* ======================================================== settings ======= */
static void PrefsDefaults(void)
{
    memset(&g_pf, 0, sizeof g_pf);
    wcopy(g_pf.font, L"Consolas", 32);
    g_pf.pt = 13;
    g_pf.cur = 13;
    g_pf.fg = C_EDIT_FG;
    g_pf.bg = C_EDIT_BG;
    g_pf.statusbar = 1;
    g_pf.wrapAround = 1;
    g_pf.marginL = 750; g_pf.marginT = 1000; g_pf.marginR = 750; g_pf.marginB = 1000;
}

static void IniLocate(void)
{
    WCHAR base[PATH_CAP];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", base, PATH_CAP - 64);
    if (!n || n >= PATH_CAP - 64) {                                  /* no %appdata%: keep it next to the exe */
        WCHAR exe[PATH_CAP];
        GetModuleFileNameW(NULL, exe, PATH_CAP - 64);
        PathDir(exe, base, PATH_CAP - 64);
    }
    wcopy(g_iniDir, base, PATH_CAP);
    PathJoin(g_iniDir, L"notepad mint", PATH_CAP);
    wcopy(g_ini, g_iniDir, PATH_CAP);
    PathJoin(g_ini, L"settings.ini", PATH_CAP);
}

static int IniGet(const WCHAR *sec, const WCHAR *key, int def)
{
    WCHAR b[32];
    GetPrivateProfileStringW(sec, key, L"", b, 32, g_ini);
    return b[0] ? wtoi(b) : def;
}

static void IniPutInt(const WCHAR *sec, const WCHAR *key, int v)
{
    WCHAR b[16];
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, g_ini);
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* the editor colours come with the theme: g_pf.fg / bg are derived, never saved */
static void ThemeUse(int theme)
{
    ThemeSet(theme);
    g_pf.theme = ThemeGet();
    g_pf.fg = C_EDIT_FG;
    g_pf.bg = C_EDIT_BG;
}

static void PrefsLoad(void)
{
    WCHAR f[32];
    PrefsDefaults();
    GetPrivateProfileStringW(L"editor", L"font", L"", f, 32, g_ini);
    if (f[0]) wcopy(g_pf.font, f, 32);
    g_pf.pt      = Clamp(IniGet(L"editor", L"size", g_pf.pt), FONT_MIN, FONT_MAX);
    g_pf.cur     = g_pf.pt;                                          /* the working size always starts at the chosen one */
    g_pf.bold    = IniGet(L"editor", L"bold", 0) != 0;
    g_pf.italic  = IniGet(L"editor", L"italic", 0) != 0;
    g_pf.wrap    = IniGet(L"editor", L"wrap", 0) != 0;
    g_pf.statusbar = IniGet(L"view", L"statusbar", 1) != 0;
    GetPrivateProfileStringW(L"view", L"theme", L"dark", f, 32, g_ini);
    ThemeUse(wcmpi(f, L"light") == 0 ? THEME_LIGHT : THEME_DARK);       /* before any window exists: classes take g_brFace */
    g_pf.winx    = IniGet(L"window", L"x", 0);
    g_pf.winy    = IniGet(L"window", L"y", 0);
    g_pf.winw    = IniGet(L"window", L"w", 0);
    g_pf.winh    = IniGet(L"window", L"h", 0);
    g_pf.maximized = IniGet(L"window", L"maximized", 0) != 0;
    g_pf.matchCase  = IniGet(L"find", L"matchcase", 0) != 0;
    g_pf.wrapAround = IniGet(L"find", L"wraparound", 1) != 0;
    g_pf.marginL = IniGet(L"page", L"left", g_pf.marginL);
    g_pf.marginT = IniGet(L"page", L"top", g_pf.marginT);
    g_pf.marginR = IniGet(L"page", L"right", g_pf.marginR);
    g_pf.marginB = IniGet(L"page", L"bottom", g_pf.marginB);
}

static void CapturePlacement(void)
{
    WINDOWPLACEMENT wp;
    if (!g_hwnd) return;
    wp.length = sizeof wp;
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    g_pf.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    g_pf.winx = wp.rcNormalPosition.left;
    g_pf.winy = wp.rcNormalPosition.top;
    g_pf.winw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    g_pf.winh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
}

void AppSavePrefs(void)
{
    static const BYTE bom[2] = { 0xFF, 0xFE };
    HANDLE f;
    DWORD wr;

    CapturePlacement();
    CreateDirectoryW(g_iniDir, NULL);
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);   /* utf-16 ini: font names can be anything */
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, bom, 2, &wr, NULL); CloseHandle(f); }

    WritePrivateProfileStringW(L"editor", L"font", g_pf.font, g_ini);
    IniPutInt(L"editor", L"size", g_pf.pt);
    IniPutInt(L"editor", L"bold", g_pf.bold);
    IniPutInt(L"editor", L"italic", g_pf.italic);
    WritePrivateProfileStringW(L"editor", L"text", NULL, g_ini);          /* old custom colours: the theme decides now */
    WritePrivateProfileStringW(L"editor", L"background", NULL, g_ini);
    IniPutInt(L"editor", L"wrap", g_pf.wrap);
    IniPutInt(L"view", L"statusbar", g_pf.statusbar);
    WritePrivateProfileStringW(L"view", L"theme", g_pf.theme == THEME_LIGHT ? L"light" : L"dark", g_ini);
    IniPutInt(L"window", L"x", g_pf.winx);
    IniPutInt(L"window", L"y", g_pf.winy);
    IniPutInt(L"window", L"w", g_pf.winw);
    IniPutInt(L"window", L"h", g_pf.winh);
    IniPutInt(L"window", L"maximized", g_pf.maximized);
    IniPutInt(L"find", L"matchcase", g_pf.matchCase);
    IniPutInt(L"find", L"wraparound", g_pf.wrapAround);
    IniPutInt(L"page", L"left", g_pf.marginL);
    IniPutInt(L"page", L"top", g_pf.marginT);
    IniPutInt(L"page", L"right", g_pf.marginR);
    IniPutInt(L"page", L"bottom", g_pf.marginB);
}

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

BOOL AppIsDirty(void)
{
    return g_edit && SendMessageW(g_edit, EM_GETMODIFY, 0, 0) != 0;
}

static void FocusEdit(void)
{
    if (g_edit && IsWindowEnabled(g_hwnd)) SetFocus(g_edit);
}

void AppUpdateTitle(void)
{
    WCHAR t[PATH_CAP + 64];
    t[0] = 0;
    if (AppIsDirty()) wcopy(t, L"*", COUNTOF(t));
    wcat(t, g_doc.path[0] ? PathName(g_doc.path) : L"untitled", COUNTOF(t));
    wcat(t, L" - " APP_NAME, COUNTOF(t));
    if (wcmp(t, g_title) != 0) {
        wcopy(g_title, t, COUNTOF(g_title));
        SetWindowTextW(g_hwnd, t);                      /* (taskbar / alt-tab; the strip repaints from WM_SETTEXT) */
    }
}

void AppUpdateStatus(void)
{
    WCHAR b[64];
    int line, col;
    if (!g_status || !g_pf.statusbar) return;
    EditCaretPos(&line, &col);
    wsprintfW(b, L"ln %d, col %d", line, col);
    StatusSet(g_status, SB_POS, b);
    wsprintfW(b, L"%d pt", g_pf.cur);
    StatusSet(g_status, SB_ZOOM, b);
    StatusSet(g_status, SB_EOL, g_eolName[g_doc.eol]);
    EncLabel(g_doc.enc, b, COUNTOF(b));
    StatusSet(g_status, SB_ENC, b);
}

/* ========================================================== layout ======= */
static void EditArea(RECT *r)                       /* the area (incl. its sunken frame) the edit lives in */
{
    RECT cr;
    int sh = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &cr);
    r->left = 0;
    r->top = FrameHeight() + MenuBarHeight();         /* [title strip][menu bar][edit][status bar] */
    r->right = cr.right;
    r->bottom = cr.bottom - sh;
    if (r->bottom < r->top + 4) r->bottom = r->top + 4;
}

static void Layout(void)
{
    RECT cr, f;
    int sh = g_pf.statusbar ? StatusHeight() : 0;
    GetClientRect(g_hwnd, &cr);
    EditArea(&f);
    if (g_bar) MoveWindow(g_bar, 0, FrameHeight(), cr.right, MenuBarHeight(), TRUE);
    if (g_edit) MoveWindow(g_edit, f.left + 2, f.top + 2, f.right - f.left - 4, f.bottom - f.top - 4, TRUE);
    if (g_status) {
        if (sh) {
            MoveWindow(g_status, 0, cr.bottom - sh, cr.right, sh, TRUE);
            ShowWindow(g_status, SW_SHOW);
        } else {
            ShowWindow(g_status, SW_HIDE);
        }
    }
    InvalidateRect(g_hwnd, NULL, FALSE);            /* the frame lines move with the edit */
}

/* ===================================================== document state ==== */
static void MarkChanged(void)                       /* encoding / line ending changed: needs a save */
{
    if (g_doc.path[0] || GetWindowTextLengthW(g_edit) > 0) SendMessageW(g_edit, EM_SETMODIFY, TRUE, 0);
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

/* load `path` into the edit control. force = -1 detects the encoding */
static BOOL OpenDoc(const WCHAR *path, int force)
{
    WCHAR *t;
    int len, enc, eol;
    DWORD er = DocRead(path, &t, &len, &enc, &eol, force);
    if (er) { FileError(er, path, FALSE); return FALSE; }
    EditSetDocText(t);
    mem_free(t);
    wcopy(g_doc.path, path, PATH_CAP);
    g_doc.enc = enc;
    g_doc.eol = eol;
    AppUpdateTitle();
    AppUpdateStatus();
    return TRUE;
}

/* write the document. returns TRUE when it reached the disk */
static BOOL WriteDoc(const WCHAR *path, int enc, int eol)
{
    int len;
    WCHAR *t = EditGetDocText(&len);
    BOOL lossy = FALSE;
    DWORD er;

    if (!t) { FileError(ERR_NOMEM, path, TRUE); return FALSE; }
    er = DocWrite(path, t, len, enc, eol, &lossy);
    if (er == ERR_LOSSY) {
        if (MpAsk(g_hwnd, APP_NAME,
                  L"this file contains characters in unicode format which will be lost if you save this file "
                  L"in this encoding. to keep the unicode information, click cancel below and then pick one of "
                  L"the unicode options in the encoding menu.\n\ncontinue?",
                  L"ok", L"cancel", NULL, 2) != 1) {
            mem_free(t);
            return FALSE;
        }
        lossy = TRUE;
        er = DocWrite(path, t, len, enc, eol, &lossy);
    }
    mem_free(t);
    if (er) { FileError(er, path, TRUE); return FALSE; }
    wcopy(g_doc.path, path, PATH_CAP);
    g_doc.enc = enc;
    g_doc.eol = eol;
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    AppUpdateTitle();
    AppUpdateStatus();
    return TRUE;
}

static BOOL FileSave(BOOL saveAs)
{
    WCHAR path[PATH_CAP];
    int enc = g_doc.enc, eol = g_doc.eol;
    wcopy(path, g_doc.path, PATH_CAP);
    if (saveAs || !path[0]) {
        if (!FileDlgSave(g_hwnd, path, PATH_CAP, &enc, &eol)) return FALSE;
    }
    return WriteDoc(path, enc, eol);
}

/* TRUE when the current document may be replaced / closed (saved, or the user said don't save) */
static BOOL Confirm(void)
{
    WCHAR msg[PATH_CAP + 64];
    if (!AppIsDirty()) return TRUE;
    wcopy(msg, L"do you want to save changes to ", COUNTOF(msg));
    wcat(msg, g_doc.path[0] ? PathName(g_doc.path) : L"untitled", COUNTOF(msg));
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
    EditSetDocText(L"");
    g_doc.path[0] = 0;
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;
    AppUpdateTitle();
    AppUpdateStatus();
}

static void FileOpen(void)
{
    WCHAR path[PATH_CAP];
    if (!Confirm()) return;
    wcopy(path, g_doc.path, PATH_CAP);
    if (!FileDlgOpen(g_hwnd, path, PATH_CAP)) return;
    OpenDoc(path, -1);
}

void AppOpenPath(const WCHAR *path)
{
    if (Confirm()) OpenDoc(path, -1);
}

/* the file named on the command line. like notepad: "foo" falls back to "foo.txt", a missing file can be created */
static void OpenCmdFile(const WCHAR *arg)
{
    WCHAR full[PATH_CAP], msg[PATH_CAP + 128];
    const WCHAR *nm;
    BOOL dot = FALSE;
    int r;

    if (!GetFullPathNameW(arg, PATH_CAP, full, NULL)) wcopy(full, arg, PATH_CAP);
    if (IsDir(full)) return;
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) { OpenDoc(full, -1); return; }

    for (nm = PathName(full); *nm; nm++) if (*nm == '.') dot = TRUE;
    if (!dot) {
        WCHAR t[PATH_CAP];
        wcopy(t, full, PATH_CAP);
        wcat(t, L".txt", PATH_CAP);
        if (GetFileAttributesW(t) != INVALID_FILE_ATTRIBUTES) { OpenDoc(t, -1); return; }
    }

    wcopy(msg, L"cannot find the ", COUNTOF(msg));
    wcat(msg, full, COUNTOF(msg));
    wcat(msg, L" file.\n\ndo you want to create a new file?", COUNTOF(msg));
    r = MpAsk(g_hwnd, APP_NAME, msg, L"yes", L"no", L"cancel", 3);
    if (r == 1) {
        HANDLE f = CreateFileW(full, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { FileError(GetLastError(), full, TRUE); return; }
        CloseHandle(f);
        wcopy(g_doc.path, full, PATH_CAP);
        AppUpdateTitle();
        AppUpdateStatus();
    } else if (r == 3) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
}

/* ====================================================== edit commands ==== */
static void InsertTimeDate(void)
{
    SYSTEMTIME st;
    WCHAR b[160];
    int n;
    GetLocalTime(&st);
    n = GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, b, 64);
    if (n > 0) b[n - 1] = ' '; else n = 1;
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, b + n, 64);
    EditInsert(b);
}

static void ToggleWrap(void)
{
    g_pf.wrap = !g_pf.wrap;
    EditCreate(g_hwnd);                             /* a native edit can't switch wrapping live: rebuild it */
    Layout();
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
    if (!g_doc.path[0]) {
        Say(L"this document hasn't been saved yet, so there is nothing to reopen.");
        return;
    }
    if (!EncDlg(g_hwnd, &e, 1)) return;
    if (AppIsDirty() &&
        MpAsk(g_hwnd, APP_NAME, L"reopening the file will discard your unsaved changes.\n\ndo you want to continue?",
              L"yes", L"no", NULL, 2) != 1) return;
    OpenDoc(g_doc.path, e);
}

/* our top-level windows on this thread (the main window, a modeless find dialog): frame colours + a full repaint */
static BOOL CALLBACK RethemeWnd(HWND h, LPARAM l)
{
    WCHAR cn[32];
    (void)l;
    GetClassNameW(h, cn, 32);
    if (wcmp(cn, APP_CLASS) != 0 && !(cn[0] == 'm' && cn[1] == 'p' && cn[2] == '_')) return TRUE;   /* ime windows etc. */
    if ((GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) DarkFrame(h, h == GetActiveWindow());
    RedrawWindow(h, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

/* view > theme. everything we draw reads the palette at paint time, so a repaint is enough after this */
static void ApplyTheme(int theme)
{
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

static void Cmd(int id)
{
    switch (id) {
    case IDM_FILE_NEW:      FileNew(); break;
    case IDM_FILE_NEWWIN:   NewWindow(NULL); break;
    case IDM_FILE_OPEN:     FileOpen(); break;
    case IDM_FILE_SAVE:     FileSave(FALSE); break;
    case IDM_FILE_SAVEAS:   FileSave(TRUE); break;
    case IDM_FILE_PAGESETUP: PageSetup(g_hwnd); break;
    case IDM_FILE_PRINT:    PrintDoc(g_hwnd); break;
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
    case IDM_EDIT_SELALL:   SendMessageW(g_edit, EM_SETSEL, 0, (LPARAM)-1); break;
    case IDM_EDIT_TIMEDATE: InsertTimeDate(); break;

    case IDM_FMT_WRAP:      ToggleWrap(); break;
    case IDM_FMT_FONT:      if (FontDlg(g_hwnd)) AppApplyPrefs(); break;
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
    case IDM_RTL:           EditToggleRtl(); break;

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

    case IDM_HELP_TOPICS:   HelpDlg(g_hwnd); break;
    case IDM_HELP_ABOUT:    AboutDlg(g_hwnd); break;

    case IDM_SYS_RESTORE: case IDM_SYS_MOVE: case IDM_SYS_SIZE:
    case IDM_SYS_MIN: case IDM_SYS_MAX: case IDM_SYS_CLOSE:
        FrameSysCommand(g_hwnd, id);
        return;

    default:
        if (id >= IDM_UCC_BASE && id < IDM_UCC_BASE + 17) {
            WCHAR s[2];
            s[0] = g_ucc[id - IDM_UCC_BASE]; s[1] = 0;
            EditInsert(s);
        }
        break;
    }
    FocusEdit();
}

/* the chrome (menu bar, popups, status bar) follows the editor font: same face, CHROME_PT_LESS (3) pt smaller, max 14pt */
static void ApplyChrome(void)
{
    UiSetChromeFont(g_pf.font, g_pf.pt);
    if (g_bar) MenuBarRefont(g_bar);
    if (g_status) StatusRefont(g_status);
    FrameRefont(g_hwnd);
}

void AppApplyPrefs(void)
{
    FontResolve(g_pf.font);
    g_pf.pt = Clamp(g_pf.pt, FONT_MIN, FONT_MAX);
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

static void OnDropFiles(HANDLE drop)
{
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    DragQueryFn q = sh ? (DragQueryFn)GetProcAddress(sh, "DragQueryFileW") : NULL;
    DragFinishFn fin = sh ? (DragFinishFn)GetProcAddress(sh, "DragFinish") : NULL;
    UINT n, i;
    int first = 1;
    if (!q || !fin) return;
    n = q(drop, 0xFFFFFFFF, NULL, 0);
    for (i = 0; i < n; i++) {
        WCHAR p[PATH_CAP];
        if (!q(drop, i, p, PATH_CAP) || IsDir(p)) continue;
        if (first) { first = 0; AppOpenPath(p); }          /* the first file opens here, the rest get their own windows */
        else NewWindow(p);
    }
    fin(drop);
}

static LRESULT OnCreate(HWND h)
{
    g_hwnd = h;
    UiSetDpi(UiDpiForWindow(h));
    DarkFrame(h, 1);
    FontResolve(g_pf.font);
    UiSetChromeFont(g_pf.font, g_pf.pt);            /* before the bars exist: their heights come from this font */
    g_bar = MenuBarCreate(h, MenuState);
    g_status = StatusCreate(h);
    if (!EditCreate(h)) return -1;
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
    SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        return OnCreate(h);
    case WM_SIZE:
        MenuCancel();
        if (w != SIZE_MINIMIZED && g_edit) Layout();
        return 0;
    case WM_MOVE:
        MenuCancel();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT r;
        r.left = 0; r.top = 0; r.right = StatusMinWidth(); r.bottom = S(200);   /* client size the status panels need */
        UiAdjustRect(&r, WS_OVERLAPPEDWINDOW, 0);
        ((MINMAXINFO *)l)->ptMinTrackSize.x = r.right - r.left;
        ((MINMAXINFO *)l)->ptMinTrackSize.y = r.bottom - r.top;
        return 0; }
    case WM_SETFOCUS:
        FocusEdit();
        return 0;
    case WM_ACTIVATE:
        DBG(L"activate", LOWORD(w), l);
        DarkFrame(h, LOWORD(w) != WA_INACTIVE);
        FrameActive(h, LOWORD(w) != WA_INACTIVE);
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
    case WM_CAPTURECHANGED:
        if (FrameMsg(h, m, w, l)) return 0;
        break;
    case WM_ACTIVATEAPP:
        if (!w) MenuCancel();
        break;
    case WM_CANCELMODE:
        MenuCancel();
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT f;
        FramePaint(h, dc);                                 /* the title strip */
        EditArea(&f);
        Bevel(dc, &f, BV_SUNKEN);
        EndPaint(h, &ps);
        return 0; }
    case WM_CTLCOLOREDIT:
        if ((HWND)l == g_edit) {
            SetTextColor((HDC)w, g_pf.fg);
            SetBkColor((HDC)w, g_pf.bg);
            return (LRESULT)EditBrush();
        }
        break;
    case WM_COMMAND:
        if ((HWND)l == g_edit) {                               /* notifications from the edit control */
            switch (HIWORD(w)) {
            case EN_CHANGE:
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
    { FVIRTKEY | FCONTROL,           'F',            IDM_EDIT_FIND },
    { FVIRTKEY | FCONTROL,           'H',            IDM_EDIT_REPLACE },
    { FVIRTKEY | FCONTROL,           'G',            IDM_EDIT_GOTO },
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

    UiInit(hi);
    IniLocate();
    PrefsLoad();
    g_doc.enc = ENC_UTF8;
    g_doc.eol = EOL_CRLF;

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
    ShowMain();

    CmdLineFile(arg, PATH_CAP);
    if (arg[0]) OpenCmdFile(arg);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND fd = FindDlgHwnd();
        if (fd && IsDialogMessageW(fd, &msg)) continue;        /* the modeless find / replace dialog */
        if (g_hwnd && msg.hwnd && (msg.hwnd == g_hwnd || IsChild(g_hwnd, msg.hwnd)) &&
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
