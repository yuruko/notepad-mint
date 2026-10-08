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

#define GetDeviceCaps TestCaps
#define StartDocW TestStartDoc
#define StartPage TestStartPage
#define EndPage TestEndPage
#define EndDoc TestEndDoc
#define AbortDoc TestAbortDoc
#define TextOutW TestTextOut
#define DeleteDC TestDeleteDC
#include "../../src/print.c"
#undef GetDeviceCaps
#undef StartDocW
#undef StartPage
#undef EndPage
#undef EndDoc
#undef AbortDoc
#undef TextOutW
#undef DeleteDC

Prefs g_pf;
HWND g_edit;
static int g_pass, g_fail, g_started, g_ended, g_aborted, g_pages, g_deleted, g_asked;
static int g_cancel, g_noDC, g_noText, g_failText;
static DWORD g_requested, g_response;
static WCHAR g_printed[1024];

const WCHAR *AppDocName(void) { return L"print-test"; }
void AppSavePrefs(void) {}
int MpAsk(HWND owner, const WCHAR *title, const WCHAR *msg, const WCHAR *b1, const WCHAR *b2, const WCHAR *b3, int esc)
{
    (void)owner; (void)title; (void)msg; (void)b1; (void)b2; (void)b3; (void)esc;
    g_asked++;
    return 1;
}

WCHAR *EditGetDocText(int *len)
{
    int n = GetWindowTextLengthW(g_edit);
    WCHAR *text;
    if (g_noText) return NULL;
    text = mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!text) return NULL;
    if (GetWindowTextW(g_edit, text, n + 1) != n) { mem_free(text); return NULL; }
    *len = n;
    return text;
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
static int TestStartDoc(HDC dc, const DOCINFOW *info) { (void)dc; (void)info; g_started++; return 1; }
static int TestStartPage(HDC dc) { (void)dc; return 1; }
static int TestEndPage(HDC dc) { (void)dc; g_pages++; return 1; }
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

static BOOL WINAPI TestDialog(PRINTDLGW *pd)
{
    g_requested = pd->Flags;
    if (g_cancel) return FALSE;
    pd->Flags |= g_response;
    pd->nFromPage = 10; pd->nToPage = 10;
    pd->hDC = g_noDC ? NULL : CreateCompatibleDC(NULL);
    return TRUE;
}
static BOOL WINAPI TestPageDialog(PAGESETUPDLGW *ps) { (void)ps; return FALSE; }
static DWORD WINAPI TestDialogError(void) { return 0; }

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
    g_requested = 0; g_response = response; g_printed[0] = 0;
}

void start(void)
{
    g_pf.pt = 12; g_pf.tab = 4;
    wcopy(g_pf.font, L"consolas", COUNTOF(g_pf.font));
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_POPUP | ES_MULTILINE, 0, 0, 400, 300,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_edit) { Check(L"create hidden native edit", 0); ExitProcess(1); }
    g_cd = (HMODULE)1; pPrintDlg = TestDialog; pPageDlg = TestPageDialog; pCdErr = TestDialogError;

    Reset(L"before selected after", 7, 15, PD_SELECTION);
    PrintDoc(NULL, 0);
    Check(L"selection is offered for selected text", !(g_requested & PD_NOSELECTION));
    Check(L"only selected text reaches pagination", wcmp(g_printed, L"selected") == 0);
    Check(L"selected job completes and releases its dc", g_started == 1 && g_ended == 1 && !g_aborted && g_pages == 1 && g_deleted == 1 && !g_asked);

    Reset(L"before selected after", 7, 15, 0);
    PrintDoc(NULL, 0);
    Check(L"all pages prints entire document despite selection", wcmp(g_printed, L"before selected after") == 0);

    Reset(L"before selected after", 7, 7, 0);
    PrintDoc(NULL, 0);
    Check(L"empty selection disables selection option", (g_requested & PD_NOSELECTION) != 0);

    Reset(L"before selected after", 7, 15, 0);
    PrintDoc(NULL, 1);
    Check(L"quiet uses default printer and disables selection", (g_requested & (PD_RETURNDEFAULT | PD_NOSELECTION)) == (PD_RETURNDEFAULT | PD_NOSELECTION));
    Check(L"quiet prints the complete document", wcmp(g_printed, L"before selected after") == 0);

    Reset(L"omit\r\nkeep\r\nthis\r\nomit", 6, 16, PD_SELECTION);
    PrintDoc(NULL, 0);
    Check(L"multiline selection preserves row boundaries", wcmp(g_printed, L"keep|this") == 0);

    Reset(L"x\xD83D\xDE00 y", 1, 3, PD_SELECTION);
    PrintDoc(NULL, 0);
    Check(L"selection preserves a surrogate pair", wcmp(g_printed, L"\xD83D\xDE00") == 0);

    Reset(L"", 0, 0, 0);
    PrintDoc(NULL, 0);
    Check(L"empty document completes one blank page", g_started == 1 && g_ended == 1 && g_pages == 1 && !g_aborted && !g_printed[0] && g_deleted == 1 && !g_asked);

    Reset(L"cancel", 0, 0, 0); g_cancel = 1;
    PrintDoc(NULL, 0);
    Check(L"dialog cancellation is silent and starts no job", !g_started && !g_deleted && !g_asked);

    Reset(L"out of memory", 0, 0, 0); g_noText = 1;
    PrintDoc(NULL, 0);
    Check(L"text allocation failure releases dc and reports error", !g_started && g_deleted == 1 && g_asked == 1);

    Reset(L"no dc", 0, 0, 0); g_noDC = 1;
    PrintDoc(NULL, 0);
    Check(L"missing printer dc reports error without starting", !g_started && !g_deleted && g_asked == 1);

    Reset(L"failed output", 0, 0, 0); g_failText = 1;
    PrintDoc(NULL, 0);
    Check(L"TextOut failure aborts and reports error", g_started == 1 && g_aborted == 1 && !g_ended && !g_pages && g_deleted == 1 && g_asked == 1);

    Reset(L"one page", 0, 0, PD_PAGENUMS);
    PrintDoc(NULL, 0);
    Check(L"range beyond document aborts silently", g_started == 1 && g_aborted == 1 && !g_ended && !g_pages && !g_printed[0] && g_deleted == 1 && !g_asked);

    DestroyWindow(g_edit);
    Check(L"printing regression suite", g_pass == 15 && g_fail == 0);
    ExitProcess(g_fail ? 1 : 0);
}
