/* prefs.c - persisted preferences and the shared recent-file list.
 * Keep INI paths and history storage private; callers receive copied paths so
 * refreshing another window's history cannot invalidate an in-flight command. */
#include "app_internal.h"

Prefs g_pf;
static Prefs g_saved;                               /* what this window last read from or wrote to settings.ini (AppSavePrefs writes only what it changed) */
static WCHAR g_iniDir[PATH_CAP], g_ini[PATH_CAP];

/* ======================================================== settings ======= */
static void PrefsDefaults(void)
{
    memset(&g_pf, 0, sizeof g_pf);
    wcopy(g_pf.font, L"Consolas", 32);
    g_pf.pt = 100;
    g_pf.cur = g_pf.pt;
    g_pf.fg = C_EDIT_FG;
    g_pf.bg = C_EDIT_BG;
    g_pf.statusbar = 1;
    g_pf.tab = 4;
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
    PathJoin(g_iniDir, L"notepad-mint", PATH_CAP);
    wcopy(g_ini, g_iniDir, PATH_CAP);
    PathJoin(g_ini, L"settings.ini", PATH_CAP);
}

static int IniGet(const WCHAR *sec, const WCHAR *key, int def)
{
    WCHAR b[32];
    GetPrivateProfileStringW(sec, key, L"", b, 32, g_ini);
    return b[0] ? wtoi(b) : def;
}

/* WritePrivateProfileStringW fails while something else (an antivirus scan, a backup tool) has the file open for a moment: try again a few times.
 * mine = FALSE: this window has not changed the value since it read it, so only a missing key is written (another window may have saved a newer one) */
static void IniPutStr(const WCHAR *sec, const WCHAR *key, const WCHAR *v, BOOL mine)
{
    static DWORD retryAfter;
    WCHAR current[PATH_CAP];
    int n;
    DWORD er;
    if (retryAfter && (LONG)(GetTickCount() - retryAfter) < 0) return;
    retryAfter = 0;
    GetPrivateProfileStringW(sec, key, L"", current, COUNTOF(current), g_ini);
    if (v && (wcmp(current, v) == 0 || (!mine && current[0]))) return;
    for (n = 0; n < 8; n++) {
        if (WritePrivateProfileStringW(sec, key, v, g_ini)) return;
        er = GetLastError();
        if (er != 32 && er != 33) break;
        if (n < 7) Sleep(25);
    }
    retryAfter = GetTickCount() + 1000;            /* one unavailable file must not stall for every settings key */
}

static void IniPutInt(const WCHAR *sec, const WCHAR *key, int v, BOOL mine)
{
    WCHAR b[16];
    wsprintfW(b, L"%d", v);
    IniPutStr(sec, key, b, mine);
}

/* the ini is utf-16 (font names and paths can be anything): make sure it exists as such before anything is written to it */
static void IniEnsure(void)
{
    static const BYTE bom[2] = { 0xFF, 0xFE };
    HANDLE f;
    DWORD wr;
    CreateDirectoryW(g_iniDir, NULL);
    f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, bom, 2, &wr, NULL); CloseHandle(f); }
}

/* file > recent: the last RECENT_MAX files opened or saved, newest first, kept in settings.ini ([recent] 1 .. 9). every window reads the list again
 * before it changes it and when it is activated, so windows opened side by side share one list */
static WCHAR g_recent[RECENT_MAX][PATH_CAP];
static int   g_nRecent;
#define RECENT_SECTION_CAP (RECENT_MAX * (PATH_CAP + 4) + 1)

void RecentLoad(void)
{
    WCHAR section[RECENT_SECTION_CAP], *p;
    int i, k;
    DWORD n = GetPrivateProfileSectionW(L"recent", section, COUNTOF(section), g_ini);
    g_nRecent = 0;
    if (n < COUNTOF(section) - 2) for (i = 1; i <= RECENT_MAX; i++) {
        for (p = section; *p; p += wlen(p) + 1) {
            if (p[0] != '0' + i || p[1] != '=' || !p[2]) continue;
            for (k = 0; k < g_nRecent; k++) if (wcmpi(g_recent[k], p + 2) == 0) break;
            if (k == g_nRecent) wcopy(g_recent[g_nRecent++], p + 2, PATH_CAP);
            break;
        }
    }
    MenuSetRecent((const WCHAR (*)[PATH_CAP])g_recent, g_nRecent);
}

static void RecentStore(void)
{
    WCHAR section[RECENT_SECTION_CAP] = { 0 }, *p = section;
    int i;
    IniEnsure();
    for (i = 0; i < g_nRecent; i++) {
        *p++ = (WCHAR)('1' + i); *p++ = '=';
        wcopy(p, g_recent[i], PATH_CAP);
        p += wlen(p) + 1;
    }
    /* One atomic section write prevents other windows observing a half-shifted list. */
    for (i = 0; i < 8; i++) {
        DWORD er;
        if (WritePrivateProfileSectionW(L"recent", section, g_ini)) break;
        er = GetLastError();
        if (er != 32 && er != 33) break;
        if (i < 7) Sleep(25);
    }
    MenuSetRecent((const WCHAR (*)[PATH_CAP])g_recent, g_nRecent);
}

void RecentAdd(const WCHAR *path)
{
    int i, k;
    if (!path || !path[0]) return;
    RecentLoad();
    for (i = 0; i < g_nRecent; i++) if (wcmpi(g_recent[i], path) == 0) break;
    if (i == g_nRecent) { if (g_nRecent < RECENT_MAX) g_nRecent++; i = g_nRecent - 1; }       /* new: it pushes the oldest out; known: it moves up */
    for (k = i; k > 0; k--) wcopy(g_recent[k], g_recent[k - 1], PATH_CAP);
    wcopy(g_recent[0], path, PATH_CAP);
    RecentStore();
}

void RecentRemove(const WCHAR *path)                  /* a file that can't be opened any more */
{
    int i;
    RecentLoad();
    for (i = 0; i < g_nRecent; i++) if (wcmpi(g_recent[i], path) == 0) break;
    if (i == g_nRecent) return;
    for (; i + 1 < g_nRecent; i++) wcopy(g_recent[i], g_recent[i + 1], PATH_CAP);
    g_nRecent--;
    RecentStore();
}

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* the editor colours come with the theme: g_pf.fg / bg are derived, never saved */
void ThemeUse(int theme)
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
    g_pf.pt      = IniGet(L"editor", L"size10", 0);             /* tenths of a point; a settings.ini of 1.0.8 or older has only whole points in "size" */
    if (!g_pf.pt) g_pf.pt = 10 * IniGet(L"editor", L"size", 10);
    g_pf.pt      = Clamp(g_pf.pt, FONT_MIN, FONT_MAX);
    g_pf.cur     = g_pf.pt;                                          /* the working size always starts at the chosen one */
    g_pf.bold    = IniGet(L"editor", L"bold", 0) != 0;
    g_pf.italic  = IniGet(L"editor", L"italic", 0) != 0;
    g_pf.wrap    = IniGet(L"editor", L"wrap", 0) != 0;
    g_pf.statusbar = IniGet(L"view", L"statusbar", 1) != 0;
    g_pf.tab     = IniGet(L"editor", L"tab", 4);
    if (g_pf.tab != 2 && g_pf.tab != 8) g_pf.tab = 4;                    /* only 2, 4 and 8 */
    GetPrivateProfileStringW(L"view", L"theme", L"dark", f, 32, g_ini);
    ThemeUse(wcmpi(f, L"light") == 0 ? THEME_LIGHT : THEME_DARK);       /* before any window exists: classes take g_brFace */
    g_pf.winx    = IniGet(L"window", L"x", 0);
    g_pf.winy    = IniGet(L"window", L"y", 0);
    g_pf.winw    = IniGet(L"window", L"w", 0);
    g_pf.winh    = IniGet(L"window", L"h", 0);
    g_pf.maximized = IniGet(L"window", L"maximized", 0) != 0;
    g_pf.matchCase  = IniGet(L"find", L"matchcase", 0) != 0;
    g_pf.wrapAround = IniGet(L"find", L"wraparound", 1) != 0;
    g_pf.wholeWord  = IniGet(L"find", L"wholeword", 0) != 0;
    g_pf.marginL = IniGet(L"page", L"left", g_pf.marginL);
    g_pf.marginT = IniGet(L"page", L"top", g_pf.marginT);
    g_pf.marginR = IniGet(L"page", L"right", g_pf.marginR);
    g_pf.marginB = IniGet(L"page", L"bottom", g_pf.marginB);
    g_saved = g_pf;
}

static void CapturePlacement(void)
{
    WINDOWPLACEMENT wp;
    if (!g_hwnd) return;
    wp.length = sizeof wp;
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    g_pf.maximized = wp.showCmd == SW_SHOWMAXIMIZED || (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & 2));   /* (WPF_RESTORETOMAXIMIZED: minimized from maximized) */
    g_pf.winx = wp.rcNormalPosition.left;
    g_pf.winy = wp.rcNormalPosition.top;
    g_pf.winw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    g_pf.winh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
}

void AppSavePrefs(void)
{
#define MINE(f) (g_pf.f != g_saved.f)
    CapturePlacement();
    IniEnsure();                                  /* utf-16 ini: font names can be anything */

    IniPutStr(L"editor", L"font", g_pf.font, wcmp(g_pf.font, g_saved.font) != 0);
    IniPutInt(L"editor", L"size10", g_pf.pt, MINE(pt));
    IniPutInt(L"editor", L"bold", g_pf.bold, MINE(bold));
    IniPutInt(L"editor", L"italic", g_pf.italic, MINE(italic));
    IniPutInt(L"editor", L"wrap", g_pf.wrap, MINE(wrap));
    IniPutInt(L"editor", L"tab", g_pf.tab, MINE(tab));
    IniPutInt(L"view", L"statusbar", g_pf.statusbar, MINE(statusbar));
    IniPutStr(L"view", L"theme", g_pf.theme == THEME_LIGHT ? L"light" : L"dark", MINE(theme));
    IniPutInt(L"window", L"x", g_pf.winx, MINE(winx));
    IniPutInt(L"window", L"y", g_pf.winy, MINE(winy));
    IniPutInt(L"window", L"w", g_pf.winw, MINE(winw));
    IniPutInt(L"window", L"h", g_pf.winh, MINE(winh));
    IniPutInt(L"window", L"maximized", g_pf.maximized, MINE(maximized));
    IniPutInt(L"find", L"matchcase", g_pf.matchCase, MINE(matchCase));
    IniPutInt(L"find", L"wraparound", g_pf.wrapAround, MINE(wrapAround));
    IniPutInt(L"find", L"wholeword", g_pf.wholeWord, MINE(wholeWord));
    IniPutInt(L"page", L"left", g_pf.marginL, MINE(marginL));
    IniPutInt(L"page", L"top", g_pf.marginT, MINE(marginT));
    IniPutInt(L"page", L"right", g_pf.marginR, MINE(marginR));
    IniPutInt(L"page", L"bottom", g_pf.marginB, MINE(marginB));
    g_saved = g_pf;
#undef MINE
}

/* the number behind the next unsaved document's name (DocNameNext): every window takes one from the same settings.ini ([name] last), so every
 * new document gets a higher name than the one before it, in any window. a named mutex keeps two windows that start together from taking the same one */
unsigned PrefsTakeDocName(unsigned clock)
{
    HANDLE mx = CreateMutexW(NULL, FALSE, L"notepad-mint-docname");
    DWORD wait = mx ? WaitForSingleObject(mx, 2000) : 1;      /* (0 = ours, 0x80 = abandoned by a crashed window: ours too) */
    unsigned v;
    WCHAR b[16];
    v = DocNameNext(clock, (unsigned)IniGet(L"name", L"last", -1));
    IniEnsure();
    wsprintfW(b, L"%u", v);
    IniPutStr(L"name", L"last", b, TRUE);
    if (wait == 0 || wait == 0x80) ReleaseMutex(mx);
    if (mx) CloseHandle(mx);
    return v;
}

/* Initialize persistence before any windows use theme colours or recent menus. */
void PrefsInit(void)
{
    IniLocate();
    PrefsLoad();
    RecentLoad();
}

void RecentClear(void)
{
    g_nRecent = 0;
    RecentStore();
}

BOOL RecentPath(int index, WCHAR *path, int cap)
{
    RecentLoad();
    if (index < 0 || index >= g_nRecent || !path || cap <= 0) return FALSE;
    if (wlen(g_recent[index]) >= cap) return FALSE;
    wcopy(path, g_recent[index], cap);
    return TRUE;
}
