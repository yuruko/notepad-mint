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

static BOOL CdRun(HWND owner, PRINTDLGW *pd, PAGESETUPDLGW *ps) {
    WCHAR msg[96];
    HWND held = DialogHoldFind();
    DWORD er = 0;
    BOOL ok = FALSE;
    int tries;
    for (tries = 0; tries < 2; tries++) {
        if (pd) {
            pd->hDevMode = g_devMode; pd->hDevNames = g_devNames;
            ok = pPrintDlg(pd);
            g_devMode = pd->hDevMode; g_devNames = pd->hDevNames;
        } else {
            ps->hDevMode = g_devMode; ps->hDevNames = g_devNames;
            ok = pPageDlg(ps);
            g_devMode = ps->hDevMode; g_devNames = ps->hDevNames;
        }
        if (ok) break;
        er = pCdErr();
        if (er != PDERR_PRINTERNOTFOUND && er != PDERR_DNDMMISMATCH && er != PDERR_DEFAULTDIFFERENT) break;
        ForgetPrinter();
    }
    DialogReleaseFind(held);
    if (ok) return TRUE;
    if (!er || er == PDERR_NODEFAULTPRN || er == PDERR_NODEVICES) return FALSE;
    wsprintfW(msg, L"cannot use the printer (error 0x%lx).", er);
    MpNote(owner, APP_NAME, msg);
    return FALSE;
}

void PageSetup(HWND owner) {
    PAGESETUPDLGW ps;
    if (!CdLoad(owner)) return;
    memset(&ps, 0, sizeof ps);
    ps.lStructSize = sizeof ps;
    ps.hwndOwner = owner;
    ps.Flags = PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES;
    ps.rtMargin.left   = g_pf.marginL > 0 ? g_pf.marginL : 0;
    ps.rtMargin.top    = g_pf.marginT > 0 ? g_pf.marginT : 0;
    ps.rtMargin.right  = g_pf.marginR > 0 ? g_pf.marginR : 0;
    ps.rtMargin.bottom = g_pf.marginB > 0 ? g_pf.marginB : 0;
    if (!CdRun(owner, NULL, &ps)) return;
    g_pf.marginL = ps.rtMargin.left;
    g_pf.marginT = ps.rtMargin.top;
    g_pf.marginR = ps.rtMargin.right;
    g_pf.marginB = ps.rtMargin.bottom;
    AppSavePrefs();
}

typedef struct {
    HDC dc;
    const WCHAR *t;
    int n, pos;
    int sp, le, col, open;
    WCHAR *wb;
    int wn, wcap, used, skip;
    int width, chunk;
} Rows;

static void RowDrop(Rows *r, int n) {
    if (n <= 0) return;
    r->wn -= n;
    memmove(r->wb, r->wb + n, (size_t)r->wn * sizeof(WCHAR));
}

static BOOL RowFill(Rows *r, int need) {
    while (r->wn < need && r->sp < r->le) {
        WCHAR c = r->t[r->sp++];
        if (r->wn + 8 > r->wcap) {
            int cap = r->wcap * 2 + 256;
            WCHAR *p = (WCHAR *)mem_realloc(r->wb, (size_t)cap * sizeof(WCHAR));
            if (!p) return FALSE;
            r->wb = p;
            r->wcap = cap;
        }
        if (c == '\t') {
            int ts = g_pf.tab > 0 ? g_pf.tab : 8;
            int s = ts - (r->col % ts);
            r->col += s;
            while (s-- > 0) r->wb[r->wn++] = ' ';
        } else {
            r->wb[r->wn++] = c;
            if (!LO_SUR(c)) r->col++;
        }
    }
    return TRUE;
}

static BOOL IsCjk(WCHAR c) {
    return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}

static BOOL CjkBreak(WCHAR a, WCHAR b) {
    if (!IsCjk(a)) return FALSE;
    switch (a) {
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3016: case 0x3018: case 0x301A:
    case 0x301D: case 0xFF08: case 0xFF3B: case 0xFF5B:
        return FALSE;
    }
    switch (b) {
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

static int RowNext(Rows *r, const WCHAR **s, int *k) {
    int need = r->chunk, fit = 0, j, i, c;
    SIZE sz;
    RowDrop(r, r->used);
    r->used = 0;
    for (;;) {
        if (!r->open) {
            if (r->pos >= r->n) return 0;
            r->sp = r->le = r->pos;
            while (r->le < r->n && r->t[r->le] != '\r' && r->t[r->le] != '\n') r->le++;
            r->pos = r->le;
            if (r->pos < r->n) r->pos += (r->t[r->pos] == '\r' && r->pos + 1 < r->n && r->t[r->pos + 1] == '\n') ? 2 : 1;
            r->col = 0; r->wn = 0; r->skip = 0; r->open = 1;
        }
        if (!RowFill(r, need)) return -1;
        if (!r->skip) break;
        for (i = 0; i < r->wn && r->wb[i] == ' '; i++) {}
        RowDrop(r, i);
        if (r->wn) r->skip = 0;
        else if (r->sp >= r->le) r->open = 0;
    }
    for (;;) {
        if (!RowFill(r, need)) return -1;
        if (!r->wn) { r->open = 0; *s = r->wb; *k = 0; return 1; }
        if (!GetTextExtentExPointW(r->dc, r->wb, r->wn, r->width, &fit, NULL, &sz)) return -1;
        if (fit < r->wn || r->sp >= r->le) break;
        r->chunk = need = r->wn * 2;
    }
    if (fit >= r->wn) {
        j = r->wn;
    } else {
        j = r->wb[fit] == ' ' ? fit + 1 : fit;
        while (j > 0 && r->wb[j - 1] != ' ') j--;
        for (i = 0; i < j && r->wb[i] == ' '; i++) {}
        if (i == j && i < fit) j = 0;
        for (c = fit; c > j && !CjkBreak(r->wb[c - 1], r->wb[c]); c--) {}
        if (c > j && j) {
            if (!GetTextExtentExPointW(r->dc, r->wb, j, 0, NULL, NULL, &sz)) return -1;
            if (sz.cx * 2 >= r->width) c = j;
        }
        if (c > j) {
            j = c;
        } else if (j) {
            r->skip = 1;
        } else {
            j = fit > 0 ? fit : 1;
            if (j < r->wn && LO_SUR(r->wb[j]) && HI_SUR(r->wb[j - 1])) j = j > 1 ? j - 1 : 2;
        }
    }
    r->used = j;
    if (j == r->wn && r->sp >= r->le) r->open = 0;
    *s = r->wb;
    *k = j;
    return 1;
}

static int Px(int thou, int dpi) { return thou > 0 ? MulDiv(thou, dpi, 1000) : 0; }

static BOOL PageBegin(HDC dc, HFONT font, const RECT *box, int vert, int lh, const WCHAR *name, int page) {
    WCHAR foot[32];
    RECT r;
    if (StartPage(dc) <= 0) return FALSE;
    SelectObject(dc, font);
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkColor(dc, RGB(0xff, 0xff, 0xff));
    SetBkMode(dc, TRANSPARENT);
    if (box->top >= lh) {
        SetRect(&r, box->left, 0, box->right, box->top);
        DrawTextW(dc, name, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (vert - box->bottom >= lh) {
        wsprintfW(foot, L"page %d", page);
        SetRect(&r, box->left, box->bottom, box->right, vert);
        DrawTextW(dc, foot, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    return TRUE;
}

static BOOL UserCancelled(DWORD er) { return er == ERROR_PRINT_CANCELLED || er == ERROR_CANCELLED; }

static int PrintJob(HDC dc, const WCHAR *text, int n, int copies, int from, int to) {
    const WCHAR *name = AppDocName(), *s = NULL;
    TEXTMETRICW tm;
    DOCINFOW di;
    RECT box;
    Rows r;
    HFONT font;
    HGDIOBJ old;
    DWORD er = 0;
    int lpx, lpy, hz, vt, pw, ph, ox, oy, lh, rows, copy, page, row, k = 0, got, out, ok, started, printed = 0;
    lpx = GetDeviceCaps(dc, LOGPIXELSX);
    lpy = GetDeviceCaps(dc, LOGPIXELSY);
    if (lpx <= 0) lpx = 96;
    if (lpy <= 0) lpy = 96;
    hz = GetDeviceCaps(dc, HORZRES);
    vt = GetDeviceCaps(dc, VERTRES);
    pw = GetDeviceCaps(dc, PHYSICALWIDTH);
    ph = GetDeviceCaps(dc, PHYSICALHEIGHT);
    ox = GetDeviceCaps(dc, PHYSICALOFFSETX);
    oy = GetDeviceCaps(dc, PHYSICALOFFSETY);
    if (pw <= 0 || ph <= 0) { pw = hz; ph = vt; ox = oy = 0; }
    font = CreateFontW(-MulDiv(g_pf.pt, lpy, 720), 0, 0, 0, g_pf.bold ? FW_BOLD : FW_NORMAL, g_pf.italic ? TRUE : FALSE, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_pf.font);
    if (!font) return 0;
    old = SelectObject(dc, font);
    memset(&tm, 0, sizeof tm);
    GetTextMetricsW(dc, &tm);
    lh = tm.tmHeight + tm.tmExternalLeading;
    if (lh < 1) lh = 1;
    box.left   = Px(g_pf.marginL, lpx) - ox;
    box.top    = Px(g_pf.marginT, lpy) - oy;
    box.right  = pw - Px(g_pf.marginR, lpx) - ox;
    box.bottom = ph - Px(g_pf.marginB, lpy) - oy;
    if (box.left < 0) box.left = 0;
    if (box.top < 0) box.top = 0;
    if (box.right > hz) box.right = hz;
    if (box.bottom > vt) box.bottom = vt;
    if (box.right - box.left < lpx / 2) { box.left = 0; box.right = hz; }
    if (box.bottom - box.top < lh) { box.top = 0; box.bottom = vt; }
    if (box.right <= box.left) box.right = box.left + 1;
    rows = (box.bottom - box.top) / lh;
    if (rows < 1) rows = 1;
    memset(&r, 0, sizeof r);
    r.dc = dc;
    r.t = text;
    r.n = n;
    r.width = box.right - box.left;
    r.chunk = r.width / (tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 1) + 16;
    memset(&di, 0, sizeof di);
    di.cbSize = (int)sizeof di;
    di.lpszDocName = name;
    started = StartDocW(dc, &di) > 0;
    ok = started;
    if (!started && UserCancelled(GetLastError())) ok = -1;
    for (copy = 0; started && ok && copy < copies; copy++) {
        r.pos = r.open = r.wn = r.used = r.skip = 0;
        for (page = 1; ok && page <= to; page++) {
            got = RowNext(&r, &s, &k);
            if (got < 0) { ok = FALSE; break; }
            if (!got && page > 1) break;
            out = page >= from;
            if (out) {
                ok = PageBegin(dc, font, &box, vt, lh, name, page);
                if (!ok) er = GetLastError();
            }
            for (row = 0; ok && got > 0; ) {
                if (out && k > 0 && !TextOutW(dc, box.left, box.top + row * lh, s, k)) {
                    er = GetLastError(); ok = FALSE; break;
                }
                if (++row >= rows) break;
                got = RowNext(&r, &s, &k);
                if (got < 0) ok = FALSE;
            }
            if (ok && out) {
                ok = EndPage(dc) > 0;
                if (ok) printed++; else er = GetLastError();
            }
            if (!got) break;
        }
    }
    if (started && ok) {
        if (!printed) { AbortDoc(dc); ok = -1; }
        else { ok = EndDoc(dc) > 0; if (!ok) er = GetLastError(); }
    }
    if (started && !ok) {
        AbortDoc(dc);
        if (UserCancelled(er)) ok = -1;
    }
    SelectObject(dc, old);
    DeleteObject(font);
    mem_free(r.wb);
    return ok;
}

void PrintDoc(HWND owner, int quiet) {
    PRINTDLGW pd;
    HCURSOR cur;
    WCHAR *text;
    DWORD selStart = 0, selEnd = 0;
    int n = 0, from = 1, to = 0x7FFFFFFF, ok;
    if (!CdLoad(owner)) return;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = owner;
    pd.Flags = (quiet ? PD_RETURNDEFAULT : 0) | PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_HIDEPRINTTOFILE | PD_DISABLEPRINTTOFILE;
    if (quiet || selStart == selEnd) pd.Flags |= PD_NOSELECTION;
    pd.nFromPage = 1;
    pd.nToPage = 1;
    pd.nMinPage = 1;
    pd.nMaxPage = 0xFFFF;
    pd.nCopies = 1;
    if (!CdRun(owner, &pd, NULL)) return;
    if (pd.Flags & PD_PAGENUMS) { from = pd.nFromPage; to = pd.nToPage; }
    cur = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    if (pd.Flags & PD_SELECTION) {
        void *h = NULL;
        const WCHAR *source = EditLockText(&h, &n);
        text = NULL;
        if (source) {
            if (selStart > (DWORD)n) selStart = (DWORD)n;
            if (selEnd > (DWORD)n) selEnd = (DWORD)n;
            n = (int)(selEnd - selStart);
            text = (WCHAR *)mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
            if (text) { memcpy(text, source + selStart, (size_t)n * sizeof(WCHAR)); text[n] = 0; }
        }
        EditUnlockText(h);
    } else text = EditGetDocText(&n);
    ok = pd.hDC && text ? PrintJob(pd.hDC, text, n, pd.nCopies > 1 ? pd.nCopies : 1, from, to) : 0;
    SetCursor(cur ? cur : LoadCursorW(NULL, IDC_ARROW));
    mem_free(text);
    if (pd.hDC) DeleteDC(pd.hDC);
    if (!ok) MpNote(owner, APP_NAME, L"cannot print the document.");
}
