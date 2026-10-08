/* Application-private services; dialog/editor callers use mp.h instead. */
#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H
#include "mp.h"

/* prefs.c owns the INI location and refreshes shared history before mutations. */
void PrefsInit(void);
void ThemeUse(int theme);
void RecentLoad(void);
void RecentAdd(const WCHAR *path);
void RecentRemove(const WCHAR *path);
void RecentClear(void);
BOOL RecentPath(int index, WCHAR *path, int cap);

/* app_state.c: establish the baseline only after a successful load/save/new. */
void AppMarkClean(void);

/* app_status.c: consume the deferred statistics timer from MainProc. */
void AppStatusTimer(HWND h, WPARAM timer);
#endif
