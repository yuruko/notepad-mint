/* menu.c - hand-drawn menu bar + popup menus (classic 2000-era look, dark + mint).
 *
 * no native menus anywhere: the bar is a child window, popups are tiny
 * WS_EX_NOACTIVATE windows, and a private message loop drives hover / keyboard /
 * mnemonics / submenus. keeps the app window itself a plain native frame. */
#include "mp.h"

/* ---------------------------------------------------------- definitions -- */
#define SEP         { NULL, NULL, 0, NULL }
#define IT(l, a, i) { l, a, i, NULL }
#define SUB(l, d)   { l, NULL, 0, d }

static const MenuItem miFile[] = {
    IT(L"&new",          L"ctrl+n",       IDM_FILE_NEW),
    IT(L"new &window",   L"ctrl+shift+n", IDM_FILE_NEWWIN),
    IT(L"&open...",      L"ctrl+o",       IDM_FILE_OPEN),
    IT(L"&save",         L"ctrl+s",       IDM_FILE_SAVE),
    IT(L"save &as...",   L"ctrl+shift+s", IDM_FILE_SAVEAS),
    SEP,
    IT(L"page set&up...", NULL,           IDM_FILE_PAGESETUP),
    IT(L"&print...",     L"ctrl+p",       IDM_FILE_PRINT),
    SEP,
    IT(L"e&xit",         NULL,            IDM_FILE_EXIT),
};
static const MenuItem miEdit[] = {
    IT(L"&undo",          L"ctrl+z",   IDM_EDIT_UNDO),
    SEP,
    IT(L"cu&t",           L"ctrl+x",   IDM_EDIT_CUT),
    IT(L"&copy",          L"ctrl+c",   IDM_EDIT_COPY),
    IT(L"&paste",         L"ctrl+v",   IDM_EDIT_PASTE),
    IT(L"de&lete",        L"del",      IDM_EDIT_DELETE),
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
    IT(L"&utf-8",          NULL, IDM_ENC_UTF8),
    IT(L"utf-8 with &bom", NULL, IDM_ENC_UTF8BOM),
    IT(L"utf-16 &le",      NULL, IDM_ENC_UTF16LE),
    IT(L"utf-16 b&e",      NULL, IDM_ENC_UTF16BE),
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
static const MenuItem miFormat[] = {
    IT(L"&word wrap",          NULL, IDM_FMT_WRAP),
    IT(L"&font && colors...",  NULL, IDM_FMT_FONT),
    SEP,
    SUB(L"line &ending",       &g_mdEol),
    SUB(L"e&ncoding",          &g_mdEnc),
};
static const MenuItem miView[] = {
    SUB(L"&zoom",              &g_mdZoom),
    IT(L"&status bar",         NULL, IDM_VIEW_STATUS),
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
    int i, x = S(3);
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

static void BarPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mdc;
    HBITMAP bmp;
    HGDIOBJ old, of;
    RECT rc, r;
    int i;

    GetClientRect(h, &rc);
    mdc = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old = SelectObject(mdc, bmp);
    of = SelectObject(mdc, g_fontMenu);

    FillC(mdc, &rc, C_FACE);
    r = rc; r.top = rc.bottom - 2; r.bottom = rc.bottom - 1; FillC(mdc, &r, C_LO2);   /* etched line */
    r.top = rc.bottom - 1; r.bottom = rc.bottom;             FillC(mdc, &r, C_HI2);

    for (i = 0; i < (int)NBAR; i++) {
        BOOL on = (i == g_open || i == g_kbd);
        r.left = g_itemL[i]; r.right = g_itemL[i + 1];
        r.top = S(1); r.bottom = rc.bottom - 3;
        if (on) FillC(mdc, &r, C_ACCENT);
        TextC(mdc, g_ent[i].title, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
              on ? C_FACE : (i == g_hot ? C_ACCENT : C_TEXT));
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, of);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    EndPaint(h, &ps);
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
            COLORREF fg = gray ? C_DIM : (sel ? C_FACE : C_TEXT);
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
        return TRUE;
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
    g_owner = owner; g_np = 0; g_hot = -1;
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
    case WM_MOUSEMOVE: {
        int i = BarHit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i != g_hot) {
            TRACKMOUSEEVENT te;
            g_hot = i;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0; }
    case WM_MOUSELEAVE:
        g_hot = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = BarHit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (i >= 0) { g_hot = -1; RunMenu(GetParent(h), i, 1, NULL, 0, 0, 0); }
        return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

HWND MenuBarCreate(HWND parent, MenuStateFn fn)
{
    g_stateFn = fn;
    RegClass(L"mp_popup", PopProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_menubar", BarProc, CS_DBLCLKS, NULL);
    BarLayout();
    g_bar = CreateWindowExW(0, L"mp_menubar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 100, g_barH, parent, NULL, g_hinst, NULL);
    return g_bar;
}
