#include "mp.h"

typedef void *RegKey;
typedef LONG (WINAPI *RegCreateFn)(RegKey, LPCWSTR, DWORD, LPWSTR, DWORD, DWORD, void *, RegKey *, DWORD *);
typedef LONG (WINAPI *RegSetFn)(RegKey, LPCWSTR, DWORD, DWORD, const BYTE *, DWORD);
typedef LONG (WINAPI *RegCloseFn)(RegKey);
typedef LONG (WINAPI *RegGetFn)(RegKey, LPCWSTR, LPCWSTR, DWORD, DWORD *, void *, DWORD *);
#define HKLM_ROOT   ((RegKey)(ULONG_PTR)0x80000002)
#define RRF_SZ_64   (0x00000002 | 0x00010000)
#define HKCU_ROOT   ((RegKey)(ULONG_PTR)0x80000001)
#define KEY_SET     0x0002
#define RT_SZ       1
#define RT_EXPAND   2

const WCHAR *const g_assocExt[ASSOC_EXT_COUNT] = { L".txt", L".log", L".ini", L".cfg", L".conf", L".md", L".csv", L".nfo", L".diz", L".text" };

static RegCreateFn g_regCreate;
static RegSetFn    g_regSet;
static RegCloseFn  g_regClose;
static RegGetFn    g_regGet;

static BOOL Load(void) {
    HMODULE advapi32;
    if (g_regCreate && g_regSet && g_regClose && g_regGet) return TRUE;
    advapi32 = LoadLibraryW(L"advapi32.dll");
    if (!advapi32) return FALSE;
    g_regCreate = (RegCreateFn)GetProcAddress(advapi32, "RegCreateKeyExW");
    g_regSet = (RegSetFn)GetProcAddress(advapi32, "RegSetValueExW");
    g_regClose = (RegCloseFn)GetProcAddress(advapi32, "RegCloseKey");
    g_regGet = (RegGetFn)GetProcAddress(advapi32, "RegGetValueW");
    return g_regCreate && g_regSet && g_regClose && g_regGet;
}

static void VerbCommand(const WCHAR *exe, const WCHAR *tail, WCHAR *cmd, int cap) {
    wcopy(cmd, L"\"", cap); wcat(cmd, exe, cap); wcat(cmd, tail, cap);
}

static void OpenCommand(const WCHAR *exe, WCHAR *cmd, int cap) {
    VerbCommand(exe, L"\" \"%1\"", cmd, cap);
}

BOOL AssocMachineHas(const WCHAR *exe) {
    WCHAR want[PATH_CAP + 16], got[PATH_CAP + 16];
    DWORD size = sizeof got;
    if (!Load()) return FALSE;
    if (g_regGet(HKLM_ROOT, L"Software\\RegisteredApplications", ASSOC_REGAPP, RRF_SZ_64, NULL, got, &size)) return FALSE;
    size = sizeof got;
    if (g_regGet(HKLM_ROOT, L"Software\\Classes\\" ASSOC_PROGID L"\\shell\\open\\command", NULL, RRF_SZ_64, NULL, got, &size)) return FALSE;
    OpenCommand(exe, want, COUNTOF(want));
    return wcmpi(got, want) == 0;
}

static BOOL Put(const WCHAR *key, const WCHAR *name, DWORD type, const WCHAR *value) {
    RegKey keyHandle;
    LONG result;
    if (g_regCreate(HKCU_ROOT, key, 0, NULL, 0, KEY_SET, NULL, &keyHandle, NULL)) return FALSE;
    result = g_regSet(keyHandle, name, 0, type, (const BYTE *)value, (DWORD)(wlen(value) + 1) * sizeof(WCHAR));
    g_regClose(keyHandle);
    return result == 0;
}

BOOL AssocRegisterUser(const WCHAR *exe) {
    WCHAR cmd[PATH_CAP + 16], verb[PATH_CAP + 32], icon[PATH_CAP + 4], key[96];
    BOOL ok = TRUE;
    int i;
    if (!Load()) return FALSE;
    OpenCommand(exe, cmd, COUNTOF(cmd));
    wcopy(icon, exe, COUNTOF(icon)); wcat(icon, L",0", COUNTOF(icon));
    ok &= Put(L"Software\\Classes\\" ASSOC_PROGID, NULL, RT_SZ, L"text document");
    ok &= Put(L"Software\\Classes\\" ASSOC_PROGID L"\\DefaultIcon", NULL, RT_EXPAND, L"%SystemRoot%\\system32\\imageres.dll,-102");
    ok &= Put(L"Software\\Classes\\" ASSOC_PROGID L"\\shell\\open\\command", NULL, RT_SZ, cmd);
    VerbCommand(exe, L"\" /p \"%1\"", verb, COUNTOF(verb));
    ok &= Put(L"Software\\Classes\\" ASSOC_PROGID L"\\shell\\print\\command", NULL, RT_SZ, verb);
    VerbCommand(exe, L"\" /pt \"%1\" \"%2\" \"%3\" \"%4\"", verb, COUNTOF(verb));
    ok &= Put(L"Software\\Classes\\" ASSOC_PROGID L"\\shell\\printto\\command", NULL, RT_SZ, verb);
    ok &= Put(L"Software\\Classes\\Applications\\notepad-mint.exe", L"FriendlyAppName", RT_SZ, APP_NAME);
    ok &= Put(L"Software\\Classes\\Applications\\notepad-mint.exe\\shell\\open\\command", NULL, RT_SZ, cmd);
    ok &= Put(ASSOC_CAPS, L"ApplicationName", RT_SZ, APP_NAME);
    ok &= Put(ASSOC_CAPS, L"ApplicationDescription", RT_SZ, L"a small dark / light notepad");
    ok &= Put(ASSOC_CAPS, L"ApplicationIcon", RT_SZ, icon);
    for (i = 0; i < ASSOC_EXT_COUNT; i++) {
        wcopy(key, L"Software\\Classes\\", COUNTOF(key)); wcat(key, g_assocExt[i], COUNTOF(key)); wcat(key, L"\\OpenWithProgids", COUNTOF(key));
        ok &= Put(key, ASSOC_PROGID, RT_SZ, L"");
        ok &= Put(L"Software\\Classes\\Applications\\notepad-mint.exe\\SupportedTypes", g_assocExt[i], RT_SZ, L"");
        ok &= Put(ASSOC_CAPS L"\\FileAssociations", g_assocExt[i], RT_SZ, ASSOC_PROGID);
    }
    ok &= Put(L"Software\\RegisteredApplications", ASSOC_REGAPP, RT_SZ, ASSOC_CAPS);
    return ok;
}
