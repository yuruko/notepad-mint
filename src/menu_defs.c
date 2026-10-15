#include "menu_internal.h"

#define SEP         { NULL, NULL, 0, NULL }
#define IT(l, a, i) { l, a, i, NULL }
#define SUB(l, d)   { l, NULL, 0, d }

static MenuItem g_recItem[RECENT_MAX + 2] = { { L"(none)", NULL, IDM_RECENT_NONE, NULL } };
static WCHAR    g_recLbl[RECENT_MAX][64];
MenuDef g_mdRecent = { g_recItem, 1 };

void MenuSetRecent(const WCHAR (*paths)[PATH_CAP], int count) {
    int i;
    if (count > RECENT_MAX) count = RECENT_MAX;
    if (count < 1) {
        g_recItem[0].label = L"(none)"; g_recItem[0].accel = NULL; g_recItem[0].id = IDM_RECENT_NONE; g_recItem[0].sub = NULL;
        g_mdRecent.n = 1;
        return;
    }
    for (i = 0; i < count; i++) {
        const WCHAR *path = paths[i];
        WCHAR *label = g_recLbl[i];
        int len = lstrlenW(path), j = 0, k;
        label[j++] = '&'; label[j++] = (WCHAR)('1' + i); label[j++] = ' '; label[j++] = ' ';
        for (k = 0; k < len && j < 56; k++) {
            if (len > 50 && k == 3) { label[j++] = '.'; label[j++] = '.'; label[j++] = '.'; k = len - 44; }
            if (path[k] == '&') label[j++] = '&';
            label[j++] = path[k];
        }
        label[j] = 0;
        g_recItem[i].label = label; g_recItem[i].accel = NULL; g_recItem[i].id = IDM_RECENT_BASE + i; g_recItem[i].sub = NULL;
    }
    g_recItem[count].label = NULL; g_recItem[count].accel = NULL; g_recItem[count].id = 0; g_recItem[count].sub = NULL;
    g_recItem[count + 1].label = L"&clear list"; g_recItem[count + 1].accel = NULL; g_recItem[count + 1].id = IDM_RECENT_CLEAR; g_recItem[count + 1].sub = NULL;
    g_mdRecent.n = count + 2;
}

static const MenuItem miFile[] = {
    IT(L"&new",          L"ctrl+n",       IDM_FILE_NEW),
    IT(L"new &window",   L"ctrl+shift+n", IDM_FILE_NEWWIN),
    IT(L"&open...",      L"ctrl+o",       IDM_FILE_OPEN),
    SUB(L"&recent",      &g_mdRecent),
    IT(L"&save",      L"ctrl+s",       IDM_FILE_SAVE),
    IT(L"save &as...",   L"ctrl+shift+s", IDM_FILE_SAVEAS),
    SEP,
    IT(L"page set&up...", NULL,           IDM_FILE_PAGESETUP),
    IT(L"&print...",     L"ctrl+p",       IDM_FILE_PRINT),
    SEP,
    IT(L"e&xit",         L"ctrl+w",       IDM_FILE_EXIT),
};
static const MenuItem miEdit[] = {
    IT(L"&undo",          L"ctrl+z",   IDM_EDIT_UNDO),
    IT(L"red&o",          L"ctrl+y",   IDM_EDIT_REDO),
    SEP,
    IT(L"cu&t",           L"ctrl+x",   IDM_EDIT_CUT),
    IT(L"&copy",          L"ctrl+c",   IDM_EDIT_COPY),
    IT(L"&paste",         L"ctrl+v",   IDM_EDIT_PASTE),
    IT(L"de&lete",        L"del",      IDM_EDIT_DELETE),
    IT(L"cl&ear line",    L"ctrl+k",   IDM_EDIT_CLEARLINE),
    SEP,
    IT(L"&find...",       L"ctrl+f",   IDM_EDIT_FIND),
    IT(L"find &next",     L"f3",       IDM_EDIT_FINDNEXT),
    IT(L"find pre&vious", L"shift+f3", IDM_EDIT_FINDPREV),
    IT(L"&replace...",    L"ctrl+h",   IDM_EDIT_REPLACE),
    IT(L"&go to...",      L"ctrl+g",   IDM_EDIT_GOTO),
    SEP,
    IT(L"select &all",    L"ctrl+a",   IDM_EDIT_SELALL),
    IT(L"time/&date",     L"f5",       IDM_EDIT_TIMEDATE),
};
static const MenuItem miEol[] = {
    IT(L"&windows (crlf)",    NULL, IDM_EOL_CRLF),
    IT(L"&unix (lf)",         NULL, IDM_EOL_LF),
    IT(L"&classic mac (cr)",  NULL, IDM_EOL_CR),
};
static const MenuItem miEnc[] = {
    IT(L"&utf8",           NULL, IDM_ENC_UTF8),
    IT(L"utf8 &bom",       NULL, IDM_ENC_UTF8BOM),
    IT(L"utf16 &le",       NULL, IDM_ENC_UTF16LE),
    IT(L"utf16 b&e",       NULL, IDM_ENC_UTF16BE),
    IT(L"&ansi",           NULL, IDM_ENC_ANSI),
    SEP,
    IT(L"&other code page...",       NULL, IDM_ENC_OTHER),
    IT(L"&reopen with encoding...",  NULL, IDM_ENC_REOPEN),
};
static const MenuItem miUcc[] = {
    IT(L"lrm   left-to-right mark",                     NULL, IDM_UCC_BASE + 0),
    IT(L"rlm   right-to-left mark",                     NULL, IDM_UCC_BASE + 1),
    IT(L"zwj   zero width joiner",                      NULL, IDM_UCC_BASE + 2),
    IT(L"zwnj  zero width non-joiner",                  NULL, IDM_UCC_BASE + 3),
    IT(L"lre   start of left-to-right embedding",       NULL, IDM_UCC_BASE + 4),
    IT(L"rle   start of right-to-left embedding",       NULL, IDM_UCC_BASE + 5),
    IT(L"lro   start of left-to-right override",        NULL, IDM_UCC_BASE + 6),
    IT(L"rlo   start of right-to-left override",        NULL, IDM_UCC_BASE + 7),
    IT(L"pdf   pop directional formatting",             NULL, IDM_UCC_BASE + 8),
    IT(L"nads  national digit shapes substitution",     NULL, IDM_UCC_BASE + 9),
    IT(L"nods  nominal (european) digit shapes",        NULL, IDM_UCC_BASE + 10),
    IT(L"ass   activate symmetric swapping",            NULL, IDM_UCC_BASE + 11),
    IT(L"iss   inhibit symmetric swapping",             NULL, IDM_UCC_BASE + 12),
    IT(L"aafs  activate arabic form shaping",           NULL, IDM_UCC_BASE + 13),
    IT(L"iafs  inhibit arabic form shaping",            NULL, IDM_UCC_BASE + 14),
    IT(L"rs    record separator",                       NULL, IDM_UCC_BASE + 15),
    IT(L"us    unit separator",                         NULL, IDM_UCC_BASE + 16),
};
static const MenuItem miZoom[] = {
    IT(L"zoom &in",               L"ctrl+plus",  IDM_ZOOM_IN),
    IT(L"zoom &out",              L"ctrl+minus", IDM_ZOOM_OUT),
    IT(L"&restore default zoom",  L"ctrl+0",     IDM_ZOOM_RESET),
};
static const MenuItem miTheme[] = {
    IT(L"&dark",               NULL, IDM_THEME_DARK),
    IT(L"&light",              NULL, IDM_THEME_LIGHT),
    SEP,
    IT(L"&toggle",             L"alt+x", IDM_THEME_TOGGLE),
};
static const MenuItem miTab[] = {
    IT(L"&2",                  NULL, IDM_TAB_2),
    IT(L"&4",                  NULL, IDM_TAB_4),
    IT(L"&8",                  NULL, IDM_TAB_8),
};
const MenuDef g_mdTab = { miTab, COUNTOF(miTab) };
static const MenuItem miFormat[] = {
    IT(L"&word wrap",          L"alt+z", IDM_FMT_WRAP),
    IT(L"&font...",            NULL, IDM_FMT_FONT),
    SUB(L"&tab size",          &g_mdTab),
    SEP,
    SUB(L"line &ending",       &g_mdEol),
    SUB(L"e&ncoding",          &g_mdEnc),
};
static const MenuDef mdTheme  = { miTheme,  COUNTOF(miTheme)  };
static const MenuItem miView[] = {
    SUB(L"&zoom",              &g_mdZoom),
    IT(L"&status bar",         L"ctrl+u", IDM_VIEW_STATUS),
    SUB(L"&theme",             &mdTheme),
};
static const MenuItem miHelp[] = {
    IT(L"&help topics",        L"f1", IDM_HELP_TOPICS),
    IT(L"set as &default text editor...", NULL, IDM_HELP_DEFAULT),
    SEP,
    IT(L"&about notepad mint", NULL, IDM_HELP_ABOUT),
};
static const MenuItem miCtx[] = {
    IT(L"&undo",       NULL, IDM_EDIT_UNDO),
    IT(L"red&o",       NULL, IDM_EDIT_REDO),
    SEP,
    IT(L"cu&t",        NULL, IDM_EDIT_CUT),
    IT(L"&copy",       NULL, IDM_EDIT_COPY),
    IT(L"&paste",      NULL, IDM_EDIT_PASTE),
    IT(L"de&lete",     NULL, IDM_EDIT_DELETE),
    SEP,
    IT(L"select &all", NULL, IDM_EDIT_SELALL),
    SEP,
    IT(L"&right to left reading order", NULL, IDM_RTL),
    SUB(L"&insert unicode control character", &g_mdUcc),
};

static const MenuItem miSys[] = {
    IT(L"&restore",    NULL,      IDM_SYS_RESTORE),
    IT(L"&move",       NULL,      IDM_SYS_MOVE),
    IT(L"&size",       NULL,      IDM_SYS_SIZE),
    SEP,
    IT(L"mi&nimize",   NULL,      IDM_SYS_MIN),
    IT(L"ma&ximize",   NULL,      IDM_SYS_MAX),
    SEP,
    IT(L"&close",      L"alt+f4", IDM_SYS_CLOSE),
};

const MenuDef g_mdSys     = { miSys,  COUNTOF(miSys)  };
const MenuDef g_mdUcc     = { miUcc,  COUNTOF(miUcc)  };
const MenuDef g_mdEol     = { miEol,  COUNTOF(miEol)  };
const MenuDef g_mdEnc     = { miEnc,  COUNTOF(miEnc)  };
const MenuDef g_mdZoom    = { miZoom, COUNTOF(miZoom) };
const MenuDef g_mdEditCtx = { miCtx,  COUNTOF(miCtx)  };
static const MenuDef mdFile   = { miFile,   COUNTOF(miFile)   };
static const MenuDef mdEdit   = { miEdit,   COUNTOF(miEdit)   };
static const MenuDef mdFormat = { miFormat, COUNTOF(miFormat) };
static const MenuDef mdView   = { miView,   COUNTOF(miView)   };
static const MenuDef mdHelp   = { miHelp,   COUNTOF(miHelp)   };

const MenuBarEntry g_menuEntries[MENU_BAR_COUNT] = {
    { L"&file",   &mdFile   },
    { L"&edit",   &mdEdit   },
    { L"f&ormat", &mdFormat },
    { L"&view",   &mdView   },
    { L"&help",   &mdHelp   },
};

const WCHAR *MenuAccelOf(int id) {
    static const struct { const MenuItem *items; int count; } l[] = { { miFormat, COUNTOF(miFormat) }, { miTheme, COUNTOF(miTheme) } };
    int i, j;
    for (i = 0; i < (int)COUNTOF(l); i++) for (j = 0; j < l[i].count; j++) if (l[i].items[j].id == id) return l[i].items[j].accel;
    return NULL;
}
