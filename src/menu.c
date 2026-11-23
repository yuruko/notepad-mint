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
static int         g_itemL[MENU_BAR_COUNT + 1];
static BOOL        g_active, g_done, g_cancel;
static int         g_result;
static MSG         g_redisp;
static BOOL        g_haveRedisp;
static RECT        g_doubleRect;
static DWORD       g_doubleTime;
static int         g_doubleArmed;
static int         g_cues;
static int         g_hotTheme;

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
    HGDIOBJ oldFont = SelectObject(hdc, g_fontBar);
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

int MenuBarMinWidth(void) {
    if (!g_barH) BarLayout();
    return g_itemL[MENU_BAR_COUNT] + S(BAR_THEME_W);
}

static void ThemeRect(HWND hwnd, RECT *rect) {
    GetClientRect(hwnd, rect);
    rect->left = rect->right - S(BAR_THEME_W);
    rect->bottom -= S(1);
}

static BOOL ThemeHit(HWND hwnd, int x, int y) {
    RECT rect;
    POINT pt;
    ThemeRect(hwnd, &rect);
    pt.x = x; pt.y = y;
    return PtInRect(&rect, pt);
}

static void BarPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps), memDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap, oldFont;
    RECT client, rect;
    int i;
    COLORREF idleText = RGB(
        GetRValue(C_FACE) + (GetRValue(C_TEXT) - GetRValue(C_FACE)) * 80 / 100,
        GetGValue(C_FACE) + (GetGValue(C_TEXT) - GetGValue(C_FACE)) * 80 / 100,
        GetBValue(C_FACE) + (GetBValue(C_TEXT) - GetBValue(C_FACE)) * 80 / 100);
    GetClientRect(hwnd, &client);
    memDc = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    oldBitmap = SelectObject(memDc, bitmap);
    oldFont = SelectObject(memDc, g_fontBar);
    FillC(memDc, &client, C_FACE);
    for (i = 0; i < (int)MENU_BAR_COUNT; i++) {
        BOOL on = (i == g_open || i == g_kbd || i == g_hot);
        rect.left = g_itemL[i]; rect.right = g_itemL[i + 1];
        rect.top = 0; rect.bottom = client.bottom - S(1);
        if (on) FillC(memDc, &rect, C_ACCENT);
        TextC(memDc, g_menuEntries[i].title, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | CueFormat(), on ? C_ON_ACCENT : idleText);
    }
    {
        int iconSize = S(BAR_BTN_ICON);
        ThemeRect(hwnd, &rect);
        if (g_hotTheme) FillC(memDc, &rect, C_ACCENT);
        ThemeIconDraw(memDc, (rect.left + rect.right - iconSize) / 2, (rect.top + rect.bottom - iconSize) / 2, iconSize, g_hotTheme ? C_ON_ACCENT : C_TEXT, g_hotTheme ? C_ACCENT : C_FACE, g_hotTheme ? 100 : BAR_BTN_OPACITY);
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldFont);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd, &ps);
}

static void PopMeasure(Pop *pop) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ oldFont = SelectObject(hdc, g_fontBar);
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
    SelectObject(hdc, g_fontBar);
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
    g_owner = owner; g_np = 0; g_hot = -1;
    g_barIdx = root ? -1 : barIdx;
    g_open = -1; g_kbd = -1;
    if (root) {
        PopOpen(0, root, x, y, anchorBottom, 0);
        if (openPopup == -1) MoveSel(0, 1);
        else if (openPopup >= 0) Activate(0, openPopup);
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
    RunMenu(owner, -1, -2, def, x, y, anchorBottom);
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
        int theme = ThemeHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (index != g_hot || theme != g_hotTheme) {
            TRACKMOUSEEVENT te;
            if (theme != g_hotTheme) {
                RECT rect;
                ThemeRect(hwnd, &rect);
                TipTrack(hwnd, theme ? &rect : NULL, L"toggle dark / light theme", IDM_THEME_TOGGLE, 1);
            }
            g_hot = index;
            g_hotTheme = theme;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = hwnd; te.dwHoverTime = 0;
#ifndef MENU_NO_TRACK
            TrackMouseEvent(&te);
#endif
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0; }
    case WM_MOUSELEAVE:
        if (g_hotTheme) TipTrack(hwnd, NULL, NULL, 0, 0);
        g_hot = -1;
        g_hotTheme = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int index = BarHit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (ThemeHit(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) { TipHide(); PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDM_THEME_TOGGLE, 0), 0); return 0; }
        if (index >= 0) { g_hot = -1; RunMenu(GetParent(hwnd), index, 1, NULL, 0, 0, 0); }
        return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND MenuBarCreate(HWND parent, MenuStateFn stateFn) {
    g_stateFn = stateFn;
    RegClass(L"mp_popup", PopProc, CS_DROPSHADOW, NULL);
    RegClass(L"mp_menubar", BarProc, CS_DBLCLKS, NULL);
    BarLayout();
    g_bar = CreateWindowExW(0, L"mp_menubar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, g_barH, parent, NULL, g_hinst, NULL);
    return g_bar;
}
