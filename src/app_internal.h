#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H
#include "mp.h"

void PrefsInit(void);
void ThemeUse(int theme);
void RecentLoad(void);
void RecentAdd(const WCHAR *path);
void RecentRemove(const WCHAR *path);
void RecentClear(void);
BOOL RecentPath(int index, WCHAR *path, int cap);
unsigned PrefsTakeDocName(unsigned clock);

void AppMarkClean(void);

void AppStatusTimer(HWND hwnd, WPARAM timer);
#endif
