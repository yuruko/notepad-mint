#include "mp.h"
#include "ui_internal.h"

typedef char SzPrintDlg[sizeof(PRINTDLGW) == 66 ? 1 : -1];
typedef char SzPageDlg[sizeof(PAGESETUPDLGW) == 84 ? 1 : -1];
typedef char SzDocInfo[sizeof(DOCINFOW) == 20 ? 1 : -1];

#define HI_SUR(c) ((c) >= 0xD800 && (c) <= 0xDBFF)
#define LO_SUR(c) ((c) >= 0xDC00 && (c) <= 0xDFFF)

typedef BOOL  (WINAPI *PrintDlgFn)(PRINTDLGW *);
typedef BOOL  (WINAPI *PageDlgFn)(PAGESETUPDLGW *);
typedef DWORD (WINAPI *CdErrFn)(void);
static HMODULE    g_cd;
static PrintDlgFn pPrintDlg;
static PageDlgFn  pPageDlg;
static CdErrFn    pCdErr;
static HGLOBAL    g_devMode, g_devNames;

static BOOL CdLoad(HWND owner) {
    if (!g_cd && (g_cd = LoadLibraryW(L"comdlg32.dll")) != NULL) {
        pPrintDlg = (PrintDlgFn)GetProcAddress(g_cd, "PrintDlgW");
        pPageDlg  = (PageDlgFn)GetProcAddress(g_cd, "PageSetupDlgW");
        pCdErr    = (CdErrFn)GetProcAddress(g_cd, "CommDlgExtendedError");
    }
    if (pPrintDlg && pPageDlg && pCdErr) return TRUE;
    MpNote(owner, APP_NAME, L"cannot load comdlg32.dll, so printing is not available.");
    return FALSE;
}

static void ForgetPrinter(void) {
    if (g_devMode) GlobalFree(g_devMode);
    if (g_devNames) GlobalFree(g_devNames);
    g_devMode = g_devNames = NULL;
}

static BOOL CdRun(HWND owner, PRINTDLGW *printDlg, PAGESETUPDLGW *pageDlg) {
    WCHAR errorText[96];
    HWND held = DialogHoldFind();
    DWORD err = 0;
    BOOL ok = FALSE;
    int tries;
    for (tries = 0; tries < 2; tries++) {
        if (printDlg) {
            printDlg->hDevMode = g_devMode; printDlg->hDevNames = g_devNames;
            ok = pPrintDlg(printDlg);
            g_devMode = printDlg->hDevMode; g_devNames = printDlg->hDevNames;
        } else {
            pageDlg->hDevMode = g_devMode; pageDlg->hDevNames = g_devNames;
            ok = pPageDlg(pageDlg);
            g_devMode = pageDlg->hDevMode; g_devNames = pageDlg->hDevNames;
        }
        if (ok) break;
        err = pCdErr();
        if (err != PDERR_PRINTERNOTFOUND && err != PDERR_DNDMMISMATCH && err != PDERR_DEFAULTDIFFERENT) break;
        ForgetPrinter();
    }
    DialogReleaseFind(held);
    if (ok) return TRUE;
    if (!err || err == PDERR_NODEFAULTPRN || err == PDERR_NODEVICES) return FALSE;
    wsprintfW(errorText, L"cannot use the printer (error 0x%lx).", err);
    MpNote(owner, APP_NAME, errorText);
    return FALSE;
}

#define ID_HEADER 0x7F01
#define ID_FOOTER 0x7F02
static WCHAR g_newHeader[PAGE_TEXT_CAP], g_newFooter[PAGE_TEXT_CAP];

static HWND PageRow(HWND dlg, HWND after, HFONT font, const WCHAR *label, int id, const WCHAR *text, const RECT *row, int labelWidth) {
    int height = row->bottom - row->top;
    HWND caption = CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE | SS_LEFT, row->left, row->top + height / 5, labelWidth, height, dlg, (HMENU)(LONG_PTR)-1, g_hinst, NULL);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, row->left + labelWidth, row->top, row->right - row->left - labelWidth, height, dlg, (HMENU)(LONG_PTR)id, g_hinst, NULL);
    if (!caption || !edit) return after;
    SendMessageW(caption, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(edit, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(edit, EM_LIMITTEXT, PAGE_TEXT_CAP - 1, 0);
    SetWindowPos(caption, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(edit, caption, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return edit;
}

static void PageHookInit(HWND dlg) {
    static const int buttons[] = { IDOK, IDCANCEL, 0x0402, 0x040E };
    HFONT font = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
    RECT units = { 0, 0, 36, 14 }, rect, window, row;
    HWND child, after;
    int contentBottom = 0, contentLeft = 0x7FFFFFFF, contentRight = 0, gap, band, i, isButton;
    MapDialogRect(dlg, &units);
    gap = units.bottom / 3;
    band = 2 * units.bottom + 3 * gap;
    for (child = GetWindow(dlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(child);
        if (!IsWindowVisible(child)) continue;
        for (i = 0, isButton = 0; i < COUNTOF(buttons); i++) if (id == buttons[i]) isButton = 1;
        if (isButton) continue;
        GetWindowRect(child, &rect);
        MapWindowPoints(NULL, dlg, (POINT *)&rect, 2);
        if (rect.bottom > contentBottom) contentBottom = rect.bottom;
        if (rect.left < contentLeft) contentLeft = rect.left;
        if (rect.right > contentRight) contentRight = rect.right;
    }
    if (contentRight <= contentLeft) return;
    for (i = 0; i < COUNTOF(buttons); i++) {
        HWND button = GetDlgItem(dlg, buttons[i]);
        if (!button) continue;
        GetWindowRect(button, &rect);
        MapWindowPoints(NULL, dlg, (POINT *)&rect, 2);
        if (rect.top >= contentBottom - gap) SetWindowPos(button, NULL, rect.left, rect.top + band, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    GetWindowRect(dlg, &window);
    SetWindowPos(dlg, NULL, 0, 0, window.right - window.left, window.bottom - window.top + band, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    after = GetWindow(GetDlgItem(dlg, IDOK), GW_HWNDPREV);
    SetRect(&row, contentLeft, contentBottom + gap, contentRight, contentBottom + gap + units.bottom);
    after = PageRow(dlg, after ? after : HWND_TOP, font, L"&Header:", ID_HEADER, g_pf.header, &row, units.right);
    OffsetRect(&row, 0, units.bottom + gap);
    PageRow(dlg, after, font, L"&Footer:", ID_FOOTER, g_pf.footer, &row, units.right);
}

static UINT_PTR CALLBACK PageHook(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (msg == WM_INITDIALOG) { PageHookInit(dlg); return TRUE; }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) {
        HWND header = GetDlgItem(dlg, ID_HEADER), footer = GetDlgItem(dlg, ID_FOOTER);
        if (header) GetWindowTextW(header, g_newHeader, PAGE_TEXT_CAP);
        if (footer) GetWindowTextW(footer, g_newFooter, PAGE_TEXT_CAP);
    }
    return 0;
}

void PageSetup(HWND owner) {
    PAGESETUPDLGW pageDlg;
    if (!CdLoad(owner)) return;
    memset(&pageDlg, 0, sizeof pageDlg);
    pageDlg.lStructSize = sizeof pageDlg;
    pageDlg.hwndOwner = owner;
    pageDlg.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES | PSD_ENABLEPAGESETUPHOOK;
    pageDlg.lpfnPageSetupHook = (void *)PageHook;
    wcopy(g_newHeader, g_pf.header, PAGE_TEXT_CAP);
    wcopy(g_newFooter, g_pf.footer, PAGE_TEXT_CAP);
    pageDlg.rtMargin.left   = g_pf.marginL > 0 ? g_pf.marginL : 0;
    pageDlg.rtMargin.top    = g_pf.marginT > 0 ? g_pf.marginT : 0;
    pageDlg.rtMargin.right  = g_pf.marginR > 0 ? g_pf.marginR : 0;
    pageDlg.rtMargin.bottom = g_pf.marginB > 0 ? g_pf.marginB : 0;
    if (!CdRun(owner, NULL, &pageDlg)) return;
    g_pf.marginL = pageDlg.rtMargin.left;
    g_pf.marginT = pageDlg.rtMargin.top;
    g_pf.marginR = pageDlg.rtMargin.right;
    g_pf.marginB = pageDlg.rtMargin.bottom;
    wcopy(g_pf.header, g_newHeader, PAGE_TEXT_CAP);
    wcopy(g_pf.footer, g_newFooter, PAGE_TEXT_CAP);
    AppSavePrefs();
}

typedef struct {
    HDC printDc;
    const WCHAR *text;
    int length, pos;
    int readPos, lineEnd, col, open;
    WCHAR *buf;
    int bufLen, bufCap, used, skip;
    int width, chunk;
} Rows;

static void RowDrop(Rows *rows, int count) {
    if (count <= 0) return;
    rows->bufLen -= count;
    memmove(rows->buf, rows->buf + count, (size_t)rows->bufLen * sizeof(WCHAR));
}

static BOOL RowFill(Rows *rows, int need) {
    while (rows->bufLen < need && rows->readPos < rows->lineEnd) {
        WCHAR ch = rows->text[rows->readPos++];
        if (rows->bufLen + 8 > rows->bufCap) {
            int newCapacity = rows->bufCap * 2 + 256;
            WCHAR *newBuf = (WCHAR *)mem_realloc(rows->buf, (size_t)newCapacity * sizeof(WCHAR));
            if (!newBuf) return FALSE;
            rows->buf = newBuf;
            rows->bufCap = newCapacity;
        }
        if (ch == '\t') {
            int tabSize = g_pf.tab > 0 ? g_pf.tab : 8;
            int spaces = tabSize - (rows->col % tabSize);
            rows->col += spaces;
            while (spaces-- > 0) rows->buf[rows->bufLen++] = ' ';
        } else {
            rows->buf[rows->bufLen++] = ch;
            if (!LO_SUR(ch)) rows->col++;
        }
    }
    return TRUE;
}

static BOOL IsCjk(WCHAR ch) {
    return (ch >= 0x3000 && ch <= 0x30FF) || (ch >= 0x3400 && ch <= 0x9FFF) || (ch >= 0xAC00 && ch <= 0xD7A3) || (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0xFF00 && ch <= 0xFFEF);
}

static BOOL CjkBreak(WCHAR prev, WCHAR next) {
    if (!IsCjk(prev)) return FALSE;
    switch (prev) {
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301A:
    case 0x301D: case 0xFF08: case 0xFF3B: case 0xFF5B:
        return FALSE;
    }
    switch (next) {
    case '.': case ',': case ':': case ';': case '!': case '?': case ')': case ']': case '}':
    case 0x2019: case 0x201D: case 0x2025: case 0x2026:
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F: case 0xFF1A: case 0xFF1B:
    case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017: case 0x3019:
    case 0x301B: case 0x301E: case 0x301F: case 0xFF09: case 0xFF3D: case 0xFF5D:
    case 0x3005: case 0x301C: case 0x30FB: case 0x30FC: case 0x30FD: case 0x30FE:
    case 0x3099: case 0x309A: case 0x309B: case 0x309C: case 0x309D: case 0x309E: case 0xFF9E: case 0xFF9F:
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E: case 0x3095: case 0x3096:
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6:
        return FALSE;
    }
    return TRUE;
}

static int RowNext(Rows *rows, const WCHAR **rowText, int *rowLen) {
    int need = rows->chunk, fit = 0, breakAt, i, cjkAt;
    SIZE size;
    RowDrop(rows, rows->used);
    rows->used = 0;
    for (;;) {
        if (!rows->open) {
            if (rows->pos >= rows->length) return 0;
            rows->readPos = rows->lineEnd = rows->pos;
            while (rows->lineEnd < rows->length && rows->text[rows->lineEnd] != '\r' && rows->text[rows->lineEnd] != '\n') rows->lineEnd++;
            rows->pos = rows->lineEnd;
            if (rows->pos < rows->length) rows->pos += (rows->text[rows->pos] == '\r' && rows->pos + 1 < rows->length && rows->text[rows->pos + 1] == '\n') ? 2 : 1;
            rows->col = 0; rows->bufLen = 0; rows->skip = 0; rows->open = 1;
        }
        if (!RowFill(rows, need)) return -1;
        if (!rows->skip) break;
        for (i = 0; i < rows->bufLen && rows->buf[i] == ' '; i++) {}
        RowDrop(rows, i);
        if (rows->bufLen) rows->skip = 0;
        else if (rows->readPos >= rows->lineEnd) rows->open = 0;
    }
    for (;;) {
        if (!RowFill(rows, need)) return -1;
        if (!rows->bufLen) { rows->open = 0; *rowText = rows->buf; *rowLen = 0; return 1; }
        if (!GetTextExtentExPointW(rows->printDc, rows->buf, rows->bufLen, rows->width, &fit, NULL, &size)) return -1;
        if (fit < rows->bufLen || rows->readPos >= rows->lineEnd) break;
        rows->chunk = need = rows->bufLen * 2;
    }
    if (fit >= rows->bufLen) {
        breakAt = rows->bufLen;
    } else {
        breakAt = rows->buf[fit] == ' ' ? fit + 1 : fit;
        while (breakAt > 0 && rows->buf[breakAt - 1] != ' ') breakAt--;
        for (i = 0; i < breakAt && rows->buf[i] == ' '; i++) {}
        if (i == breakAt && i < fit) breakAt = 0;
        for (cjkAt = fit; cjkAt > breakAt && !CjkBreak(rows->buf[cjkAt - 1], rows->buf[cjkAt]); cjkAt--) {}
        if (cjkAt > breakAt && breakAt) {
            if (!GetTextExtentExPointW(rows->printDc, rows->buf, breakAt, 0, NULL, NULL, &size)) return -1;
            if (size.cx * 2 >= rows->width) cjkAt = breakAt;
        }
        if (cjkAt > breakAt) {
            breakAt = cjkAt;
        } else if (breakAt) {
            rows->skip = 1;
        } else {
            breakAt = fit > 0 ? fit : 1;
            if (breakAt < rows->bufLen && LO_SUR(rows->buf[breakAt]) && HI_SUR(rows->buf[breakAt - 1])) breakAt = breakAt > 1 ? breakAt - 1 : 2;
        }
    }
    rows->used = breakAt;
    if (breakAt == rows->bufLen && rows->readPos >= rows->lineEnd) rows->open = 0;
    *rowText = rows->buf;
    *rowLen = breakAt;
    return 1;
}

static int Px(int thousandths, int dpi) { return thousandths > 0 ? MulDiv(thousandths, dpi, 1000) : 0; }

static void PageAppend(WCHAR *out, int *length, int cap, const WCHAR *add) {
    while (*add && *length < cap - 1) out[(*length)++] = *add++;
    out[*length] = 0;
}

static void PageLine(HDC printDc, const WCHAR *format, const RECT *area, int page) {
    static const UINT align[3] = { DT_LEFT, DT_CENTER, DT_RIGHT };
    WCHAR parts[3][PATH_CAP], piece[64], single[2];
    int length[3] = { 0, 0, 0 }, part = 1, i;
    const WCHAR *src;
    RECT rect;
    parts[0][0] = parts[1][0] = parts[2][0] = 0;
    for (src = format; *src; src++) {
        const WCHAR *add = single;
        single[0] = *src;
        single[1] = 0;
        if (*src == '&' && src[1]) {
            switch (wlow(src[1])) {
            case 'l': part = 0; src++; continue;
            case 'c': part = 1; src++; continue;
            case 'r': part = 2; src++; continue;
            case 'f': add = AppDocName(); src++; break;
            case 'p': wsprintfW(piece, L"%d", page); add = piece; src++; break;
            case 'd': if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, piece, 64)) piece[0] = 0; add = piece; src++; break;
            case 't': if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, piece, 64)) piece[0] = 0; add = piece; src++; break;
            case '&': src++; break;
            }
        }
        PageAppend(parts[part], &length[part], PATH_CAP, add);
    }
    for (i = 0; i < 3; i++) {
        if (!parts[i][0]) continue;
        rect = *area;
        DrawTextW(printDc, parts[i], -1, &rect, align[i] | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

static BOOL PageBegin(HDC printDc, HFONT font, const RECT *box, int vertRes, int lineHeight, int page) {
    RECT rect;
    if (StartPage(printDc) <= 0) return FALSE;
    SelectObject(printDc, font);
    SetTextColor(printDc, RGB(0, 0, 0));
    SetBkColor(printDc, RGB(0xff, 0xff, 0xff));
    SetBkMode(printDc, TRANSPARENT);
    if (box->top >= lineHeight) {
        SetRect(&rect, box->left, 0, box->right, box->top);
        PageLine(printDc, g_pf.header, &rect, page);
    }
    if (vertRes - box->bottom >= lineHeight) {
        SetRect(&rect, box->left, box->bottom, box->right, vertRes);
        PageLine(printDc, g_pf.footer, &rect, page);
    }
    return TRUE;
}

static DlgBase g_printing;
static int g_printAbort;

static BOOL PrintPump(void) {
    MSG message;
    while (!g_printAbort && PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { PostQuitMessage((int)message.wParam); break; }
        if (!g_printing.hwnd || !IsDialogMessageW(g_printing.hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !g_printAbort;
}

static BOOL CALLBACK AbortProc(HDC printDc, int code) {
    (void)printDc;
    (void)code;
    return PrintPump();
}

static LRESULT CALLBACK PrintingProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        WCHAR line[PATH_CAP + 16];
        wcopy(line, L"printing ", COUNTOF(line));
        wcat(line, AppDocName(), COUNTOF(line));
        UiLabel(hwnd, line, 20, 20, 300, 20, 0, SS_NOPREFIX | SS_ENDELLIPSIS);
        base->focus = UiButton(hwnd, L"cancel", 232, 52, 88, 24, IDCANCEL, BS_DEFPUSHBUTTON);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK) { g_printAbort = 1; EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE); return 0; }
        break;
    case WM_CLOSE:
        g_printAbort = 1;
        return 0;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void PrintingOpen(HWND owner) {
    static BOOL registered;
    if (!registered) { RegClass(L"mp_printing", PrintingProc, 0, NULL); registered = TRUE; }
    g_printAbort = 0;
    DlgBaseInit(&g_printing, owner);
    if (owner) EnableWindow(owner, FALSE);
    if (DlgOpen(&g_printing, L"mp_printing", APP_NAME, 340, 92, 1)) ShowWindow(g_printing.hwnd, SW_SHOW);
}

static void PrintingClose(HWND owner) {
    if (owner) EnableWindow(owner, TRUE);
    if (g_printing.hwnd) DestroyWindow(g_printing.hwnd);
    g_printing.hwnd = NULL;
}

static BOOL UserCancelled(DWORD err) { return err == ERROR_PRINT_CANCELLED || err == ERROR_CANCELLED; }

static int PrintJob(HDC printDc, const WCHAR *text, int length, int copies, int firstPage, int lastPage) {
    const WCHAR *name = AppDocName(), *rowText = NULL;
    TEXTMETRICW metrics;
    DOCINFOW docInfo;
    RECT box;
    Rows rows;
    HFONT font;
    HGDIOBJ oldFont;
    DWORD err = 0;
    int dpiX, dpiY, horzRes, vertRes, physWidth, physHeight, offsetX, offsetY, lineHeight, rowsPerPage, copy, page, row, rowLen = 0, rowStatus, inRange, ok, started, printed = 0;
    dpiX = GetDeviceCaps(printDc, LOGPIXELSX);
    dpiY = GetDeviceCaps(printDc, LOGPIXELSY);
    if (dpiX <= 0) dpiX = 96;
    if (dpiY <= 0) dpiY = 96;
    horzRes = GetDeviceCaps(printDc, HORZRES);
    vertRes = GetDeviceCaps(printDc, VERTRES);
    physWidth = GetDeviceCaps(printDc, PHYSICALWIDTH);
    physHeight = GetDeviceCaps(printDc, PHYSICALHEIGHT);
    offsetX = GetDeviceCaps(printDc, PHYSICALOFFSETX);
    offsetY = GetDeviceCaps(printDc, PHYSICALOFFSETY);
    if (physWidth <= 0 || physHeight <= 0) { physWidth = horzRes; physHeight = vertRes; offsetX = offsetY = 0; }
    font = CreateFontW(-MulDiv(g_pf.pt, dpiY, 720), 0, 0, 0, g_pf.bold ? FW_BOLD : FW_NORMAL, g_pf.italic ? TRUE : FALSE, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_pf.font);
    if (!font) return 0;
    oldFont = SelectObject(printDc, font);
    memset(&metrics, 0, sizeof metrics);
    GetTextMetricsW(printDc, &metrics);
    lineHeight = metrics.tmHeight + metrics.tmExternalLeading;
    if (lineHeight < 1) lineHeight = 1;
    box.left   = Px(g_pf.marginL, dpiX) - offsetX;
    box.top    = Px(g_pf.marginT, dpiY) - offsetY;
    box.right  = physWidth - Px(g_pf.marginR, dpiX) - offsetX;
    box.bottom = physHeight - Px(g_pf.marginB, dpiY) - offsetY;
    if (box.left < 0) box.left = 0;
    if (box.top < 0) box.top = 0;
    if (box.right > horzRes) box.right = horzRes;
    if (box.bottom > vertRes) box.bottom = vertRes;
    if (box.right - box.left < dpiX / 2) { box.left = 0; box.right = horzRes; }
    if (box.bottom - box.top < lineHeight) { box.top = 0; box.bottom = vertRes; }
    if (box.right <= box.left) box.right = box.left + 1;
    rowsPerPage = (box.bottom - box.top) / lineHeight;
    if (rowsPerPage < 1) rowsPerPage = 1;
    memset(&rows, 0, sizeof rows);
    rows.printDc = printDc;
    rows.text = text;
    rows.length = length;
    rows.width = box.right - box.left;
    rows.chunk = rows.width / (metrics.tmAveCharWidth > 0 ? metrics.tmAveCharWidth : 1) + 16;
    memset(&docInfo, 0, sizeof docInfo);
    docInfo.cbSize = (int)sizeof docInfo;
    docInfo.lpszDocName = name;
    SetAbortProc(printDc, AbortProc);
    started = StartDocW(printDc, &docInfo) > 0;
    ok = started;
    if (!started && UserCancelled(GetLastError())) ok = -1;
    for (copy = 0; started && ok && copy < copies; copy++) {
        rows.pos = rows.open = rows.bufLen = rows.used = rows.skip = 0;
        for (page = 1; ok && page <= lastPage; page++) {
            rowStatus = RowNext(&rows, &rowText, &rowLen);
            if (rowStatus < 0) { ok = FALSE; break; }
            if (!rowStatus && page > 1) break;
            inRange = page >= firstPage;
            if (inRange) {
                ok = PageBegin(printDc, font, &box, vertRes, lineHeight, page);
                if (!ok) err = GetLastError();
            }
            for (row = 0; ok && rowStatus > 0; ) {
                if (inRange && rowLen > 0 && !TextOutW(printDc, box.left, box.top + row * lineHeight, rowText, rowLen)) {
                    err = GetLastError(); ok = FALSE; break;
                }
                if (++row >= rowsPerPage) break;
                rowStatus = RowNext(&rows, &rowText, &rowLen);
                if (rowStatus < 0) ok = FALSE;
            }
            if (ok && inRange) {
                ok = EndPage(printDc) > 0;
                if (ok) printed++; else err = GetLastError();
            }
            if (ok && !PrintPump()) { err = ERROR_CANCELLED; ok = FALSE; }
            if (!rowStatus) break;
        }
    }
    if (started && ok) {
        if (!printed) { AbortDoc(printDc); ok = -1; }
        else { ok = EndDoc(printDc) > 0; if (!ok) err = GetLastError(); }
    }
    if (started && !ok) {
        AbortDoc(printDc);
        if (UserCancelled(err)) ok = -1;
    }
    SelectObject(printDc, oldFont);
    DeleteObject(font);
    mem_free(rows.buf);
    return ok;
}

void PrintDoc(HWND owner, int quiet, const WCHAR *printer) {
    PRINTDLGW printDlg;
    HCURSOR oldCursor;
    WCHAR *text;
    DWORD selStart = 0, selEnd = 0;
    int length = 0, firstPage = 1, lastPage = 0x7FFFFFFF, ok;
    if (!printer && !CdLoad(owner)) return;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    memset(&printDlg, 0, sizeof printDlg);
    printDlg.lStructSize = sizeof printDlg;
    printDlg.hwndOwner = owner;
    printDlg.Flags = (quiet ? PD_RETURNDEFAULT : 0) | PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_HIDEPRINTTOFILE | PD_DISABLEPRINTTOFILE;
    if (quiet || selStart == selEnd) printDlg.Flags |= PD_NOSELECTION;
    printDlg.nFromPage = 1;
    printDlg.nToPage = 1;
    printDlg.nMinPage = 1;
    printDlg.nMaxPage = 0xFFFF;
    printDlg.nCopies = 1;
    if (printer) {
        printDlg.hDC = CreateDCW(L"WINSPOOL", printer, NULL, NULL);
        if (!printDlg.hDC) { MpNote(owner, APP_NAME, L"cannot find that printer."); return; }
        printDlg.Flags &= ~(DWORD)(PD_PAGENUMS | PD_SELECTION);
    } else if (!CdRun(owner, &printDlg, NULL)) {
        return;
    }
    if (printDlg.Flags & PD_PAGENUMS) { firstPage = printDlg.nFromPage; lastPage = printDlg.nToPage; }
    oldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    if (printDlg.Flags & PD_SELECTION) {
        void *handle = NULL;
        const WCHAR *source = EditLockText(&handle, &length);
        text = NULL;
        if (source) {
            if (selStart > (DWORD)length) selStart = (DWORD)length;
            if (selEnd > (DWORD)length) selEnd = (DWORD)length;
            length = (int)(selEnd - selStart);
            text = (WCHAR *)mem_alloc(((size_t)length + 1) * sizeof(WCHAR));
            if (text) { memcpy(text, source + selStart, (size_t)length * sizeof(WCHAR)); text[length] = 0; }
        }
        EditUnlockText(handle);
    } else text = EditGetDocText(&length);
    PrintingOpen(owner);
    ok = printDlg.hDC && text ? PrintJob(printDlg.hDC, text, length, printDlg.nCopies > 1 ? printDlg.nCopies : 1, firstPage, lastPage) : 0;
    PrintingClose(owner);
    SetCursor(oldCursor ? oldCursor : LoadCursorW(NULL, IDC_ARROW));
    mem_free(text);
    if (printDlg.hDC) DeleteDC(printDlg.hDC);
    if (!ok) MpNote(owner, APP_NAME, L"cannot print the document.");
}
