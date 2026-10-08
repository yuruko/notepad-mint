/* Private bridge from declarative menu data to the menu windows. */
#ifndef MENU_INTERNAL_H
#define MENU_INTERNAL_H
#include "mp.h"

enum { MENU_BAR_COUNT = 5 };
typedef struct { const WCHAR *title; const MenuDef *def; } MenuBarEntry;
extern const MenuBarEntry g_menuEntries[MENU_BAR_COUNT];
const WCHAR *MenuAccelOf(int id);
#endif
