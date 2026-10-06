/* print.c - file > page setup... and file > print... (comdlg32, loaded on first use) + plain text pagination */
#include "mp.h"

/* the 32-bit sdk sizes (commdlg.h packs PRINTDLGW to 1 byte on x86) */
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
static HGLOBAL    g_devMode, g_devNames;        /* the printer and its settings, shared by both dialogs */

/* ------------------------------------------------------------ comdlg32 -- */
static BOOL CdLoad(HWND owner)
{
    if (!g_cd && (g_cd = LoadLibraryW(L"comdlg32.dll")) != NULL) {
        pPrintDlg = (PrintDlgFn)GetProcAddress(g_cd, "PrintDlgW");
        pPageDlg  = (PageDlgFn)GetProcAddress(g_cd, "PageSetupDlgW");
        pCdErr    = (CdErrFn)GetProcAddress(g_cd, "CommDlgExtendedError");
    }
    if (pPrintDlg && pPageDlg && pCdErr) return TRUE;
    MpAsk(owner, APP_NAME, L"cannot load comdlg32.dll, so printing is not available.", L"ok", NULL, NULL, 1);
    return FALSE;
}

static void ForgetPrinter(void)
{
    if (g_devMode) GlobalFree(g_devMode);
    if (g_devNames) GlobalFree(g_devNames);
    g_devMode = g_devNames = NULL;
}

/* runs the print (pd) or page setup (ps) dialog. while it runs the dialog owns the device handles (it may
 * reallocate them), so what it hands back is simply adopted. a printer that is gone or no longer the default
 * since the last dialog => forget it and ask once more. FALSE = cancelled (silent) or failed (reported) */
static BOOL CdRun(HWND owner, PRINTDLGW *pd, PAGESETUPDLGW *ps)
{
    WCHAR msg[96];
    DWORD er = 0;
    BOOL ok;
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
        if (ok) return TRUE;
        er = pCdErr();
        if (er != PDERR_PRINTERNOTFOUND && er != PDERR_DNDMMISMATCH && er != PDERR_DEFAULTDIFFERENT) break;
        ForgetPrinter();
    }
    if (!er || er == PDERR_NODEFAULTPRN) return FALSE;      /* cancelled / no printer (comdlg32 has said so itself) */
    wsprintfW(msg, L"cannot use the printer (error 0x%lx).", er);
    MpAsk(owner, APP_NAME, msg, L"ok", NULL, NULL, 1);
    return FALSE;
}

/* ---------------------------------------------------------- page setup -- */
void PageSetup(HWND owner)
{
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

/* --------------------------------------------------------------- rows ---- */
/* the text as printed rows, produced lazily: only the current logical line from the current row on is kept
 * (tabs expanded), so memory stays at about one row whatever the document size */
typedef struct {
    HDC dc;
    const WCHAR *t;
    int n, pos;                     /* the text (CRLF) and the start of the next logical line */
    int sp, le, col, open;          /* the current line: next source char, its end, expanded column, rows left */
    WCHAR *wb;                      /* its expanded chars from the current row on */
    int wn, wcap, used, skip;       /* chars in wb, capacity, chars handed out by the last RowNext, it broke at a space */
    int width, chunk;               /* text box width (device px), how many chars to measure at once */
} Rows;

static void RowDrop(Rows *r, int n)
{
    if (n <= 0) return;
    r->wn -= n;
    memmove(r->wb, r->wb + n, (size_t)r->wn * sizeof(WCHAR));
}

/* expands source chars of the current line into wb until it holds `need` chars or the line ends */
static BOOL RowFill(Rows *r, int need)
{
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
            int s = 8 - (r->col & 7);
            r->col += s;
            while (s-- > 0) r->wb[r->wn++] = ' ';
        } else {
            r->wb[r->wn++] = c;
            if (!LO_SUR(c)) r->col++;                       /* a surrogate pair is one column */
        }
    }
    return TRUE;
}

/* the next row: *s / *k (valid until the next call). 1 = a row, 0 = no rows left, -1 = failure */
static int RowNext(Rows *r, const WCHAR **s, int *k)
{
    int need = r->chunk, fit = 0, j, i;
    SIZE sz;
    RowDrop(r, r->used);
    r->used = 0;
    for (;;) {
        if (!r->open) {                                     /* next logical line: split at CRLF, bare CR or bare LF */
            if (r->pos >= r->n) return 0;
            r->sp = r->le = r->pos;
            while (r->le < r->n && r->t[r->le] != '\r' && r->t[r->le] != '\n') r->le++;
            r->pos = r->le;
            if (r->pos < r->n) r->pos += (r->t[r->pos] == '\r' && r->pos + 1 < r->n && r->t[r->pos + 1] == '\n') ? 2 : 1;
            r->col = 0; r->wn = 0; r->skip = 0; r->open = 1;
        }
        if (!RowFill(r, need)) return -1;
        if (!r->skip) break;
        for (i = 0; i < r->wn && r->wb[i] == ' '; i++) {}   /* after a break at a space: drop the spaces the row starts with */
        RowDrop(r, i);
        if (r->wn) r->skip = 0;
        else if (r->sp >= r->le) r->open = 0;               /* only spaces were left */
    }
    for (;;) {
        if (!RowFill(r, need)) return -1;
        if (!r->wn) { r->open = 0; *s = r->wb; *k = 0; return 1; }     /* an empty line */
        if (!GetTextExtentExPointW(r->dc, r->wb, r->wn, r->width, &fit, NULL, &sz)) return -1;
        if (fit < r->wn || r->sp >= r->le) break;
        r->chunk = need = r->wn * 2;                        /* all of it fits and the line goes on: measure more */
    }
    if (fit >= r->wn) {
        j = r->wn;
    } else {
        j = r->wb[fit] == ' ' ? fit + 1 : fit;              /* break after the last space that fits */
        while (j > 0 && r->wb[j - 1] != ' ') j--;
        for (i = 0; i < j && r->wb[i] == ' '; i++) {}
        if (j && (i < j || i >= fit)) {                     /* after a word, or a run of spaces wider than the row */
            r->skip = 1;
        } else {                                            /* no space (indentation alone doesn't count): hard break */
            j = fit > 0 ? fit : 1;                          /* always at least one char, never half a surrogate pair */
            if (j < r->wn && LO_SUR(r->wb[j]) && HI_SUR(r->wb[j - 1])) j = j > 1 ? j - 1 : 2;
        }
    }
    r->used = j;
    if (j == r->wn && r->sp >= r->le) r->open = 0;
    *s = r->wb;
    *k = j;
    return 1;
}

/* ------------------------------------------------------------- printing -- */
static int Px(int thou, int dpi) { return thou > 0 ? MulDiv(thou, dpi, 1000) : 0; }

static BOOL PageBegin(HDC dc, HFONT font, const RECT *box, int vert, int lh, const WCHAR *name, int page)
{
    WCHAR foot[32];
    RECT r;
    if (StartPage(dc) <= 0) return FALSE;
    SelectObject(dc, font);
    SetTextColor(dc, RGB(0, 0, 0));                         /* black on white paper whatever the theme */
    SetBkColor(dc, RGB(0xff, 0xff, 0xff));
    SetBkMode(dc, TRANSPARENT);
    if (box->top >= lh) {                                   /* header: the file name, centred in the top margin */
        SetRect(&r, box->left, 0, box->right, box->top);
        DrawTextW(dc, name, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (vert - box->bottom >= lh) {                         /* footer: the page number, centred in the bottom margin */
        wsprintfW(foot, L"page %d", page);
        SetRect(&r, box->left, box->bottom, box->right, vert);
        DrawTextW(dc, foot, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    return TRUE;
}

/* one print job: `copies` collated copies of pages from..to. 1 = printed, 0 = failed (the job is aborted),
 * -1 = the user cancelled it before it started (e.g. the "save print output as" box of a pdf printer) */
static int PrintJob(HDC dc, const WCHAR *text, int n, int copies, int from, int to)
{
    const WCHAR *name = g_doc.path[0] ? PathName(g_doc.path) : L"untitled", *s = NULL;
    TEXTMETRICW tm;
    DOCINFOW di;
    RECT box;
    Rows r;
    HFONT font;
    HGDIOBJ old;
    int lpx, lpy, hz, vt, pw, ph, ox, oy, lh, rows, copy, page, row, k = 0, got, out, ok, started;

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
    if (pw <= 0 || ph <= 0) { pw = hz; ph = vt; ox = oy = 0; }       /* not a printer: all of the page is printable */

    font = CreateFontW(-MulDiv(g_pf.pt, lpy, 72), 0, 0, 0, g_pf.bold ? FW_BOLD : FW_NORMAL, g_pf.italic ? TRUE : FALSE, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, g_pf.font);
    if (!font) return 0;
    old = SelectObject(dc, font);
    memset(&tm, 0, sizeof tm);
    GetTextMetricsW(dc, &tm);
    lh = tm.tmHeight + tm.tmExternalLeading;
    if (lh < 1) lh = 1;

    /* the text box: margins count from the paper edge, the dc origin is the corner of the printable area */
    box.left   = Px(g_pf.marginL, lpx) - ox;
    box.top    = Px(g_pf.marginT, lpy) - oy;
    box.right  = pw - Px(g_pf.marginR, lpx) - ox;
    box.bottom = ph - Px(g_pf.marginB, lpy) - oy;
    if (box.left < 0) box.left = 0;
    if (box.top < 0) box.top = 0;
    if (box.right > hz) box.right = hz;
    if (box.bottom > vt) box.bottom = vt;
    if (box.right - box.left < lpx / 2) { box.left = 0; box.right = hz; }  /* margins leave no room: the printable area */
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
    if (!started && (GetLastError() == ERROR_CANCELLED || GetLastError() == ERROR_PRINT_CANCELLED)) ok = -1;
    for (copy = 0; started && ok && copy < copies; copy++) {
        r.pos = r.open = r.wn = r.used = r.skip = 0;
        for (page = 1; ok && page <= to; page++) {
            got = RowNext(&r, &s, &k);
            if (got < 0) { ok = FALSE; break; }
            if (!got && page > 1) break;                    /* a page starts only when it has a row (page 1 always) */
            out = page >= from;                             /* pages before the range are laid out, not printed */
            if (out) ok = PageBegin(dc, font, &box, vt, lh, name, page);
            for (row = 0; ok && got > 0; ) {
                if (out && k > 0) TextOutW(dc, box.left, box.top + row * lh, s, k);
                if (++row >= rows) break;
                got = RowNext(&r, &s, &k);
                if (got < 0) ok = FALSE;
            }
            if (ok && out) ok = EndPage(dc) > 0;
            if (!got) break;
        }
    }
    if (started && ok) ok = EndDoc(dc) > 0;
    if (started && !ok) AbortDoc(dc);
    SelectObject(dc, old);
    DeleteObject(font);
    mem_free(r.wb);
    return ok;
}

void PrintDoc(HWND owner)
{
    PRINTDLGW pd;
    HCURSOR cur;
    WCHAR *text;
    int n = 0, from = 1, to = 0x7FFFFFFF, ok;

    if (!CdLoad(owner)) return;
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nFromPage = 1;
    pd.nToPage = 1;
    pd.nMinPage = 1;
    pd.nMaxPage = 0xFFFF;
    pd.nCopies = 1;
    if (!CdRun(owner, &pd, NULL)) return;
    if (pd.Flags & PD_PAGENUMS) { from = pd.nFromPage; to = pd.nToPage; }

    cur = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    text = EditGetDocText(&n);
    /* with PD_USEDEVMODECOPIESANDCOLLATE the driver makes the copies and nCopies comes back 1; more => repeat the document */
    ok = pd.hDC && text ? PrintJob(pd.hDC, text, n, pd.nCopies > 1 ? pd.nCopies : 1, from, to) : 0;
    SetCursor(cur);
    mem_free(text);
    if (pd.hDC) DeleteDC(pd.hDC);
    if (!ok) MpAsk(owner, APP_NAME, L"cannot print the document.", L"ok", NULL, NULL, 1);
}
