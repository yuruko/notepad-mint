#include "edit_internal.h"

#define IDC_EDIT 100
#define SIZE_TIMER 0x4D50

HWND g_edit;

static WNDPROC g_orig;
static HFONT   g_font;
static HBRUSH  g_brEdit;
static int     g_wheel, g_hwheel;
static int     g_dragPress, g_dragBypass;
static WPARAM  g_dragKeys;
static LPARAM  g_dragDown;
static int     g_paintDepth;
static int     g_shiftDir;
static int     g_stubW, g_stubMax;

#define WM_BARS (WM_APP + 7)
static int g_barsPending, g_inBars;
static int g_revealPending, g_sizeDeferred;
static int g_allowTallReveal, g_revealing, g_tallCaret;
static int g_invisibles, g_invisSync;

static void InvisiblesPaint(HWND hwnd, HDC hdc, const RECT *clip);
static void InvisiblesSync(HWND hwnd);

BOOL EditInvisiblesShown(void) { return g_invisibles != 0; }

void EditShowInvisibles(BOOL show) {
    if (g_invisibles == (show != 0)) return;
    g_invisibles = show != 0;
    if (g_edit) {
        InvalidateRect(g_edit, NULL, FALSE);
        UpdateWindow(g_edit);
    }
}

void EditScrollSoon(void) {
    if (g_edit && !g_barsPending && !g_inBars) { g_barsPending = 1; PostMessageW(g_edit, WM_BARS, 0, 0); }
}

static int LineHeight(void) {
    static int lh;
    static HFONT lf;
    HDC hdc;
    HGDIOBJ oldFont;
    TEXTMETRICW tm;
    if (lh && lf == g_font) return lh;
    hdc = GetDC(g_edit);
    if (!hdc) return 1;
    memset(&tm, 0, sizeof tm);
    oldFont = SelectObject(hdc, g_font);
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, oldFont);
    ReleaseDC(g_edit, hdc);
    lf = g_font;
    return lh = tm.tmHeight + tm.tmExternalLeading > 0 ? tm.tmHeight + tm.tmExternalLeading : 1;
}

static void FitTallCaret(HWND hwnd) {
    if (!g_allowTallReveal || g_inBars || g_revealing) return;
    g_revealing = 1;
    EditCaretFit(hwnd, g_font, g_orig, &g_tallCaret);
    g_revealing = 0;
}

static void RevealCaret(HWND hwnd) {
    if (g_revealPending && !g_inBars && !g_sizeDeferred) {
        SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    }
}

static int VNeeded(void) {
    RECT formatRect;
    int lines = (int)SendMessageW(g_edit, EM_GETLINECOUNT, 0, 0), visibleLines;
    SendMessageW(g_edit, EM_GETRECT, 0, (LPARAM)&formatRect);
    visibleLines = (formatRect.bottom - formatRect.top) / LineHeight();
    if (visibleLines < 1) visibleLines = 1;
    return lines > visibleLines;
}

static int HNeeded(void) {
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE;
    if (!GetScrollInfo(g_edit, SB_HORZ, &si)) return 0;
    DBG(L"bar h range/page", si.nMax - si.nMin + 1, si.nPage);
    return si.nMax - si.nMin + 1 > (int)si.nPage;
}

#define BARS(e) (GetWindowLongPtrW(e, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL))

static void UpdateBars(void) {
    int pass;
    LONG_PTR before;
    if (!g_edit) return;
    before = BARS(g_edit);
    g_inBars = 1;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    for (pass = 0; pass < 4; pass++) {
        int changed = 0, needV = VNeeded(), hadV = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_VSCROLL) != 0;
        if (needV != hadV) {
            if (!needV) {
                int firstLine = (int)SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
                if (firstLine) SendMessageW(g_edit, EM_LINESCROLL, 0, (LPARAM)-firstLine);
            }
            ShowScrollBar(g_edit, SB_VERT, needV);
            changed = 1;
        }
        if (!g_pf.wrap) {
            int hadH = (GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_HSCROLL) != 0, needH;
            if (!hadH) ShowScrollBar(g_edit, SB_HORZ, TRUE);
            needH = HNeeded();
            DBG(L"bars pass v/h", needV, needH);
            if (!needH) {
                SendMessageW(g_edit, EM_LINESCROLL, (WPARAM)-100000, 0);
                ShowScrollBar(g_edit, SB_HORZ, FALSE);
            }
            if (needH != hadH) changed = 1;
        }
        if (!changed) break;
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    if (BARS(g_edit) != before) RedrawWindow(g_edit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    g_inBars = 0;
    RevealCaret(g_edit);
    if (!g_sizeDeferred) g_revealPending = 0;
    SbarSync(g_edit);
}

static int CALLBACK FoundCb(const void *logFont, const void *textMetric, DWORD fontType, LPARAM lParam) {
    (void)logFont; (void)textMetric; (void)fontType;
    *(int *)lParam = 1;
    return 0;
}

static BOOL FontInstalled(const WCHAR *face) {
    LOGFONTW lf;
    int found = 0;
    HDC hdc = GetDC(NULL);
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    wcopy(lf.lfFaceName, face, 32);
    EnumFontFamiliesExW(hdc, &lf, FoundCb, (LPARAM)&found, 0);
    ReleaseDC(NULL, hdc);
    return found != 0;
}

void FontResolve(WCHAR *face) {
    static const WCHAR *const fb[] = { L"Consolas", L"Lucida Console", L"Courier New" };
    int i;
    if (face[0] && FontInstalled(face)) return;
    for (i = 0; i < COUNTOF(fb); i++) {
        if (FontInstalled(fb[i])) { wcopy(face, fb[i], 32); return; }
    }
    wcopy(face, L"Courier New", 32);
}

void EditViewRect(HWND hwnd, RECT *viewRect) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    GetClientRect(hwnd, viewRect);
    if (!(style & WS_VSCROLL)) {
        if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LEFTSCROLLBAR) viewRect->left += S(SBAR_TRIM);
        else viewRect->right -= S(SBAR_TRIM);
    }
    if (!(style & WS_HSCROLL)) viewRect->bottom -= S(SBAR_TRIM);
}

static void EditPad2(HWND hwnd, int margins) {
    RECT formatRect;
    int pad = S(EDIT_PAD), top = S(EDIT_PAD_TOP), height = LineHeight(), visibleBottom;
    if (margins) SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    EditViewRect(hwnd, &formatRect);
    visibleBottom = formatRect.bottom;
    if (formatRect.bottom - formatRect.top > top + pad + 8 && formatRect.right - formatRect.left > 2 * pad + 8) {
        formatRect.left += pad;
        if (g_pf.wrap || EditIsRtl()) formatRect.right -= pad;
        formatRect.top += top; formatRect.bottom -= pad;
    }
    if (formatRect.right <= formatRect.left) formatRect.right = formatRect.left + 1;
    if (formatRect.bottom <= formatRect.top) formatRect.bottom = formatRect.top + 1;
    if (formatRect.bottom - formatRect.top < height) {
        formatRect.top = 0;
        formatRect.bottom = visibleBottom > height ? visibleBottom : height;
    }
    SendMessageW(hwnd, EM_SETRECTNP, 0, (LPARAM)&formatRect);
}

static void EditPad(HWND hwnd) { EditPad2(hwnd, 1); }

static int g_tabPx;

void EditApplyTabs(void) {
    HDC hdc;
    HGDIOBJ oldFont;
    TEXTMETRICW tm;
    UINT tabStop = 4u * (UINT)g_pf.tab;
    if (!g_edit || !g_font) return;
    g_allowTallReveal = 1;
    EditCaretInvalidate();
    KillTimer(g_edit, SIZE_TIMER);
    g_sizeDeferred = 0;
    hdc = GetDC(g_edit);
    oldFont = SelectObject(hdc, g_font);
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, oldFont);
    ReleaseDC(g_edit, hdc);
    g_tabPx = g_pf.tab * tm.tmAveCharWidth;
    SendMessageW(g_edit, EM_SETTABSTOPS, 1, (LPARAM)&tabStop);
    EditPad(g_edit);
    g_revealPending = 1;
    RevealCaret(g_edit);
    InvalidateRect(g_edit, NULL, TRUE);
    EditScrollSoon();
}

void EditApplyFont(void) {
    LOGFONTW lf;
    HFONT newFont, oldFont = g_font;
    int px = MulDiv(g_pf.cur, g_dpi, 720);
    if (px < 1) px = 1;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -px;
    lf.lfWeight = g_pf.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)(g_pf.italic ? 1 : 0);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcopy(lf.lfFaceName, g_pf.font, 32);
    newFont = CreateFontIndirectW(&lf);
    if (!newFont) return;
    if (g_edit) EditCaretRemember(g_edit, g_font);
    g_font = newFont;
    g_stubW = 0;
    if (g_edit) {
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)newFont, TRUE);
        EditApplyTabs();
    }
    if (oldFont) DeleteObject(oldFont);
}

HBRUSH EditBrush(void) {
    if (!g_brEdit) g_brEdit = CreateSolidBrush(C_EDIT_BG);
    return g_brEdit;
}

void EditApplyColors(void) {
    HBRUSH oldBrush = g_brEdit;
    g_brEdit = CreateSolidBrush(C_EDIT_BG);
    if (g_edit) InvalidateRect(g_edit, NULL, TRUE);
    if (oldBrush) DeleteObject(oldBrush);
}

static const int g_ladder[] = { 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 220, 240, 260, 280, 300, 320, 340, 360,
                                400, 440, 480 };

static int Ladder(int current, int dir) {
    int i;
    if (dir > 0) {
        for (i = 0; i < COUNTOF(g_ladder); i++) if (g_ladder[i] > current) return g_ladder[i];
    } else {
        for (i = COUNTOF(g_ladder) - 1; i >= 0; i--) if (g_ladder[i] < current) return g_ladder[i];
    }
    return current;
}

BOOL EditZoomCan(int dir) {
    return Ladder(g_pf.cur, dir) != g_pf.cur;
}

void EditZoomStep(int dir) {
    int newSize = Ladder(g_pf.cur, dir);
    if (newSize == g_pf.cur) return;
    g_pf.cur = newSize;
    EditApplyFont();
    AppUpdateStatus();
}

void EditZoomReset(void) {
    if (g_pf.cur == g_pf.pt) return;
    g_pf.cur = g_pf.pt;
    EditApplyFont();
    AppUpdateStatus();
}

#define RTL_BITS (WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR)

static void StubSync(HWND hwnd);
static void BandSync(HWND hwnd);

BOOL EditIsRtl(void) {
    return (GetWindowLongPtrW(g_edit, GWL_EXSTYLE) & WS_EX_RTLREADING) != 0;
}

BOOL EditSetRtl(BOOL rtl) {
    LONG_PTR exStyle;
    if (!g_edit || !rtl == !EditIsRtl()) return FALSE;
    exStyle = GetWindowLongPtrW(g_edit, GWL_EXSTYLE);
    EditCaretInvalidate();
    SetWindowLongPtrW(g_edit, GWL_EXSTYLE, rtl ? (exStyle | RTL_BITS) : (exStyle & ~(LONG_PTR)RTL_BITS));
    SetWindowPos(g_edit, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    EditPad(g_edit);
    g_revealPending = 1;
    RevealCaret(g_edit);
    InvalidateRect(g_edit, NULL, TRUE);
    EditScrollSoon();
    StubSync(g_edit);
    BandSync(g_edit);
    InvisiblesSync(g_edit);
    return TRUE;
}

void EditDirKeyCancel(void) { g_shiftDir = 0; }

int EditStrongDir(int limit) {
    void *lock;
    const WCHAR *text;
    WORD types[64];
    int length, i, j, chunk, dir = 0;
    text = EditLockText(&lock, &length);
    if (!text) return 0;
    if (length > limit) length = limit;
    for (i = 0; i < length && !dir; i += chunk) {
        chunk = length - i < COUNTOF(types) ? length - i : COUNTOF(types);
        if (!GetStringTypeW(2, text + i, chunk, types)) break;
        for (j = 0; j < chunk; j++) if (types[j] == 1 || types[j] == 2) { dir = types[j]; break; }
    }
    EditUnlockText(lock);
    return dir;
}

#define STUB_MAX 512
#ifdef SHOTDC
static int g_shotWiped, g_shotKept;
#endif

typedef struct StubGeom {
    RECT formatRect;
    int  firstLine, lineCount;
    int  originY, pitch, scrollX;
    int  stubWidth;
    int  maxRow;
} StubGeom;

typedef struct Stub { int row, x, y, dir; } Stub;

static struct { int count, row[STUB_MAX], docX[STUB_MAX], dir[STUB_MAX]; } g_stub;

static LRESULT Ctl(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return CallWindowProcW(g_orig, hwnd, msg, wParam, lParam);
}

static int StubHasSel(HWND hwnd) {
    DWORD selStart = 0, selEnd = 0;
    Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    return selEnd > selStart;
}

static void StubMetrics(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    SIZE sz;
    TEXTMETRICW tm;
    sz.cx = 0; sz.cy = 0;
    memset(&tm, 0, sizeof tm);
    if (hdc) {
        HGDIOBJ oldFont = SelectObject(hdc, g_font);
        GetTextExtentPoint32W(hdc, L" ", 1, &sz);
        GetTextMetricsW(hdc, &tm);
        SelectObject(hdc, oldFont);
        ReleaseDC(hwnd, hdc);
    }
    g_stubW = sz.cx > 3 ? (int)sz.cx : 3;
    g_stubMax = tm.tmMaxCharWidth > g_stubW ? (int)tm.tmMaxCharWidth : g_stubW;
}

static int StubGeomGet(HWND hwnd, StubGeom *geom) {
    int firstIndex, nextIndex;
    LRESULT firstPos, nextPos;
    Ctl(hwnd, EM_GETRECT, 0, (LPARAM)&geom->formatRect);
    geom->lineCount = (int)Ctl(hwnd, EM_GETLINECOUNT, 0, 0);
    geom->firstLine = (int)Ctl(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (geom->firstLine < 0 || geom->firstLine + 1 >= geom->lineCount) return 0;
    firstIndex = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)geom->firstLine, 0);
    nextIndex = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)(geom->firstLine + 1), 0);
    if (firstIndex < 0 || nextIndex < 0) return 0;
    firstPos = Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)firstIndex, 0);
    nextPos = Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)nextIndex, 0);
    geom->originY = (short)HIWORD(firstPos);
    geom->pitch = (short)HIWORD(nextPos) - geom->originY;
    if (geom->pitch <= 0) geom->pitch = LineHeight();
    geom->scrollX = EditIsRtl() ? 0 : geom->formatRect.left - (short)LOWORD(firstPos);
    if (!g_stubW) StubMetrics(hwnd);
    geom->stubWidth = g_stubW;
    geom->maxRow = 30000 / g_stubMax;
    return 1;
}

static void StubRect(RECT *rect, const StubGeom *geom, int x, int y, int dir) {
    rect->left = dir > 0 ? x : x - geom->stubWidth;
    rect->right = dir > 0 ? x + geom->stubWidth : x;
    rect->top = y;
    rect->bottom = y + geom->pitch;
}

static int StubList(HWND hwnd, const StubGeom *geom, Stub *out, int cap) {
    DWORD selStart = 0, selEnd = 0;
    HLOCAL hText;
    const WCHAR *text;
    int count = 0, row, lastRow, lineStart, nextStart, rtl = EditIsRtl();
    Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd <= selStart) return 0;
    hText = (HLOCAL)Ctl(hwnd, EM_GETHANDLE, 0, 0);
    text = hText ? (const WCHAR *)LocalLock(hText) : NULL;
    if (!text) return 0;
    lastRow = geom->firstLine + (geom->formatRect.bottom - geom->formatRect.top + geom->pitch - 1) / geom->pitch - 1;
    if (lastRow > geom->lineCount - 2) lastRow = geom->lineCount - 2;
    lineStart = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)geom->firstLine, 0);
    for (row = geom->firstLine; row <= lastRow && count < cap; row++) {
        int breakIndex;
        nextStart = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)(row + 1), 0);
        if (nextStart < 2) break;
        breakIndex = nextStart - 2;
        if (breakIndex >= lineStart && breakIndex - lineStart <= geom->maxRow && text[breakIndex] == '\r' && text[breakIndex + 1] == '\n' && (int)selStart <= breakIndex && (int)selEnd > breakIndex) {
            LRESULT breakPos = Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)breakIndex, 0);
            out[count].row = row;
            out[count].x = (short)LOWORD(breakPos);
            out[count].y = (short)HIWORD(breakPos);
            out[count].dir = 1;
            if (rtl) {
                LRESULT startPos = Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)lineStart, 0);
                if (out[count].x <= (short)LOWORD(startPos)) out[count].dir = -1;
            }
            count++;
        }
        lineStart = nextStart;
    }
    LocalUnlock(hText);
    return count;
}

static void StubDrawAll(HWND hwnd, HDC hdc, const StubGeom *geom, const Stub *stubs, int count, const RECT *clip) {
    int i, caretHidden = 0;
    HDC ownDc = NULL;
    if (!hdc) {
        ownDc = hdc = GetDC(hwnd);
        if (!hdc) return;
        caretHidden = GetFocus() == hwnd && HideCaret(hwnd);
    }
    for (i = 0; i < count; i++) {
        RECT rect;
        StubRect(&rect, geom, stubs[i].x, stubs[i].y, stubs[i].dir);
        if (!IntersectRect(&rect, &rect, &geom->formatRect)) continue;
        if (clip && !IntersectRect(&rect, &rect, clip)) continue;
        FillRect(hdc, &rect, (HBRUSH)(COLOR_HIGHLIGHT + 1));
    }
    if (caretHidden) ShowCaret(hwnd);
    if (ownDc) ReleaseDC(hwnd, ownDc);
}

static void StubPaint(HWND hwnd, HDC hdc, const RECT *clip) {
    StubGeom geom;
    Stub stubs[STUB_MAX];
    int count;
    if (hwnd != g_edit || !StubHasSel(hwnd) || !StubGeomGet(hwnd, &geom)) return;
    count = StubList(hwnd, &geom, stubs, STUB_MAX);
    if (count) StubDrawAll(hwnd, hdc, &geom, stubs, count, clip);
}

static void StubSync(HWND hwnd) {
    StubGeom geom;
    Stub stubs[STUB_MAX];
    int count = 0, i, j, invalidated = 0, visibleRows;
    if (hwnd != g_edit || !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE)) return;
    if (!StubHasSel(hwnd) && !g_stub.count) return;
    if (!StubGeomGet(hwnd, &geom)) { g_stub.count = 0; return; }
    count = StubList(hwnd, &geom, stubs, STUB_MAX);
    visibleRows = (geom.formatRect.bottom - geom.formatRect.top + geom.pitch - 1) / geom.pitch;
    for (i = 0, j = 0; i < g_stub.count; i++) {
        int row = g_stub.row[i];
        RECT rect;
        while (j < count && stubs[j].row < row) j++;
        if (j < count && stubs[j].row == row && stubs[j].dir == g_stub.dir[i] && stubs[j].x - geom.formatRect.left + geom.scrollX == g_stub.docX[i]) {
#ifdef SHOTDC
            HDC probeDc = GetDC(hwnd);
            if (probeDc) {
                int probeX = stubs[j].dir > 0 ? stubs[j].x + 2 : stubs[j].x - 3, probeY = stubs[j].y + geom.pitch / 2;
                if (probeX < geom.formatRect.right && GetPixel(probeDc, probeX, probeY) != GetSysColor(COLOR_HIGHLIGHT)) g_shotWiped++;
                g_shotKept++;
                ReleaseDC(hwnd, probeDc);
            }
#endif
            continue;
        }
        if (row < geom.firstLine || row >= geom.firstLine + visibleRows) continue;
        StubRect(&rect, &geom, geom.formatRect.left - geom.scrollX + g_stub.docX[i], geom.originY + (row - geom.firstLine) * geom.pitch, g_stub.dir[i]);
        if (IntersectRect(&rect, &rect, &geom.formatRect)) { InvalidateRect(hwnd, &rect, TRUE); invalidated = 1; }
    }
    if (invalidated) UpdateWindow(hwnd);
    if (count) StubDrawAll(hwnd, NULL, &geom, stubs, count, NULL);
    g_stub.count = count;
    for (i = 0; i < count; i++) { g_stub.row[i] = stubs[i].row; g_stub.docX[i] = stubs[i].x - geom.formatRect.left + geom.scrollX; g_stub.dir[i] = stubs[i].dir; }
}

static int StubWatch(UINT msg, WPARAM wParam) {
    switch (msg) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
    case WM_SETFONT: case WM_SETTEXT:
    case EM_SETSEL: case EM_SETRECTNP: case EM_SCROLL: case EM_LINESCROLL: case EM_SCROLLCARET: case EM_REPLACESEL: case EM_UNDO: case EM_SETMARGINS:
        return 1;
    case WM_IME_COMPOSITION: case WM_IME_CHAR: case WM_IME_ENDCOMPOSITION:
        return 1;
    case WM_MOUSEMOVE:
        return (wParam & 1) != 0;
    }
    return 0;
}

#define BAND_SEL_EXTRA 1

typedef struct BandGeom {
    RECT formatRect, band;
    RECT top, left;
    int  firstLine, lineCount, rows;
    int  originY, originX, pitch;
    int  bottomText;
    int  topText;
    int  leftText;
} BandGeom;

static int BandRect(HWND hwnd, BandGeom *geom) {
    RECT client, formatRect;
    if (EditIsRtl()) return 0;
    GetClientRect(hwnd, &client);
    Ctl(hwnd, EM_GETRECT, 0, (LPARAM)&formatRect);
    geom->formatRect = formatRect;
    geom->band.left = client.left;
    geom->band.right = client.right;
    geom->band.top = formatRect.bottom;
    geom->band.bottom = client.bottom - S(EDIT_BAND_INSET);
    geom->top.left = client.left; geom->top.right = client.right;
    geom->top.top = client.top; geom->top.bottom = formatRect.top;
    geom->left.left = client.left; geom->left.right = formatRect.left;
    geom->left.top = formatRect.top; geom->left.bottom = formatRect.bottom;
    return geom->band.bottom > geom->band.top && geom->band.right > geom->band.left;
}

static int BandGeomGet(HWND hwnd, BandGeom *geom) {
    int firstIndex, nextIndex;
    LRESULT firstPos, nextPos;
    memset(geom, 0, sizeof *geom);
    if (!BandRect(hwnd, geom)) return 0;
    geom->lineCount = (int)Ctl(hwnd, EM_GETLINECOUNT, 0, 0);
    geom->firstLine = (int)Ctl(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (geom->firstLine < 0) return 1;
    firstIndex = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)geom->firstLine, 0);
    nextIndex = geom->firstLine + 1 < geom->lineCount ? (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)(geom->firstLine + 1), 0) : -1;
    if (firstIndex < 0) return 1;
    firstPos = Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)firstIndex, 0);
    nextPos = nextIndex >= 0 ? Ctl(hwnd, EM_POSFROMCHAR, (WPARAM)nextIndex, 0) : -1;
    geom->originY = (short)HIWORD(firstPos);
    geom->originX = (short)LOWORD(firstPos);
    geom->pitch = (short)HIWORD(nextPos) - geom->originY;
    if (geom->pitch <= 0) geom->pitch = LineHeight();
    geom->rows = (geom->formatRect.bottom - geom->originY) / geom->pitch;
    if (geom->originX > geom->formatRect.left || geom->formatRect.left - geom->originX > 30000) return 1;
    geom->topText = geom->firstLine >= 1 && geom->top.bottom > geom->top.top;
    geom->leftText = geom->originX < geom->formatRect.left && geom->left.right > geom->left.left;
    if (geom->rows < 1 || geom->firstLine + 1 >= geom->lineCount || geom->firstLine + geom->rows >= geom->lineCount) return 1;
    geom->bottomText = 1;
    return 1;
}

static int BandKind(WCHAR ch) {
    if ((ch >= 0xFB1D && ch < 0xFE00) || (ch >= 0xFE70 && ch < 0xFF00)) return 2;
    if ((ch >= 0x0590 && ch < 0x0900) || (ch >= 0x200E && ch <= 0x200F) || (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2066 && ch <= 0x2069)) return 1;
    return 0;
}

static int BandWidth(HDC hdc, const WCHAR *text, int length) {
    return length > 0 ? (int)LOWORD(GetTabbedTextExtentW(hdc, text, length, g_tabPx > 0, &g_tabPx)) : 0;
}

static int BandText(HDC hdc, int x, int y, const WCHAR *text, int length, int tabOrigin) {
    return length > 0 ? x + (int)LOWORD(TabbedTextOutW(hdc, x, y, text, length, g_tabPx > 0, &g_tabPx, tabOrigin)) : x;
}

static void BandRows(HWND hwnd, HDC hdc, const BandGeom *geom, int firstRow, int endRow, const RECT *cut) {
    DWORD selStart = 0, selEnd = 0;
    HLOCAL hText;
    const WCHAR *text;
    HGDIOBJ oldFont;
    COLORREF selTextColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
    int length, row, lineStart, maxRow;
    hText = (HLOCAL)Ctl(hwnd, EM_GETHANDLE, 0, 0);
    text = hText ? (const WCHAR *)LocalLock(hText) : NULL;
    if (!text) return;
    if (!g_stubW) StubMetrics(hwnd);
    maxRow = 30000 / g_stubMax;
    length = (int)Ctl(hwnd, WM_GETTEXTLENGTH, 0, 0);
    Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    oldFont = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    IntersectClipRect(hdc, cut->left, cut->top, geom->formatRect.right < cut->right ? geom->formatRect.right : cut->right, cut->bottom);
    row = firstRow;
    if (endRow > geom->lineCount) endRow = geom->lineCount;
    lineStart = row >= 0 && row < geom->lineCount ? (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)row, 0) : -1;
    for (; row < endRow && lineStart >= 0 && lineStart <= length; row++) {
        int y = geom->originY + (row - geom->firstLine) * geom->pitch, lineEnd, rowStart, rowEnd, rowSelStart, rowSelEnd, i, cost, kind = 0, hardBreak = 0;
        RECT selRect;
        if (y >= cut->bottom) break;
        lineEnd = row + 1 < geom->lineCount ? (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)(row + 1), 0) : length;
        if (lineEnd < lineStart || lineEnd > length) break;
        rowStart = lineStart; rowEnd = lineEnd;
        lineStart = lineEnd;
        if (row + 1 < geom->lineCount && rowEnd - rowStart >= 2 && text[rowEnd - 2] == '\r' && text[rowEnd - 1] == '\n') { rowEnd -= 2; hardBreak = 1; }
        if (rowEnd - rowStart > maxRow) continue;
        for (i = rowStart, cost = 0; i < rowEnd && kind < 2; i++) {
            int charKind = BandKind(text[i]);
            if (charKind > kind) kind = charKind;
            cost += text[i] == '\t' ? 8 : 1;
        }
        if (cost > maxRow || kind == 2) continue;
        if (kind == 1 && selEnd > selStart && (int)selStart < lineEnd && (int)selEnd > rowStart) continue;
        rowSelStart = (int)selStart < rowStart ? rowStart : ((int)selStart > rowEnd ? rowEnd : (int)selStart);
        rowSelEnd = (int)selEnd < rowStart ? rowStart : ((int)selEnd > rowEnd ? rowEnd : (int)selEnd);
        selRect.top = y; selRect.bottom = y + geom->pitch;
        if (rowSelEnd > rowSelStart) {
            selRect.left = geom->originX + BandWidth(hdc, text + rowStart, rowSelStart - rowStart);
            selRect.right = geom->originX + BandWidth(hdc, text + rowStart, rowSelEnd - rowStart) + BAND_SEL_EXTRA;
            FillRect(hdc, &selRect, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        if (hardBreak && (int)selStart <= rowEnd && (int)selEnd > rowEnd) {
            selRect.left = geom->originX + BandWidth(hdc, text + rowStart, rowEnd - rowStart);
            selRect.right = selRect.left + g_stubW;
            FillRect(hdc, &selRect, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        }
        {
            int x = geom->originX;
            SetTextColor(hdc, C_EDIT_FG);
            x = BandText(hdc, x, y, text + rowStart, rowSelStart - rowStart, geom->originX);
            SetTextColor(hdc, selTextColor);
            x = BandText(hdc, x, y, text + rowSelStart, rowSelEnd - rowSelStart, geom->originX);
            SetTextColor(hdc, C_EDIT_FG);
            BandText(hdc, x, y, text + rowSelEnd, rowEnd - rowSelEnd, geom->originX);
        }
    }
    SelectObject(hdc, oldFont);
    LocalUnlock(hText);
}

static void BandStrip(HWND hwnd, HDC hdc, const BandGeom *geom, const RECT *area, int hasText, int firstRow, int endRow, const RECT *clip) {
    RECT blitRect = *area;
    HDC memDc;
    HBITMAP bmp;
    HGDIOBJ oldBitmap = NULL;
    if (area->right <= area->left || area->bottom <= area->top) return;
    if (clip && !IntersectRect(&blitRect, &blitRect, clip)) return;
    memDc = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, area->right - area->left, area->bottom - area->top);
    if (memDc && bmp) {
        oldBitmap = SelectObject(memDc, bmp);
        SetViewportOrgEx(memDc, -area->left, -area->top, NULL);
        FillRect(memDc, area, EditBrush());
        if (hasText) BandRows(hwnd, memDc, geom, firstRow, endRow, area);
        BitBlt(hdc, blitRect.left, blitRect.top, blitRect.right - blitRect.left, blitRect.bottom - blitRect.top, memDc, blitRect.left, blitRect.top, SRCCOPY);
        SelectObject(memDc, oldBitmap);
    }
    if (bmp) DeleteObject(bmp);
    if (memDc) DeleteDC(memDc);
}

static void BandDraw(HWND hwnd, HDC hdc, const BandGeom *geom, const RECT *clip) {
    BandStrip(hwnd, hdc, geom, &geom->band, geom->bottomText, geom->firstLine + geom->rows, geom->lineCount, clip);
    BandStrip(hwnd, hdc, geom, &geom->top, geom->topText, geom->firstLine - 1, geom->firstLine, clip);
    BandStrip(hwnd, hdc, geom, &geom->left, geom->leftText, geom->firstLine, geom->firstLine + geom->rows, clip);
}

static void BandPaint(HWND hwnd, HDC hdc, const RECT *clip) {
    BandGeom geom;
    RECT overlap;
    HDC ownDc = NULL;
    if (hwnd != g_edit || !BandRect(hwnd, &geom)) return;
    if (clip && !IntersectRect(&overlap, &geom.band, clip) && !IntersectRect(&overlap, &geom.top, clip) && !IntersectRect(&overlap, &geom.left, clip)) return;
    if (!BandGeomGet(hwnd, &geom)) return;
    if (!hdc) {
        ownDc = hdc = GetDC(hwnd);
        if (!hdc) return;
    }
    BandDraw(hwnd, hdc, &geom, clip);
    if (ownDc) ReleaseDC(hwnd, ownDc);
}

static void BandSync(HWND hwnd) {
    if (hwnd != g_edit || !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE)) return;
    BandPaint(hwnd, NULL, NULL);
}

static LRESULT BandErase(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    BandGeom geom;
    HDC hdc = (HDC)wParam;
    int saved;
    LRESULT result;
    if (hwnd != g_edit || !BandRect(hwnd, &geom)) return Ctl(hwnd, WM_ERASEBKGND, wParam, lParam);
    saved = SaveDC(hdc);
    ExcludeClipRect(hdc, geom.band.left, geom.band.top, geom.band.right, geom.band.bottom);
    ExcludeClipRect(hdc, geom.top.left, geom.top.top, geom.top.right, geom.top.bottom);
    ExcludeClipRect(hdc, geom.left.left, geom.left.top, geom.left.right, geom.left.bottom);
    result = Ctl(hwnd, WM_ERASEBKGND, wParam, lParam);
    RestoreDC(hdc, saved);
    return result;
}

static int EditPaintBuffered(HWND hwnd, const RECT *dirty) {
    RECT client;
    PAINTSTRUCT ps;
    HDC ownDc = NULL, memDc = NULL, hdc;
    HBITMAP bmp = NULL;
    HGDIOBJ oldBitmap = NULL;
    int saved = 0, depth, done = 0, copied = 0;
    if (!GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0) return 0;
    ownDc = GetDC(hwnd);
    if (!ownDc) return 0;
    memDc = CreateCompatibleDC(ownDc);
    if (!memDc) goto cleanup;
    bmp = CreateCompatibleBitmap(ownDc, client.right, client.bottom);
    if (!bmp) goto cleanup;
    oldBitmap = SelectObject(memDc, bmp);
    if (!oldBitmap) goto cleanup;
    IntersectClipRect(memDc, dirty->left, dirty->top, dirty->right, dirty->bottom);
    saved = SaveDC(memDc);
    if (!saved || !FillRect(memDc, &client, EditBrush())) goto cleanup;
    ReleaseDC(hwnd, ownDc);
    ownDc = NULL;
    memset(&ps, 0, sizeof ps);
    depth = g_paintDepth;
    g_paintDepth = 0;
    hdc = BeginPaint(hwnd, &ps);
    g_paintDepth = depth;
    if (!hdc) {
        EndPaint(hwnd, &ps);
        InvalidateRect(hwnd, dirty, TRUE);
        goto cleanup;
    }
    g_paintDepth++;
    SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)memDc, 0);
    Ctl(hwnd, WM_PRINTCLIENT, (WPARAM)memDc, PRF_CLIENT | PRF_ERASEBKGND);
    g_paintDepth--;
    if (RestoreDC(memDc, saved)) {
        StubPaint(hwnd, memDc, NULL);
        BandPaint(hwnd, memDc, NULL);
        InvisiblesPaint(hwnd, memDc, NULL);
        copied = BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, memDc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    }
    saved = 0;
    if (!copied) {
        int state = SaveDC(hdc);
        g_paintDepth++;
        SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)hdc, 0);
        Ctl(hwnd, WM_PRINTCLIENT, (WPARAM)hdc, PRF_CLIENT | PRF_ERASEBKGND);
        g_paintDepth--;
        if (state) RestoreDC(hdc, state);
        StubPaint(hwnd, hdc, &ps.rcPaint);
        BandPaint(hwnd, hdc, &ps.rcPaint);
        InvisiblesPaint(hwnd, hdc, &ps.rcPaint);
    }
    EndPaint(hwnd, &ps);
    done = 1;
cleanup:
    if (saved) RestoreDC(memDc, saved);
    if (oldBitmap) SelectObject(memDc, oldBitmap);
    if (memDc) DeleteDC(memDc);
    if (bmp) DeleteObject(bmp);
    if (ownDc) ReleaseDC(hwnd, ownDc);
    return done;
}

#ifdef SHOTDC
API DWORD WINAPI GetTempPathW(DWORD, LPWSTR);
static int g_shotPaintW, g_shotPaintH, g_shotPaintY, g_shotPaintHit;

static int ShotInt(char *dst, int value) {
    char digits[12];
    int count = 0, i = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) dst[i++] = digits[--count];
    return i;
}

static void ShotDc(HWND hwnd, int width, int height, int startY) {
    WCHAR path[300];
    char header[40];
    int headerLength = 0, x, y;
    BYTE *buffer;
    HDC hdc;
    HANDLE file;
    DWORD written;
    BOOL caretHidden;
    if (width < 1 || height < 1) return;
    hdc = GetDC(hwnd);
    if (!hdc) return;
    header[headerLength++] = 'P'; header[headerLength++] = '6'; header[headerLength++] = '\n';
    headerLength += ShotInt(header + headerLength, width); header[headerLength++] = ' '; headerLength += ShotInt(header + headerLength, height);
    header[headerLength++] = '\n'; header[headerLength++] = '2'; header[headerLength++] = '5'; header[headerLength++] = '5'; header[headerLength++] = '\n';
    buffer = (BYTE *)mem_alloc((size_t)width * height * 3);
    if (!buffer) { ReleaseDC(hwnd, hdc); return; }
    caretHidden = HideCaret(hwnd);
    for (y = 0; y < height; y++) for (x = 0; x < width; x++) {
            COLORREF color = GetPixel(hdc, x, startY + y);
            BYTE *pixel = buffer + ((size_t)y * width + x) * 3;
            pixel[0] = (BYTE)(color & 255); pixel[1] = (BYTE)((color >> 8) & 255); pixel[2] = (BYTE)((color >> 16) & 255);
        }
    if (caretHidden) ShowCaret(hwnd);
    ReleaseDC(hwnd, hdc);
    GetTempPathW(250, path);
    wcat(path, L"mint_dc.ppm", 300);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, header, (DWORD)headerLength, &written, NULL);
        WriteFile(file, buffer, (DWORD)((size_t)width * height * 3), &written, NULL);
        CloseHandle(file);
    }
    mem_free(buffer);
}
#endif

int EditLineHeight(void) {
    return LineHeight();
}

static BOOL IndexPos(int index, POINT *pos) {
    RECT formatRect;
    int length = (int)Ctl(g_edit, WM_GETTEXTLENGTH, 0, 0);
    LRESULT packed = index < length ? Ctl(g_edit, EM_POSFROMCHAR, (WPARAM)index, 0) : -1;
    if (packed != -1) { pos->x = (short)LOWORD(packed); pos->y = (short)HIWORD(packed); return TRUE; }
    if (index < length) return FALSE;
    Ctl(g_edit, EM_GETRECT, 0, (LPARAM)&formatRect);
    pos->x = EditCaretEndX(g_edit, g_font, length, &formatRect);
    pos->y = formatRect.top + ((int)Ctl(g_edit, EM_LINEFROMCHAR, (WPARAM)length, 0) - (int)Ctl(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0)) * LineHeight();
    return TRUE;
}

enum { INV_SPACE = 1, INV_TAB, INV_BREAK, INV_WIDE, INV_CONTROL };

static int InvisibleKind(const WCHAR *text, int index, int length) {
    WCHAR ch = text[index];
    if (ch == ' ') return INV_SPACE;
    if (ch == '\t') return INV_TAB;
    if (ch == '\r' || (ch == '\n' && (!index || text[index - 1] != '\r')) || ch == 0x2028 || ch == 0x2029) return INV_BREAK;
    if (ch == '\n') return 0;
    if (ch == 0x00A0 || ch == 0x1680 || (ch >= 0x2000 && ch <= 0x200A) || ch == 0x202F || ch == 0x205F || ch == 0x3000) return INV_WIDE;
    if (ch < 0x20 || (ch >= 0x7F && ch <= 0x9F) || ch == 0x00AD || ch == 0x034F || ch == 0x061C ||
        (ch >= 0x115F && ch <= 0x1160) || (ch >= 0x17B4 && ch <= 0x17B5) || (ch >= 0x180B && ch <= 0x180F) ||
        (ch >= 0x200B && ch <= 0x200F) || (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2060 && ch <= 0x206F) ||
        ch == 0x2800 || ch == 0x3164 || (ch >= 0xFE00 && ch <= 0xFE0F) || ch == 0xFEFF || ch == 0xFFA0 ||
        (ch >= 0xFFF0 && ch <= 0xFFFB)) return INV_CONTROL;
    if (index + 1 < length && ((ch == 0xDB40 && ((text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDC7F) ||
        (text[index + 1] >= 0xDD00 && text[index + 1] <= 0xDDEF))) ||
        (ch == 0xD834 && text[index + 1] >= 0xDD73 && text[index + 1] <= 0xDD7A) ||
        (ch == 0xD82F && text[index + 1] >= 0xDCA0 && text[index + 1] <= 0xDCA3))) return INV_CONTROL;
    return 0;
}

static COLORREF InvisibleColor(COLORREF bg, COLORREF fg) {
    return RGB((3 * GetRValue(bg) + GetRValue(fg)) / 4,
               (3 * GetGValue(bg) + GetGValue(fg)) / 4,
               (3 * GetBValue(bg) + GetBValue(fg)) / 4);
}

static void InvisibleLine(HDC hdc, int x, int y, int endX, int endY) {
    MoveToEx(hdc, x, y, NULL);
    LineTo(hdc, endX, endY);
}

static int InvisibleCenter(HWND hwnd, int row, int index, const POINT *next, int dir, int pitch, int advance, int *center) {
    int offset, first = 0, last = 0, limit = advance < S(256) ? advance : S(256);
    for (offset = 1; offset <= limit; offset++) {
        int x = next->x - dir * offset;
        LRESULT hit = Ctl(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(x, next->y + pitch / 2));
        if (HIWORD(hit) == (WORD)row && LOWORD(hit) == (WORD)index) {
            if (!first) first = offset;
            last = offset;
        } else if (first) break;
    }
    if (!first) return 0;
    *center = next->x - dir * ((first + last) / 2);
    return 1;
}

static void InvisibleDraw(HWND hwnd, HDC hdc, int row, int index, int kind, const POINT *pos, const POINT *next, int pitch, int cell, int rtl, COLORREF color) {
    int left = pos->x, width = cell, midY = pos->y + pitch / 2;
    int size, x, arm, dir = rtl ? -1 : 1;
    RECT dot;
    if (next && next->y == pos->y && next->x != pos->x) {
        width = next->x - pos->x;
        if (width < 0) { left = next->x; width = -width; dir = -1; }
        else dir = 1;
    } else if (rtl) left -= width;
    if (width < 1) width = 1;
    x = left + width / 2;
    if ((kind == INV_SPACE || kind == INV_WIDE) && width > cell && next && next->y == pos->y &&
        !InvisibleCenter(hwnd, row, index, next, dir, pitch, cell, &x)) return;
    size = ClampInt(pitch / 3, S(4), S(10));
    if (kind == INV_SPACE) {
        int diameter = ClampInt(pitch / 10, 1, S(2));
        dot.left = x - diameter / 2; dot.right = dot.left + diameter;
        dot.top = midY - diameter / 2; dot.bottom = dot.top + diameter;
        FillC(hdc, &dot, color);
        return;
    }
    SetDCPenColor(hdc, color);
    if (kind == INV_TAB) {
        int span = width - S(4), tip;
        if (span > S(20)) span = S(20);
        if (span < S(3)) span = S(3);
        x = left + (width - span) / 2;
        arm = ClampInt(size / 3, 1, S(3));
        tip = dir > 0 ? x + span : x;
        InvisibleLine(hdc, x, midY, x + span + 1, midY);
        InvisibleLine(hdc, tip - dir * arm, midY - arm, tip, midY);
        InvisibleLine(hdc, tip, midY, tip - dir * arm, midY + arm);
    } else if (kind == INV_BREAK) {
        int start = pos->x + dir * S(2), tip = start + dir * size;
        arm = ClampInt(size / 3, 1, S(3));
        InvisibleLine(hdc, tip, midY - size / 2, tip, midY + 1);
        InvisibleLine(hdc, tip, midY, start, midY);
        InvisibleLine(hdc, start + dir * arm, midY - arm, start, midY);
        InvisibleLine(hdc, start, midY, start + dir * arm, midY + arm);
    } else if (kind == INV_WIDE) {
        int half = ClampInt(size / 3, 1, S(3));
        InvisibleLine(hdc, x - half, midY, x, midY - half);
        InvisibleLine(hdc, x, midY - half, x + half, midY);
        InvisibleLine(hdc, x + half, midY, x, midY + half);
        InvisibleLine(hdc, x, midY + half, x - half, midY);
    } else {
        int top = pos->y + S(2), height = ClampInt(pitch / 4, S(3), S(7));
        x = pos->x;
        InvisibleLine(hdc, x - S(2), top, x + S(2) + 1, top);
        InvisibleLine(hdc, x, top, x, top + height);
    }
}

static int InvisibleRange(HWND hwnd, int row, int rowStart, int rowEnd, int y, const RECT *view, int *start, int *end) {
    int x, lo = rowEnd, hi = rowStart, step = (view->right - view->left) / 8;
    *start = rowStart; *end = rowEnd;
    if (rowEnd - rowStart <= 4096 && rowEnd - rowStart <= 30000 / g_stubMax) return 0;
    if (step < 1) step = 1;
    for (x = view->left; x <= view->right; x += step) {
        LRESULT hit = Ctl(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(x, y));
        int index;
        if (HIWORD(hit) != (WORD)row) continue;
        index = rowStart + (int)(WORD)(LOWORD(hit) - (WORD)rowStart);
        if (index > rowEnd) continue;
        if (index < lo) lo = index;
        if (index > hi) hi = index;
    }
    if (lo > hi) { lo = rowStart; hi = rowStart; }
    *start = lo - rowStart > 2 ? lo - 2 : rowStart;
    *end = rowEnd - hi > 3 ? hi + 3 : rowEnd;
    if (*end - *start > 8192) *end = *start + 8192;
    return 1;
}

static int InvisibleHit(HWND hwnd, int row, int index, const POINT *pos, const POINT *next, int pitch) {
    int x = pos->x, delta;
    LRESULT hit;
    if (next && next->y == pos->y && next->x != pos->x && next->x - pos->x < 30000 && pos->x - next->x < 30000) x += (next->x - pos->x) / 2;
    hit = Ctl(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(x, pos->y + pitch / 2));
    if (HIWORD(hit) != (WORD)row) return 0;
    delta = (short)(LOWORD(hit) - (WORD)index);
    return delta >= -2 && delta <= 2;
}

static void InvisiblesPaint(HWND hwnd, HDC hdc, const RECT *clip) {
    RECT view, formatRect;
    HDC ownDc = NULL;
    HLOCAL hText;
    const WCHAR *text;
    DWORD selStart = 0, selEnd = 0;
    COLORREF normal, selected;
    int saved, length, pitch, first, last, lineCount, row, cell, caretHidden = 0;
    if (!g_invisibles || hwnd != g_edit) return;
    EditViewRect(hwnd, &view);
    if (clip && !IntersectRect(&view, &view, clip)) return;
    if (view.right <= view.left || view.bottom <= view.top) return;
    hText = (HLOCAL)Ctl(hwnd, EM_GETHANDLE, 0, 0);
    text = hText ? (const WCHAR *)LocalLock(hText) : NULL;
    if (!text) return;
    if (!hdc) {
        ownDc = hdc = GetDC(hwnd);
        if (!hdc) { LocalUnlock(hText); return; }
        caretHidden = GetFocus() == hwnd && HideCaret(hwnd);
    }
    saved = SaveDC(hdc);
    if (!saved) goto done;
    IntersectClipRect(hdc, view.left, view.top, view.right, view.bottom);
    SelectObject(hdc, GetStockObject(DC_PEN));
    SelectObject(hdc, g_font);
    Ctl(hwnd, EM_GETRECT, 0, (LPARAM)&formatRect);
    Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    length = (int)Ctl(hwnd, WM_GETTEXTLENGTH, 0, 0);
    lineCount = (int)Ctl(hwnd, EM_GETLINECOUNT, 0, 0);
    first = (int)Ctl(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    pitch = LineHeight();
    last = first + (view.bottom - formatRect.top + pitch - 1) / pitch + 1;
    if (last > lineCount) last = lineCount;
    if (first > 0) first--;
    if (!g_stubW) StubMetrics(hwnd);
    cell = g_stubW;
    normal = InvisibleColor(C_EDIT_BG, C_EDIT_FG);
    selected = InvisibleColor(GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT));
    for (row = first; row < last; row++) {
        int rowStart = (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)row, 0);
        int rowEnd = row + 1 < lineCount ? (int)Ctl(hwnd, EM_LINEINDEX, (WPARAM)(row + 1), 0) : length;
        int i, start, end, y, checkHit, previousKind = 0, previousX = 0, previousY = 0;
        COLORREF previousColor = 0;
        POINT origin;
        if (rowStart < 0 || rowEnd < rowStart || rowEnd > length || !IndexPos(rowStart, &origin)) continue;
        y = origin.y;
        if (y >= view.bottom || y + pitch <= view.top) continue;
        checkHit = InvisibleRange(hwnd, row, rowStart, rowEnd, y + pitch / 2, &view, &start, &end);
        for (i = start; i < end; i++) {
            int kind = InvisibleKind(text, i, length), rtl = EditIsRtl(), advance = cell;
            COLORREF color;
            POINT pos, next;
            BOOL hasNext;
            if (!kind || !IndexPos(i, &pos) || pos.y + pitch <= view.top || pos.y >= view.bottom) continue;
            hasNext = i + 1 <= length && IndexPos(i + 1, &next);
            if (checkHit && !InvisibleHit(hwnd, row, i, &pos, hasNext ? &next : NULL, pitch)) continue;
            if (kind == INV_BREAK && rtl && pos.x > origin.x) rtl = 0;
            color = (DWORD)i >= selStart && (DWORD)i < selEnd ? selected : normal;
            if (kind == previousKind && pos.x == previousX && pos.y == previousY && color == previousColor) continue;
            if (kind == INV_WIDE) {
                SIZE size = { 0, 0 };
                if (GetTextExtentPoint32W(hdc, text + i, 1, &size) && size.cx > 0) advance = size.cx;
            }
            InvisibleDraw(hwnd, hdc, row, i, kind, &pos, hasNext ? &next : NULL, pitch, advance, rtl, color);
            previousKind = kind; previousX = pos.x; previousY = pos.y; previousColor = color;
        }
    }
    RestoreDC(hdc, saved);
done:
    if (caretHidden) ShowCaret(hwnd);
    if (ownDc) ReleaseDC(hwnd, ownDc);
    LocalUnlock(hText);
}

static void InvisiblesSync(HWND hwnd) {
    if (!g_invisibles || g_invisSync || g_paintDepth || g_inBars || hwnd != g_edit || !IsWindowVisible(hwnd)) return;
    g_invisSync = 1;
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    g_invisSync = 0;
}

int EditIndexFromPoint(POINT pt) {
    RECT formatRect;
    int lineHeight = LineHeight(), firstLine, lineCount, row, rowStart, rowLength, index;
    WORD low;
    Ctl(g_edit, EM_GETRECT, 0, (LPARAM)&formatRect);
    firstLine = (int)Ctl(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    lineCount = (int)Ctl(g_edit, EM_GETLINECOUNT, 0, 0);
    row = pt.y < formatRect.top ? firstLine : firstLine + (pt.y - formatRect.top) / lineHeight;
    if (row >= lineCount) row = lineCount - 1;
    if (row < 0) row = 0;
    rowStart = (int)Ctl(g_edit, EM_LINEINDEX, (WPARAM)row, 0);
    rowLength = (int)Ctl(g_edit, EM_LINELENGTH, (WPARAM)rowStart, 0);
    low = LOWORD(Ctl(g_edit, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, formatRect.top + (row - firstLine) * lineHeight + lineHeight / 2)));
    index = rowStart + (int)(WORD)(low - (WORD)rowStart);
    if (index > rowStart + rowLength) index = rowStart + rowLength;
    return index < rowStart ? rowStart : index;
}

BOOL EditCaretRect(int index, RECT *rect) {
    POINT pos;
    RECT visible;
    HDC hdc;
    int width = 1;
    if (!g_edit || !IndexPos(index, &pos)) return FALSE;
    SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
    if (width < 1) width = 1;
    rect->left = pos.x;
    rect->top = pos.y;
    rect->right = pos.x + width;
    rect->bottom = pos.y + LineHeight();
    hdc = GetDC(g_edit);
    if (!hdc) return FALSE;
    GetClipBox(hdc, &visible);
    ReleaseDC(g_edit, hdc);
    return IntersectRect(rect, rect, &visible);
}

BOOL EditHitSelection(POINT pt) {
    DWORD selStart = 0, selEnd = 0;
    int index, charIndex, lineHeight = LineHeight();
    POINT charPos, nextPos;
    if (!g_edit) return FALSE;
    Ctl(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd <= selStart) return FALSE;
    index = EditIndexFromPoint(pt);
    for (charIndex = index - 1; charIndex <= index; charIndex++) {
        int left, right;
        if (charIndex < (int)selStart || charIndex >= (int)selEnd || !IndexPos(charIndex, &charPos)) continue;
        if (pt.y < charPos.y || pt.y >= charPos.y + lineHeight) continue;
        if (IndexPos(charIndex + 1, &nextPos) && nextPos.y == charPos.y && nextPos.x != charPos.x) {
            left = nextPos.x < charPos.x ? nextPos.x : charPos.x;
            right = nextPos.x < charPos.x ? charPos.x : nextPos.x;
        } else {
            if (!g_stubW) StubMetrics(g_edit);
            left = EditIsRtl() ? charPos.x - g_stubW : charPos.x;
            right = left + g_stubW;
        }
        if (pt.x >= left && pt.x < right) return TRUE;
    }
    return FALSE;
}

static BOOL PasteText(void) {
    HANDLE data;
    const WCHAR *src;
    WCHAR *text = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_edit)) return FALSE;
    data = GetClipboardData(CF_UNICODETEXT);
    src = data ? (const WCHAR *)GlobalLock(data) : NULL;
    if (src) { text = TextToCrlf(src, (int)(GlobalSize(data) / sizeof(WCHAR)), NULL); GlobalUnlock(data); }
    CloseClipboard();
    if (!text) return FALSE;
    EditInsert(text);
    mem_free(text);
    return TRUE;
}

static int CaretMoveMessage(UINT msg, WPARAM wParam) {
    switch (msg) {
    case WM_KEYDOWN: case WM_CHAR: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO: case WM_TIMER:
    case EM_SETSEL: case EM_REPLACESEL: case EM_UNDO:
        return 1;
    case WM_MOUSEMOVE:
        return (wParam & 1) != 0;
    }
    return 0;
}

static LRESULT CALLBACK EditProcMain(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT result;
    int anchor = -1;
    int wasRevealing;
    if (CaretMoveMessage(msg, wParam)) {
        DWORD selStart = 0, selEnd = 0;
        int active = EditCaretIndex();
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        anchor = active == (int)selStart ? (int)selEnd : (int)selStart;
        if (msg != WM_TIMER) g_allowTallReveal = 1;
    }
    if (msg == EM_SCROLLCARET) g_allowTallReveal = 1;
    switch (msg) {
#ifdef SHOTDC
    case WM_APP + 90:
        ShotDc(hwnd, (int)wParam, (int)(lParam & 0xFFFF), (int)((lParam >> 16) & 0xFFFF));
        return 0;
    case WM_APP + 97:
        return wParam ? ShowCaret(hwnd) : HideCaret(hwnd);
    case WM_APP + 91: {
        LRESULT packed = (LRESULT)((g_shotWiped << 16) | g_shotKept);
        g_shotWiped = g_shotKept = 0;
        return packed; }
    case WM_APP + 92: {
        HDC hdc;
        if (!wParam) return PROBE_ID;
        hdc = GetDC(hwnd);
        if (hdc) {
            SendMessageW(hwnd, WM_ERASEBKGND, (WPARAM)hdc, 0);
            ReleaseDC(hwnd, hdc);
        }
        ShotDc(hwnd, (int)wParam, (int)(lParam & 0xFFFF), (int)((lParam >> 16) & 0xFFFF));
        return 0; }
    case WM_APP + 93: {
        HDC hdc = GetDC(hwnd);
        HBRUSH brush = CreateSolidBrush(0x00FF00FF);
        RECT client;
        if (hdc && brush && GetClientRect(hwnd, &client)) FillRect(hdc, &client, brush);
        if (brush) DeleteObject(brush);
        if (hdc) ReleaseDC(hwnd, hdc);
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        ShotDc(hwnd, (int)wParam, (int)(lParam & 0xFFFF), (int)((lParam >> 16) & 0xFFFF));
        return 0; }
    case WM_APP + 94:
        if (!wParam) return PROBE_ID;
        g_shotPaintW = (int)wParam;
        g_shotPaintH = (int)(lParam & 0xFFFF);
        g_shotPaintY = (int)((lParam >> 16) & 0xFFFF);
        g_shotPaintHit = 0;
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        g_shotPaintW = 0;
        return g_shotPaintHit;
    case WM_APP + 95: {
        RECT dirty = { 17, 19, 157, 51 };
        HDC hdc;
        int height = (int)(lParam & 0xFFFF), startY = (int)((lParam >> 16) & 0xFFFF);
        if (!wParam) return PROBE_ID;
        hdc = GetDC(hwnd);
        if (hdc) {
            SetPixelV(hdc, (int)wParam - 4, startY + height - 4, 0x00FF00FF);
            ReleaseDC(hwnd, hdc);
        }
        InvalidateRect(hwnd, &dirty, TRUE);
        UpdateWindow(hwnd);
        ShotDc(hwnd, (int)wParam, height, startY);
        return 0; }
    case WM_APP + 96: {
        DWORD selStart = 0, selEnd = 0, now, elapsed;
        int length, i, startPos, endPos, line, col;
        if (!wParam) return PROBE_ID;
        length = GetWindowTextLengthW(hwnd);
        endPos = length > 16 ? length - 16 : length;
        startPos = endPos > (int)lParam ? endPos - (int)lParam : 0;
        Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        Ctl(hwnd, EM_SETSEL, (WPARAM)startPos, (LPARAM)startPos);
        EditCaretPos(&line, &col);
        now = GetTickCount();
        for (i = 0; i < (int)wParam; i++) {
            int index = (i & 1) ? startPos : endPos;
            Ctl(hwnd, EM_SETSEL, (WPARAM)index, (LPARAM)index);
            EditCaretPos(&line, &col);
        }
        elapsed = GetTickCount() - now;
        Ctl(hwnd, EM_SETSEL, selStart, selEnd);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        AppUpdateStatus();
        return (LRESULT)elapsed; }
#endif
    case WM_ERASEBKGND:
        if (!g_paintDepth) return 0;
        result = BandErase(hwnd, wParam, lParam);
#ifdef SHOTDC
        if (g_shotPaintW && !g_shotPaintHit) {
            g_shotPaintHit = 1;
            ShotDc(hwnd, g_shotPaintW, g_shotPaintH, g_shotPaintY);
        }
#endif
#ifdef FLICKER_PROBE
        Sleep(8);
#endif
        return result;
    case WM_SIZE: {
        static HWND szH;
        static LPARAM szL;
        static DWORD lastAt, lastCost;
        DWORD now = GetTickCount();
        if (hwnd == szH && lParam == szL) return 0;
        szH = hwnd; szL = lParam;
        EditCaretRemember(hwnd, g_font);
        if (!g_inBars) { g_revealPending = 1; g_allowTallReveal = 1; }
        if (!g_inBars && lastCost > 25 && now - lastAt < 2 * lastCost && EditCaretInView(hwnd, LineHeight())) {
            g_sizeDeferred = 1;
            SetTimer(hwnd, SIZE_TIMER, 40, NULL);
            return 0;
        }
        KillTimer(hwnd, SIZE_TIMER);
        g_sizeDeferred = 0;
        EditPad2(hwnd, 0);
        lastAt = GetTickCount(); lastCost = lastAt - now;
        RevealCaret(hwnd);
        EditScrollSoon();
        SbarSync(hwnd);
        StubSync(hwnd);
        BandSync(hwnd);
        InvisiblesSync(hwnd);
        return 0; }
    case WM_TIMER:
        if (wParam == SIZE_TIMER) {
            KillTimer(hwnd, SIZE_TIMER);
            g_sizeDeferred = 0;
            EditPad2(hwnd, 0);
            RevealCaret(hwnd);
            EditScrollSoon();
            SbarSync(hwnd);
            StubSync(hwnd);
            BandSync(hwnd);
            InvisiblesSync(hwnd);
            return 0;
        }
        break;
    case WM_PAINT: {
        RECT updateRect;
        int hasUpdate = !wParam && GetUpdateRect(hwnd, &updateRect, FALSE);
        if (hasUpdate && EditPaintBuffered(hwnd, &updateRect)) { FitTallCaret(hwnd); return 0; }
        g_paintDepth++;
        result = CallWindowProcW(g_orig, hwnd, msg, wParam, lParam);
        g_paintDepth--;
        if (wParam) { StubPaint(hwnd, (HDC)wParam, NULL); BandPaint(hwnd, (HDC)wParam, NULL); InvisiblesPaint(hwnd, (HDC)wParam, NULL); }
        else if (hasUpdate) { StubPaint(hwnd, NULL, &updateRect); BandPaint(hwnd, NULL, &updateRect); InvisiblesPaint(hwnd, NULL, &updateRect); }
        FitTallCaret(hwnd);
        return result; }
    case WM_PRINTCLIENT:
        g_paintDepth++;
        result = CallWindowProcW(g_orig, hwnd, msg, wParam, lParam);
        g_paintDepth--;
        StubPaint(hwnd, (HDC)wParam, NULL);
        BandPaint(hwnd, (HDC)wParam, NULL);
        InvisiblesPaint(hwnd, (HDC)wParam, NULL);
        return result;
    case WM_PRINT:
        g_paintDepth++;
        result = CallWindowProcW(g_orig, hwnd, msg, wParam, lParam);
        g_paintDepth--;
        return result;
    case WM_BARS:
        g_barsPending = 0;
        UpdateBars();
        InvisiblesSync(hwnd);
        if (g_tallCaret) FitTallCaret(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        g_shiftDir = 0;
        if (wParam & MK_CONTROL) {
            g_wheel += GET_WHEEL_DELTA_WPARAM(wParam);
            while (g_wheel >= 120)  { g_wheel -= 120; EditZoomStep(1); }
            while (g_wheel <= -120) { g_wheel += 120; EditZoomStep(-1); }
            return 0;
        }
        g_revealPending = 0;
        g_allowTallReveal = 0;
        break;
    case WM_VSCROLL: case WM_HSCROLL:
        if (!g_inBars && !g_revealing) { g_revealPending = 0; g_allowTallReveal = 0; }
        break;
    case WM_CHAR:
        if (wParam == 0x7F) { EditDeleteWord(-1); return 0; }
        break;
    case WM_SYSCHAR:
        if (wParam != ' ' && wParam != VK_BACK && MenuBarMnemonic((WCHAR)wParam) >= 0) {
            SendMessageW(GetParent(hwnd), WM_SYSCOMMAND, SC_KEYMENU, (LPARAM)wParam);
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wParam == VK_SHIFT || wParam == VK_CONTROL) && g_shiftDir) {
            PostMessageW(GetParent(hwnd), WM_COMMAND, g_shiftDir == 2 ? IDM_DIR_RTL : IDM_DIR_LTR, 0);
            g_shiftDir = 0;
        }
        break;
    case WM_LBUTTONDOWN:
        g_shiftDir = 0;
        if (!g_dragBypass && !(wParam & MK_SHIFT) && DndReady()) {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            if (EditHitSelection(pt)) {
                g_dragPress = 1;
                g_dragKeys = wParam;
                g_dragDown = lParam;
                SetFocus(hwnd);
                SetCapture(hwnd);
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (g_dragPress) {
            int dx = GET_X_LPARAM(lParam) - GET_X_LPARAM(g_dragDown), dy = GET_Y_LPARAM(lParam) - GET_Y_LPARAM(g_dragDown);
            if ((dx < 0 ? -dx : dx) > GetSystemMetrics(SM_CXDRAG) || (dy < 0 ? -dy : dy) > GetSystemMetrics(SM_CYDRAG)) {
                g_dragPress = 0;
                ReleaseCapture();
                DndDragSelection();
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragPress) {
            g_dragPress = 0;
            ReleaseCapture();
            g_dragBypass = 1;
            SendMessageW(hwnd, WM_LBUTTONDOWN, g_dragKeys, g_dragDown);
            g_dragBypass = 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if ((HWND)lParam != hwnd) g_dragPress = 0;
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && DndReady()) {
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt) && EditHitSelection(pt)) { SetCursor(LoadCursorW(NULL, IDC_ARROW)); return TRUE; }
        }
        break;
    case WM_RBUTTONDOWN: case WM_SYSKEYDOWN:
        g_shiftDir = 0;
        if (g_dragPress) { g_dragPress = 0; ReleaseCapture(); }
        break;
    case WM_PASTE:
        if (PasteText()) return 0;
        break;
    case WM_MOUSEHWHEEL: {
        UINT perNotch = 3;
        int chars;
        SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &perNotch, 0);
        g_hwheel += GET_WHEEL_DELTA_WPARAM(wParam) * (int)perNotch;
        chars = g_hwheel / 120;
        g_hwheel -= chars * 120;
        if (chars && !g_pf.wrap) SendMessageW(hwnd, EM_LINESCROLL, (WPARAM)(EditIsRtl() ? -chars : chars), 0);
        return 0; }
    case WM_KEYDOWN:
        if (wParam == VK_SHIFT && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) g_shiftDir = ((lParam >> 16) & 0xFF) == 0x36 ? 2 : 1;
        else if (wParam != VK_SHIFT && wParam != VK_CONTROL) g_shiftDir = 0;
        if (g_dragPress) { if (wParam == VK_ESCAPE) { g_dragPress = 0; ReleaseCapture(); } return 0; }
        if (wParam == VK_DELETE && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            EditDeleteWord(1);
            return 0;
        }
        if (wParam >= VK_PRIOR && wParam <= VK_DOWN && !((wParam == VK_UP || wParam == VK_DOWN) && (GetKeyState(VK_CONTROL) & 0x8000)) && !EditCaretInView(hwnd, LineHeight())) {
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        break;
    case WM_CONTEXTMENU: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (x == -1 && y == -1) {
            POINT pt;
            if (!GetCaretPos(&pt)) { pt.x = 8; pt.y = 8; }
            ClientToScreen(hwnd, &pt);
            x = pt.x; y = pt.y + S(20);
            MenuShowCues();
        }
        MenuPopup(GetParent(hwnd), &g_mdEditCtx, x, y, 0);
        return 0; }
    }
    wasRevealing = g_revealing;
    if (anchor >= 0 || msg == EM_SCROLLCARET || msg == WM_SETFONT || msg == EM_SETRECTNP || msg == EM_SETTABSTOPS || msg == EM_SETMARGINS) g_revealing = 1;
    result = CallWindowProcW(g_orig, hwnd, msg, wParam, lParam);
    g_revealing = wasRevealing;
    if (msg == WM_SETTEXT) { g_textRev++; g_stub.count = 0; }
    if (anchor >= 0) {
        DWORD selStart = 0, selEnd = 0;
        Ctl(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (msg == EM_SETSEL && (int)wParam >= 0) EditCaretHint(lParam >= 0 && (int)wParam > lParam ? (int)selStart : (int)selEnd);
        else if (selStart == selEnd || (int)selEnd == anchor) EditCaretHint((int)selStart);
        else if ((int)selStart == anchor) EditCaretHint((int)selEnd);
    }
    if (anchor >= 0 || msg == WM_SETFOCUS || msg == WM_KEYUP || msg == EM_SCROLLCARET) FitTallCaret(hwnd);
    switch (msg) {
    case WM_SETFONT: case WM_SETTEXT:
        if (msg != WM_SETTEXT) EditPad(hwnd);
        EditScrollSoon();
        break;
    }
    if (!g_inBars) switch (msg) {
    case WM_KEYDOWN: case WM_CHAR: case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL: case WM_SETTEXT:
    case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER: case WM_SETFONT:
        SbarSync(hwnd);
        break;
    case WM_MOUSEMOVE:
        if (wParam & 1) SbarSync(hwnd);
        break;
    default:
        if (msg >= 0x00B0 && msg <= 0x00D8) SbarSync(hwnd);
        break;
    }
    switch (msg) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_TIMER:
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
    case EM_SETSEL: case EM_REPLACESEL:
        AppUpdateStatus();
        break;
    case WM_MOUSEMOVE:
        if (wParam & 1) AppUpdateStatus();
        break;
    }
    if (StubWatch(msg, wParam)) {
        StubSync(hwnd);
        BandSync(hwnd);
        InvisiblesSync(hwnd);
    }
    if (g_tallCaret) FitTallCaret(hwnd);
    return result;
}

void EditNativeUndoClear(void) {
    if (g_edit && g_orig) CallWindowProcW(g_orig, g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT result;
    int captured;
    switch (msg) {
    case WM_UNDO: case EM_UNDO: return HistUndo();
    case EM_REDO: return HistRedo();
    case EM_CANUNDO: return HistCanUndo();
    case EM_CANREDO: return HistCanRedo();
    case EM_EMPTYUNDOBUFFER: HistClear(); return 0;
    case WM_CHAR:
        if (wParam == 0x1A || wParam == 0x19) {
            if (wParam == 0x19 || (GetKeyState(VK_SHIFT) & 0x8000)) HistRedo(); else HistUndo();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_KILLFOCUS:
        HistBreak();
        break;
    case WM_KEYDOWN:
        if (wParam >= VK_PRIOR && wParam <= VK_DOWN) HistBreak();
        break;
    }
    captured = HistEnter(msg, wParam);
    result = EditProcMain(hwnd, msg, wParam, lParam);
    if (captured) {
        HistLeave();
        EditNativeUndoClear();
    } else if (msg == WM_SETTEXT) {
        HistClear();
    }
    return result;
}

HWND EditCreate(HWND parent) {
    HWND oldEdit = g_edit, edit;
    WCHAR *text = NULL;
    DWORD selStart = 0, selEnd = 0, style = WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    LONG_PTR exStyle = 0;
    int modified = 0, hadFocus = 0, x = 0, y = 0, width = 0, height = 0, backward = 0;
    if (!g_pf.wrap) style |= WS_HSCROLL | ES_AUTOHSCROLL;
    if (oldEdit) {
        RECT windowRect;
        POINT topLeft, bottomRight;
        text = EditGetDocText(NULL);
        if (!text) return NULL;
        SendMessageW(oldEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        backward = selEnd > selStart && EditCaretIndex() == (int)selStart;
        modified = (int)SendMessageW(oldEdit, EM_GETMODIFY, 0, 0);
        exStyle = GetWindowLongPtrW(oldEdit, GWL_EXSTYLE) & RTL_BITS;
        hadFocus = (GetFocus() == oldEdit);
        GetWindowRect(oldEdit, &windowRect);
        topLeft.x = windowRect.left; topLeft.y = windowRect.top; bottomRight.x = windowRect.right; bottomRight.y = windowRect.bottom;
        ScreenToClient(parent, &topLeft);
        ScreenToClient(parent, &bottomRight);
        x = topLeft.x; y = topLeft.y; width = bottomRight.x - topLeft.x; height = bottomRight.y - topLeft.y;
    }
    edit = CreateWindowExW((DWORD)exStyle, L"EDIT", NULL, style, x, y, width, height, parent, (HMENU)(ULONG_PTR)IDC_EDIT, g_hinst, NULL);
    if (!edit) { mem_free(text); return NULL; }
    SendMessageW(edit, EM_LIMITTEXT, 0, 0);
    if (text && !SetWindowTextW(edit, text)) { DestroyWindow(edit); mem_free(text); return NULL; }
    g_edit = edit;
    g_barsPending = 0;
    g_revealPending = g_sizeDeferred = 0;
    g_allowTallReveal = 1;
    g_tallCaret = 0;
    EditCaretInvalidate();
    EditCaretHint(0);
    EditCaretDisplayAdjusted(FALSE);
    SetWindowPos(edit, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_stub.count = 0;
    g_orig = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    DarkScroll(edit);
    SbarTrim(edit, S(SBAR_TRIM), S(SBAR_TRIM));
    EditApplyFont();
    if (text) {
        SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        SendMessageW(edit, EM_SETSEL, backward ? selEnd : selStart, backward ? selStart : selEnd);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(edit, EM_SETMODIFY, (WPARAM)modified, 0);
        SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        mem_free(text);
    }
    UpdateBars();
    EditScrollSoon();
    ShowWindow(edit, SW_SHOW);
    SbarSync(edit);
    if (oldEdit) { SbarDetach(oldEdit); DestroyWindow(oldEdit); }
    if (hadFocus || !oldEdit) SetFocus(edit);
    return edit;
}
