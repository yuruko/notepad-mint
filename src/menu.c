#include "menu_internal.h"

#define MAXLV 3
#define MAXIT 24
#define BW    2

typedef struct Pop {
    HWND hwnd;
    const MenuDef *def;
    int sel, subOf, x, y, width, height, count, gutter;
    int itemTop[MAXIT], itemHeight[MAXIT];
} Pop;

static Pop         g_pop[MAXLV];
static int         g_np;
static HWND        g_bar, g_owner;
static MenuStateFn g_stateFn;
static int         g_hot = -1, g_open = -1, g_kbd = -1, g_barIdx = -1, g_barH;
static int         g_hotBtn = -1;
static int         g_itemL[MENU_BAR_COUNT + 1];
static BOOL        g_active, g_done, g_cancel;
static int         g_result;
static MSG         g_redisp;
static BOOL        g_haveRedisp;
static RECT        g_doubleRect;
static DWORD       g_doubleTime;
static int         g_doubleArmed;
static int         g_cues;

static void Strip(const WCHAR *src, WCHAR *dst, int cap) {
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

static UINT CueFormat(void) {
    BOOL always = FALSE;
    return g_cues || (SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &always, 0) && always) ? 0 : DT_HIDEPREFIX;
}

static WCHAR Mnem(const WCHAR *label) {
    for (; *label; label++) {
        if (*label == '&') {
            if (label[1] == '&') label++;
            else if (label[1]) return wlow(label[1]);
        }
    }
    return 0;
}

static unsigned ItemState(const MenuItem *item) {
    if (item->sub || !g_stateFn) return 0;
    return g_stateFn(item->id);
}

static BOOL Selectable(const Pop *pop, int index) {
    const MenuItem *item;
    if (index < 0 || index >= pop->count) return FALSE;
    item = &pop->def->items[index];
    if (!item->label) return FALSE;
    return !(ItemState(item) & MS_GRAY);
}

static void BarLayout(void) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW tm;
    WCHAR buf[64];
    int i, x = 0;
    GetTextMetricsW(hdc, &tm);
    g_barH = tm.tmHeight + S(9);
    for (i = 0; i < (int)MENU_BAR_COUNT; i++) {
        Strip(g_menuEntries[i].title, buf, 64);
        g_itemL[i] = x;
        x += TextW(hdc, buf, -1) + S(18);
    }
    g_itemL[MENU_BAR_COUNT] = x;
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
}

int MenuBarHeight(void) {
    if (!g_barH) BarLayout();
    return g_barH;
}

void MenuBarRefont(HWND bar) {
    BarLayout();
    InvalidateRect(bar, NULL, FALSE);
}

static int BarHit(int x, int y) {
    int i;
    if (y < 0 || y >= g_barH) return -1;
    for (i = 0; i < (int)MENU_BAR_COUNT; i++) if (x >= g_itemL[i] && x < g_itemL[i + 1]) return i;
    return -1;
}

int MenuBarMnemonic(WCHAR ch) {
    int i;
    ch = wlow(ch);
    for (i = 0; i < (int)MENU_BAR_COUNT; i++) if (Mnem(g_menuEntries[i].title) == ch) return i;
    return -1;
}

enum { TB_LIGHT, TB_DARK, TB_WRAP };
enum { BT_WRAP, BT_THEME, BT_COUNT };
static const struct { int id; const WCHAR *tip; } g_btn[BT_COUNT] = {
    [BT_WRAP]  = { IDM_FMT_WRAP,     L"toggle word wrap" },
    [BT_THEME] = { IDM_THEME_TOGGLE, L"toggle dark / light theme" },
};
#define ICON_SS  8
#define ICON_MAX 64

int MenuBarMinWidth(void) {
    if (!g_barH) BarLayout();
    return g_itemL[MENU_BAR_COUNT] + BT_COUNT * S(BAR_BTN_W);
}

static void BtnRect(int clientWidth, int button, RECT *rect) {
    rect->left = clientWidth - (BT_COUNT - button) * S(BAR_BTN_W);
    rect->right = rect->left + S(BAR_BTN_W);
    rect->top = 0;
    rect->bottom = g_barH - S(1);
}

static int BtnHit(HWND hwnd, int x, int y) {
    RECT client, rect;
    int button;
    if (y < 0 || y >= g_barH) return -1;
    GetClientRect(hwnd, &client);
    for (button = 0; button < BT_COUNT; button++) {
        BtnRect(client.right, button, &rect);
        if (x >= rect.left && x < rect.right) return button;
    }
    return -1;
}

#define D(v) ((v) * 64)
static int IconInside(int kind, int x, int y) {
    int dx = x - D(28), dy = y - D(28), ax, ay, bx, by;
    if (kind == TB_WRAP) {
#define PX(k) ((k) * 299 - 149)
        if (y >= PX(1) && y < PX(2) && x >= PX(1) && x < PX(11)) return 1;
        if (y >= PX(3) && y < PX(4) && x >= PX(1) && x < PX(10)) return 1;
        if (x >= PX(9) && x < PX(10) && y >= PX(3) && y < PX(7)) return 1;
        if (y >= PX(6) && y < PX(7) && x >= PX(3) && x < PX(10)) return 1;
        ax = x - PX(2); ay = y - (PX(6) + 150); if (ay < 0) ay = -ay;
        if (ax >= 0 && x < PX(5) && ay * 2 <= ax + 150) return 1;
        return y >= PX(9) && y < PX(10) && x >= PX(1) && x < PX(6);
#undef PX
    }
    if (kind == TB_LIGHT) {
        if (dx * dx + dy * dy < D(11) * D(11)) return 1;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 154 && dy >= 1043 && dy < D(25)) return 1;
        if (dy < 154 && dx >= 1043 && dx < D(25)) return 1;
        return dx - dy < 217 && dy - dx < 217 && dx + dy >= 1448 && dx + dy < 2037;
    }
    ax = x - D(26); ay = y - D(30); bx = x - D(38); by = y - D(22);
    return ax * ax + ay * ay < D(22) * D(22) && bx * bx + by * by >= D(19) * D(19);
}
#undef D

static BYTE g_icCov[3][ICON_MAX * ICON_MAX];
static int  g_icSize[3];

static void IconCoverage(int kind, int size, BYTE *out) {
    int grid[ICON_MAX * ICON_SS], count, i, j, subX, subY, coverage, shift;
    count = size * ICON_SS;
    shift = (size & 1) ? 0 : 1792 / size;
    for (i = 0; i < count; i++) grid[i] = (2 * i + 1) * 56 * 32 / count - shift;
    for (j = 0; j < size; j++) for (i = 0; i < size; i++) {
            coverage = 0;
            for (subY = 0; subY < ICON_SS; subY++) for (subX = 0; subX < ICON_SS; subX++) coverage += IconInside(kind, grid[i * ICON_SS + subX], grid[j * ICON_SS + subY]);
            out[j * size + i] = (BYTE)coverage;
        }
}

static void ThemeIcon(HDC hdc, int kind, int x, int y, int size, COLORREF fg, COLORREF bg, int opacity) {
    int i, j, coverage, num, den = ICON_SS * ICON_SS * 100;
    const BYTE *coverageMap;
    if (size > ICON_MAX) size = ICON_MAX;
    if (g_icSize[kind] != size) { IconCoverage(kind, size, g_icCov[kind]); g_icSize[kind] = size; }
    coverageMap = g_icCov[kind];
    for (j = 0; j < size; j++) {
        for (i = 0; i < size; i++) {
            coverage = coverageMap[j * size + i];
            if (!coverage) continue;
            num = coverage * opacity;
            SetPixelV(hdc, x + i, y + j, num >= den ? fg : RGB(GetRValue(bg) + (GetRValue(fg) - GetRValue(bg)) * num / den, GetGValue(bg) + (GetGValue(fg) - GetGValue(bg)) * num / den, GetBValue(bg) + (GetBValue(fg) - GetBValue(bg)) * num / den));
        }
    }
}

static void BarPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps), memDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap, oldFont;
    RECT client, rect;
    int i, iconSize = S(BAR_BTN_ICON);
    GetClientRect(hwnd, &client);
    memDc = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    oldBitmap = SelectObject(memDc, bitmap);
    oldFont = SelectObject(memDc, g_fontMenu);
    FillC(memDc, &client, C_FACE);
    for (i = 0; i < (int)MENU_BAR_COUNT; i++) {
        BOOL on = (i == g_open || i == g_kbd || i == g_hot);
        rect.left = g_itemL[i]; rect.right = g_itemL[i + 1];
        rect.top = 0; rect.bottom = client.bottom - S(1);
        if (on) FillC(memDc, &rect, C_ACCENT);
        TextC(memDc, g_menuEntries[i].title, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | CueFormat(), on ? C_ON_ACCENT : C_TEXT);
    }
    for (i = 0; i < BT_COUNT; i++) {
        BOOL hot = (i == g_hotBtn);
        int kind = i == BT_WRAP ? TB_WRAP : (ThemeGet() == THEME_LIGHT ? TB_LIGHT : TB_DARK);
        int opacity = (i == BT_WRAP && g_pf.wrap) ? BAR_BTN_OPACITY_ON : BAR_BTN_OPACITY;
        BtnRect(client.right, i, &rect);
        if (hot) FillC(memDc, &rect, C_ACCENT);
        ThemeIcon(memDc, kind, (rect.left + rect.right - iconSize) / 2, (rect.top + rect.bottom - iconSize) / 2 + (i == BT_WRAP ? S(BAR_WRAP_ICON_DY) : 0), iconSize, hot ? C_ON_ACCENT : C_TEXT, hot ? C_ACCENT : C_FACE, hot ? 100 : opacity);
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldFont);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd, &ps);
}

#define TIP_TIMER 1
static HWND g_tip;

static void TipHide(void) {
    if (g_bar) KillTimer(g_bar, TIP_TIMER);
    if (g_tip) DestroyWindow(g_tip);
    g_tip = NULL;
}

static void TipShow(int button) {
    WCHAR text[96];
    const WCHAR *accel = MenuAccelOf(g_btn[button].id);
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW tm;
    MONITORINFO mi;
    RECT rect;
    POINT pt;
    int width, height;
    wcopy(text, g_btn[button].tip, 96);
    if (accel) { wcat(text, L" (", 96); wcat(text, accel, 96); wcat(text, L")", 96); }
    GetTextMetricsW(hdc, &tm);
    width = TextW(hdc, text, -1) + 2 * (BW + S(7));
    height = tm.tmHeight + 2 * (BW + S(3));
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
    GetClientRect(g_bar, &rect);
    BtnRect(rect.right, button, &rect);
    pt.x = rect.right - width; pt.y = g_barH + S(2);
    ClientToScreen(g_bar, &pt);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    if (pt.x + width > mi.rcWork.right) pt.x = mi.rcWork.right - width;
    if (pt.x < mi.rcWork.left) pt.x = mi.rcWork.left;
    g_tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"mp_tip", text, WS_POPUP, pt.x, pt.y, width, height, GetParent(g_bar), NULL, g_hinst, NULL);
    ShowWindow(g_tip, SW_SHOWNOACTIVATE);
    SetTimer(g_bar, TIP_TIMER, BAR_TIP_SHOW, NULL);
}

static void TipTrack(int button) {
    BOOL wasShown = g_tip != NULL;
    TipHide();
    if (button < 0 || g_active) return;
    if (wasShown) TipShow(button);
    else SetTimer(g_bar, TIP_TIMER, BAR_TIP_DELAY, NULL);
}

static LRESULT CALLBACK TipProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return (LRESULT)-1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), memDc;
        HBITMAP bitmap;
        HGDIOBJ oldBitmap, oldFont;
        RECT client, textRect;
        WCHAR text[96];
        int length = GetWindowTextW(hwnd, text, 96);
        GetClientRect(hwnd, &client);
        memDc = CreateCompatibleDC(hdc);
        bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        oldBitmap = SelectObject(memDc, bitmap);
        oldFont = SelectObject(memDc, g_fontMenu);
        FillC(memDc, &client, C_FACE2);
        Bevel(memDc, &client, BV_RAISED);
        textRect = client; textRect.left = BW + S(7);
        TextC(memDc, text, length, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, C_TEXT);
        BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldFont);
        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd, &ps);
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void PopMeasure(Pop *pop) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontMenu);
    TEXTMETRICW tm;
    WCHAR buf[64];
    int i, labelWidth = 0, accelWidth = 0, y, textWidth, rowHeight;
    GetTextMetricsW(hdc, &tm);
    rowHeight = tm.tmHeight + S(8);
    if (rowHeight < S(22)) rowHeight = S(22);
    pop->gutter = rowHeight + S(2);
    pop->count = pop->def->n > MAXIT ? MAXIT : pop->def->n;
    y = BW + S(2);
    for (i = 0; i < pop->count; i++) {
        const MenuItem *item = &pop->def->items[i];
        pop->itemTop[i] = y;
        if (!item->label) {
            pop->itemHeight[i] = S(7);
        } else {
            pop->itemHeight[i] = rowHeight;
            Strip(item->label, buf, 64);
            textWidth = TextW(hdc, buf, -1);
            if (textWidth > labelWidth) labelWidth = textWidth;
            if (item->accel) {
                textWidth = TextW(hdc, item->accel, -1);
                if (textWidth > accelWidth) accelWidth = textWidth;
            }
        }
        y += pop->itemHeight[i];
    }
    pop->height = y + S(2) + BW;
    pop->width = BW + pop->gutter + labelWidth + (accelWidth ? S(28) + accelWidth : 0) + S(26) + BW;
    if (pop->width < S(120)) pop->width = S(120);
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
}

static void PopDraw(Pop *pop, HDC hdc) {
    RECT client, itemRect, lineRect;
    int i;
    client.left = 0; client.top = 0; client.right = pop->width; client.bottom = pop->height;
    FillC(hdc, &client, C_FACE2);
    Bevel(hdc, &client, BV_RAISED);
    SelectObject(hdc, g_fontMenu);
    for (i = 0; i < pop->count; i++) {
        const MenuItem *item = &pop->def->items[i];
        itemRect.left = BW; itemRect.right = pop->width - BW; itemRect.top = pop->itemTop[i]; itemRect.bottom = pop->itemTop[i] + pop->itemHeight[i];
        if (!item->label) {
            lineRect.left = BW + S(3); lineRect.right = pop->width - BW - S(3);
            lineRect.top = itemRect.top + pop->itemHeight[i] / 2 - 1; lineRect.bottom = lineRect.top + 1;
            FillC(hdc, &lineRect, C_LO2);
            lineRect.top++; lineRect.bottom++;
            FillC(hdc, &lineRect, C_HI2);
            continue;
        }
        {
            unsigned state = ItemState(item);
            BOOL gray = (state & MS_GRAY) ? TRUE : FALSE;
            BOOL sel = (i == pop->sel) && !gray;
            COLORREF fg = gray ? C_DIM : (sel ? C_ON_ACCENT : C_TEXT);
            RECT textRect;
            if (sel) FillC(hdc, &itemRect, C_ACCENT);
            if (state & MS_CHECK) {
                if (state & MS_RADIO) {
                    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
                    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
                    int centerX = itemRect.left + pop->gutter / 2, centerY = itemRect.top + pop->itemHeight[i] / 2, radius = S(3);
                    SetDCBrushColor(hdc, fg);
                    Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                } else {
                    CheckGlyph(hdc, itemRect.left + (pop->gutter - S(13)) / 2 + S(1), itemRect.top + (pop->itemHeight[i] - S(13)) / 2, fg);
                }
            }
            textRect = itemRect; textRect.left = BW + pop->gutter;
            TextC(hdc, item->label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | CueFormat(), fg);
            if (item->accel) {
                textRect = itemRect; textRect.right = pop->width - BW - S(22);
                TextC(hdc, item->accel, -1, &textRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, fg);
            }
            if (item->sub) Tri(hdc, pop->width - BW - S(14), itemRect.top + pop->itemHeight[i] / 2 - S(4), S(4), fg);
        }
    }
}

static LRESULT CALLBACK PopProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Pop *pop = (Pop *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lParam)->lpCreateParams);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps), memDc;
        HBITMAP bitmap;
        HGDIOBJ oldBitmap;
        if (pop) {
            memDc = CreateCompatibleDC(hdc);
            bitmap = CreateCompatibleBitmap(hdc, pop->width, pop->height);
            oldBitmap = SelectObject(memDc, bitmap);
            PopDraw(pop, memDc);
            BitBlt(hdc, 0, 0, pop->width, pop->height, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memDc);
        }
        EndPaint(hwnd, &ps);
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ClosePops(int level) {
    int i;
    for (i = g_np - 1; i >= level; i--) {
        if (g_pop[i].hwnd) DestroyWindow(g_pop[i].hwnd);
        g_pop[i].hwnd = NULL;
    }
    if (level < g_np) g_np = level;
    if (level > 0 && level <= MAXLV) g_pop[level - 1].subOf = -1;
}

static void PopOpen(int level, const MenuDef *def, int x, int y, int anchorBottom, int altX) {
    Pop *pop = &g_pop[level];
    POINT pt;
    MONITORINFO mi;
    RECT workArea;
    memset(pop, 0, sizeof *pop);
    pop->def = def; pop->sel = -1; pop->subOf = -1;
    PopMeasure(pop);
    if (anchorBottom) y -= pop->height;
    pt.x = x; pt.y = y;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    workArea = mi.rcWork;
    if (x + pop->width > workArea.right) x = altX ? altX - pop->width : workArea.right - pop->width;
    if (x < workArea.left) x = workArea.left;
    if (y + pop->height > workArea.bottom) y = workArea.bottom - pop->height;
    if (y < workArea.top) y = workArea.top;
    pop->x = x; pop->y = y;
    pop->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"mp_popup", NULL, WS_POPUP, x, y, pop->width, pop->height, g_owner, NULL, g_hinst, pop);
    ShowWindow(pop->hwnd, SW_SHOWNOACTIVATE);
    g_np = level + 1;
}

static void SetSel(int level, int index, BOOL openSub) {
    Pop *pop = &g_pop[level];
    if (pop->sel != index) {
        pop->sel = index;
        InvalidateRect(pop->hwnd, NULL, FALSE);
    }
    if (g_np > level + 1 && index != pop->subOf) ClosePops(level + 1);
    if (openSub && index >= 0 && pop->def->items[index].sub && Selectable(pop, index) && pop->subOf != index && level + 1 < MAXLV) {
        pop->subOf = index;
        PopOpen(level + 1, pop->def->items[index].sub, pop->x + pop->width - S(4), pop->y + pop->itemTop[index] - S(3), 0, pop->x + S(4));
    }
}

static void MoveSel(int level, int dir) {
    Pop *pop = &g_pop[level];
    int i = pop->sel, k;
    for (k = 0; k < pop->count; k++) {
        i += dir;
        if (i >= pop->count) i = 0;
        if (i < 0) i = pop->count - 1;
        if (Selectable(pop, i)) { SetSel(level, i, FALSE); return; }
    }
}

static void OpenBarPopup(int index, BOOL selectFirst) {
    POINT pt;
    pt.x = g_itemL[index]; pt.y = g_barH;
    ClientToScreen(g_bar, &pt);
    ClosePops(0);
    g_barIdx = index; g_open = index; g_kbd = -1;
    PopOpen(0, g_menuEntries[index].def, pt.x, pt.y - 1, 0, 0);
    if (selectFirst) MoveSel(0, 1);
    InvalidateRect(g_bar, NULL, FALSE);
}

static void Activate(int level, int index) {
    Pop *pop = &g_pop[level];
    const MenuItem *item;
    if (!Selectable(pop, index)) return;
    item = &pop->def->items[index];
    if (item->sub) {
        SetSel(level, index, TRUE);
        if (g_np > level + 1) MoveSel(level + 1, 1);
    } else {
        g_result = item->id;
        g_done = TRUE;
    }
}

static int PopHit(POINT pt, int *item) {
    int level, i;
    for (level = g_np - 1; level >= 0; level--) {
        Pop *pop = &g_pop[level];
        if (pt.x >= pop->x && pt.x < pop->x + pop->width && pt.y >= pop->y && pt.y < pop->y + pop->height) {
            int localY = pt.y - pop->y;
            *item = -1;
            for (i = 0; i < pop->count; i++) if (localY >= pop->itemTop[i] && localY < pop->itemTop[i] + pop->itemHeight[i]) { *item = i; break; }
            return level;
        }
    }
    return -1;
}

static int BarHitScreen(POINT pt) {
    RECT barRect;
    if (!g_bar || g_barIdx < 0) return -1;
    GetWindowRect(g_bar, &barRect);
    if (pt.x < barRect.left || pt.x >= barRect.right || pt.y < barRect.top || pt.y >= barRect.bottom) return -1;
    return BarHit(pt.x - barRect.left, pt.y - barRect.top);
}

static void SwitchBar(int index, BOOL selectFirst) {
    if (index < 0) index = (int)MENU_BAR_COUNT - 1;
    if (index >= (int)MENU_BAR_COUNT) index = 0;
    OpenBarPopup(index, selectFirst);
}

static void MnemonicKey(WCHAR ch) {
    ch = wlow(ch);
    if (g_np == 0) {
        int index = MenuBarMnemonic(ch);
        if (index >= 0 && g_barIdx >= 0) OpenBarPopup(index, TRUE);
    } else {
        int level = g_np - 1, i, hits = 0, first = -1, next = -1;
        Pop *pop = &g_pop[level];
        for (i = 0; i < pop->count; i++) {
            if (!Selectable(pop, i) || Mnem(pop->def->items[i].label) != ch) continue;
            hits++;
            if (first < 0) first = i;
            if (next < 0 && i > pop->sel) next = i;
        }
        if (hits == 1) Activate(level, first);
        else if (hits > 1) SetSel(level, next >= 0 ? next : first, FALSE);
    }
}

static void KeyNav(WPARAM vk) {
    int level = g_np - 1, i;
    if (!g_cues) {
        g_cues = 1;
        if (g_bar) InvalidateRect(g_bar, NULL, FALSE);
        for (i = 0; i < g_np; i++) if (g_pop[i].hwnd) InvalidateRect(g_pop[i].hwnd, NULL, FALSE);
    }
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
        else MoveSel(level, 1);
        break;
    case VK_UP:
        if (g_np > 0) MoveSel(level, -1);
        break;
    case VK_HOME:
        if (g_np > 0) { g_pop[level].sel = -1; MoveSel(level, 1); }
        break;
    case VK_END:
        if (g_np > 0) { g_pop[level].sel = g_pop[level].count; MoveSel(level, -1); }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (g_np == 0) { if (g_kbd >= 0) OpenBarPopup(g_kbd, TRUE); }
        else if (g_pop[level].sel >= 0) Activate(level, g_pop[level].sel);
        break;
    case VK_RIGHT:
        if (g_np == 0) { if (g_kbd >= 0) { g_kbd = (g_kbd + 1) % (int)MENU_BAR_COUNT; InvalidateRect(g_bar, NULL, FALSE); } }
        else if (g_pop[level].sel >= 0 && g_pop[level].def->items[g_pop[level].sel].sub) Activate(level, g_pop[level].sel);
        else if (g_barIdx >= 0) SwitchBar(g_barIdx + 1, TRUE);
        break;
    case VK_LEFT:
        if (g_np == 0) { if (g_kbd >= 0) { g_kbd = (g_kbd + (int)MENU_BAR_COUNT - 1) % (int)MENU_BAR_COUNT; InvalidateRect(g_bar, NULL, FALSE); } }
        else if (g_np > 1) ClosePops(g_np - 1);
        else if (g_barIdx >= 0) SwitchBar(g_barIdx - 1, TRUE);
        break;
    }
}

static void RunMenu(HWND owner, int barIdx, int openPopup, const MenuDef *root, int x, int y, int anchorBottom) {
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
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message != WM_MOUSEMOVE && msg.message != 0x113 && msg.message != 0x0F) DBG(L"menumsg", msg.message, msg.wParam);
        switch (msg.message) {
        case WM_MOUSEMOVE: {
            int item, level = PopHit(msg.pt, &item), barIndex;
            if (level >= 0) {
                if (item >= 0 && g_pop[level].def->items[item].label) SetSel(level, Selectable(&g_pop[level], item) ? item : -1, TRUE);
            } else if ((barIndex = BarHitScreen(msg.pt)) >= 0) {
                if (g_np > 0 && barIndex != g_barIdx) OpenBarPopup(barIndex, FALSE);
                else if (g_np == 0 && barIndex != g_kbd) { g_kbd = barIndex; InvalidateRect(g_bar, NULL, FALSE); }
            }
            break; }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int item, level = PopHit(msg.pt, &item), barIndex;
            if (level >= 0) {
                if (item >= 0 && Selectable(&g_pop[level], item)) SetSel(level, item, TRUE);
            } else if ((barIndex = BarHitScreen(msg.pt)) >= 0) {
                if (g_np > 0 && barIndex == g_barIdx) g_done = TRUE;
                else OpenBarPopup(barIndex, FALSE);
            } else {
                if (g_doubleArmed && PtInRect(&g_doubleRect, msg.pt) && msg.time - g_doubleTime <= GetDoubleClickTime()) g_result = IDM_SYS_CLOSE;
                g_done = TRUE;
            }
            break; }
        case WM_LBUTTONUP: {
            int item, level = PopHit(msg.pt, &item);
            if (level >= 0 && item >= 0 && item == g_pop[level].sel && !g_pop[level].def->items[item].sub) Activate(level, item);
            break; }
        case WM_RBUTTONDOWN:
            g_done = TRUE;
            break;
        case WM_RBUTTONUP:
        case WM_MOUSEWHEEL:
            break;
        case 0x00A1:
        case 0x00A4:
            g_redisp = msg; g_haveRedisp = TRUE;
            g_done = TRUE;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(msg.wParam == VK_MENU && (msg.lParam & 0x40000000))) KeyNav(msg.wParam);
            TranslateMessage(&msg);
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
    {
        MSG junk;
        while (PeekMessageW(&junk, NULL, WM_CHAR, WM_DEADCHAR, PM_REMOVE)) {}
        while (PeekMessageW(&junk, NULL, WM_SYSCHAR, WM_SYSDEADCHAR, PM_REMOVE)) {}
    }
    ClosePops(0);
    for (i = 0; i < MAXLV; i++) g_pop[i].hwnd = NULL;
    g_open = -1; g_kbd = -1; g_barIdx = -1;
    g_active = FALSE;
    g_doubleArmed = 0;
    g_cues = 0;
    if (g_bar) InvalidateRect(g_bar, NULL, FALSE);
    if (g_result) PostMessageW(owner, WM_COMMAND, MAKEWPARAM(g_result, 0), 0);
    if (g_haveRedisp) DispatchMessageW(&g_redisp);
}

void MenuArmDoubleClose(const RECT *screenRect, DWORD time) {
    g_doubleRect = *screenRect;
    g_doubleTime = time;
    g_doubleArmed = 1;
}

void MenuCancel(void) {
    if (!g_active) return;
    g_cancel = TRUE;
    PostMessageW(g_owner, WM_NULL, 0, 0);
}

void MenuShowCues(void) {
    g_cues = 1;
}

void MenuBarActivate(int index, int openPopup) {
    if (index < 0) index = 0;
    g_cues = 1;
    RunMenu(GetParent(g_bar), index, openPopup, NULL, 0, 0, 0);
}

void MenuPopup(HWND owner, const MenuDef *def, int x, int y, int anchorBottom) {
    RunMenu(owner, -1, 1, def, x, y, anchorBottom);
}

static LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        BarPaint(hwnd);
        return 0;
#ifdef MENU_NO_TRACK
    case WM_APP + 91:
        return PROBE_ID;
#endif
    case WM_MOUSEMOVE: {
        int index = BarHit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        int button = index < 0 ? BtnHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
        if (index != g_hot || button != g_hotBtn) {
            TRACKMOUSEEVENT te;
            if (button != g_hotBtn) TipTrack(button);
            g_hot = index; g_hotBtn = button;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = hwnd; te.dwHoverTime = 0;
#ifndef MENU_NO_TRACK
            TrackMouseEvent(&te);
#endif
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0; }
    case WM_TIMER:
        if (wParam == TIP_TIMER) {
            if (g_tip || g_hotBtn < 0 || g_active) TipHide();
            else TipShow(g_hotBtn);
        }
        return 0;
    case WM_MOUSELEAVE:
        TipTrack(-1);
        g_hot = -1; g_hotBtn = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int index = BarHit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        int button = index < 0 ? BtnHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
        TipHide();
        if (index >= 0) { g_hot = -1; RunMenu(GetParent(hwnd), index, 1, NULL, 0, 0, 0); }
        else if (button >= 0) PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(g_btn[button].id, 0), 0);
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND MenuBarCreate(HWND parent, MenuStateFn stateFn) {
    g_stateFn = stateFn;
    RegClass(L"mp_popup", PopProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_tip", TipProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_menubar", BarProc, CS_DBLCLKS, NULL);
    BarLayout();
    g_bar = CreateWindowExW(0, L"mp_menubar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, g_barH, parent, NULL, g_hinst, NULL);
    return g_bar;
}
