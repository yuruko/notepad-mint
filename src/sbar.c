#include "mp.h"

#define SBT_REPEAT   1
#define SBT_POLL     2
#define POLL_MS      40
#define REPEAT_FIRST 400
#define REPEAT_NEXT  50

enum { P_NONE, P_ARROW1, P_ARROW2, P_PAGE1, P_THUMB, P_PAGE2, P_CORNER };

typedef struct { HWND target; int horz; } SbInit;

typedef struct Sb {
    HWND self, target;
    int horz;
    int shown;
    RECT bounds;
    int corner;
    SCROLLINFO scrollInfo;
    int part, over;
    int grabOffset, dragStart;
    int mouseX, mouseY;
} Sb;

typedef struct { int length, arrowSize, trackStart, trackEnd, thumbStart, thumbEnd, enabled; } Geo;

static struct { HWND target; Sb *vBar, *hBar; int trimR, trimB; } g_tab[8];
static HBRUSH g_dither;

static int ReadSi(const Sb *bar, SCROLLINFO *scrollInfo) {
    memset(scrollInfo, 0, sizeof *scrollInfo);
    scrollInfo->cbSize = sizeof *scrollInfo;
    scrollInfo->fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    return GetScrollInfo(bar->target, bar->horz ? SB_HORZ : SB_VERT, scrollInfo);
}

static void Measure(const Sb *bar, int width, int height, Geo *geo) {
    int thickness = bar->horz ? height : width;
    int range = bar->scrollInfo.nMax - bar->scrollInfo.nMin + 1, page = (int)bar->scrollInfo.nPage;
    int trackLen, thumbLen, maxPos, pos;
    geo->length = bar->horz ? width : height - bar->corner;
    geo->arrowSize = thickness;
    if (geo->length < 2 * geo->arrowSize) geo->arrowSize = geo->length / 2;
    geo->trackStart = geo->arrowSize;
    geo->trackEnd = geo->length - geo->arrowSize;
    trackLen = geo->trackEnd - geo->trackStart;
    geo->thumbStart = geo->thumbEnd = 0;
    geo->enabled = (page > 0 ? range > page : range > 1) && trackLen > 0;
    if (!geo->enabled) return;
    thumbLen = page > 0 ? MulDiv(trackLen, page, range) : thickness;
    if (thumbLen < thickness) thumbLen = thickness;
    if (thumbLen > trackLen) thumbLen = trackLen;
    maxPos = range - (page > 0 ? page : 0);
    pos = ClampInt(bar->scrollInfo.nPos - bar->scrollInfo.nMin, 0, maxPos);
    geo->thumbStart = geo->trackStart + (maxPos > 0 ? MulDiv(trackLen - thumbLen, pos, maxPos) : 0);
    geo->thumbEnd = geo->thumbStart + thumbLen;
}

static int Hit(const Sb *bar, int x, int y, const Geo *geo) {
    int axisPos = bar->horz ? x : y;
    if (!bar->horz && y >= geo->length) return P_CORNER;
    if (!geo->enabled) return P_NONE;
    if (axisPos < geo->arrowSize) return P_ARROW1;
    if (axisPos >= geo->length - geo->arrowSize) return P_ARROW2;
    if (axisPos < geo->thumbStart) return P_PAGE1;
    if (axisPos >= geo->thumbEnd) return P_PAGE2;
    return P_THUMB;
}

static void Seg(const Sb *bar, int from, int to, int width, int height, RECT *rect) {
    if (bar->horz) { rect->left = from; rect->right = to; rect->top = 0; rect->bottom = height; }
    else         { rect->left = 0; rect->right = width; rect->top = from; rect->bottom = to; }
}

static void Glyph(HDC hdc, const RECT *rect, int dir, COLORREF color, int offset) {
    POINT points[3];
    int size = rect->right - rect->left < rect->bottom - rect->top ? rect->right - rect->left : rect->bottom - rect->top;
    int height = (size * 3 + 8) / 17, halfWidth, centerX, centerY, boxLeft, boxTop;
    HGDIOBJ oldPen, oldBrush;
    if (height < 2) height = 2;
    halfWidth = height;
    centerX = (rect->left + rect->right) / 2 + offset;
    centerY = (rect->top + rect->bottom) / 2 + offset;
    boxLeft = centerX - (height + 1) / 2;
    boxTop = centerY - (height + 1) / 2;
    switch (dir) {
    case 0: points[0].x = centerX; points[0].y = boxTop;      points[1].x = centerX - halfWidth; points[1].y = boxTop + height; points[2].x = centerX + halfWidth; points[2].y = boxTop + height; break;
    case 1: points[0].x = centerX; points[0].y = boxTop + height; points[1].x = centerX - halfWidth; points[1].y = boxTop;      points[2].x = centerX + halfWidth; points[2].y = boxTop;      break;
    case 2: points[0].x = boxLeft; points[0].y = centerY;      points[1].x = boxLeft + height; points[1].y = centerY - halfWidth; points[2].x = boxLeft + height; points[2].y = centerY + halfWidth; break;
    default: points[0].x = boxLeft + height; points[0].y = centerY; points[1].x = boxLeft; points[1].y = centerY - halfWidth;      points[2].x = boxLeft;      points[2].y = centerY + halfWidth; break;
    }
    oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    SetDCPenColor(hdc, color);
    SetDCBrushColor(hdc, color);
    Polygon(hdc, points, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
}

static void Dither(HDC hdc, const RECT *rect) {
    static const WORD bits[8] = { 0x00AA, 0x0055, 0x00AA, 0x0055, 0x00AA, 0x0055, 0x00AA, 0x0055 };
    if (!g_dither) {
        HBITMAP bitmap = CreateBitmap(8, 8, 1, 1, bits);
        g_dither = bitmap ? CreatePatternBrush(bitmap) : NULL;
        if (bitmap) DeleteObject(bitmap);
    }
    if (!g_dither) { FillC(hdc, rect, C_FACE); return; }
    SetTextColor(hdc, C_FACE);
    SetBkColor(hdc, ThemeGet() == THEME_LIGHT ? C_HI : C_FACE2);
    FillRect(hdc, rect, g_dither);
}

static void Button(HDC hdc, const RECT *rect, int pressed) {
    FillC(hdc, rect, C_FACE);
    Bevel(hdc, rect, pressed ? BV_FLAT_DN : BV_RAISED);
}

static void Paint(Sb *bar, HDC hdc, int width, int height) {
    Geo geo;
    RECT rect, client;
    int firstDir = bar->horz ? 2 : 0, secondDir = bar->horz ? 3 : 1, offset;
    COLORREF glyphColor;
    client.left = 0; client.top = 0; client.right = width; client.bottom = height;
    FillC(hdc, &client, C_FACE);
    Measure(bar, width, height, &geo);
    glyphColor = geo.enabled ? C_TEXT : C_DIM;
    Seg(bar, geo.trackStart, geo.trackEnd, width, height, &rect);
    if (rect.right > rect.left && rect.bottom > rect.top) Dither(hdc, &rect);
    Seg(bar, 0, geo.arrowSize, width, height, &rect);
    offset = (geo.enabled && bar->part == P_ARROW1 && bar->over) ? 1 : 0;
    Button(hdc, &rect, offset);
    Glyph(hdc, &rect, firstDir, glyphColor, offset);
    Seg(bar, geo.length - geo.arrowSize, geo.length, width, height, &rect);
    offset = (geo.enabled && bar->part == P_ARROW2 && bar->over) ? 1 : 0;
    Button(hdc, &rect, offset);
    Glyph(hdc, &rect, secondDir, glyphColor, offset);
    if (geo.enabled) {
        int thumbStart = geo.thumbStart, thumbEnd = geo.thumbEnd;
        if (bar->part == P_THUMB) { thumbEnd = bar->dragStart + (geo.thumbEnd - geo.thumbStart); thumbStart = bar->dragStart; }
        Seg(bar, thumbStart, thumbEnd, width, height, &rect);
        Button(hdc, &rect, 0);
    }
}

static void PaintTo(Sb *bar, HDC hdc) {
    RECT client;
    HDC memDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap;
    GetClientRect(bar->self, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    memDc = CreateCompatibleDC(hdc);
    bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    oldBitmap = SelectObject(memDc, bitmap);
    Paint(bar, memDc, client.right, client.bottom);
    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
}

static int IsClass(HWND hwnd, const WCHAR *name) {
    WCHAR className[24];
    return GetClassNameW(hwnd, className, COUNTOF(className)) && wcmpi(className, name) == 0;
}

static void Step(Sb *bar, int part) {
    UINT code = part == P_ARROW1 ? SB_LINEUP : part == P_ARROW2 ? SB_LINEDOWN : part == P_PAGE1 ? SB_PAGEUP : SB_PAGEDOWN;
    SendMessageW(bar->target, bar->horz ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(code, 0), 0);
}

static void ScrollTo(Sb *bar, int pos) {
    SCROLLINFO scrollInfo;
    int maxPos, firstLine;
    if (!ReadSi(bar, &scrollInfo)) return;
    maxPos = scrollInfo.nMax - (int)scrollInfo.nPage + 1;
    pos = ClampInt(pos, scrollInfo.nMin, maxPos > scrollInfo.nMin ? maxPos : scrollInfo.nMin);
    if (!bar->horz && IsClass(bar->target, L"Edit")) {
        firstLine = (int)SendMessageW(bar->target, EM_GETFIRSTVISIBLELINE, 0, 0);
        if (pos != firstLine) SendMessageW(bar->target, EM_LINESCROLL, 0, (LPARAM)(pos - firstLine));
    } else if (!bar->horz && IsClass(bar->target, L"ListBox")) {
        SendMessageW(bar->target, LB_SETTOPINDEX, (WPARAM)pos, 0);
    } else if (pos != scrollInfo.nPos) {
        SendMessageW(bar->target, bar->horz ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (WORD)ClampInt(pos, 0, 65535)), 0);
    }
}

static void Pointer(Sb *bar, LPARAM lParam) {
    bar->mouseX = (short)LOWORD(lParam);
    bar->mouseY = (short)HIWORD(lParam);
}

static void Drag(Sb *bar) {
    RECT client;
    Geo geo;
    int axisPos, thumbLen, trackLen, newPos, range, page, maxPos;
    GetClientRect(bar->self, &client);
    Measure(bar, client.right, client.bottom, &geo);
    if (!geo.enabled) return;
    axisPos = bar->horz ? bar->mouseX : bar->mouseY;
    thumbLen = geo.thumbEnd - geo.thumbStart;
    trackLen = geo.trackEnd - geo.trackStart;
    newPos = ClampInt(axisPos - bar->grabOffset, geo.trackStart, geo.trackEnd - thumbLen);
    bar->dragStart = newPos;
    range = bar->scrollInfo.nMax - bar->scrollInfo.nMin + 1;
    page = (int)bar->scrollInfo.nPage;
    maxPos = range - (page > 0 ? page : 0);
    if (trackLen > thumbLen) ScrollTo(bar, bar->scrollInfo.nMin + MulDiv(newPos - geo.trackStart, maxPos, trackLen - thumbLen));
    InvalidateRect(bar->self, NULL, FALSE);
}

static void Release(Sb *bar) {
    KillTimer(bar->self, SBT_REPEAT);
    bar->part = P_NONE;
    bar->over = 0;
    InvalidateRect(bar->self, NULL, FALSE);
}

static void Sync(Sb *bar);

static LRESULT CALLBACK SbProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Sb *bar = (Sb *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        const SbInit *init = (const SbInit *)((CREATESTRUCTW *)lParam)->lpCreateParams;
        bar = (Sb *)mem_zalloc(sizeof *bar);
        if (!bar) return FALSE;
        bar->self = hwnd;
        bar->target = init->target;
        bar->horz = init->horz;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)bar);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!bar) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_NCDESTROY: {
        int i;
        KillTimer(hwnd, SBT_REPEAT);
        KillTimer(hwnd, SBT_POLL);
        for (i = 0; i < COUNTOF(g_tab); i++) {
            if (g_tab[i].vBar == bar) g_tab[i].vBar = NULL;
            if (g_tab[i].hBar == bar) g_tab[i].hBar = NULL;
            if (g_tab[i].target && !g_tab[i].vBar && !g_tab[i].hBar) g_tab[i].target = NULL;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        mem_free(bar);
        break; }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintTo(bar, hdc);
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_PRINTCLIENT:
        PaintTo(bar, (HDC)wParam);
        return 0;
    case WM_MOUSEWHEEL:
        return SendMessageW(bar->target, msg, wParam, lParam);
    case WM_LBUTTONDOWN: {
        RECT client;
        Geo geo;
        int part;
        GetClientRect(hwnd, &client);
        Sync(bar);
        Measure(bar, client.right, client.bottom, &geo);
        Pointer(bar, lParam);
        part = Hit(bar, bar->mouseX, bar->mouseY, &geo);
        if (part == P_NONE || part == P_CORNER) return 0;
        SetCapture(hwnd);
        bar->part = part;
        bar->over = 1;
        if (part == P_THUMB) {
            bar->grabOffset = (bar->horz ? bar->mouseX : bar->mouseY) - geo.thumbStart;
            bar->dragStart = geo.thumbStart;
        } else {
            Step(bar, part);
            SetTimer(hwnd, SBT_REPEAT, REPEAT_FIRST, NULL);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0; }
    case WM_MOUSEMOVE:
        Pointer(bar, lParam);
        if (bar->part == P_THUMB) {
            Drag(bar);
        } else if (bar->part) {
            RECT client;
            Geo geo;
            int over;
            GetClientRect(hwnd, &client);
            Measure(bar, client.right, client.bottom, &geo);
            over = Hit(bar, bar->mouseX, bar->mouseY, &geo) == bar->part;
            if (over != bar->over) { bar->over = over; InvalidateRect(hwnd, NULL, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();
        Release(bar);
        return 0;
    case WM_CAPTURECHANGED:
        Release(bar);
        return 0;
    case WM_TIMER:
        if (wParam == SBT_POLL) {
            Sync(bar);
        } else if (wParam == SBT_REPEAT) {
            RECT client;
            Geo geo;
            SetTimer(hwnd, SBT_REPEAT, REPEAT_NEXT, NULL);
            if (bar->part && bar->part != P_THUMB) {
                Sync(bar);
                GetClientRect(hwnd, &client);
                Measure(bar, client.right, client.bottom, &geo);
                bar->over = Hit(bar, bar->mouseX, bar->mouseY, &geo) == bar->part;
                if (bar->over) Step(bar, bar->part);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Strips(HWND target, RECT *windowRect, int *leftStrip, int *rightStrip, int *bottomStrip, int trimR, int trimB) {
    RECT client;
    POINT origin;
    GetWindowRect(target, windowRect);
    GetClientRect(target, &client);
    origin.x = 0; origin.y = 0;
    ClientToScreen(target, &origin);
    *leftStrip = origin.x - windowRect->left;
    *rightStrip = windowRect->right - (origin.x + client.right);
    *bottomStrip = windowRect->bottom - (origin.y + client.bottom);
    if (GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_LEFTSCROLLBAR) { windowRect->left += trimR; *leftStrip -= trimR; }
    else { windowRect->right -= trimR; *rightStrip -= trimR; }
    windowRect->bottom -= trimB; *bottomStrip -= trimB;
    if (*leftStrip < 0) *leftStrip = 0;
    if (*rightStrip < 0) *rightStrip = 0;
    if (*bottomStrip < 0) *bottomStrip = 0;
}

static void Place(Sb *bar, int visible, const RECT *screenRect) {
    SCROLLINFO scrollInfo;
    RECT bounds;
    POINT topLeft, bottomRight;
    int changed = 0;
    if (!visible) {
        if (bar->shown) { ShowWindow(bar->self, SW_HIDE); bar->shown = 0; }
        return;
    }
    topLeft.x = screenRect->left; topLeft.y = screenRect->top; bottomRight.x = screenRect->right; bottomRight.y = screenRect->bottom;
    ScreenToClient(GetParent(bar->self), &topLeft);
    ScreenToClient(GetParent(bar->self), &bottomRight);
    bounds.left = topLeft.x; bounds.top = topLeft.y; bounds.right = bottomRight.x; bounds.bottom = bottomRight.y;
    if (!bar->shown || bounds.left != bar->bounds.left || bounds.top != bar->bounds.top || bounds.right != bar->bounds.right || bounds.bottom != bar->bounds.bottom) {
        SetWindowPos(bar->self, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        bar->bounds = bounds;
        bar->shown = 1;
        changed = 1;
    }
    if (ReadSi(bar, &scrollInfo) && (scrollInfo.nMin != bar->scrollInfo.nMin || scrollInfo.nMax != bar->scrollInfo.nMax || scrollInfo.nPage != bar->scrollInfo.nPage || scrollInfo.nPos != bar->scrollInfo.nPos)) {
        bar->scrollInfo = scrollInfo;
        changed = 1;
    }
    if (changed) InvalidateRect(bar->self, NULL, FALSE);
}

static void SyncEntry(int i) {
    HWND target = g_tab[i].target;
    Sb *vBar = g_tab[i].vBar, *hBar = g_tab[i].hBar;
    RECT windowRect, barRect;
    int leftStrip, rightStrip, bottomStrip, vertWidth, onLeft, visible;
    if (!target || !IsWindow(target)) return;
    Strips(target, &windowRect, &leftStrip, &rightStrip, &bottomStrip, g_tab[i].trimR, g_tab[i].trimB);
    visible = (GetWindowLongPtrW(target, GWL_STYLE) & WS_VISIBLE) != 0;
    vertWidth = leftStrip > rightStrip ? leftStrip : rightStrip;
    onLeft = leftStrip > rightStrip;
    if (vBar) {
        barRect.left = onLeft ? windowRect.left : windowRect.right - vertWidth; barRect.right = barRect.left + vertWidth;
        barRect.top = windowRect.top; barRect.bottom = windowRect.bottom;
        if (vBar->corner != bottomStrip) { vBar->corner = bottomStrip; InvalidateRect(vBar->self, NULL, FALSE); }
        Place(vBar, visible && vertWidth > 0, &barRect);
    }
    if (hBar) {
        barRect.left = windowRect.left + (onLeft ? vertWidth : 0); barRect.right = windowRect.right - (onLeft ? 0 : vertWidth);
        barRect.top = windowRect.bottom - bottomStrip; barRect.bottom = windowRect.bottom;
        Place(hBar, visible && bottomStrip > 0, &barRect);
    }
}

static void Sync(Sb *bar) {
    int i;
    for (i = 0; i < COUNTOF(g_tab); i++) if (g_tab[i].target == bar->target) { SyncEntry(i); return; }
}

static int Find(HWND target) {
    int i;
    for (i = 0; i < COUNTOF(g_tab); i++) if (g_tab[i].target == target) return i;
    return -1;
}

static Sb *Make(HWND target, int horz) {
    SbInit init;
    HWND hwnd;
    init.target = target;
    init.horz = horz;
    hwnd = CreateWindowExW(0, L"mp_sbar", NULL, WS_CHILD, 0, 0, 0, 0, GetParent(target), NULL, g_hinst, &init);
    return hwnd ? (Sb *)GetWindowLongPtrW(hwnd, GWLP_USERDATA) : NULL;
}

void SbarAttach(HWND target) {
    static int reg;
    int i;
    LONG_PTR style;
    if (!target || Find(target) >= 0) return;
    if (!reg) { RegClass(L"mp_sbar", SbProc, 0, NULL); reg = 1; }
    style = GetWindowLongPtrW(target, GWL_STYLE);
    if (!(style & WS_CLIPSIBLINGS)) {
        SetWindowLongPtrW(target, GWL_STYLE, style | WS_CLIPSIBLINGS);
        SetWindowPos(target, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    for (i = 0; i < COUNTOF(g_tab) && g_tab[i].target; i++) {}
    if (i == COUNTOF(g_tab)) return;
    g_tab[i].target = target;
    g_tab[i].trimR = g_tab[i].trimB = 0;
    g_tab[i].vBar = Make(target, 0);
    g_tab[i].hBar = Make(target, 1);
    if (g_tab[i].vBar) SetTimer(g_tab[i].vBar->self, SBT_POLL, POLL_MS, NULL);
    SyncEntry(i);
}

void SbarTrim(HWND target, int right, int bottom) {
    int i = Find(target);
    if (i < 0) return;
    g_tab[i].trimR = right;
    g_tab[i].trimB = bottom;
    SyncEntry(i);
}

void SbarSync(HWND target) {
    int i = Find(target);
    if (i >= 0) SyncEntry(i);
}

void SbarDetach(HWND target) {
    int i = Find(target);
    Sb *vBar, *hBar;
    if (i < 0) return;
    vBar = g_tab[i].vBar;
    hBar = g_tab[i].hBar;
    g_tab[i].target = NULL;
    g_tab[i].vBar = g_tab[i].hBar = NULL;
    if (vBar) DestroyWindow(vBar->self);
    if (hBar) DestroyWindow(hBar->self);
}
