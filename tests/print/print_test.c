/* Real pagination and native edit selection, with the dialog/spooler intercepted.
 * No printer, paper, PDF output, visible window, or interactive desktop is needed. */
#include "mp.h"

API HANDLE WINAPI GetStdHandle(DWORD);
static int TestCaps(HDC dc, int cap);
static int TestStartDoc(HDC dc, const DOCINFOW *info);
static int TestStartPage(HDC dc);
static int TestEndPage(HDC dc);
static int TestEndDoc(HDC dc);
static int TestAbortDoc(HDC dc);
static BOOL TestTextOut(HDC dc, int x, int y, LPCWSTR text, int n);
static BOOL TestDeleteDC(HDC dc);
static void *TestAlloc(size_t n);
static int TestSetAbortProc(HDC dc, ABORTPROC proc);
static int TestDrawText(HDC dc, LPCWSTR text, int n, RECT *rect, UINT format);

#define GetDeviceCaps TestCaps
#define StartDocW TestStartDoc
#define StartPage TestStartPage
#define EndPage TestEndPage
#define EndDoc TestEndDoc
#define AbortDoc TestAbortDoc
#define TextOutW TestTextOut
#define DeleteDC TestDeleteDC
#define mem_alloc TestAlloc
#define SetAbortProc TestSetAbortProc
#define DrawTextW TestDrawText
#include "../../src/print.c"
#undef GetDeviceCaps
#undef StartDocW
#undef StartPage
#undef EndPage
#undef EndDoc
#undef AbortDoc
#undef TextOutW
#undef DeleteDC
#undef mem_alloc
#undef SetAbortProc
#undef DrawTextW

Prefs g_pf;
HWND g_edit;
static HWND g_find;
static int g_pass, g_fail, g_started, g_ended, g_aborted, g_pages, g_deleted, g_asked;
static int g_cancel, g_noDC, g_noText, g_failText;
static int g_failAlloc, g_allocCalls, g_copyCalls, g_locked, g_spoolLocked;
static int g_holdCalls, g_releaseCalls, g_dialogCalls, g_nativeFindEnabled, g_retryCount;
static DWORD g_retryError, g_error, g_nativeError;
static size_t g_allocLimit, g_allocMax;
static const WCHAR *g_dialogText, *g_spoolText;
static DWORD g_requested, g_response;
static WCHAR g_printed[1024], g_drawn[512];
static UINT g_drawnAlign[8];
static int g_drawnCount, g_cancelOnPage;
static ABORTPROC g_abortProc;

HINSTANCE g_hinst;
HFONT g_fontUI;
void RegClass(const WCHAR *name, WNDPROC proc, UINT style, HBRUSH bg) { (void)name; (void)proc; (void)style; (void)bg; }
void DlgBaseInit(DlgBase *base, HWND owner) { memset(base, 0, sizeof *base); base->owner = owner; }
HWND DlgOpen(DlgBase *base, const WCHAR *className, const WCHAR *title, int width, int height, int modeless) { (void)base; (void)className; (void)title; (void)width; (void)height; (void)modeless; return NULL; }
DlgBase *DlgFromHwnd(HWND hwnd, UINT msg, LPARAM lParam) { (void)hwnd; (void)msg; (void)lParam; return NULL; }
BOOL DlgCommon(DlgBase *base, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *ret) { (void)base; (void)msg; (void)wParam; (void)lParam; *ret = 0; return FALSE; }
HWND UiLabel(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD extra) { (void)parent; (void)text; (void)x; (void)y; (void)width; (void)height; (void)id; (void)extra; return NULL; }
HWND UiButton(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD style) { (void)parent; (void)text; (void)x; (void)y; (void)width; (void)height; (void)id; (void)style; return NULL; }

const WCHAR *AppDocName(void) { return L"print-test"; }
void AppSavePrefs(void) {}

/* The shared dialog helper's contract, exercised with a real hidden window. The
 * intercepted native calls below verify print.c holds it throughout retries. */
HWND DialogHoldFind(void)
{
    g_holdCalls++;
    if (!g_find || !IsWindow(g_find) || !IsWindowEnabled(g_find)) return NULL;
    EnableWindow(g_find, FALSE);
    return g_find;
}

void DialogReleaseFind(HWND held)
{
    g_releaseCalls++;
    if (held && IsWindow(held)) EnableWindow(held, TRUE);
}

int MpAsk(HWND owner, const WCHAR *title, const WCHAR *msg, const WCHAR *b1, const WCHAR *b2, const WCHAR *b3, int esc)
{
    (void)owner; (void)title; (void)msg; (void)b1; (void)b2; (void)b3; (void)esc;
    g_asked++;
    return 1;
}

void MpNote(HWND owner, const WCHAR *title, const WCHAR *msg)
{
    MpAsk(owner, title, msg, L"ok", NULL, NULL, 1);
}

WCHAR *EditGetDocText(int *len)
{
    int n = GetWindowTextLengthW(g_edit);
    WCHAR *text;
    g_copyCalls++;
    if (g_noText) return NULL;
    text = TestAlloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!text) return NULL;
    if (GetWindowTextW(g_edit, text, n + 1) != n) { mem_free(text); return NULL; }
    *len = n;
    return text;
}

const WCHAR *EditLockText(void **h, int *n)
{
    const WCHAR *text;
    *h = NULL;
    *n = GetWindowTextLengthW(g_edit);
    if (g_noText) return NULL;
    *h = (void *)SendMessageW(g_edit, EM_GETHANDLE, 0, 0);
    text = *h ? (const WCHAR *)LocalLock((HLOCAL)*h) : NULL;
    if (text) g_locked++;
    return text;
}

void EditUnlockText(void *h)
{
    if (h) { LocalUnlock((HLOCAL)h); if (g_locked) g_locked--; }
}

static void *TestAlloc(size_t n)
{
    g_allocCalls++;
    if (n > g_allocMax) g_allocMax = n;
    if (g_failAlloc || (g_allocLimit && n > g_allocLimit)) return NULL;
    return mem_alloc(n);
}

static int TestCaps(HDC dc, int cap)
{
    (void)dc;
    switch (cap) {
    case LOGPIXELSX: case LOGPIXELSY: return 96;
    case HORZRES: case PHYSICALWIDTH: return 816;
    case VERTRES: case PHYSICALHEIGHT: return 1056;
    default: return 0;
    }
}
static int TestStartDoc(HDC dc, const DOCINFOW *info)
{
    (void)dc; (void)info;
    if (g_locked) g_spoolLocked = 1;
    if (g_spoolText && !g_locked) SetWindowTextW(g_edit, g_spoolText);
    g_started++;
    return 1;
}
static int TestStartPage(HDC dc) { (void)dc; return 1; }
static int TestEndPage(HDC dc) { (void)dc; g_pages++; if (g_cancelOnPage && g_pages >= g_cancelOnPage) g_printAbort = 1; return 1; }
static int TestSetAbortProc(HDC dc, ABORTPROC proc) { (void)dc; g_abortProc = proc; return 1; }
static int TestDrawText(HDC dc, LPCWSTR text, int n, RECT *rect, UINT format)
{
    int used = wlen(g_drawn), length = n < 0 ? wlen(text) : n;
    (void)dc; (void)rect;
    if (g_drawnCount < COUNTOF(g_drawnAlign)) g_drawnAlign[g_drawnCount++] = format & (DT_CENTER | DT_RIGHT);
    if (used) g_drawn[used++] = '|';
    if (length > COUNTOF(g_drawn) - used - 1) return 0;
    memcpy(g_drawn + used, text, (size_t)length * sizeof(WCHAR));
    g_drawn[used + length] = 0;
    return 1;
}
static int TestEndDoc(HDC dc) { (void)dc; g_ended++; return 1; }
static int TestAbortDoc(HDC dc) { (void)dc; g_aborted++; return 1; }
static BOOL TestDeleteDC(HDC dc) { g_deleted++; return DeleteDC(dc); }
static BOOL TestTextOut(HDC dc, int x, int y, LPCWSTR text, int n)
{
    int used = wlen(g_printed);
    (void)dc; (void)x; (void)y;
    if (g_failText) { SetLastError(29); return FALSE; }
    if (used) g_printed[used++] = '|';
    if (n > COUNTOF(g_printed) - used - 1) return FALSE;
    memcpy(g_printed + used, text, (size_t)n * sizeof(WCHAR));
    g_printed[used + n] = 0;
    return TRUE;
}

static BOOL NativeDialogResult(void)
{
    g_dialogCalls++;
    if (IsWindowEnabled(g_find)) g_nativeFindEnabled++;
    g_nativeError = g_dialogCalls <= g_retryCount ? g_retryError : g_error;
    return !g_cancel && g_dialogCalls > g_retryCount;
}

static BOOL WINAPI TestDialog(PRINTDLGW *pd)
{
    g_requested = pd->Flags;
    if (!NativeDialogResult()) return FALSE;
    pd->Flags |= g_response;
    pd->nFromPage = 10; pd->nToPage = 10;
    pd->hDC = g_noDC ? NULL : CreateCompatibleDC(NULL);
    if (g_dialogText) SetWindowTextW(g_edit, g_dialogText);
    return TRUE;
}
static BOOL WINAPI TestPageDialog(PAGESETUPDLGW *ps)
{
    if (!NativeDialogResult()) return FALSE;
    ps->rtMargin.left = 100; ps->rtMargin.top = 200;
    ps->rtMargin.right = 300; ps->rtMargin.bottom = 400;
    return TRUE;
}
static DWORD WINAPI TestDialogError(void) { return g_nativeError; }

static void Check(const WCHAR *name, int ok)
{
    WCHAR line[160];
    char bytes[500];
    DWORD wrote;
    int n;
    wcopy(line, ok ? L"PASS " : L"FAIL ", COUNTOF(line));
    wcat(line, name, COUNTOF(line)); wcat(line, L"\r\n", COUNTOF(line));
    n = WideCharToMultiByte(CP_UTF8, 0, line, -1, bytes, sizeof bytes, NULL, NULL);
    if (n > 1) WriteFile(GetStdHandle((DWORD)-11), bytes, (DWORD)(n - 1), &wrote, NULL);
    if (ok) g_pass++; else g_fail++;
}

static void Reset(const WCHAR *text, int start, int end, DWORD response)
{
    SetWindowTextW(g_edit, text);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)start, (LPARAM)end);
    g_started = g_ended = g_aborted = g_pages = g_deleted = g_asked = 0;
    g_cancel = g_noDC = g_noText = g_failText = 0;
    g_failAlloc = g_allocCalls = g_copyCalls = g_locked = g_spoolLocked = 0;
    g_holdCalls = g_releaseCalls = g_dialogCalls = g_nativeFindEnabled = g_retryCount = 0;
    g_retryError = g_error = g_nativeError = 0;
    g_allocLimit = g_allocMax = 0;
    g_dialogText = g_spoolText = NULL;
    g_requested = 0; g_response = response; g_printed[0] = 0;
    g_drawn[0] = 0; g_drawnCount = 0; g_cancelOnPage = 0; g_abortProc = NULL;
    EnableWindow(g_find, TRUE);
}

void start(void)
{
    WCHAR *large;
    int i;
    g_pf.pt = 12; g_pf.tab = 4;
    wcopy(g_pf.font, L"consolas", COUNTOF(g_pf.font));
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_POPUP | ES_MULTILINE | ES_AUTOHSCROLL, 0, 0, 400, 300,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_edit) { Check(L"create hidden native edit", 0); ExitProcess(1); }
    g_find = CreateWindowExW(0, L"EDIT", L"", WS_POPUP, 0, 0, 100, 20,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_find) { Check(L"create hidden find-dialog stand-in", 0); DestroyWindow(g_edit); ExitProcess(1); }
    g_cd = (HMODULE)1; pPrintDlg = TestDialog; pPageDlg = TestPageDialog; pCdErr = TestDialogError;

    Reset(L"before selected after", 7, 15, PD_SELECTION);
    PrintDoc(NULL, 0, NULL);
    Check(L"selection is offered for selected text", !(g_requested & PD_NOSELECTION));
    Check(L"only selected text reaches pagination", wcmp(g_printed, L"selected") == 0);
    Check(L"selected job completes and releases its dc", g_started == 1 && g_ended == 1 && !g_aborted && g_pages == 1 && g_deleted == 1 && !g_asked);
    Check(L"selection snapshot allocates only its text and terminator", !g_copyCalls && g_allocCalls == 1 && g_allocMax == 18);
    Check(L"editor text is unlocked before any spooler callback", !g_locked && !g_spoolLocked);
    Check(L"print dialog holds find and restores it after success", !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_holdCalls == 1 && g_releaseCalls == 1);

    large = mem_alloc(1000001u * sizeof(WCHAR));
    if (!large) { Check(L"allocate large printing fixture", 0); ExitProcess(1); }
    for (i = 0; i < 1000000; i++) large[i] = 'x';
    memcpy(large + 500000, L"selected", 8 * sizeof(WCHAR));
    large[1000000] = 0;
    Reset(large, 500000, 500008, PD_SELECTION);
    mem_free(large);
    g_allocLimit = 64;                                      /* snapshot budget only: pagination keeps its ordinary small row buffer */
    PrintDoc(NULL, 0, NULL);
    Check(L"tiny selection in a million-character document fits a bounded snapshot", wcmp(g_printed, L"selected") == 0 && g_allocMax == 18 && !g_copyCalls);
    Check(L"bounded selection completes with no locked editor or leaked printer dc", g_started == 1 && g_ended == 1 && g_deleted == 1 && !g_locked && !g_spoolLocked && !g_asked);

    Reset(L"before selected after", 7, 15, PD_SELECTION); g_spoolText = L"changed by printer callback";
    PrintDoc(NULL, 0, NULL);
    Check(L"selected snapshot survives document changes in a printer callback", wcmp(g_printed, L"selected") == 0 && !g_spoolLocked && !g_locked && GetWindowTextLengthW(g_edit) == 27);

    Reset(L"before selected after", 2, 15, PD_SELECTION); g_dialogText = L"short";
    PrintDoc(NULL, 0, NULL);
    Check(L"clamped selection allocates only its remaining text", wcmp(g_printed, L"ort") == 0 && g_allocMax == 8 && !g_asked);

    Reset(L"before selected after", 7, 15, PD_SELECTION); g_dialogText = L"short";
    PrintDoc(NULL, 0, NULL);
    Check(L"empty clamped selection uses a two-byte snapshot and prints one page", !g_printed[0] && g_allocMax == 2 && g_started == 1 && g_ended == 1 && g_pages == 1 && !g_asked);

    Reset(L"before selected after", 7, 15, PD_SELECTION); g_failAlloc = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"selection snapshot allocation failure unlocks text and releases dc", !g_started && !g_locked && g_deleted == 1 && g_asked == 1);

    Reset(L"before selected after", 7, 15, PD_SELECTION); g_noText = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"selection text access failure starts no job and releases dc", !g_started && !g_locked && !g_allocCalls && g_deleted == 1 && g_asked == 1);

    Reset(L"before selected after", 7, 15, 0);
    PrintDoc(NULL, 0, NULL);
    Check(L"all pages prints entire document despite selection", wcmp(g_printed, L"before selected after") == 0);

    Reset(L"before selected after", 7, 7, 0);
    PrintDoc(NULL, 0, NULL);
    Check(L"empty selection disables selection option", (g_requested & PD_NOSELECTION) != 0);

    Reset(L"before selected after", 7, 15, 0);
    PrintDoc(NULL, 1, NULL);
    Check(L"quiet uses default printer and disables selection", (g_requested & (PD_RETURNDEFAULT | PD_NOSELECTION)) == (PD_RETURNDEFAULT | PD_NOSELECTION));
    Check(L"quiet prints the complete document", wcmp(g_printed, L"before selected after") == 0);
    Check(L"quiet default-printer lookup holds find and restores it", !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_dialogCalls == 1 && g_releaseCalls == 1);

    Reset(L"omit\r\nkeep\r\nthis\r\nomit", 6, 16, PD_SELECTION);
    PrintDoc(NULL, 0, NULL);
    Check(L"multiline selection preserves row boundaries", wcmp(g_printed, L"keep|this") == 0);

    Reset(L"x\xD83D\xDE00 y", 1, 3, PD_SELECTION);
    PrintDoc(NULL, 0, NULL);
    Check(L"selection preserves a surrogate pair", wcmp(g_printed, L"\xD83D\xDE00") == 0);

    Reset(L"", 0, 0, 0);
    PrintDoc(NULL, 0, NULL);
    Check(L"empty document completes one blank page", g_started == 1 && g_ended == 1 && g_pages == 1 && !g_aborted && !g_printed[0] && g_deleted == 1 && !g_asked);

    Reset(L"cancel", 0, 0, 0); g_cancel = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"dialog cancellation is silent and starts no job", !g_started && !g_deleted && !g_asked);
    Check(L"print cancellation restores find", !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_holdCalls == 1 && g_releaseCalls == 1);

    Reset(L"dialog error", 0, 0, 0); g_cancel = 1; g_error = 5;
    PrintDoc(NULL, 0, NULL);
    Check(L"print dialog errors restore find and report once", !g_nativeFindEnabled && IsWindowEnabled(g_find) && !g_started && g_asked == 1 && g_releaseCalls == 1);

    Reset(L"printer changed", 0, 0, 0); g_retryCount = 1; g_retryError = PDERR_DEFAULTDIFFERENT;
    PrintDoc(NULL, 0, NULL);
    Check(L"printer retry keeps find held until successful return", g_dialogCalls == 2 && !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_started == 1 && !g_asked && g_holdCalls == 1 && g_releaseCalls == 1);

    Reset(L"printer unavailable", 0, 0, 0); g_retryCount = 2; g_retryError = PDERR_PRINTERNOTFOUND;
    PrintDoc(NULL, 0, NULL);
    Check(L"exhausted printer retry restores find and reports once", g_dialogCalls == 2 && !g_nativeFindEnabled && IsWindowEnabled(g_find) && !g_started && g_asked == 1 && g_releaseCalls == 1);

    Reset(L"already held", 0, 0, 0); EnableWindow(g_find, FALSE);
    PrintDoc(NULL, 0, NULL);
    Check(L"printing preserves an already disabled find window", !g_nativeFindEnabled && !IsWindowEnabled(g_find) && g_started == 1 && g_releaseCalls == 1);

    Reset(L"page setup", 0, 0, 0);
    PageSetup(NULL);
    Check(L"page setup holds find and restores it after success", g_dialogCalls == 1 && !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_releaseCalls == 1 && !g_asked);
    Check(L"successful page setup adopts the chosen margins", g_pf.marginL == 100 && g_pf.marginT == 200 && g_pf.marginR == 300 && g_pf.marginB == 400);

    Reset(L"cancel page setup", 0, 0, 0); g_cancel = 1;
    PageSetup(NULL);
    Check(L"page setup cancellation restores find without changing margins", !g_nativeFindEnabled && IsWindowEnabled(g_find) && !g_asked && g_releaseCalls == 1 && g_pf.marginL == 100);

    Reset(L"page setup error", 0, 0, 0); g_cancel = 1; g_error = 5;
    PageSetup(NULL);
    Check(L"page setup errors restore find and report once", !g_nativeFindEnabled && IsWindowEnabled(g_find) && g_asked == 1 && g_releaseCalls == 1);

    Reset(L"page setup retry", 0, 0, 0); g_retryCount = 1; g_retryError = PDERR_DNDMMISMATCH;
    PageSetup(NULL);
    Check(L"page setup retry holds find across both native calls", g_dialogCalls == 2 && !g_nativeFindEnabled && IsWindowEnabled(g_find) && !g_asked && g_holdCalls == 1 && g_releaseCalls == 1);

    Reset(L"page setup already held", 0, 0, 0); EnableWindow(g_find, FALSE);
    PageSetup(NULL);
    Check(L"page setup preserves an already disabled find window", !g_nativeFindEnabled && !IsWindowEnabled(g_find) && g_releaseCalls == 1 && !g_asked);

    Reset(L"out of memory", 0, 0, 0); g_noText = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"text allocation failure releases dc and reports error", !g_started && g_deleted == 1 && g_asked == 1);

    Reset(L"no dc", 0, 0, 0); g_noDC = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"missing printer dc reports error without starting", !g_started && !g_deleted && g_asked == 1);

    Reset(L"failed output", 0, 0, 0); g_failText = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"TextOut failure aborts and reports error", g_started == 1 && g_aborted == 1 && !g_ended && !g_pages && g_deleted == 1 && g_asked == 1);

    Reset(L"one page", 0, 0, PD_PAGENUMS);
    PrintDoc(NULL, 0, NULL);
    Check(L"range beyond document aborts silently", g_started == 1 && g_aborted == 1 && !g_ended && !g_pages && !g_printed[0] && g_deleted == 1 && !g_asked);

    Reset(L"header", 0, 0, 0);
    wcopy(g_pf.header, L"&lleft &&&c&f&rpage &p", PAGE_TEXT_CAP);
    wcopy(g_pf.footer, L"", PAGE_TEXT_CAP);
    PrintDoc(NULL, 0, NULL);
    Check(L"header codes: &l &c &r split the line, &f is the name, &p the page, && an ampersand", wcmp(g_drawn, L"left &|print-test|page 1") == 0);
    Check(L"... each part drawn with its own alignment", g_drawnCount == 3 && g_drawnAlign[0] == 0 && g_drawnAlign[1] == DT_CENTER && g_drawnAlign[2] == DT_RIGHT);
    Check(L"the job installs an abort procedure (cancel from the printing window)", g_abortProc != NULL);

    Reset(L"footer", 0, 0, 0);
    wcopy(g_pf.header, L"", PAGE_TEXT_CAP);
    wcopy(g_pf.footer, L"&x &P", PAGE_TEXT_CAP);
    PrintDoc(NULL, 0, NULL);
    Check(L"an unknown code prints as typed, codes ignore case, an empty header draws nothing", wcmp(g_drawn, L"&x 1") == 0 && g_drawnCount == 1 && g_drawnAlign[0] == DT_CENTER);
    wcopy(g_pf.footer, L"", PAGE_TEXT_CAP);

    Reset(L"", 0, 0, 0);
    large = (WCHAR *)mem_alloc(4000 * sizeof(WCHAR));
    for (i = 0; large && i < 3998; i += 2) { large[i] = 'x'; large[i + 1] = '\n'; }
    if (large) { large[3998] = 0; SetWindowTextW(g_edit, large); mem_free(large); }
    g_cancelOnPage = 1;
    PrintDoc(NULL, 0, NULL);
    Check(L"cancel in the printing window stops the job after the page, aborts it and says nothing", g_started == 1 && g_aborted == 1 && !g_ended && g_pages == 1 && g_deleted == 1 && !g_asked);

    DestroyWindow(g_edit);
    DestroyWindow(g_find);
    Check(L"printing regression suite", g_pass == 42 && g_fail == 0);
    ExitProcess(g_fail ? 1 : 0);
}
