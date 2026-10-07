/* menu.c - hand-drawn menu bar + popup menus (classic 2000-era look, dark or light + mint).
 *
 * no native menus anywhere: the bar is a child window, popups are tiny
 * WS_EX_NOACTIVATE windows, and a private message loop drives hover / keyboard /
 * mnemonics / submenus. keeps the app window itself a plain native frame.
 * the bar also carries two buttons at its right end (word wrap; the theme: a sun or a moon), each with a tooltip. */
#include "mp.h"

/* ---------------------------------------------------------- definitions -- */
#define SEP         { NULL, NULL, 0, NULL }
#define IT(l, a, i) { l, a, i, NULL }
#define SUB(l, d)   { l, NULL, 0, d }

/* file > recent: the items are rebuilt by MenuSetRecent whenever the list changes. a label is "&1  " + the path (shortened in the middle when long: the
 * drive, "..." and the end), with every & doubled so that a path never makes a mnemonic. a list with files ends with a separator and "clear list" */
static MenuItem g_recItem[RECENT_MAX + 2] = { { L"(none)", NULL, IDM_RECENT_NONE, NULL } };
static WCHAR    g_recLbl[RECENT_MAX][64];
MenuDef g_mdRecent = { g_recItem, 1 };

void MenuSetRecent(const WCHAR (*paths)[PATH_CAP], int n)
{
    int i;
    if (n > RECENT_MAX) n = RECENT_MAX;
    if (n < 1) {
        g_recItem[0].label = L"(none)"; g_recItem[0].accel = NULL; g_recItem[0].id = IDM_RECENT_NONE; g_recItem[0].sub = NULL;
        g_mdRecent.n = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        const WCHAR *p = paths[i];
        WCHAR *o = g_recLbl[i];
        int len = lstrlenW(p), j = 0, k;
        o[j++] = '&'; o[j++] = (WCHAR)('1' + i); o[j++] = ' '; o[j++] = ' ';
        for (k = 0; k < len && j < 56; k++) {
            if (len > 50 && k == 3) { o[j++] = '.'; o[j++] = '.'; o[j++] = '.'; k = len - 44; }      /* long: the first three characters, "...", the last 44 */
            if (p[k] == '&') o[j++] = '&';
            o[j++] = p[k];
        }
        o[j] = 0;
        g_recItem[i].label = o; g_recItem[i].accel = NULL; g_recItem[i].id = IDM_RECENT_BASE + i; g_recItem[i].sub = NULL;
    }
    g_recItem[n].label = NULL; g_recItem[n].accel = NULL; g_recItem[n].id = 0; g_recItem[n].sub = NULL;                  /* the separator */
    g_recItem[n + 1].label = L"&clear list"; g_recItem[n + 1].accel = NULL; g_recItem[n + 1].id = IDM_RECENT_CLEAR; g_recItem[n + 1].sub = NULL;
    g_mdRecent.n = n + 2;
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
static const MenuItem miUcc[] = {                       /* same set the stock edit control offers */
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
    IT(L"&help topics",        NULL, IDM_HELP_TOPICS),
    SEP,
    IT(L"&about notepad mint", NULL, IDM_HELP_ABOUT),
};
static const MenuItem miCtx[] = {
    IT(L"&undo",       NULL, IDM_EDIT_UNDO),
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

static const MenuItem miSys[] = {                       /* the window menu behind the title bar icon / right click */
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

typedef struct { const WCHAR *title; const MenuDef *def; } BarEntry;
static const BarEntry g_ent[] = {
    { L"&file",   &mdFile   },
    { L"&edit",   &mdEdit   },
    { L"f&ormat", &mdFormat },
    { L"&view",   &mdView   },
    { L"&help",   &mdHelp   },
};
#define NBAR COUNTOF(g_ent)

/* --------------------------------------------------------------- state -- */
#define MAXLV 3
#define MAXIT 24
#define BW    2                                  /* popup border width (device px) */

typedef struct Pop {
    HWND hwnd;
    const MenuDef *def;
    int sel, subOf, x, y, w, h, n, gl;
    int top[MAXIT], hgt[MAXIT];
} Pop;

static Pop         g_pop[MAXLV];
static int         g_np;
static HWND        g_bar, g_owner;
static MenuStateFn g_stateFn;
static int         g_hot = -1, g_open = -1, g_kbd = -1, g_barIdx = -1, g_barH;
static int         g_hotBtn = -1;                        /* the button (BT_*) under the pointer */
static int         g_itemL[NBAR + 1];
static BOOL        g_active, g_done, g_cancel;
static int         g_result;
static MSG         g_redisp;
static BOOL        g_haveRedisp;

/* -------------------------------------------------------------- helpers - */
static void Strip(const WCHAR *src, WCHAR *dst, int cap)
{
    int j = 0;
    for (; *src && j < cap - 1; src++) {
        if (*src == '&') {
            if (src[1] == '&') { dst[j++] = '&'; src++; }
            continue;
        }
        dst[j++] = *src;
    }
    dst[j] = 0;
}

static WCHAR Mnem(const WCHAR *s)
{
    for (; *s; s++) {
        if (*s == '&') {
            if (s[1] == '&') s++;
            else if (s[1]) return wlow(s[1]);
        }
    }
    return 0;
}

static unsigned ItemState(const MenuItem *it)
{
    if (it->sub || !g_stateFn) return 0;
    return g_stateFn(it->id);
}

static BOOL Selectable(const Pop *p, int i)
{
    const MenuItem *it;
    if (i < 0 || i >= p->n) return FALSE;
    it = &p->def->items[i];
    if (!it->label) return FALSE;
    return !(ItemState(it) & MS_GRAY);
}

/* ------------------------------------------------------------- menu bar - */
static void BarLayout(void)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    WCHAR buf[64];
    int i, x = 0;                                        /* the first item starts at the left edge: no margin */
    GetTextMetricsW(dc, &tm);
    g_barH = tm.tmHeight + S(9);
    for (i = 0; i < (int)NBAR; i++) {
        Strip(g_ent[i].title, buf, 64);
        g_itemL[i] = x;
        x += TextW(dc, buf, -1) + S(18);
    }
    g_itemL[NBAR] = x;
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
}

int MenuBarHeight(void)
{
    if (!g_barH) BarLayout();
    return g_barH;
}

void MenuBarRefont(HWND bar)
{
    BarLayout();
    InvalidateRect(bar, NULL, FALSE);
}

static int BarHit(int x, int y)
{
    int i;
    if (y < 0 || y >= g_barH) return -1;
    for (i = 0; i < (int)NBAR; i++)
        if (x >= g_itemL[i] && x < g_itemL[i + 1]) return i;
    return -1;
}

int MenuBarMnemonic(WCHAR ch)
{
    int i;
    ch = wlow(ch);
    for (i = 0; i < (int)NBAR; i++)
        if (Mnem(g_ent[i].title) == ch) return i;
    return -1;
}

/* ---------------------------------------------- theme button (right end) -- */
/* the theme button, the very right one of the bar's two buttons (word wrap is left of it), switches the theme (the same command as alt+x / view > theme > toggle). its icon shows the theme in
 * use: a sun in the light theme, a moon in the dark one, so a click just swaps the two icons. no frame and no sunken state. at rest the icon is
 * drawn at BAR_BTN_OPACITY percent over the bar; hover = the accent as the background with the icon at full strength, like the menu items. a click
 * acts on the press, like the menu items do. no bitmaps: the icons are two shapes worked out here pixel by pixel (GDI has no antialiased shapes, and
 * stretching a big drawing down with halftone mode leaves a halo around it) */
enum { TB_LIGHT, TB_DARK, TB_WRAP };                           /* the icons: sun, moon (the theme button), wrapped text (the word wrap button) */
enum { BT_WRAP, BT_THEME, BT_COUNT };                          /* the buttons, left to right: the theme button is the last, flush with the right edge */
static const struct { int id; const WCHAR *tip; } g_btn[BT_COUNT] = {   /* per button: the command a click runs and what its tooltip says (the key combo is added from the menu item that runs the same command); indexed by BT_* so that the order above is the only place that says which is left */
    [BT_WRAP]  = { IDM_FMT_WRAP,     L"toggle word wrap" },
    [BT_THEME] = { IDM_THEME_TOGGLE, L"toggle dark / light theme" },
};
#define ICON_SS  8                                             /* every icon pixel is sampled ICON_SS x ICON_SS times: its coverage is the share inside the shape */
#define ICON_MAX 64                                            /* the biggest icon box in device pixels (a bigger one is drawn at this size) */

int MenuBarMinWidth(void)
{
    if (!g_barH) BarLayout();
    return g_itemL[NBAR] + BT_COUNT * S(BAR_BTN_W);
}

static void BtnRect(int cw, int b, RECT *r)                    /* right aligned */
{
    r->left = cw - (BT_COUNT - b) * S(BAR_BTN_W);
    r->right = r->left + S(BAR_BTN_W);
    r->top = 0;
    r->bottom = g_barH - S(1);                                 /* (as tall as the menu items) */
}

static int BtnHit(HWND h, int x, int y)                        /* the button under the point, or -1 */
{
    RECT rc, r;
    int b;
    if (y < 0 || y >= g_barH) return -1;
    GetClientRect(h, &rc);
    for (b = 0; b < BT_COUNT; b++) {
        BtnRect(rc.right, b, &r);
        if (x >= r.left && x < r.right) return b;
    }
    return -1;
}

/* is the point (x, y) inside the icon's shape? the shapes live on a 56 x 56 grid; x and y are in 1/64 of a grid unit (so a 12 px icon has about 4.7 units per pixel).
 * the sun: a disc with eight short rays around it (four straight, four diagonal and a little shorter), centred in the box.
 * the moon: a disc minus a smaller one over its upper right */
#define D(v) ((v) * 64)
static int IconInside(int kind, int x, int y)
{
    int dx = x - D(28), dy = y - D(28), ax, ay, bx, by;
    if (kind == TB_WRAP) {                                       /* wrapped text: a line, a line that turns down and comes back as an arrow, a short line. PX(k) = the left / top edge of pixel k of a 12 px icon (it is moved half a pixel, see ThemeIcon) */
#define PX(k) ((k) * 299 - 149)
        if (y >= PX(1) && y < PX(2) && x >= PX(1) && x < PX(11)) return 1;                         /* the first line */
        if (y >= PX(3) && y < PX(4) && x >= PX(1) && x < PX(10)) return 1;                         /* the second line ... */
        if (x >= PX(9) && x < PX(10) && y >= PX(3) && y < PX(7)) return 1;                         /* ... turns down at its end ... */
        if (y >= PX(6) && y < PX(7) && x >= PX(3) && x < PX(10)) return 1;                         /* ... and runs back left ... */
        ax = x - PX(2); ay = y - (PX(6) + 150); if (ay < 0) ay = -ay;
        if (ax >= 0 && x < PX(5) && ay * 2 <= ax + 150) return 1;                            /* ... into an arrow head */
        return y >= PX(9) && y < PX(10) && x >= PX(1) && x < PX(6);                                /* the third line, short */
#undef PX
    }
    if (kind == TB_LIGHT) {
        if (dx * dx + dy * dy < D(11) * D(11)) return 1;         /* the disc: centre (28, 28), radius 11 */
        if (dx < 0) dx = -dx;                                    /* (the rays are symmetric: look at one quarter) */
        if (dy < 0) dy = -dy;
        if (dx < 154 && dy >= 1043 && dy < D(25)) return 1;     /* the straight rays: 4.8 wide (one pixel at 12 px), from 16.3 to 25 away from the centre ... */
        if (dy < 154 && dx >= 1043 && dx < D(25)) return 1;
        return dx - dy < 217 && dy - dx < 217 && dx + dy >= 1448 && dx + dy < 2037;                       /* ... and the diagonal ones, as wide, from 16 to 22.5 */
    }
    ax = x - D(26); ay = y - D(30); bx = x - D(38); by = y - D(22);
    return ax * ax + ay * ay < D(22) * D(22) && bx * bx + by * by >= D(19) * D(19);       /* disc (26, 30) r 22 without disc (38, 22) r 19 */
}
#undef D

/* a sun (TB_LIGHT) or a crescent moon (TB_DARK) in a size x size box at x, y: fg over bg at `pct` percent opacity (bg is what is under it already: only the
 * pixels the shape touches are drawn). antialiased by area: each pixel takes the share of its ICON_SS x ICON_SS sample points that are inside the shape,
 * times pct, as the share of fg in the mix */
static void ThemeIcon(HDC dc, int kind, int x, int y, int size, COLORREF fg, COLORREF bg, int pct)
{
    int g[ICON_MAX * ICON_SS], n, i, j, a, b, cov, num, den = ICON_SS * ICON_SS * 100, shift;
    if (size > ICON_MAX) size = ICON_MAX;
    n = size * ICON_SS;
    shift = (size & 1) ? 0 : 1792 / size;                       /* an even size puts the middle of the 56 grid on the line between two pixels: the picture moves half a pixel right and down, so the middle is the middle of a pixel (the sun's straight rays are then one crisp pixel wide, not two faint ones) */
    for (i = 0; i < n; i++) g[i] = (2 * i + 1) * 56 * 32 / n - shift;   /* the middle of sample i, in 1/64 grid units (the same for x and y) */
    for (j = 0; j < size; j++) {
        for (i = 0; i < size; i++) {
            cov = 0;
            for (b = 0; b < ICON_SS; b++)
                for (a = 0; a < ICON_SS; a++) cov += IconInside(kind, g[i * ICON_SS + a], g[j * ICON_SS + b]);
            if (!cov) continue;
            num = cov * pct;                                    /* the share of fg is num / den */
            SetPixelV(dc, x + i, y + j, num >= den ? fg : RGB(GetRValue(bg) + (GetRValue(fg) - GetRValue(bg)) * num / den,
                                                              GetGValue(bg) + (GetGValue(fg) - GetGValue(bg)) * num / den,
                                                              GetBValue(bg) + (GetBValue(fg) - GetBValue(bg)) * num / den));
        }
    }
}

static void BarPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mdc;
    HBITMAP bmp;
    HGDIOBJ old, of;
    RECT rc, r;
    int i, isz = S(BAR_BTN_ICON);

    GetClientRect(h, &rc);
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old = SelectObject(mdc, bmp);
    of = SelectObject(mdc, g_fontMenu);

    FillC(mdc, &rc, C_FACE);                                   /* no underline of its own: the editor's sunken frame right below is the one border */

    for (i = 0; i < (int)NBAR; i++) {
        BOOL on = (i == g_open || i == g_kbd || i == g_hot);   /* open, keyboard-selected or hovered: the accent as the BACKGROUND */
        r.left = g_itemL[i]; r.right = g_itemL[i + 1];
        r.top = 0; r.bottom = rc.bottom - S(1);                /* (no margin above the items either) */
        if (on) FillC(mdc, &r, C_ACCENT);
        TextC(mdc, g_ent[i].title, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, on ? C_ON_ACCENT : C_TEXT);
    }
    for (i = 0; i < BT_COUNT; i++) {                           /* the buttons: BAR_BTN_OPACITY percent opaque unless hovered; the word wrap one is BAR_BTN_OPACITY_ON while word wrap is on */
        BOOL hot = (i == g_hotBtn);
        int kind = i == BT_WRAP ? TB_WRAP : (ThemeGet() == THEME_LIGHT ? TB_LIGHT : TB_DARK);        /* the theme button shows the theme in use */
        int pct = (i == BT_WRAP && g_pf.wrap) ? BAR_BTN_OPACITY_ON : BAR_BTN_OPACITY;
        BtnRect(rc.right, i, &r);
        if (hot) FillC(mdc, &r, C_ACCENT);
        ThemeIcon(mdc, kind, (r.left + r.right - isz) / 2, (r.top + r.bottom - isz) / 2 + (i == BT_WRAP ? S(BAR_WRAP_ICON_DY) : 0), isz,
                  hot ? C_ON_ACCENT : C_TEXT, hot ? C_ACCENT : C_FACE, hot ? 100 : pct);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    EndPaint(h, &ps);
}

/* --------------------------------------------------------------- tooltips -- */
/* the two bar buttons say what they do: once the pointer has rested BAR_TIP_DELAY ms on one, a small popup (class mp_tip) shows right under it, its right edge on
 * the button's: "toggle word wrap (alt+z)". our own window, not the system's tooltip control: that one lives in comctl32 (a 4th dll) and would ignore the palette.
 * it looks like a menu popup (the same fill, bevel and font), never takes the mouse or the focus, and goes away when the pointer leaves the button, on a click,
 * when a menu opens, after BAR_TIP_SHOW ms, or with its owner. the bar's one timer (TIP_TIMER) first times the delay, then how long the tip stays. while a tip is
 * up, moving to the other button shows that button's tip at once. the key combo is read from the menu item that runs the same command, so it can not drift */
#define TIP_TIMER 1
static HWND g_tip;                                             /* the tip window while one is up */

static const WCHAR *AccelOf(int id)                            /* the key combo the menus show for a command (NULL: none) */
{
    static const struct { const MenuItem *it; int n; } l[] = { { miFormat, COUNTOF(miFormat) }, { miTheme, COUNTOF(miTheme) } };
    int i, j;
    for (i = 0; i < (int)COUNTOF(l); i++)
        for (j = 0; j < l[i].n; j++)
            if (l[i].it[j].id == id) return l[i].it[j].accel;
    return NULL;
}

static void TipHide(void)                                      /* no tip up, none pending */
{
    if (g_bar) KillTimer(g_bar, TIP_TIMER);
    if (g_tip) DestroyWindow(g_tip);
    g_tip = NULL;
}

static void TipShow(int b)                                     /* the tip of button b: right under the bar, right aligned to the button */
{
    WCHAR t[96];
    const WCHAR *a = AccelOf(g_btn[b].id);
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    MONITORINFO mi;
    RECT r;
    POINT pt;
    int w, h;

    wcopy(t, g_btn[b].tip, 96);
    if (a) { wcat(t, L" (", 96); wcat(t, a, 96); wcat(t, L")", 96); }
    GetTextMetricsW(dc, &tm);
    w = TextW(dc, t, -1) + 2 * (BW + S(7));
    h = tm.tmHeight + 2 * (BW + S(3));
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);

    GetClientRect(g_bar, &r);
    BtnRect(r.right, b, &r);
    pt.x = r.right - w; pt.y = g_barH + S(2);
    ClientToScreen(g_bar, &pt);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    if (pt.x + w > mi.rcWork.right) pt.x = mi.rcWork.right - w;
    if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
    g_tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"mp_tip", t, WS_POPUP, pt.x, pt.y, w, h, GetParent(g_bar), NULL, g_hinst, NULL);
    ShowWindow(g_tip, SW_SHOWNOACTIVATE);
    SetTimer(g_bar, TIP_TIMER, BAR_TIP_SHOW, NULL);            /* (the same timer: from now on it times how long the tip stays) */
}

static void TipTrack(int b)                                    /* the pointer is over button b now (-1: over none) */
{
    BOOL was = g_tip != NULL;
    TipHide();
    if (b < 0 || g_active) return;                             /* (no tips while a menu is open) */
    if (was) TipShow(b);                                       /* a tip was up: the next one shows at once ... */
    else SetTimer(g_bar, TIP_TIMER, BAR_TIP_DELAY, NULL);      /* ... else after the pointer has rested on the button for a moment */
}

static LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NCHITTEST:
        return (LRESULT)-1;                                    /* HTTRANSPARENT: the pointer goes through to the bar, the tip never takes the mouse */
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps), mdc;
        HBITMAP bmp;
        HGDIOBJ old, of;
        RECT rc, tr;
        WCHAR t[96];
        int n = GetWindowTextW(h, t, 96);
        GetClientRect(h, &rc);
        mdc = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = SelectObject(mdc, bmp);
        of = SelectObject(mdc, g_fontMenu);
        FillC(mdc, &rc, C_FACE2);
        Bevel(mdc, &rc, BV_RAISED);
        tr = rc; tr.left = BW + S(7);
        TextC(mdc, t, n, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, C_TEXT);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, of);
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

/* -------------------------------------------------------------- popups -- */
static void PopMeasure(Pop *p)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, g_fontMenu);
    TEXTMETRICW tm;
    WCHAR buf[64];
    int i, wl = 0, wa = 0, y, t, ih;

    GetTextMetricsW(dc, &tm);
    ih = tm.tmHeight + S(8);                           /* item height follows the font */
    if (ih < S(22)) ih = S(22);
    p->gl = ih + S(2);
    p->n = p->def->n > MAXIT ? MAXIT : p->def->n;
    y = BW + S(2);
    for (i = 0; i < p->n; i++) {
        const MenuItem *it = &p->def->items[i];
        p->top[i] = y;
        if (!it->label) {
            p->hgt[i] = S(7);
        } else {
            p->hgt[i] = ih;
            Strip(it->label, buf, 64);
            t = TextW(dc, buf, -1);
            if (t > wl) wl = t;
            if (it->accel) {
                t = TextW(dc, it->accel, -1);
                if (t > wa) wa = t;
            }
        }
        y += p->hgt[i];
    }
    p->h = y + S(2) + BW;
    p->w = BW + p->gl + wl + (wa ? S(28) + wa : 0) + S(26) + BW;
    if (p->w < S(120)) p->w = S(120);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
}

static void PopDraw(Pop *p, HDC dc)
{
    RECT rc, r, a;
    int i;
    rc.left = 0; rc.top = 0; rc.right = p->w; rc.bottom = p->h;
    FillC(dc, &rc, C_FACE2);
    Bevel(dc, &rc, BV_RAISED);
    SelectObject(dc, g_fontMenu);

    for (i = 0; i < p->n; i++) {
        const MenuItem *it = &p->def->items[i];
        r.left = BW; r.right = p->w - BW; r.top = p->top[i]; r.bottom = p->top[i] + p->hgt[i];
        if (!it->label) {                                  /* separator: etched line */
            a.left = BW + S(3); a.right = p->w - BW - S(3);
            a.top = r.top + p->hgt[i] / 2 - 1; a.bottom = a.top + 1;
            FillC(dc, &a, C_LO2);
            a.top++; a.bottom++;
            FillC(dc, &a, C_HI2);
            continue;
        }
        {
            unsigned st = ItemState(it);
            BOOL gray = (st & MS_GRAY) ? TRUE : FALSE;
            BOOL sel = (i == p->sel) && !gray;
            COLORREF fg = gray ? C_DIM : (sel ? C_ON_ACCENT : C_TEXT);     /* text, marks and the submenu arrow alike */
            RECT tr;

            if (sel) FillC(dc, &r, C_ACCENT);
            if (st & MS_CHECK) {
                if (st & MS_RADIO) {
                    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
                    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
                    int cx = r.left + p->gl / 2, cy = r.top + p->hgt[i] / 2, rr = S(3);
                    SetDCBrushColor(dc, fg);
                    Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
                    SelectObject(dc, op);
                    SelectObject(dc, ob);
                } else {
                    CheckGlyph(dc, r.left + (p->gl - S(13)) / 2 + S(1), r.top + (p->hgt[i] - S(13)) / 2, fg);
                }
            }
            tr = r; tr.left = BW + p->gl;
            TextC(dc, it->label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE, fg);
            if (it->accel) {
                tr = r; tr.right = p->w - BW - S(22);
                TextC(dc, it->accel, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, fg);
            }
            if (it->sub) Tri(dc, p->w - BW - S(14), r.top + p->hgt[i] / 2 - S(4), S(4), 0, fg);
        }
    }
}

static LRESULT CALLBACK PopProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    Pop *p = (Pop *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        break;                                             /* DefWindowProcW still has to see it */
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps), mdc;
        HBITMAP bmp;
        HGDIOBJ old;
        if (p) {
            mdc = CreateCompatibleDC(dc);
            bmp = CreateCompatibleBitmap(dc, p->w, p->h);
            old = SelectObject(mdc, bmp);
            PopDraw(p, mdc);
            BitBlt(dc, 0, 0, p->w, p->h, mdc, 0, 0, SRCCOPY);
            SelectObject(mdc, old);
            DeleteObject(bmp);
            DeleteDC(mdc);
        }
        EndPaint(h, &ps);
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

static void ClosePops(int level)
{
    int i;
    for (i = g_np - 1; i >= level; i--) {
        if (g_pop[i].hwnd) DestroyWindow(g_pop[i].hwnd);
        g_pop[i].hwnd = NULL;
    }
    if (level < g_np) g_np = level;
    if (level > 0 && level <= MAXLV) g_pop[level - 1].subOf = -1;
}

static void PopOpen(int lv, const MenuDef *def, int x, int y, int anchorBottom, int altX)
{
    Pop *p = &g_pop[lv];
    POINT pt;
    MONITORINFO mi;
    RECT wa;

    memset(p, 0, sizeof *p);
    p->def = def; p->sel = -1; p->subOf = -1;
    PopMeasure(p);

    if (anchorBottom) y -= p->h;
    pt.x = x; pt.y = y;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    wa = mi.rcWork;
    if (x + p->w > wa.right) x = altX ? altX - p->w : wa.right - p->w;   /* submenus flip to the left */
    if (x < wa.left) x = wa.left;
    if (y + p->h > wa.bottom) y = wa.bottom - p->h;
    if (y < wa.top) y = wa.top;
    p->x = x; p->y = y;

    p->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"mp_popup", NULL, WS_POPUP,
                              x, y, p->w, p->h, g_owner, NULL, g_hinst, p);
    ShowWindow(p->hwnd, SW_SHOWNOACTIVATE);
    g_np = lv + 1;
}

static void SetSel(int lv, int idx, BOOL openSub)
{
    Pop *p = &g_pop[lv];
    if (p->sel != idx) {
        p->sel = idx;
        InvalidateRect(p->hwnd, NULL, FALSE);
    }
    if (g_np > lv + 1 && idx != p->subOf) ClosePops(lv + 1);
    if (openSub && idx >= 0 && p->def->items[idx].sub && Selectable(p, idx) && p->subOf != idx && lv + 1 < MAXLV) {
        p->subOf = idx;
        PopOpen(lv + 1, p->def->items[idx].sub, p->x + p->w - S(4), p->y + p->top[idx] - S(3), 0, p->x + S(4));
        g_pop[lv].subOf = idx;
    }
}

static void MoveSel(int lv, int dir)
{
    Pop *p = &g_pop[lv];
    int i = p->sel, k;
    for (k = 0; k < p->n; k++) {
        i += dir;
        if (i >= p->n) i = 0;
        if (i < 0) i = p->n - 1;
        if (Selectable(p, i)) { SetSel(lv, i, FALSE); return; }
    }
}

static void OpenBarPopup(int idx, BOOL selectFirst)
{
    POINT pt;
    pt.x = g_itemL[idx]; pt.y = g_barH;
    ClientToScreen(g_bar, &pt);
    ClosePops(0);
    g_barIdx = idx; g_open = idx; g_kbd = -1;
    PopOpen(0, g_ent[idx].def, pt.x, pt.y - 1, 0, 0);
    if (selectFirst) MoveSel(0, 1);
    InvalidateRect(g_bar, NULL, FALSE);
}

/* pops a leaf item (run it) or a submenu item (open it, selecting its first entry) */
static void Activate(int lv, int idx)
{
    Pop *p = &g_pop[lv];
    const MenuItem *it;
    if (!Selectable(p, idx)) return;
    it = &p->def->items[idx];
    if (it->sub) {
        SetSel(lv, idx, TRUE);
        if (g_np > lv + 1) MoveSel(lv + 1, 1);
    } else {
        g_result = it->id;
        g_done = TRUE;
    }
}

/* screen point -> (level, item). returns level or -1 */
static int PopHit(POINT pt, int *item)
{
    int lv, i;
    for (lv = g_np - 1; lv >= 0; lv--) {
        Pop *p = &g_pop[lv];
        if (pt.x >= p->x && pt.x < p->x + p->w && pt.y >= p->y && pt.y < p->y + p->h) {
            int cy = pt.y - p->y;
            *item = -1;
            for (i = 0; i < p->n; i++)
                if (cy >= p->top[i] && cy < p->top[i] + p->hgt[i]) { *item = i; break; }
            return lv;
        }
    }
    return -1;
}

static int BarHitScreen(POINT pt)
{
    RECT rc;
    if (!g_bar || g_barIdx < 0) return -1;
    GetWindowRect(g_bar, &rc);
    if (pt.x < rc.left || pt.x >= rc.right || pt.y < rc.top || pt.y >= rc.bottom) return -1;
    return BarHit(pt.x - rc.left, pt.y - rc.top);
}

static void SwitchBar(int idx, BOOL selectFirst)
{
    if (idx < 0) idx = (int)NBAR - 1;
    if (idx >= (int)NBAR) idx = 0;
    OpenBarPopup(idx, selectFirst);
}

static void MnemonicKey(WCHAR ch)
{
    ch = wlow(ch);
    if (g_np == 0) {
        int i = MenuBarMnemonic(ch);
        if (i >= 0 && g_barIdx >= 0) OpenBarPopup(i, TRUE);
    } else {
        int lv = g_np - 1, i, hits = 0, first = -1, next = -1;
        Pop *p = &g_pop[lv];
        for (i = 0; i < p->n; i++) {
            if (!Selectable(p, i) || Mnem(p->def->items[i].label) != ch) continue;
            hits++;
            if (first < 0) first = i;
            if (next < 0 && i > p->sel) next = i;
        }
        if (hits == 1) Activate(lv, first);
        else if (hits > 1) SetSel(lv, next >= 0 ? next : first, FALSE);
    }
}

static void KeyNav(WPARAM vk)
{
    int lv = g_np - 1;
    switch (vk) {
    case VK_ESCAPE:
        if (g_np > 1) { ClosePops(g_np - 1); }
        else if (g_np == 1 && g_barIdx >= 0) { ClosePops(0); g_open = -1; g_kbd = g_barIdx; InvalidateRect(g_bar, NULL, FALSE); }
        else g_done = TRUE;
        break;
    case VK_MENU:
    case VK_F10:
        g_done = TRUE;
        break;
    case VK_DOWN:
        if (g_np == 0) { if (g_kbd >= 0) OpenBarPopup(g_kbd, TRUE); }
        else MoveSel(lv, 1);
        break;
    case VK_UP:
        if (g_np > 0) MoveSel(lv, -1);
        break;
    case VK_HOME:
        if (g_np > 0) { g_pop[lv].sel = -1; MoveSel(lv, 1); }
        break;
    case VK_END:
        if (g_np > 0) { g_pop[lv].sel = g_pop[lv].n; MoveSel(lv, -1); }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (g_np == 0) { if (g_kbd >= 0) OpenBarPopup(g_kbd, TRUE); }
        else if (g_pop[lv].sel >= 0) Activate(lv, g_pop[lv].sel);
        break;
    case VK_RIGHT:
        if (g_np == 0) { if (g_kbd >= 0) { g_kbd = (g_kbd + 1) % (int)NBAR; InvalidateRect(g_bar, NULL, FALSE); } }
        else if (g_pop[lv].sel >= 0 && g_pop[lv].def->items[g_pop[lv].sel].sub) Activate(lv, g_pop[lv].sel);
        else if (g_np == 1 && g_barIdx >= 0) SwitchBar(g_barIdx + 1, TRUE);
        break;
    case VK_LEFT:
        if (g_np == 0) { if (g_kbd >= 0) { g_kbd = (g_kbd + (int)NBAR - 1) % (int)NBAR; InvalidateRect(g_bar, NULL, FALSE); } }
        else if (g_np > 1) ClosePops(g_np - 1);
        else if (g_barIdx >= 0) SwitchBar(g_barIdx - 1, TRUE);
        break;
    }
}

/* ---------------------------------------------------------- the loop ---- */
static void RunMenu(HWND owner, int barIdx, int openPopup, const MenuDef *root, int x, int y, int anchorBottom)
{
    MSG msg;
    int i;

    DBG(L"runmenu", barIdx, g_active);
    if (g_active) return;
    g_active = TRUE; g_done = FALSE; g_cancel = FALSE; g_result = 0; g_haveRedisp = FALSE;
    g_owner = owner; g_np = 0; g_hot = -1; g_hotBtn = -1; TipHide();
    g_barIdx = root ? -1 : barIdx;
    g_open = -1; g_kbd = -1;

    if (root) {
        PopOpen(0, root, x, y, anchorBottom, 0);
    } else if (openPopup) {
        OpenBarPopup(barIdx, FALSE);
    } else {
        g_kbd = barIdx;
        InvalidateRect(g_bar, NULL, FALSE);
    }

    while (!g_done && !g_cancel) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message != WM_MOUSEMOVE && msg.message != 0x113 && msg.message != 0x0F) DBG(L"menumsg", msg.message, msg.wParam);
        switch (msg.message) {
        case WM_MOUSEMOVE: {
            int item, lv = PopHit(msg.pt, &item), bi;
            if (lv >= 0) {
                if (item >= 0 && g_pop[lv].def->items[item].label) SetSel(lv, Selectable(&g_pop[lv], item) ? item : -1, TRUE);
            } else if ((bi = BarHitScreen(msg.pt)) >= 0) {
                if (g_np > 0 && bi != g_barIdx) OpenBarPopup(bi, FALSE);
                else if (g_np == 0 && bi != g_kbd) { g_kbd = bi; InvalidateRect(g_bar, NULL, FALSE); }
            }
            break; }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int item, lv = PopHit(msg.pt, &item), bi;
            if (lv >= 0) {
                if (item >= 0 && Selectable(&g_pop[lv], item)) {
                    SetSel(lv, item, TRUE);
                } else if (item >= 0 && !g_pop[lv].def->items[item].label) {
                    /* separator: ignore */
                }
            } else if ((bi = BarHitScreen(msg.pt)) >= 0) {
                if (g_np > 0 && bi == g_barIdx) g_done = TRUE;            /* clicking the open title closes */
                else OpenBarPopup(bi, FALSE);
            } else {
                g_done = TRUE;                                            /* click outside: close + swallow */
            }
            break; }
        case WM_LBUTTONUP: {
            int item, lv = PopHit(msg.pt, &item);
            if (lv >= 0 && item >= 0 && item == g_pop[lv].sel && !g_pop[lv].def->items[item].sub)
                Activate(lv, item);
            break; }
        case WM_RBUTTONDOWN:
            g_done = TRUE;
            break;
        case WM_RBUTTONUP:
        case WM_MOUSEWHEEL:
            break;
        case 0x00A1: /* WM_NCLBUTTONDOWN */
        case 0x00A4: /* WM_NCRBUTTONDOWN */
            g_redisp = msg; g_haveRedisp = TRUE;                          /* title-bar click: close, then let it through */
            g_done = TRUE;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(msg.wParam == VK_MENU && (msg.lParam & 0x40000000)))   /* ignore alt auto-repeat */
                KeyNav(msg.wParam);
            TranslateMessage(&msg);                                       /* so we get WM_CHAR for mnemonics */
            break;
        case WM_CHAR:
        case WM_SYSCHAR:
            if (msg.wParam > 32) MnemonicKey((WCHAR)msg.wParam);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
            break;
        default:
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            break;
        }
    }

    DBG(L"runmenu-end", g_result, g_cancel * 10 + g_done);
    {   /* enter / space that picked an item (or esc) left a WM_CHAR queued: it must not reach the edit as typed text */
        MSG junk;
        while (PeekMessageW(&junk, NULL, WM_CHAR, WM_DEADCHAR, PM_REMOVE)) {}
        while (PeekMessageW(&junk, NULL, WM_SYSCHAR, WM_SYSDEADCHAR, PM_REMOVE)) {}
    }
    ClosePops(0);
    for (i = 0; i < MAXLV; i++) g_pop[i].hwnd = NULL;
    g_open = -1; g_kbd = -1; g_barIdx = -1;
    g_active = FALSE;
    if (g_bar) InvalidateRect(g_bar, NULL, FALSE);
    if (g_result) PostMessageW(owner, WM_COMMAND, MAKEWPARAM(g_result, 0), 0);
    if (g_haveRedisp) DispatchMessageW(&g_redisp);
}

BOOL MenuActive(void) { return g_active; }

void MenuCancel(void)
{
    if (!g_active) return;
    g_cancel = TRUE;
    PostMessageW(g_owner, WM_NULL, 0, 0);                                 /* wake the loop */
}

void MenuBarActivate(HWND bar, int idx, int openPopup)
{
    (void)bar;
    if (idx < 0) idx = 0;
    RunMenu(GetParent(g_bar), idx, openPopup, NULL, 0, 0, 0);
}

void MenuPopup(HWND owner, const MenuDef *def, int x, int y, int anchorBottom)
{
    RunMenu(owner, -1, 1, def, x, y, anchorBottom);
}

/* -------------------------------------------------------- bar window ---- */
static LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        BarPaint(h);
        return 0;
#ifdef MENU_NO_TRACK
    case WM_APP + 91:                                   /* probe builds only: "this exe keeps a hover" (the gui tests look for it before they try one) */
        return 0x4D494E54;
#endif
    case WM_MOUSEMOVE: {
        int i = BarHit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        int b = i < 0 ? BtnHit(h, GET_X_LPARAM(l), GET_Y_LPARAM(l)) : -1;
        if (i != g_hot || b != g_hotBtn) {
            TRACKMOUSEEVENT te;
            if (b != g_hotBtn) TipTrack(b);                 /* (a tip follows the pointer from button to button) */
            g_hot = i; g_hotBtn = b;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
#ifndef MENU_NO_TRACK                                   /* probe builds only (tools\probe.bat /DMENU_NO_TRACK): the system answers the leave request at once when the real pointer is elsewhere (the gui tests send WM_MOUSEMOVE on a private desktop), which would end every hover before it can be looked at */
            TrackMouseEvent(&te);
#endif
            InvalidateRect(h, NULL, FALSE);
        }
        return 0; }
    case WM_TIMER:                                      /* the tip's timer: the delay is over (show the tip), or the tip has been up long enough (hide it) */
        if (w == TIP_TIMER) {
            if (g_tip || g_hotBtn < 0 || g_active) TipHide();
            else TipShow(g_hotBtn);
        }
        return 0;
    case WM_MOUSELEAVE:
        TipTrack(-1);
        g_hot = -1; g_hotBtn = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SIZE:                                       /* the buttons are right aligned: a size change repaints all of the bar (no stale icons left in the middle) */
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = BarHit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        int b = i < 0 ? BtnHit(h, GET_X_LPARAM(l), GET_Y_LPARAM(l)) : -1;
        TipHide();                                      /* (a click ends the tip; it comes back once the pointer has left the button and returned) */
        if (i >= 0) { g_hot = -1; RunMenu(GetParent(h), i, 1, NULL, 0, 0, 0); }
        else if (b >= 0) PostMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(g_btn[b].id, 0), 0);   /* the same commands as alt+z / alt+x */
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

HWND MenuBarCreate(HWND parent, MenuStateFn fn)
{
    g_stateFn = fn;
    RegClass(L"mp_popup", PopProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_tip", TipProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_menubar", BarProc, CS_DBLCLKS, NULL);
    BarLayout();
    g_bar = CreateWindowExW(0, L"mp_menubar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 100, g_barH, parent, NULL, g_hinst, NULL);
    return g_bar;
}
