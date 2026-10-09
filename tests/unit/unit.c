/* unit.c - unit tests for the pure parts of notepad mint: doc.c (encodings, line endings), search.c, the util.c helpers
 * and the rt.asm runtime. no crt, like the app: tests\unit\build.bat builds it into build\tests\unit.exe (console).
 * every check prints "PASS <name>" or "FAIL <name>: <why>" (expected vs actual); the exit code is 1 when anything failed */
#include "mp.h"

/* kernel32 bits the app itself doesn't need (exact 32-bit sdk signatures) */
#ifndef STD_OUTPUT_HANDLE
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2
#endif
API HANDLE WINAPI GetStdHandle(DWORD);
API DWORD  WINAPI GetTempPathW(DWORD, LPWSTR);
API BOOL   WINAPI RemoveDirectoryW(LPCWSTR);
API BOOL   WINAPI IsValidCodePage(UINT);
API UINT   WINAPI GetACP(void);
API DWORD  WINAPI GetCurrentProcessId(void);
API HANDLE WINAPI FindFirstFileW(LPCWSTR, WIN32_FIND_DATAW *);
API BOOL   WINAPI FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
API BOOL   WINAPI FindClose(HANDLE);
API BOOL   WINAPI GetFileTime(HANDLE, FILETIME *, FILETIME *, FILETIME *);
API BOOL   WINAPI SetFileTime(HANDLE, const FILETIME *, const FILETIME *, const FILETIME *);

#ifdef DOC_IO_TEST
void DocTestFault(int fault);
#endif

#define LIT(s)  s, (int)(sizeof(s) - 1)                         /* a byte string literal + its length (embedded nuls ok) */
#define WLIT(s) s, (int)(sizeof(s) / sizeof(WCHAR) - 1)         /* the same for an L"" literal or a WCHAR array */

static HANDLE g_out;
static int    g_pass, g_fail, g_skip;
static WCHAR  g_why[1024];                                      /* first mismatch of the running check ("" = fine so far) */
static WCHAR  g_dir[PATH_CAP], g_file[PATH_CAP], g_missing[PATH_CAP];
static const WCHAR g_emoji[] = { 0xD83D, 0xDE00, 0 };          /* U+1F600: a surrogate pair, in no 8-bit code page */
static const WCHAR *const g_eolShort[EOL_COUNT] = { L"crlf", L"lf", L"cr" };

/* ------------------------------------------------------------- output -- */
static void Out(const WCHAR *s)
{
    char b[2400];
    DWORD wr;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, b, (int)sizeof b, NULL, NULL);
    if (n > 1) WriteFile(g_out, b, (DWORD)(n - 1), &wr, NULL);
}

static void Line(const WCHAR *a, const WCHAR *b, const WCHAR *c)
{
    WCHAR t[1100];
    wcopy(t, a, COUNTOF(t));
    wcat(t, b, COUNTOF(t));
    if (c) { wcat(t, L": ", COUNTOF(t)); wcat(t, c, COUNTOF(t)); }
    wcat(t, L"\r\n", COUNTOF(t));
    Out(t);
}

static void Result(const WCHAR *name, const WCHAR *why)        /* why NULL = passed */
{
    Line(why ? L"FAIL " : L"PASS ", name, why);
    if (why) g_fail++; else g_pass++;
}

static void Skip(const WCHAR *name, const WCHAR *why) { Line(L"SKIP ", name, why); g_skip++; }
static void Group(const WCHAR *name) { Line(L"\r\n== ", name, NULL); }

/* s[0..n) quoted, ascii only: \r \n \t \\ \" and \uXXXX for the rest, cut after 48 chars */
static void Esc(WCHAR *o, int cap, const WCHAR *s, int n)
{
    WCHAR t[8];
    int i;
    if (!s) { wcopy(o, L"(null)", cap); return; }
    wcopy(o, L"\"", cap);
    for (i = 0; i < n; i++) {
        WCHAR c = s[i];
        if (i == 48) { wcat(o, L"...", cap); break; }
        if (c == '\r') wcopy(t, L"\\r", 8);
        else if (c == '\n') wcopy(t, L"\\n", 8);
        else if (c == '\t') wcopy(t, L"\\t", 8);
        else if (c == '\\' || c == '"') { t[0] = '\\'; t[1] = c; t[2] = 0; }
        else if (c < 32 || c > 126) wsprintfW(t, L"\\u%04x", (int)c);
        else { t[0] = c; t[1] = 0; }
        wcat(o, t, cap);
    }
    wcat(o, L"\"", cap);
}

static void Hex(WCHAR *o, int cap, const void *p, int n)
{
    const BYTE *b = (const BYTE *)p;
    WCHAR t[8];
    int i;
    if (!b) { wcopy(o, L"(null)", cap); return; }
    wcopy(o, n ? L"" : L"(no bytes)", cap);
    for (i = 0; i < n && i < 40; i++) { wsprintfW(t, i ? L" %02x" : L"%02x", (int)b[i]); wcat(o, t, cap); }
    if (n > 40) wcat(o, L" ...", cap);
}

/* multi-part checks: every Want* records the first mismatch, Done prints one line for the whole check */
static void Want(BOOL ok, const WCHAR *fmt, int a, int b)
{
    if (!ok && !g_why[0]) wsprintfW(g_why, fmt, a, b);
}

static void WantStr(const WCHAR *what, const WCHAR *got, int gn, const WCHAR *want, int wn)
{
    WCHAR a[400], b[400];
    if (g_why[0] || (got && gn == wn && !memcmp(got, want, (size_t)wn * sizeof(WCHAR)))) return;
    Esc(a, COUNTOF(a), want, wn);
    Esc(b, COUNTOF(b), got, gn);
    wsprintfW(g_why, L"%s: expected %s (%d chars), got %s (%d chars)", what, a, wn, b, gn);
}

static void WantBytes(const WCHAR *what, const void *got, int gn, const void *want, int wn)
{
    WCHAR a[200], b[200];
    if (g_why[0] || (got && gn == wn && !memcmp(got, want, (size_t)wn))) return;
    Hex(a, COUNTOF(a), want, wn);
    Hex(b, COUNTOF(b), got, gn);
    wsprintfW(g_why, L"%s: expected [%s] (%d bytes), got [%s] (%d bytes)", what, a, wn, b, gn);
}

static void WantEnc(int got, int want)
{
    WCHAR a[48], b[48];
    if (g_why[0] || got == want) return;
    EncLabel(want, a, 48);
    if (got < 0) wcopy(b, L"nothing", 48); else EncLabel(got, b, 48);
    wsprintfW(g_why, L"encoding: expected %s, got %s", a, b);
}

static void WantEol(int got, int want)
{
    if (g_why[0] || got == want) return;
    wsprintfW(g_why, L"line ending: expected %s, got %s", g_eolShort[want], got >= 0 && got < EOL_COUNT ? g_eolShort[got] : L"nothing");
}

static void Done(const WCHAR *name)
{
    Result(name, g_why[0] ? g_why : NULL);
    g_why[0] = 0;
}

static void Int(const WCHAR *name, int got, int want)
{
    Want(got == want, L"expected %d, got %d", want, got);
    Done(name);
}

static void Str(const WCHAR *name, const WCHAR *got, const WCHAR *want)
{
    WantStr(L"text", got, got ? wlen(got) : 0, want, wlen(want));
    Done(name);
}

/* -------------------------------------------------------------- files -- */
static BOOL FilePut(const WCHAR *path, const void *p, int n)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0;
    BOOL ok;
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ok = n == 0 || (WriteFile(f, p, (DWORD)n, &wr, NULL) && wr == (DWORD)n);
    CloseHandle(f);
    return ok;
}

/* the whole file (mem_alloc'd, *n bytes) or NULL */
static BYTE *FileGet(const WCHAR *path, int *n)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    __int64 sz = 0;
    BYTE *b = NULL;
    DWORD rd = 0;
    *n = 0;
    if (f == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(f, &sz) && sz < 0x10000000 && (b = (BYTE *)mem_alloc((size_t)sz + 1)) != NULL) {
        if (!ReadFile(f, b, (DWORD)sz, &rd, NULL)) rd = 0;
        *n = (int)rd;
    }
    CloseHandle(f);
    return b;
}

static BOOL TempDir(void)
{
    WCHAR base[PATH_CAP];
    DWORD n = GetTempPathW(PATH_CAP - 40, base);
    if (!n || n >= PATH_CAP - 40) return FALSE;
    wsprintfW(g_dir, L"%snm-unit-%u", base, (UINT)GetCurrentProcessId());
    CreateDirectoryW(g_dir, NULL);
    if (!IsDir(g_dir)) return FALSE;
    wcopy(g_file, g_dir, PATH_CAP);
    PathJoin(g_file, L"t.txt", PATH_CAP);
    wcopy(g_missing, g_dir, PATH_CAP);
    PathJoin(g_missing, L"missing.txt", PATH_CAP);
    return TRUE;
}

/* ------------------------------------------------------- util strings -- */
static void TestStrings(void)
{
    WCHAR b[16];
    Group(L"util.c strings");
    Int(L"wlen", wlen(L"abc"), 3);
    Int(L"wlen empty", wlen(L""), 0);
    memset(b, 0x55, sizeof b);
    wcopy(b, L"abcdef", 4);
    WantStr(L"text", b, wlen(b), WLIT(L"abc"));
    Want(b[4] == 0x5555, L"wrote past cap", 0, 0);
    Done(L"wcopy truncates to cap - 1 and terminates");
    wcopy(b, L"abc", 16);
    b[6] = 0x5555;
    wcat(b, L"xyz", 6);
    WantStr(L"text", b, wlen(b), WLIT(L"abcxy"));
    Want(b[6] == 0x5555, L"wrote past cap", 0, 0);
    Done(L"wcat truncates to cap");
    Want(wcmp(L"abc", L"abc") == 0 && wcmp(L"abc", L"abd") < 0 && wcmp(L"b", L"abc") > 0 && wcmp(L"ab", L"abc") < 0, L"wrong order", 0, 0);
    Done(L"wcmp");
    Want(wcmpi(L"ABC", L"abc") == 0 && wcmpi(L"a", L"B") < 0 && wcmpi(L"Zeta", L"alpha") > 0, L"wrong order", 0, 0);
    Want(wcmpi(L"\u00c4RGER", L"\u00e4rger") == 0, L"non-ascii case folding", 0, 0);
    Done(L"wcmpi");
    Want(wlow('A') == 'a' && wlow('z') == 'z' && wlow('1') == '1' && wlow('[') == '[', L"ascii", 0, 0);
    Want(wlow(0x00C4) == 0x00E4 && wlow(0x0414) == 0x0434 && wlow(0x0394) == 0x03B4, L"non-ascii: got %04x for U+00C4", wlow(0x00C4), 0);
    Done(L"wlow");
    Int(L"wtoi 42", wtoi(L"42"), 42);
    Int(L"wtoi leading blanks", wtoi(L" \t 17"), 17);
    Int(L"wtoi negative", wtoi(L"-7"), -7);
    Int(L"wtoi plus", wtoi(L"+3"), 3);
    Int(L"wtoi stops at junk", wtoi(L"12ab"), 12);
    Int(L"wtoi empty", wtoi(L""), 0);
    Int(L"wtoi no digits", wtoi(L"abc"), 0);
    Int(L"wtoi huge input doesn't overflow", wtoi(L"99999999999999"), 999999999);
}

/* ------------------------------------------------- default document name -- */
static void NameAt(const WCHAR *label, int y, int mo, int d, int h, int mi, int s, const WCHAR *want)
{
    SYSTEMTIME st;
    WCHAR b[16];
    memset(&st, 0, sizeof st);
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    st.wHour = (WORD)h; st.wMinute = (WORD)mi; st.wSecond = (WORD)s;
    DefaultDocName(DocNameClock(&st), b, COUNTOF(b));
    Str(label, b, want);
}

static int DaysIn(int y, int m)
{
    static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 29 : n[m - 1];
}

static void TestDocName(void)
{
    SYSTEMTIME st;
    WCHAR b[16], c;
    unsigned prev, v;
    int y, m, d, i, bad, gaps, back;

    Group(L"util.c default document name (mint- + 4 base-36 chars, 45 steps a day from 2026-01-01)");
    NameAt(L"midnight, 1 january 2026 = 0000", 2026, 1, 1, 0, 0, 0, L"mint-0000");
    NameAt(L"00:31:59 is still the first step", 2026, 1, 1, 0, 31, 59, L"mint-0000");
    NameAt(L"00:32:00 is the next one", 2026, 1, 1, 0, 32, 0, L"mint-0001");
    NameAt(L"the last second of 1 january 2026 = step 44", 2026, 1, 1, 23, 59, 59, L"mint-0018");
    NameAt(L"2 january 2026 = 45", 2026, 1, 2, 0, 0, 0, L"mint-0019");
    NameAt(L"9 october 2026 noon", 2026, 10, 9, 12, 0, 0, L"mint-09rv");
    NameAt(L"29 february 2028 06:00 (leap year)", 2028, 2, 29, 6, 0, 0, L"mint-0rek");
    NameAt(L"1 march 2028", 2028, 3, 1, 0, 0, 0, L"mint-0rfi");
    NameAt(L"28 february 2100 noon", 2100, 2, 28, 12, 0, 0, L"mint-q4i4");
    NameAt(L"1 march 2100 comes right after 28 february (2100 is no leap year)", 2100, 3, 1, 0, 0, 0, L"mint-q4ir");
    NameAt(L"the last second of 2125 = 1643579", 2125, 12, 31, 23, 59, 59, L"mint-z86z");
    NameAt(L"2126 starts again at 0000 (a 100 year cycle)", 2126, 1, 1, 0, 0, 0, L"mint-0000");
    NameAt(L"a clock before 2026 counts as the end of the cycle", 2025, 12, 31, 23, 59, 59, L"mint-z86z");
    NameAt(L"2226 = 2026", 2226, 10, 9, 12, 0, 0, L"mint-09rv");

    memset(&st, 0, sizeof st);
    bad = 0; gaps = 0; back = 0; prev = 0;
    for (y = 2026; y < 2126; y++)
        for (m = 1; m <= 12; m++)
            for (d = 1; d <= DaysIn(y, m); d++) {
                st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = (WORD)d;
                st.wHour = 0; st.wMinute = 0; st.wSecond = 0;
                v = DocNameClock(&st);
                if (!(y == 2026 && m == 1 && d == 1) && v != prev + 1) gaps++;
                st.wHour = 23; st.wMinute = 59; st.wSecond = 59;
                prev = DocNameClock(&st);
                if (prev != v + DOCNAME_STEPS - 1) back++;
                DefaultDocName(prev, b, COUNTOF(b));
                if (wlen(b) != 9 || b[0] != 'm' || b[4] != '-') bad++;
                for (i = 5; i < 9; i++) {
                    c = b[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) bad++;
                }
            }
    Int(L"every day of the century: \"mint-\" + exactly four of 0-9 a-z", bad, 0);
    Int(L"each day starts one step after the day before ends (no gap, never backwards)", gaps, 0);
    Int(L"each day has exactly 45 steps", back, 0);
    Want(prev < 1679616u, L"the last step of the century fits four base-36 characters", (int)prev, 0);
    Done(L"the century fits four characters");

    Group(L"util.c DocNameNext (one more than the last name while that is ahead of the clock)");
    Int(L"no name handed out yet: the clock", (int)DocNameNext(100, DOCNAME_NONE), 100);
    Int(L"last name behind the clock: the clock", (int)DocNameNext(100, 50), 100);
    Int(L"last name one step behind: the clock", (int)DocNameNext(100, 99), 100);
    Int(L"last name = the clock (two documents in one step): one more", (int)DocNameNext(100, 100), 101);
    Int(L"last name ahead (many documents): one more", (int)DocNameNext(100, 150), 151);
    Int(L"ahead by just under a year: one more", (int)DocNameNext(100, 99 + DOCNAME_AHEAD), (int)(100 + DOCNAME_AHEAD));
    Int(L"ahead by more than a year (stale): the clock", (int)DocNameNext(100, 100 + DOCNAME_AHEAD), 100);
    Int(L"a new cycle (last = end of 2125): the clock", (int)DocNameNext(5, 1643579u), 5);
    Int(L"a counter from the 2000-based names of 1.0.10 (far ahead): the clock", (int)DocNameNext(12667, 440032u), 12667);

    memset(b, 0x55, sizeof b);
    DefaultDocName(0, b, 9);
    Want(b[0] == 0 && b[1] == 0x5555, L"cap 9: expected an empty string and nothing written past it", 0, 0);
    Done(L"a buffer that cannot hold 9 chars + nul gets an empty string");
    memset(b, 0x55, sizeof b);
    DefaultDocName(0, b, 10);
    Want(wlen(b) == 9 && b[10] == 0x5555, L"cap 10: expected 9 characters and nothing written past the nul", 0, 0);
    Done(L"cap 10 is exactly enough");
}

/* ------------------------------------------------------------- paths --- */
static void TestPaths(void)
{
    WCHAR b[64];
    Group(L"util.c paths");
    Str(L"pathname file", PathName(L"c:\\dir\\file.txt"), L"file.txt");
    Str(L"pathname bare name", PathName(L"file.txt"), L"file.txt");
    Str(L"pathname trailing slash", PathName(L"c:\\dir\\"), L"");
    Str(L"pathname forward slashes", PathName(L"c:/a/b.txt"), L"b.txt");
    Str(L"pathname unc", PathName(L"\\\\server\\share\\f.txt"), L"f.txt");
    Str(L"pathname empty", PathName(L""), L"");
    PathDir(L"c:\\dir\\file.txt", b, 64); Str(L"pathdir file", b, L"c:\\dir");
    PathDir(L"c:\\file.txt", b, 64);      Str(L"pathdir keeps the drive root", b, L"c:\\");
    PathDir(L"file.txt", b, 64);          Str(L"pathdir bare name", b, L"");
    PathDir(L"\\file.txt", b, 64);        Str(L"pathdir root without drive", b, L"\\");
    PathDir(L"\\\\server\\share\\f.txt", b, 64); Str(L"pathdir unc", b, L"\\\\server\\share");
    PathDir(L"c:\\a\\b\\", b, 64);        Str(L"pathdir trailing slash", b, L"c:\\a\\b");
    memset(b, 0x55, sizeof b);
    PathDir(L"c:\\dir\\file", b, 4);
    WantStr(L"text", b, wlen(b), WLIT(L"c:\\"));
    Want(b[4] == 0x5555, L"wrote past cap", 0, 0);
    Done(L"pathdir truncates to cap");
    memset(b, 0x55, sizeof b);
    PathDir(L"c:\\dir\\file", b, 0);
    Want(b[0] == 0x5555 && b[1] == 0x5555, L"zero capacity changed the buffer", 0, 0);
    Done(L"pathdir zero capacity writes nothing");
    PathDir(L"c:\\dir\\file", b, -4);
    Want(b[0] == 0x5555 && b[1] == 0x5555, L"negative capacity changed the buffer", 0, 0);
    Done(L"pathdir negative capacity writes nothing");
    PathDir(L"c:\\dir\\file", b, 1);
    Want(b[0] == 0 && b[1] == 0x5555, L"capacity one failed to terminate or wrote past its limit", 0, 0);
    Done(L"pathdir capacity one writes only its terminator");
    wcopy(b, L"c:\\dir", 64);   PathJoin(b, L"f.txt", 64); Str(L"pathjoin adds a backslash", b, L"c:\\dir\\f.txt");
    wcopy(b, L"c:\\dir\\", 64); PathJoin(b, L"f.txt", 64); Str(L"pathjoin no double backslash", b, L"c:\\dir\\f.txt");
    wcopy(b, L"c:\\", 64);      PathJoin(b, L"f", 64);     Str(L"pathjoin drive root", b, L"c:\\f");
    wcopy(b, L"", 64);          PathJoin(b, L"f", 64);     Str(L"pathjoin empty dir", b, L"f");
    memset(b, 0x55, sizeof b);
    wcopy(b, L"c:\\dir", 8);
    PathJoin(b, L"file.txt", 8);
    WantStr(L"text", b, wlen(b), WLIT(L"c:\\dir\\"));
    Want(b[8] == 0x5555, L"wrote past cap", 0, 0);
    Done(L"pathjoin truncates to cap");
}

/* ------------------------------------------------------------- rt.asm -- */
typedef void *(*CopyFn)(void *, const void *, size_t);
typedef int   (*CmpFn)(const void *, const void *, size_t);
static CopyFn volatile g_move = memmove, g_copy = memcpy;      /* through pointers: the compiler can't inline them */
static CmpFn  volatile g_cmp = memcmp;

static int Naive(const WCHAR *p, int n)
{
    int c = 0, i;
    for (i = 0; i < n; i++) if (p[i] == '\n') c++;
    return c;
}

/* a frame far bigger than a page: the compiler calls __chkstk (rt.asm) on the way in */
static int BigFrame(int k)
{
    volatile BYTE big[20000];
    int i, s = 0;
    for (i = 0; i < 20000; i += 1000) big[i] = (BYTE)(i / 1000 + k);
    for (i = 0; i < 20000; i += 1000) s += big[i];
    return s;
}

static void TestRt(void)
{
    static const WCHAR pick[8] = { 'a', '\n', 0x0A00, 0x0A0A, '\n', 0x000B, 0x8A0A, '\r' };
    static WCHAR buf[400];
    WCHAR *big, why[96];
    BYTE a[64], b[64], x = 0x80, y = 0x01;
    unsigned seed = 12345;
    int i, off, n, bad = 0, got, want, ok;

    Group(L"rt.asm");
    for (i = 0; i < COUNTOF(buf); i++) {
        seed = seed * 1103515245u + 12345u;
        buf[i] = pick[(seed >> 16) & 7];
    }
    for (off = 0; off < 8; off++)
        for (n = 0; n <= 300; n++) {
            got = (int)mp_count_lf(buf + off, (size_t)n);
            want = Naive(buf + off, n);
            if (got != want && bad++ < 5) {
                wsprintfW(why, L"offset %d, length %d: expected %d, got %d", off, n, want, got);
                Result(L"mp_count_lf", why);
            }
        }
    if (!bad) Result(L"mp_count_lf every length 0..300 at 8 alignments (traps: 0x0a00, 0x0a0a, 0x8a0a)", NULL);
    n = 1000003;
    big = (WCHAR *)mem_alloc((size_t)n * sizeof(WCHAR));
    if (big) {
        for (i = 0; i < n; i++) big[i] = (WCHAR)(i % 7 ? 'x' : '\n');
        Int(L"mp_count_lf one million chars", (int)mp_count_lf(big, (size_t)n), Naive(big, n));
        mem_free(big);
    }

    for (i = 0; i < 64; i++) a[i] = (BYTE)i;
    g_move(a + 3, a, 40);
    for (ok = 1, i = 0; i < 64; i++) if (a[i] != (BYTE)(i < 3 ? i : (i < 43 ? i - 3 : i))) ok = 0;
    Want(ok, L"wrong bytes", 0, 0);
    Done(L"memmove overlapping, destination above the source");
    for (i = 0; i < 64; i++) a[i] = (BYTE)i;
    g_move(a, a + 5, 40);
    for (ok = 1, i = 0; i < 64; i++) if (a[i] != (BYTE)(i < 40 ? i + 5 : i)) ok = 0;
    Want(ok, L"wrong bytes", 0, 0);
    Done(L"memmove overlapping, destination below the source");
    for (i = 0; i < 64; i++) a[i] = (BYTE)(i * 7);
    memset(b, 0, sizeof b);
    g_copy(b + 1, a, 61);
    for (ok = 1, i = 0; i < 64; i++) if (b[i] != (i >= 1 && i <= 61 ? a[i - 1] : 0)) ok = 0;
    Want(ok, L"wrong bytes", 0, 0);
    Done(L"memcpy odd length and offset");
    Want(g_cmp(a, a, 64) == 0, L"equal buffers differ", 0, 0);
    Want(g_cmp(&x, &y, 1) > 0 && g_cmp(&y, &x, 1) < 0, L"bytes must compare as unsigned", 0, 0);
    Want(g_cmp(&x, &y, 0) == 0, L"zero length must be equal", 0, 0);
    Done(L"memcmp");
    for (ok = 1, n = 0; n <= 41; n++) {                                 /* every length (dword steps and a byte tail) x every overlap, up and down, against a plain byte loop */
        for (off = 1; off <= 7; off++) {
            BYTE ref[64];
            for (i = 0; i < 64; i++) a[i] = ref[i] = (BYTE)(i * 5 + 1);
            g_move(a + off, a, (size_t)n);                              /* destination above */
            for (i = n - 1; i >= 0; i--) ref[off + i] = (BYTE)(i * 5 + 1);
            if (memcmp(a, ref, 64) != 0) ok = 0;
            for (i = 0; i < 64; i++) a[i] = ref[i] = (BYTE)(i * 5 + 1);
            g_move(a, a + off, (size_t)n);                              /* destination below */
            for (i = 0; i < n; i++) ref[i] = (BYTE)((off + i) * 5 + 1);
            if (memcmp(a, ref, 64) != 0) ok = 0;
        }
    }
    Want(ok, L"wrong bytes", 0, 0);
    Done(L"memmove every length 0..41 at every overlap 1..7, both directions");
    for (ok = 1, n = 0; n < 37; n++) {                                  /* one different byte at every position: the sign says which side is bigger */
        for (i = 0; i < 37; i++) a[i] = b[i] = (BYTE)(i + 3);
        b[n] = (BYTE)(a[n] + 1);
        if (g_cmp(a, b, 37) >= 0 || g_cmp(b, a, 37) <= 0 || g_cmp(a, b, (size_t)n) != 0) ok = 0;
    }
    Want(ok, L"a different byte was missed or compared the wrong way", 0, 0);
    Done(L"memcmp finds a different byte at every position of 37");
    Int(L"__chkstk big stack frame", BigFrame(3), 190 + 20 * 3);
}

/* ------------------------------------------------------------ search.c -- */
static void TestFind(void)
{
    static const WCHAR t1[] = L"abc ABC abc", t2[] = L"alpha beta\r\ngamma beta\r\n";
    static const struct { const WCHAR *t, *pat; int from, up, mc, want; } t[] = {
        { t1, L"abc", 0, 0, 0, 0 },          { t1, L"abc", 1, 0, 0, 4 },         { t1, L"abc", 1, 0, 1, 8 },
        { t1, L"abc", 8, 0, 0, 8 },          { t1, L"abc", 9, 0, 0, -1 },        { t1, L"abc", 11, 0, 0, -1 },
        { t1, L"abc", 11, 1, 0, 8 },         { t1, L"abc", 10, 1, 0, 4 },        { t1, L"abc", 10, 1, 1, 0 },
        { t1, L"abc", 7, 1, 0, 4 },          { t1, L"abc", 6, 1, 0, 0 },         { t1, L"abc", 3, 1, 0, 0 },
        { t1, L"abc", 2, 1, 0, -1 },         { t1, L"abc", 0, 1, 0, -1 },        { t1, L"ABC", 0, 0, 1, 4 },
        { t1, L"aBc", 0, 0, 1, -1 },         { t1, L"aBc", 5, 0, 0, 8 },         { t1, L"abc", -5, 0, 0, 0 },
        { t1, L"abc", 100, 1, 0, 8 },
        { L"aaaaa", L"aaa", 0, 0, 0, 0 },    { L"aaaaa", L"aaa", 1, 0, 0, 1 },   { L"aaaaa", L"aaa", 2, 0, 0, 2 },
        { L"aaaaa", L"aaa", 3, 0, 0, -1 },   { L"aaaaa", L"aaa", 5, 1, 0, 2 },   { L"aaaaa", L"aaa", 4, 1, 0, 1 },
        { L"aaaaa", L"aaa", 3, 1, 0, 0 },    { L"aaaaa", L"aaa", 2, 1, 0, -1 },
        { t2, L"beta", 0, 0, 0, 6 },         { t2, L"beta", 10, 0, 0, 18 },      { t2, L"beta", 22, 0, 0, -1 },
        { t2, L"beta", 24, 1, 0, 18 },       { t2, L"beta", 18, 1, 0, 6 },       { t2, L"BETA", 7, 0, 0, 18 },
        { L"a\r\nb", L"\r\nb", 0, 0, 0, 1 }, { L"a\r\nb", L"a\r", 0, 0, 0, 0 },  { L"a\r\nb", L"\n", 0, 0, 0, 2 },
        { L"a\r\nb", L"\r\n", 4, 1, 0, 1 },  { L"a\r\nb", L"\r\n", 2, 1, 0, -1 },
        { L"xxab", L"ab", 0, 0, 0, 2 },      { L"xxab", L"ab", 4, 1, 0, 2 },     { L"xxab", L"ab", 3, 0, 0, -1 },
        { L"xxab", L"ab", 3, 1, 0, -1 },     { L"ab", L"abc", 0, 0, 0, -1 },     { L"ab", L"abc", 2, 1, 0, -1 },
        { L"xABC", L"Abc", 0, 0, 0, 1 },     { L"1a 1A", L"1A", 0, 0, 0, 0 },    { L"1a 1A", L"1A", 1, 0, 0, 3 },
        { L"1a 1A", L"1A", 0, 0, 1, 3 },
        { L"\u00c4BC \u00e4bc", L"\u00e4BC", 0, 0, 0, 0 },   { L"\u00c4BC \u00e4bc", L"\u00e4BC", 1, 0, 0, 4 },
        { L"\u00c4BC \u00e4bc", L"\u00e4BC", 0, 0, 1, -1 },  { L"\u00c4BC \u00e4bc", L"\u00e4bc", 0, 0, 1, 4 },
        { L"\u041f\u0420\u0418\u0412\u0415\u0422", L"\u043f\u0440\u0438", 0, 0, 0, 0 },
        { L"\u041f\u0420\u0418\u0412\u0415\u0422", L"\u043f\u0440\u0438", 0, 0, 1, -1 },
    };
    WCHAR name[400], a[100], b[100], emo[8];
    int i;
    Group(L"search.c FindInText");
    for (i = 0; i < COUNTOF(t); i++) {
        Esc(a, COUNTOF(a), t[i].pat, wlen(t[i].pat));
        Esc(b, COUNTOF(b), t[i].t, wlen(t[i].t));
        wsprintfW(name, L"find %s in %s from %d %s%s", a, b, t[i].from, t[i].up ? L"up" : L"down", t[i].mc ? L" match case" : L"");
        Int(name, FindInText(t[i].t, wlen(t[i].t), t[i].pat, wlen(t[i].pat), t[i].from, t[i].up, t[i].mc), t[i].want);
    }
    Int(L"find empty needle", FindInText(L"abc", 3, L"", 0, 0, 0, 0), -1);
    Int(L"find empty needle up", FindInText(L"abc", 3, L"", 0, 3, 1, 0), -1);
    Int(L"find negative needle length", FindInText(L"abc", 3, L"a", -1, 0, 0, 0), -1);
    Int(L"find in empty text", FindInText(L"", 0, L"a", 1, 0, 0, 0), -1);
    Int(L"find null text", FindInText(NULL, 3, L"a", 1, 0, 0, 0), -1);
    Int(L"find null needle", FindInText(L"abc", 3, NULL, 1, 0, 0, 0), -1);
    Int(L"find honours n (no terminator needed)", FindInText(L"abcabc", 4, L"abc", 3, 1, 0, 0), -1);
    Int(L"find honours n up", FindInText(L"abcabc", 5, L"abc", 3, 5, 1, 0), 0);
    Int(L"find honours m", FindInText(L"abxabc", 6, L"abc", 2, 1, 0, 0), 3);
    wcopy(emo, L"x", 8);
    wcat(emo, g_emoji, 8);
    wcat(emo, L"y", 8);
    Int(L"find a surrogate pair", FindInText(emo, wlen(emo), g_emoji, 2, 0, 0, 0), 1);
    Int(L"find a surrogate pair, case off", FindInText(emo, wlen(emo), g_emoji, 2, 0, 0, 1), 1);
}

static void TestReplace(void)
{
    static const WCHAR t2[] = L"alpha beta\r\ngamma beta\r\n";
    static const struct { const WCHAR *t, *pat, *with; int mc, count; const WCHAR *want; } t[] = {
        { t2, L"beta", L"delta", 0, 2, L"alpha delta\r\ngamma delta\r\n" },
        { t2, L"beta", L"b", 0, 2, L"alpha b\r\ngamma b\r\n" },
        { t2, L"beta", L"", 0, 2, L"alpha \r\ngamma \r\n" },
        { t2, L"zzz", L"x", 0, 0, t2 },
        { L"aaaaa", L"aa", L"b", 0, 2, L"bba" },
        { L"aaa", L"a", L"aa", 0, 3, L"aaaaaa" },
        { L"Beta BETA beta", L"beta", L"x", 0, 3, L"x x x" },
        { L"Beta BETA beta", L"beta", L"x", 1, 1, L"Beta BETA x" },
        { L"a\r\nb\r\nc", L"\r\n", L"\n", 0, 2, L"a\nb\nc" },
        { L"ab", L"abc", L"x", 0, 0, L"ab" },
        { L"", L"a", L"b", 0, 0, L"" },
        { L"abc", L"abc", L"", 0, 1, L"" },
        { L"xyz", L"", L"q", 0, 0, L"xyz" },
        { L"ab", L"b", L"cd", 0, 1, L"acd" },
        { L"\u00c4rger \u00e4rger", L"\u00e4RGER", L"x", 0, 2, L"x x" },
    };
    WCHAR name[400], a[100], b[100], c[100], *r, *big;
    int i, len, cnt, n, ok;
    Group(L"search.c ReplaceAllText");
    for (i = 0; i < COUNTOF(t); i++) {
        Esc(a, COUNTOF(a), t[i].pat, wlen(t[i].pat));
        Esc(b, COUNTOF(b), t[i].with, wlen(t[i].with));
        Esc(c, COUNTOF(c), t[i].t, wlen(t[i].t));
        wsprintfW(name, L"replace %s with %s in %s%s", a, b, c, t[i].mc ? L" match case" : L"");
        len = -1; cnt = -1;
        r = ReplaceAllText(t[i].t, wlen(t[i].t), t[i].pat, wlen(t[i].pat), t[i].with, wlen(t[i].with), t[i].mc, &len, &cnt);
        Want(r != NULL, L"returned NULL", 0, 0);
        Want(cnt == t[i].count, L"count: expected %d, got %d", t[i].count, cnt);
        WantStr(L"result", r, len, t[i].want, wlen(t[i].want));
        Want(!r || r[len] == 0, L"not nul terminated", 0, 0);
        Done(name);
        mem_free(r);
    }
    r = ReplaceAllText(L"a-b-c", 5, L"-", 1, NULL, 3, 0, &len, &cnt);
    WantStr(L"result", r, len, WLIT(L"abc"));
    Want(cnt == 2, L"count %d", cnt, 0);
    Done(L"replace with NULL = with nothing");
    mem_free(r);
    r = ReplaceAllText(L"a-b", 3, L"-", 1, L"+", 1, 0, NULL, NULL);
    Want(r && !wcmp(r, L"a+b"), L"wrong result", 0, 0);
    Done(L"replace without outLen / count");
    mem_free(r);
    r = ReplaceAllText(L"abcabc", 4, L"abc", 3, L"x", 1, 0, &len, &cnt);
    WantStr(L"result", r, len, WLIT(L"xa"));
    Done(L"replace honours n (no terminator needed)");
    mem_free(r);
    n = 100000;
    big = (WCHAR *)mem_alloc((size_t)n * sizeof(WCHAR));
    if (big) {
        for (i = 0; i < n; i++) big[i] = (WCHAR)(i & 1 ? 'b' : 'a');
        r = ReplaceAllText(big, n, L"b", 1, L"xyz", 3, 0, &len, &cnt);
        Want(r != NULL, L"returned NULL", 0, 0);
        Want(cnt == n / 2, L"count: expected %d, got %d", n / 2, cnt);
        Want(len == n * 2, L"length: expected %d, got %d", n * 2, len);
        for (ok = 1, i = 0; r && ok && i < len; i += 4) ok = r[i] == 'a' && r[i + 1] == 'x' && r[i + 2] == 'y' && r[i + 3] == 'z';
        Want(ok, L"wrong text", 0, 0);
        Done(L"replace 50000 matches (growth)");
        mem_free(r);
        mem_free(big);
    }
}

/* ------------------------------------------------- encoding list / labels */
static const WCHAR *Lab(int enc)
{
    static WCHAR b[64];
    EncLabel(enc, b, 64);
    return b;
}

static int Lower(const WCHAR *s)
{
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') return 0;
    return 1;
}

static void TestEncList(void)
{
    static const int must[] = { 1250, 1251, 1252, 1253, 1254, 932, 936, 949, 950, 20866, 28591, 54936, 65000, 437, 866 };
    static const WCHAR ansi[] = L"ansi  system default code page";
    WCHAR lab[128], last[128], want[64], name[200], q[140];
    int n = EncListCount(), i, j, k, id, ids[512], found, bad = 0;

    Group(L"doc.c encoding list");
    Str(L"enclabel utf-8", Lab(ENC_UTF8), L"utf-8");
    Str(L"enclabel utf-8 with bom", Lab(ENC_UTF8BOM), L"utf-8 with bom");
    Str(L"enclabel utf-16 le", Lab(ENC_UTF16LE), L"utf-16 le");
    Str(L"enclabel utf-16 be", Lab(ENC_UTF16BE), L"utf-16 be");
    Str(L"enclabel ansi", Lab(ENC_ANSI), L"ansi");
    Str(L"enclabel 1252", Lab(1252), L"windows-1252");
    Str(L"enclabel 932", Lab(932), L"shift-jis");
    Str(L"enclabel 20866", Lab(20866), L"koi8-r");
    Str(L"enclabel a code page the table doesn't know", Lab(12345), L"code page 12345");
    memset(lab, 0x55, sizeof lab);
    EncLabel(ENC_UTF8BOM, lab, 4);
    WantStr(L"text", lab, wlen(lab), WLIT(L"utf"));
    Want(lab[4] == 0x5555, L"wrote past cap", 0, 0);
    Done(L"enclabel truncates to cap");
    for (i = 0; i < ENC_COUNT; i++) {
        wsprintfW(name, L"g_encName[%d] \"%s\" is lowercase", i, g_encName[i]);
        Want(g_encName[i][0] && Lower(g_encName[i]), L"it isn't", 0, 0);
        Done(name);
    }
    for (i = 0; i < EOL_COUNT; i++) {
        wsprintfW(name, L"g_eolName[%d] \"%s\" is lowercase", i, g_eolName[i]);
        Want(g_eolName[i][0] && Lower(g_eolName[i]), L"it isn't", 0, 0);
        Done(name);
    }
    Str(L"g_eolName crlf", g_eolName[EOL_CRLF], L"windows (crlf)");
    Str(L"g_eolName lf", g_eolName[EOL_LF], L"unix (lf)");
    for (i = 0; i < EOL_COUNT; i++) {
        wsprintfW(name, L"g_eolShort[%d] \"%s\" is lowercase and shorter than the full name", i, g_eolShort[i]);
        Want(g_eolShort[i][0] && Lower(g_eolShort[i]) && wlen(g_eolShort[i]) < wlen(g_eolName[i]), L"it isn't", 0, 0);
        Done(name);
    }
    Str(L"g_eolShort crlf", g_eolShort[EOL_CRLF], L"crlf");
    Str(L"g_eolShort lf", g_eolShort[EOL_LF], L"lf");
    Str(L"g_eolShort cr", g_eolShort[EOL_CR], L"cr");
    for (i = 0; i < ENC_COUNT; i++) {
        wsprintfW(name, L"g_encShort[%d] \"%s\" is lowercase and no longer than the full name", i, g_encShort[i]);
        Want(g_encShort[i][0] && Lower(g_encShort[i]) && wlen(g_encShort[i]) <= wlen(g_encName[i]), L"it isn't", 0, 0);
        Done(name);
    }
    EncShort(ENC_UTF8, lab, COUNTOF(lab));
    Str(L"encshort utf-8", lab, L"utf8");
    EncShort(ENC_UTF8BOM, lab, COUNTOF(lab));
    Str(L"encshort utf-8 with bom", lab, L"utf8 bom");
    EncShort(ENC_UTF16LE, lab, COUNTOF(lab));
    Str(L"encshort utf-16 le", lab, L"utf16 le");
    EncShort(ENC_UTF16BE, lab, COUNTOF(lab));
    Str(L"encshort utf-16 be", lab, L"utf16 be");
    EncShort(1252, lab, COUNTOF(lab));
    Str(L"encshort 1252 = the code page name without hyphens", lab, L"windows1252");
    EncShort(932, lab, COUNTOF(lab));
    Str(L"encshort 932", lab, L"shiftjis");
    EncShort(12345, lab, COUNTOF(lab));
    Str(L"encshort a code page the table doesn't know", lab, L"cp 12345");

    Want(n > ENC_COUNT && n <= 512, L"count %d", n, 0);
    Done(L"enclistcount = the unicode / ansi entries + a code page table");
    if (n <= ENC_COUNT || n > 512) return;
    for (i = 0; i < n; i++) {
        id = EncListGet(i, lab, COUNTOF(lab));
        ids[i] = id;
        Want(Lower(lab), L"label isn't lowercase", 0, 0);
        if (i < ENC_COUNT) {
            const WCHAR *w = i == ENC_ANSI ? ansi : g_encName[i];
            Want(id == i, L"id: expected %d, got %d", i, id);
            WantStr(L"label", lab, wlen(lab), w, wlen(w));
        } else {
            Want(id >= ENC_CP_MIN, L"code page id %d is below ENC_CP_MIN", id, 0);
            EncLabel(id, want, COUNTOF(want));
            k = wlen(want);
            Want(wlen(lab) > k + 2 && !memcmp(lab, want, (size_t)k * sizeof(WCHAR)) && lab[k] == ' ' && lab[k + 1] == ' ' && lab[k + 2] != ' ',
                 L"label isn't \"<enclabel>  <description>\"", 0, 0);
            for (j = ENC_COUNT; j < i; j++) Want(ids[j] != id, L"code page %d is listed twice", id, 0);
        }
        if (g_why[0]) {
            Esc(q, COUNTOF(q), lab, wlen(lab));
            wsprintfW(name, L"enclistget %d (id %d, label %s)", i, id, q);
            Done(name);
            bad++;
        }
    }
    if (!bad) {
        wsprintfW(name, L"enclistget all %d entries: ids, \"name  description\" labels, lowercase, no duplicates", n);
        Result(name, NULL);
    }
    for (i = 0; i < COUNTOF(must); i++) {
        for (found = 0, j = 0; j < n; j++) if (ids[j] == must[i]) found = 1;
        wsprintfW(name, L"enclist offers code page %d", must[i]);
        Want(found, L"missing", 0, 0);
        Done(name);
    }
    id = EncListGet(n + 3, lab, COUNTOF(lab));
    k = EncListGet(n - 1, last, COUNTOF(last));
    Want(id == k && !wcmp(lab, last), L"out of range index: got %d, the last entry is %d", id, k);
    Done(L"enclistget clamps an out of range index to the last entry");
}

/* ---------------------------------------------------------------- doc.c -- */
static int HasBreak(const WCHAR *t, int n)
{
    int i;
    for (i = 0; i < n; i++) if (t[i] == '\r' || t[i] == '\n') return 1;
    return 0;
}

/* DocWrite(text, enc, eol) then DocRead(force) gives back the text, wantEnc and eol (crlf when there is no break) */
static void RoundTrip(const WCHAR *name, const WCHAR *text, int len, int enc, int eol, int force, int wantEnc)
{
    WCHAR *t = NULL;
    int n = -1, e = -1, l = -1;
    BOOL lossy = FALSE;
    DWORD r;
    DeleteFileW(g_file);
    r = DocWrite(g_file, text, len, enc, eol, &lossy);
    Want(r == 0, L"DocWrite returned %d", (int)r, 0);
    Want(!lossy, L"DocWrite flagged a lossy conversion", 0, 0);
    if (!r) {
        r = DocRead(g_file, &t, &n, &e, &l, force);
        Want(r == 0, L"DocRead returned %d", (int)r, 0);
        WantEnc(e, wantEnc);
        WantEol(l, HasBreak(text, len) ? eol : EOL_CRLF);
        WantStr(L"text", t, n, text, len);
        mem_free(t);
    }
    Done(name);
}

/* DocWrite puts exactly these bytes on disk */
static void Written(const WCHAR *name, const WCHAR *text, int len, int enc, int eol, const void *want, int wn)
{
    BOOL lossy = FALSE;
    DWORD r;
    BYTE *b;
    int n;
    DeleteFileW(g_file);
    r = DocWrite(g_file, text, len, enc, eol, &lossy);
    Want(r == 0, L"DocWrite returned %d", (int)r, 0);
    b = FileGet(g_file, &n);
    WantBytes(L"bytes", b, n, want, wn);
    mem_free(b);
    Done(name);
}

/* DocRead of these bytes (force -1 = detect) gives text / enc / eol (wantEol -1 = don't care) */
static void Read(const WCHAR *name, const void *bytes, int bn, int force, const WCHAR *want, int wn, int wantEnc, int wantEol)
{
    WCHAR *t = NULL;
    int n = -1, e = -1, l = -1;
    DWORD r;
    Want(FilePut(g_file, bytes, bn), L"cannot write the test file", 0, 0);
    r = DocRead(g_file, &t, &n, &e, &l, force);
    Want(r == 0, L"DocRead returned %d", (int)r, 0);
    WantEnc(e, wantEnc);
    if (wantEol >= 0) WantEol(l, wantEol);
    if (want) WantStr(L"text", t, n, want, wn);
    mem_free(t);
    Done(name);
}

static void Swap(WCHAR *d, const WCHAR *s, int n)              /* utf-16 le <-> be */
{
    int i;
    for (i = 0; i < n; i++) d[i] = (WCHAR)((s[i] << 8) | (s[i] >> 8));
}

static void TestDocRoundTrips(void)
{
    static const WCHAR t2[] = L"a\r\nb\r\n";
    WCHAR t1[128], name[128], enc[48];
    int e, l, acp = (int)GetACP();

    Group(L"doc.c round trips");
    wcopy(t1, L"line one\r\nzwei \u00fc\u00f6\u00df \u20ac\r\n\u65e5\u672c\u8a9e \u0645\u0631\u062d\u0628\u0627 ", COUNTOF(t1));
    wcat(t1, g_emoji, COUNTOF(t1));
    wcat(t1, L"\r\n\r\nlast line, no newline", COUNTOF(t1));
    for (e = ENC_UTF8; e <= ENC_UTF16BE; e++) {
        EncLabel(e, enc, 48);
        for (l = 0; l < EOL_COUNT; l++) {
            wsprintfW(name, L"roundtrip %s / %s (multilingual, no trailing newline)", enc, g_eolShort[l]);
            RoundTrip(name, t1, wlen(t1), e, l, -1, e);
            wsprintfW(name, L"roundtrip %s / %s (trailing newline)", enc, g_eolShort[l]);
            RoundTrip(name, WLIT(t2), e, l, -1, e);
        }
        wsprintfW(name, L"roundtrip %s empty text", enc);
        RoundTrip(name, WLIT(L""), e, EOL_LF, -1, e);
        wsprintfW(name, L"roundtrip %s one line, no break (reads back as crlf)", enc);
        RoundTrip(name, WLIT(L"just one line"), e, EOL_LF, -1, e);
    }
    /* "ansi" = the system code page: ascii alone is valid utf-8, so that is what detection says; forced it stays ansi */
    for (l = 0; l < EOL_COUNT; l++) {
        wsprintfW(name, L"roundtrip ansi / %s, forced read", g_eolShort[l]);
        RoundTrip(name, WLIT(L"plain ascii\r\ntext\r\n"), ENC_ANSI, l, ENC_ANSI, ENC_ANSI);
    }
    RoundTrip(L"ascii written as ansi is detected as utf-8", WLIT(L"plain ascii\r\n"), ENC_ANSI, EOL_CRLF, -1, ENC_UTF8);
    if (acp == 1252) {
        for (l = 0; l < EOL_COUNT; l++) {
            wsprintfW(name, L"roundtrip ansi (1252) / %s, detected as ansi (invalid utf-8)", g_eolShort[l]);
            RoundTrip(name, WLIT(L"caf\u00e9 na\u00efve\r\n\u00fcber"), ENC_ANSI, l, -1, ENC_ANSI);
        }
    } else {
        wsprintfW(name, L"system code page is %d, not 1252", acp);
        Skip(L"roundtrip ansi with accented latin text", name);
    }
}

static void TestDocCodePages(void)
{
    static const BYTE b1252[] = { 0x63, 0x61, 0x66, 0xe9, 0x20, 0x80, 0x0d, 0x0a, 0xfc };
    static const BYTE b1251[] = { 0xef, 0xf0, 0xe8, 0xe2, 0xe5, 0xf2, 0x0d, 0x0a, 0xcc, 0xe8, 0xf0 };
    static const BYTE b932[]  = { 0x93, 0xfa, 0x96, 0x7b, 0x0d, 0x0a, 0x61, 0x62, 0x63 };
    static const BYTE b936[]  = { 0xd6, 0xd0, 0xce, 0xc4, 0x0d, 0x0a, 0x78 };
    static const struct { int cp; const WCHAR *text; const BYTE *bytes; int bn; } t[] = {
        { 1252,  L"caf\u00e9 \u20ac\r\n\u00fc", b1252, (int)sizeof b1252 },
        { 1251,  L"\u043f\u0440\u0438\u0432\u0435\u0442\r\n\u041c\u0438\u0440", b1251, (int)sizeof b1251 },
        { 932,   L"\u65e5\u672c\r\nabc", b932, (int)sizeof b932 },
        { 936,   L"\u4e2d\u6587\r\nx", b936, (int)sizeof b936 },
        { 932,   L"\u65e5\u672c\u8a9e \u30c6\u30ad\u30b9\u30c8 \uff76\uff85\r\nline 2", NULL, 0 },
        { 949,   L"\ud55c\uad6d\uc5b4\r\nabc", NULL, 0 },
        { 950,   L"\u7e41\u9ad4\u4e2d\u6587\r\nabc", NULL, 0 },
        { 866,   L"\u043f\u0440\u0438\u0432\u0435\u0442\r\n\u041c\u0438\u0440", NULL, 0 },
        { 20866, L"\u043f\u0440\u0438\u0432\u0435\u0442\r\n\u041c\u0438\u0440", NULL, 0 },
        { 1253,  L"\u03b1\u03b2\u03b3 \u0394\r\nx", NULL, 0 },
        { 28591, L"caf\u00e9 \u00ff\r\nx", NULL, 0 },
        { 65000, L"utf-7 + caf\u00e9 \u65e5\r\nx", NULL, 0 },
        { 54936, L"gb18030 \u4e2d\u6587 \u00e9\r\nx", NULL, 0 },
    };
    WCHAR name[128];
    int i, l;
    Group(L"doc.c code pages");
    for (i = 0; i < COUNTOF(t); i++) {
        if (!IsValidCodePage((UINT)t[i].cp)) {
            wsprintfW(name, L"code page %d", t[i].cp);
            Skip(name, L"not installed (IsValidCodePage says no)");
            continue;
        }
        for (l = 0; l < EOL_COUNT; l++) {
            wsprintfW(name, L"roundtrip code page %d / %s, forced read", t[i].cp, g_eolShort[l]);
            RoundTrip(name, t[i].text, wlen(t[i].text), t[i].cp, l, t[i].cp, t[i].cp);
        }
        if (t[i].bytes) {
            wsprintfW(name, L"bytes on disk, code page %d / crlf", t[i].cp);
            Written(name, t[i].text, wlen(t[i].text), t[i].cp, EOL_CRLF, t[i].bytes, t[i].bn);
        }
    }
}

static void TestDocBytes(void)
{
    static const BYTE u8[] = { 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80 };
    WCHAR t[16];
    Group(L"doc.c bytes on disk");
    Written(L"bytes utf-8 / crlf", WLIT(L"a\r\nb"), ENC_UTF8, EOL_CRLF, LIT("a\r\nb"));
    Written(L"bytes utf-8 / lf", WLIT(L"a\r\nb"), ENC_UTF8, EOL_LF, LIT("a\nb"));
    Written(L"bytes utf-8 / cr", WLIT(L"a\r\nb"), ENC_UTF8, EOL_CR, LIT("a\rb"));
    Written(L"bytes utf-8 with bom / lf", WLIT(L"a\r\nb"), ENC_UTF8BOM, EOL_LF, LIT("\xef\xbb\xbf" "a\nb"));
    Written(L"bytes utf-16 le / cr (bom ff fe)", WLIT(L"a\r\nb"), ENC_UTF16LE, EOL_CR, LIT("\xff\xfe" "a\0\r\0b\0"));
    Written(L"bytes utf-16 be / crlf (bom fe ff)", WLIT(L"a\r\nb"), ENC_UTF16BE, EOL_CRLF, LIT("\xfe\xff\0" "a\0\r\0\n\0" "b"));
    Written(L"bytes trailing newline kept", WLIT(L"a\r\n"), ENC_UTF8, EOL_LF, LIT("a\n"));
    Written(L"bytes no trailing newline added", WLIT(L"a"), ENC_UTF8, EOL_CRLF, LIT("a"));
    Written(L"bytes any break style in, crlf out", WLIT(L"a\nb\rc\r\nd\r\r\ne"), ENC_UTF8, EOL_CRLF, LIT("a\r\nb\r\nc\r\nd\r\n\r\ne"));
    Written(L"bytes any break style in, lf out", WLIT(L"a\nb\rc\r\nd"), ENC_UTF8, EOL_LF, LIT("a\nb\nc\nd"));
    wcopy(t, L"\u00e9\u20ac", 16);
    wcat(t, g_emoji, 16);
    Written(L"bytes utf-8 2, 3 and 4 byte sequences", t, wlen(t), ENC_UTF8, EOL_CRLF, u8, (int)sizeof u8);
    Written(L"bytes empty utf-8", WLIT(L""), ENC_UTF8, EOL_CRLF, LIT(""));
    Written(L"bytes empty utf-8 with bom", WLIT(L""), ENC_UTF8BOM, EOL_CRLF, LIT("\xef\xbb\xbf"));
    Written(L"bytes empty utf-16 le", WLIT(L""), ENC_UTF16LE, EOL_CRLF, LIT("\xff\xfe"));
    Written(L"bytes empty utf-16 be", WLIT(L""), ENC_UTF16BE, EOL_CRLF, LIT("\xfe\xff"));
    Written(L"bytes empty ansi", WLIT(L""), ENC_ANSI, EOL_CRLF, LIT(""));
}

static void TestDocDetect(void)
{
    static const WCHAR hello[] = L"hello world\r\n", mostly[] = L"hello \u65e5\u672c world\r\n";
    static WCHAR dummy;
    WCHAR be[32], sub[PATH_CAP], *t = &dummy;
    int n = -1, e = -1, l = -1, acp = (int)GetACP();
    DWORD r;

    Group(L"doc.c detection");
    Read(L"utf-8 bom", LIT("\xef\xbb\xbf" "hi\n"), -1, WLIT(L"hi\r\n"), ENC_UTF8BOM, EOL_LF);
    Read(L"utf-16 le bom", LIT("\xff\xfeh\0i\0"), -1, WLIT(L"hi"), ENC_UTF16LE, EOL_CRLF);
    Read(L"utf-16 be bom", LIT("\xfe\xff\0h\0i"), -1, WLIT(L"hi"), ENC_UTF16BE, EOL_CRLF);
    Read(L"utf-8 bom only", LIT("\xef\xbb\xbf"), -1, WLIT(L""), ENC_UTF8BOM, EOL_CRLF);
    Read(L"utf-16 le bom only", LIT("\xff\xfe"), -1, WLIT(L""), ENC_UTF16LE, EOL_CRLF);
    Read(L"utf-16 be bom only", LIT("\xfe\xff"), -1, WLIT(L""), ENC_UTF16BE, EOL_CRLF);
    Read(L"utf-16 le without bom (ascii)", hello, (int)sizeof hello - 2, -1, WLIT(hello), ENC_UTF16LE, EOL_CRLF);
    Swap(be, hello, COUNTOF(hello) - 1);
    Read(L"utf-16 be without bom (ascii)", be, (int)sizeof hello - 2, -1, WLIT(hello), ENC_UTF16BE, EOL_CRLF);
    Read(L"utf-16 le without bom, mostly ascii with cjk", mostly, (int)sizeof mostly - 2, -1, WLIT(mostly), ENC_UTF16LE, EOL_CRLF);
    Swap(be, mostly, COUNTOF(mostly) - 1);
    Read(L"utf-16 be without bom, mostly ascii with cjk", be, (int)sizeof mostly - 2, -1, WLIT(mostly), ENC_UTF16BE, EOL_CRLF);
    Read(L"utf-16 le without bom, shortest (4 bytes)", LIT("a\0b\0"), -1, WLIT(L"ab"), ENC_UTF16LE, EOL_CRLF);
    Read(L"3 bytes are too short to guess utf-16: utf-8, nul blanked", LIT("a\0b"), -1, WLIT(L"a b"), ENC_UTF8, EOL_CRLF);
    Read(L"nul characters become spaces", LIT("a\0\nb\0c"), -1, WLIT(L"a \r\nb c"), ENC_UTF8, EOL_LF);   /* zeros at both parities: not utf-16 */
    Read(L"plain ascii is utf-8", LIT("abc"), -1, WLIT(L"abc"), ENC_UTF8, EOL_CRLF);
    Read(L"valid utf-8 2/3/4 byte sequences", LIT("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"), -1, NULL, 0, ENC_UTF8, EOL_CRLF);
    Read(L"valid 3 byte sequence at the very end", LIT("a\xe2\x82\xac"), -1, WLIT(L"a\u20ac"), ENC_UTF8, EOL_CRLF);
    Read(L"invalid utf-8: lone latin-1 byte => ansi", LIT("caf\xe9"), -1, acp == 1252 ? L"caf\u00e9" : NULL, 4, ENC_ANSI, EOL_CRLF);
    Read(L"invalid utf-8: overlong => ansi", LIT("\xc0\xaf"), -1, NULL, 0, ENC_ANSI, -1);
    Read(L"invalid utf-8: encoded surrogate => ansi", LIT("\xed\xa0\x80"), -1, NULL, 0, ENC_ANSI, -1);
    Read(L"invalid utf-8: above u+10ffff => ansi", LIT("\xf4\x90\x80\x80"), -1, NULL, 0, ENC_ANSI, -1);
    Read(L"invalid utf-8: truncated at the end => ansi", LIT("ab\xe2\x82"), -1, NULL, 0, ENC_ANSI, -1);
    Read(L"invalid utf-8: lone continuation byte => ansi", LIT("a\x80" "b"), -1, NULL, 0, ENC_ANSI, -1);
    Read(L"empty file", LIT(""), -1, WLIT(L""), ENC_UTF8, EOL_CRLF);

    Read(L"forced utf-8 on a bom file reports utf-8 with bom", LIT("\xef\xbb\xbf" "hi"), ENC_UTF8, WLIT(L"hi"), ENC_UTF8BOM, -1);
    Read(L"forced utf-8 with bom on a bom file", LIT("\xef\xbb\xbf" "hi"), ENC_UTF8BOM, WLIT(L"hi"), ENC_UTF8BOM, -1);
    Read(L"forced utf-8 with bom on a file without one", LIT("hi"), ENC_UTF8BOM, WLIT(L"hi"), ENC_UTF8, -1);
    Read(L"forced utf-16 le skips its bom", LIT("\xff\xfeh\0i\0"), ENC_UTF16LE, WLIT(L"hi"), ENC_UTF16LE, -1);
    Read(L"forced utf-16 le without a bom", LIT("h\0i\0"), ENC_UTF16LE, WLIT(L"hi"), ENC_UTF16LE, -1);
    Read(L"forced utf-16 be skips its bom", LIT("\xfe\xff\0h\0i"), ENC_UTF16BE, WLIT(L"hi"), ENC_UTF16BE, -1);
    if (IsValidCodePage(1252)) Read(L"forced 1252 on utf-8 bytes (reopen with encoding)", LIT("\xc3\xa9"), 1252, WLIT(L"\u00c3\u00a9"), 1252, -1);
    Read(L"forced utf-8 on utf-8 bytes", LIT("\xc3\xa9"), ENC_UTF8, WLIT(L"\u00e9"), ENC_UTF8, -1);

    r = DocRead(g_missing, &t, &n, &e, &l, -1);
    Want(r == ERROR_FILE_NOT_FOUND, L"expected error 2, got %d", (int)r, 0);
    Want(t == NULL && n == 0, L"text / len not cleared", 0, 0);
    Done(L"missing file => ERROR_FILE_NOT_FOUND, no text");
    FilePut(g_file, LIT("abc"));
    t = NULL;
    r = DocRead(g_file, &t, &n, &e, &l, 12345);
    Want(r == ERR_BADCP, L"expected ERR_BADCP, got %d", (int)r, 0);
    Want(t == NULL, L"text not NULL", 0, 0);
    mem_free(t);
    Done(L"forced read with a code page that doesn't exist => ERR_BADCP");
    r = DocWrite(g_file, WLIT(L"abc"), 12345, EOL_CRLF, NULL);
    Int(L"write with a code page that doesn't exist => ERR_BADCP", (int)r, ERR_BADCP);
    wcopy(sub, g_missing, PATH_CAP);
    PathJoin(sub, L"x.txt", PATH_CAP);
    r = DocWrite(sub, WLIT(L"abc"), ENC_UTF8, EOL_CRLF, NULL);
    Want(r != 0, L"writing into a missing folder succeeded", 0, 0);
    Done(L"write into a missing folder returns an error");
}

static void TestDocEol(void)
{
    Group(L"doc.c line endings (majority wins, ties go to the first kind seen)");
    Read(L"eol lf majority", LIT("a\nb\nc\r\nd"), -1, WLIT(L"a\r\nb\r\nc\r\nd"), ENC_UTF8, EOL_LF);
    Read(L"eol crlf majority", LIT("a\r\nb\nc\r\nd\re"), -1, WLIT(L"a\r\nb\r\nc\r\nd\r\ne"), ENC_UTF8, EOL_CRLF);
    Read(L"eol cr majority", LIT("a\rb\rc\nd"), -1, WLIT(L"a\r\nb\r\nc\r\nd"), ENC_UTF8, EOL_CR);
    Read(L"eol tie lf / crlf, lf first", LIT("a\nb\r\nc"), -1, WLIT(L"a\r\nb\r\nc"), ENC_UTF8, EOL_LF);
    Read(L"eol tie crlf / lf, crlf first", LIT("a\r\nb\nc"), -1, WLIT(L"a\r\nb\r\nc"), ENC_UTF8, EOL_CRLF);
    Read(L"eol tie cr / lf, cr first", LIT("a\rb\nc"), -1, WLIT(L"a\r\nb\r\nc"), ENC_UTF8, EOL_CR);
    Read(L"eol tie lf / cr, lf first", LIT("a\nb\rc"), -1, WLIT(L"a\r\nb\r\nc"), ENC_UTF8, EOL_LF);
    Read(L"eol three-way tie, cr first", LIT("a\rb\nc\r\nd"), -1, WLIT(L"a\r\nb\r\nc\r\nd"), ENC_UTF8, EOL_CR);
    Read(L"eol cr right before crlf counts as two breaks", LIT("a\r\r\nb"), -1, WLIT(L"a\r\n\r\nb"), ENC_UTF8, EOL_CR);
    Read(L"eol lf then cr is two breaks", LIT("a\n\rb"), -1, WLIT(L"a\r\n\r\nb"), ENC_UTF8, EOL_LF);
    Read(L"eol none => crlf", LIT("abc"), -1, WLIT(L"abc"), ENC_UTF8, EOL_CRLF);
    Read(L"eol a lone lf", LIT("\n"), -1, WLIT(L"\r\n"), ENC_UTF8, EOL_LF);
    Read(L"eol a lone cr", LIT("\r"), -1, WLIT(L"\r\n"), ENC_UTF8, EOL_CR);
    Read(L"eol trailing crlf only", LIT("a\r\n"), -1, WLIT(L"a\r\n"), ENC_UTF8, EOL_CRLF);
    Read(L"eol mixed in utf-16 le", LIT("\xff\xfe" "a\0\n\0b\0\n\0c\0\r\0\n\0"), -1, WLIT(L"a\r\nb\r\nc\r\n"), ENC_UTF16LE, EOL_LF);
}

static void TestDocLossy(void)
{
    static const WCHAR lost[] = L"caf\u00e9 \u65e5", amacron[] = L"\u0100";
    WCHAR emo[8];
    BOOL lossy;
    DWORD r;
    BYTE *b;
    int n;

    Group(L"doc.c lossy writes (no best fit)");
    wcopy(emo, L"x", 8);
    wcat(emo, g_emoji, 8);
    if (!IsValidCodePage(1252)) { Skip(L"lossy 1252", L"code page 1252 not installed"); return; }
    FilePut(g_file, LIT("old"));
    lossy = FALSE;
    r = DocWrite(g_file, WLIT(lost), 1252, EOL_CRLF, &lossy);
    Want(r == ERR_LOSSY, L"expected ERR_LOSSY (1113), got %d", (int)r, 0);
    Want(lossy, L"*lossy wasn't set", 0, 0);
    b = FileGet(g_file, &n);
    WantBytes(L"the file after the refused write", b, n, LIT("old"));
    mem_free(b);
    Done(L"lossy 1252 write without permission => ERR_LOSSY, *lossy set, file untouched");
    lossy = TRUE;
    r = DocWrite(g_file, WLIT(lost), 1252, EOL_CRLF, &lossy);
    Want(r == 0, L"DocWrite returned %d", (int)r, 0);
    Want(lossy, L"*lossy should still report the loss", 0, 0);
    b = FileGet(g_file, &n);
    WantBytes(L"bytes", b, n, LIT("caf\xe9 ?"));
    mem_free(b);
    Done(L"lossy 1252 write with permission => written, '?' for the lost char");
    lossy = FALSE;
    r = DocWrite(g_file, WLIT(amacron), 1252, EOL_CRLF, &lossy);
    Want(r == ERR_LOSSY && lossy, L"U+0100 wasn't flagged lossy (best fit would turn it into 'A'): %d", (int)r, 0);
    lossy = TRUE;
    r = DocWrite(g_file, WLIT(amacron), 1252, EOL_CRLF, &lossy);
    b = FileGet(g_file, &n);
    WantBytes(L"bytes", b, n, LIT("?"));
    mem_free(b);
    Done(L"no best fit: U+0100 is lossy in 1252 and written as '?', not 'A'");
    r = DocWrite(g_file, WLIT(lost), 1252, EOL_CRLF, NULL);
    Int(L"lossy write with lossy == NULL => ERR_LOSSY", (int)r, ERR_LOSSY);
    lossy = TRUE;
    r = DocWrite(g_file, WLIT(L"caf\u00e9"), 1252, EOL_CRLF, &lossy);
    Want(r == 0, L"DocWrite returned %d", (int)r, 0);
    Want(!lossy, L"*lossy set for a lossless write", 0, 0);
    Done(L"lossless 1252 write clears *lossy");
    lossy = FALSE;
    r = DocWrite(g_file, emo, wlen(emo), ENC_ANSI, EOL_CRLF, &lossy);
    Want(r == ERR_LOSSY && lossy, L"expected ERR_LOSSY, got %d", (int)r, 0);
    Done(L"ansi + emoji => ERR_LOSSY");
    if (IsValidCodePage(932)) {
        lossy = FALSE;
        r = DocWrite(g_file, emo, wlen(emo), 932, EOL_CRLF, &lossy);
        Want(r == ERR_LOSSY && lossy, L"expected ERR_LOSSY, got %d", (int)r, 0);
        Done(L"932 + emoji => ERR_LOSSY");
    }
    lossy = FALSE;
    r = DocWrite(g_file, emo, wlen(emo), ENC_UTF8, EOL_CRLF, &lossy);
    Want(r == 0 && !lossy, L"utf-8: %d", (int)r, 0);
    r = DocWrite(g_file, emo, wlen(emo), ENC_UTF16BE, EOL_CRLF, &lossy);
    Want(r == 0 && !lossy, L"utf-16 be: %d", (int)r, 0);
    Done(L"utf-8 / utf-16 are never lossy");
}

/* DocEncodedSize (the status bar's "124b") is exactly the number of bytes DocWrite puts on disk: every encoding x every line ending x a few texts
 * (empty, ascii, breaks, accents + cjk, a surrogate pair, lone surrogates). the texts have CR LF breaks only, like the edit control's */
static void TestDocSize(void)
{
    static const WCHAR *const texts[] = {
        L"",
        L"abc",
        L"a\r\nb\r\n\r\nc",
        L"café 日本\ttab\r\nx\r\n",
        L"a\xD83D\xDE00" L"b\r\n",
        L"\xD83D lone high, \xDE00 lone low\r\n",
    };
    static const int encs[] = { ENC_UTF8, ENC_UTF8BOM, ENC_UTF16LE, ENC_UTF16BE, ENC_ANSI, 1252 };
    int t, e, l, checked = 0;
    Group(L"doc.c encoded size");
    for (t = 0; t < (int)COUNTOF(texts); t++)
        for (e = 0; e < (int)COUNTOF(encs); e++)
            for (l = 0; l < EOL_COUNT; l++) {
                BOOL lossy = TRUE;                                 /* what the code page lacks is written as '?': still one byte */
                int len = wlen(texts[t]), n = -1;
                DWORD want = DocEncodedSize(texts[t], len, encs[e], l), r;
                BYTE *b;
                WCHAR m[160];
                DeleteFileW(g_file);
                r = DocWrite(g_file, texts[t], len, encs[e], l, &lossy);
                b = r ? NULL : FileGet(g_file, &n);
                mem_free(b);
                wsprintfW(m, L"text %d, encoding %d, line ending %d: DocEncodedSize says %u bytes, DocWrite (returned %u) wrote %d", t, encs[e], l, want, r, n);
                Want(r == 0 && n >= 0 && (DWORD)n == want, m, 0, 0);
                checked++;
            }
    Want(checked == (int)COUNTOF(texts) * (int)COUNTOF(encs) * EOL_COUNT, L"the loops did not run %d x", checked, 0);
    Done(L"DocEncodedSize == the bytes DocWrite writes (6 texts x 6 encodings x 3 line endings)");
}

/* DocBodySize (the status bar's size of the SELECTION, "54 B") is DocEncodedSize without the byte order mark, and a text cut at a line break adds up:
 * the selection of a piece of the document is what the piece would take in the file */
static void TestDocBodySize(void)
{
    static const WCHAR *const texts[] = {
        L"",
        L"abc",
        L"a\r\nb\r\n\r\nc",
        L"café 日本\ttab\r\nx\r\n",
        L"a\xD83D\xDE00" L"b\r\n",
    };
    static const int encs[] = { ENC_UTF8, ENC_UTF8BOM, ENC_UTF16LE, ENC_UTF16BE, ENC_ANSI, 1252 };
    static const WCHAR whole[] = L"héllo 日本\r\nsecond line\r\n\r\nthird";
    static const WCHAR head[] = L"héllo 日本\r\n", tail[] = L"second line\r\n\r\nthird";
    int t, e, l, checked = 0;
    Group(L"doc.c selection size");
    for (t = 0; t < (int)COUNTOF(texts); t++)
        for (e = 0; e < (int)COUNTOF(encs); e++)
            for (l = 0; l < EOL_COUNT; l++) {
                int len = wlen(texts[t]);
                DWORD bom = (encs[e] == ENC_UTF16LE || encs[e] == ENC_UTF16BE) ? 2 : (encs[e] == ENC_UTF8BOM ? 3 : 0);
                WCHAR m[160];
                wsprintfW(m, L"text %d, encoding %d, line ending %d: the whole file is the mark (%u) + the body", t, encs[e], l, bom);
                Want(DocEncodedSize(texts[t], len, encs[e], l) == bom + DocBodySize(texts[t], len, encs[e], l), m, 0, 0);
                checked++;
            }
    Want(checked == (int)COUNTOF(texts) * (int)COUNTOF(encs) * EOL_COUNT, L"the loops did not run %d x", checked, 0);
    Done(L"DocEncodedSize == byte order mark + DocBodySize");
    Want(DocBodySize(L"abc", 3, ENC_UTF8BOM, EOL_CRLF) == 3, L"abc in utf-8 with a bom: the selection has no mark: %d", (int)DocBodySize(L"abc", 3, ENC_UTF8BOM, EOL_CRLF), 0);
    Want(DocBodySize(L"abc", 3, ENC_UTF16LE, EOL_CRLF) == 6, L"abc in utf-16: 6 bytes, no mark: %d", (int)DocBodySize(L"abc", 3, ENC_UTF16LE, EOL_CRLF), 0);
    Want(DocBodySize(L"a\r\nb", 4, ENC_UTF8, EOL_CRLF) == 4 && DocBodySize(L"a\r\nb", 4, ENC_UTF8, EOL_LF) == 3 && DocBodySize(L"a\r\nb", 4, ENC_UTF16BE, EOL_CR) == 6,
         L"a, a break, b: 4 bytes as crlf, 3 as lf, 3 utf-16 units as cr: %d", (int)DocBodySize(L"a\r\nb", 4, ENC_UTF8, EOL_CRLF), 0);
    Want(DocBodySize(L"", 0, ENC_UTF16LE, EOL_CRLF) == 0 && DocBodySize(L"", 0, ENC_UTF8BOM, EOL_LF) == 0, L"an empty piece has no bytes at all", 0, 0);
    Done(L"a selection has no byte order mark");
    for (e = 0; e < (int)COUNTOF(encs); e++)
        for (l = 0; l < EOL_COUNT; l++) {
            WCHAR m[160];
            wsprintfW(m, L"encoding %d, line ending %d: the pieces do not add up to the whole", encs[e], l);
            Want(DocBodySize(head, wlen(head), encs[e], l) + DocBodySize(tail, wlen(tail), encs[e], l) == DocBodySize(whole, wlen(whole), encs[e], l), m, 0, 0);
        }
    Done(L"the body of two pieces cut at a line break adds up to the body of the whole");
}

static void TestDocBig(void)
{
    WCHAR *t, line[64];
    int cap = 600000, n = 0, i, k;
    Group(L"doc.c big text");
    t = (WCHAR *)mem_alloc((size_t)cap * sizeof(WCHAR));
    if (!t) { Result(L"big text", L"out of memory"); return; }
    for (i = 0; n + 64 < cap; i++) {
        k = wsprintfW(line, L"line %d caf\u00e9 \u65e5\u672c\ttab\r\n", i);
        memcpy(t + n, line, (size_t)k * sizeof(WCHAR));
        n += k;
    }
    t[n] = 0;
    RoundTrip(L"roundtrip ~1 mb utf-8 / lf", t, n, ENC_UTF8, EOL_LF, -1, ENC_UTF8);
    RoundTrip(L"roundtrip ~1 mb utf-16 be / cr", t, n, ENC_UTF16BE, EOL_CR, -1, ENC_UTF16BE);
    mem_free(t);
}

static int SaveArtifacts(void)
{
    WCHAR pattern[PATH_CAP];
    WIN32_FIND_DATAW data;
    HANDLE f;
    int n = 0;
    wcopy(pattern, g_dir, PATH_CAP);
    PathJoin(pattern, L".mint-*", PATH_CAP);
    f = FindFirstFileW(pattern, &data);
    if (f != INVALID_HANDLE_VALUE) {
        do { n++; } while (FindNextFileW(f, &data));
        FindClose(f);
    }
    return n;
}

static void KeptOriginal(void)
{
    int n;
    BYTE *b = FileGet(g_file, &n);
    WantBytes(L"original bytes", b, n, LIT("original"));
    mem_free(b);
    Want(SaveArtifacts() == 0, L"a temporary or backup file was leaked", 0, 0);
}

static void TestDocSafety(void)
{
    static const struct { const char *bytes; int n, enc; } invalid[] = {
        { "\xef\xbb\xbf\xc0\xaf", 5, -1 },
        { "\xef\xbb\xbf\xe2\x82", 5, -1 },
        { "\xf4\x90\x80\x80", 4, ENC_UTF8 },
        { "\xff\xfe" "a", 3, -1 },
        { "\xfe\xff\0", 3, -1 },
        { "a\0b", 3, ENC_UTF16LE }
    };
    static const WCHAR unpaired[] = { 0xD83D, 'x', 0 };
    WCHAR *t, name[128], stream[PATH_CAP];
    FILETIME created = { 0x12340000, 0x01D00000 }, after = { 0, 0 };
    DWORD er, attrs;
    HANDLE f;
    int i, n, enc, eol;
    BOOL lossy;
    BYTE *b;

    Group(L"doc.c failed reads and durable saves");
    for (i = 0; i < COUNTOF(invalid); i++) {
        Want(FilePut(g_file, invalid[i].bytes, invalid[i].n), L"cannot create invalid input", 0, 0);
        er = DocRead(g_file, &t, &n, &enc, &eol, invalid[i].enc);
        Want(er == ERR_LOSSY && !t && n == 0, L"expected failed decode with no partial text, error %d", (int)er, 0);
        mem_free(t);
        wsprintfW(name, L"malformed explicit unicode input %d never becomes a clean partial document", i);
        Done(name);
    }
#ifdef DOC_IO_TEST
    for (i = 1; i <= 2; i++) {
        Want(FilePut(g_file, LIT("original")), L"cannot create original", 0, 0);
        DocTestFault(i);
        er = DocRead(g_file, &t, &n, &enc, &eol, -1);
        DocTestFault(0);
        Want(er == (DWORD)(i == 1 ? 30 : 38) && !t && n == 0, L"read fault returned %d or partial text", (int)er, 0);
        mem_free(t);
        KeptOriginal();
        Done(i == 1 ? L"failed ReadFile returns its error and no text" : L"unexpected EOF after a partial read returns an error and no text");
    }
    for (i = 3; i <= 7; i++) {
        Want(FilePut(g_file, LIT("original")), L"cannot create original", 0, 0);
        DocTestFault(i);
        er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
        DocTestFault(0);
        Want(er != 0, L"injected save fault %d reported success", i, 0);
        KeptOriginal();
        wsprintfW(name, L"save fault %d (write, zero write, flush, replace, partial rename) preserves original and cleans siblings", i);
        Done(name);
    }
    DeleteFileW(g_missing);
    DocTestFault(5);
    er = DocWrite(g_missing, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    DocTestFault(0);
    Want(er != 0 && GetFileAttributesW(g_missing) == INVALID_FILE_ATTRIBUTES, L"failed first save exposed a partial file", 0, 0);
    Want(SaveArtifacts() == 0, L"failed first save leaked a sibling", 0, 0);
    Done(L"failed flush on a new file never publishes it");

    DocTestFault(10);
    er = DocWrite(g_missing, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    DocTestFault(0);
    Want(er != 0 && GetFileAttributesW(g_missing) == INVALID_FILE_ATTRIBUTES, L"failed publication with stale error zero reported success", 0, 0);
    Want(SaveArtifacts() == 0, L"failed publication leaked a sibling", 0, 0);
    Done(L"failed publication remains a failure when last error is zero");
    DocTestFault(9);
    er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    DocTestFault(0);
    Want(er != 0, L"failed replacement with stale error zero reported success", 0, 0);
    KeptOriginal();
    Done(L"failed replacement remains a failure when last error is zero");

    DocTestFault(8);
    er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    DocTestFault(0);
    Want(er == 1177, L"partial replacement should return original error: %d", (int)er, 0);
    b = FileGet(g_file, &n);
    WantBytes(L"concurrent new file", b, n, LIT("raced"));
    mem_free(b);
    Want(SaveArtifacts() == 1, L"expected exactly one retained backup", 0, 0);
    {
        WIN32_FIND_DATAW data;
        wcopy(stream, g_dir, PATH_CAP); PathJoin(stream, L".mint-*.bak", PATH_CAP);
        f = FindFirstFileW(stream, &data);
        Want(f != INVALID_HANDLE_VALUE, L"only recovery copy was deleted", 0, 0);
        if (f != INVALID_HANDLE_VALUE) {
            FindClose(f);
            wcopy(stream, g_dir, PATH_CAP); PathJoin(stream, data.cFileName, PATH_CAP);
            b = FileGet(stream, &n);
            WantBytes(L"retained recovery copy", b, n, LIT("original"));
            mem_free(b);
            DeleteFileW(stream);
        }
    }
    Done(L"failed rollback retains original backup and never overwrites a concurrent file");
#endif
    Want(FilePut(g_file, LIT("original")), L"cannot create original", 0, 0);
    Want(SetFileAttributesW(g_file, FILE_ATTRIBUTE_READONLY), L"cannot set read-only attribute", 0, 0);
    er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    Want(er == 5, L"read-only save should fail with access denied: %d", (int)er, 0);
    Want((GetFileAttributesW(g_file) & FILE_ATTRIBUTE_READONLY) != 0, L"read-only attribute cleared", 0, 0);
    SetFileAttributesW(g_file, FILE_ATTRIBUTE_NORMAL);
    KeptOriginal();
    Done(L"read-only save keeps contents and protection");

    f = CreateFileW(g_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    Want(f != INVALID_HANDLE_VALUE, L"cannot hold a reader", 0, 0);
    er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    Want(er != 0, L"save bypassed a reader that forbids writes/deletion", 0, 0);
    KeptOriginal();
    Done(L"sharing violation preserves the original file");

    f = CreateFileW(g_file, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    Want(f != INVALID_HANDLE_VALUE && SetFileTime(f, &created, NULL, NULL), L"cannot set creation time", 0, 0);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    attrs = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    Want(SetFileAttributesW(g_file, attrs), L"cannot set original attributes", 0, 0);
    er = DocWrite(g_file, WLIT(L"replacement"), ENC_UTF8, EOL_CRLF, NULL);
    Want(er == 0, L"save returned %d", (int)er, 0);
    Want((GetFileAttributesW(g_file) & attrs) == attrs, L"original attributes were lost", 0, 0);
    SetFileAttributesW(g_file, FILE_ATTRIBUTE_NORMAL);
    f = CreateFileW(g_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    Want(f != INVALID_HANDLE_VALUE && GetFileTime(f, &after, NULL, NULL), L"cannot read creation time", 0, 0);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    Want(after.dwLowDateTime == created.dwLowDateTime && after.dwHighDateTime == created.dwHighDateTime, L"creation time changed", 0, 0);
    b = FileGet(g_file, &n);
    WantBytes(L"saved bytes", b, n, LIT("replacement"));
    mem_free(b);
    Want(SaveArtifacts() == 0, L"successful save leaked a sibling", 0, 0);
    Done(L"successful replacement preserves attributes and creation time");

    wcopy(stream, g_file, PATH_CAP); wcat(stream, L":mint-test", PATH_CAP);
    if (FilePut(stream, LIT("metadata"))) {
        er = DocWrite(g_file, WLIT(L"next"), ENC_UTF8, EOL_CRLF, NULL);
        Want(er == 0, L"save returned %d", (int)er, 0);
        b = FileGet(stream, &n);
        WantBytes(L"named stream", b, n, LIT("metadata"));
        mem_free(b);
        DeleteFileW(stream);
        Done(L"replacement retains existing named data streams");
    } else Skip(L"replacement retains existing named data streams", L"filesystem has no named streams");

    if (IsValidCodePage(50220)) {
        Want(FilePut(g_file, LIT("original")), L"cannot create original", 0, 0);
        lossy = FALSE;
        er = DocWrite(g_file, g_emoji, 2, 50220, EOL_CRLF, &lossy);
        Want(er == ERR_LOSSY && lossy, L"iso-2022-jp silently lost an emoji: error %d", (int)er, 0);
        KeptOriginal();
        Done(L"iso-2022-jp round-trip check rejects silent character loss");
        lossy = TRUE;
        er = DocWrite(g_file, g_emoji, 2, 50220, EOL_CRLF, &lossy);
        Want(er == 0 && lossy, L"confirmed iso-2022-jp conversion returned %d", (int)er, 0);
        Done(L"confirmed iso-2022-jp character loss is reported");
        RoundTrip(L"iso-2022-jp still saves representable Japanese losslessly", WLIT(L"\u65e5\u672c\r\nabc"), 50220, EOL_CRLF, 50220, 50220);
    }
    Want(FilePut(g_file, LIT("original")), L"cannot create original", 0, 0);
    lossy = FALSE;
    er = DocWrite(g_file, unpaired, 2, ENC_UTF8, EOL_CRLF, &lossy);
    Want(er == ERR_LOSSY && lossy, L"unpaired surrogate was silently replaced: %d", (int)er, 0);
    KeptOriginal();
    Done(L"utf-8 save requires permission to replace an unpaired surrogate");
    Int(L"unavailable code page size never underflows on a line ending", (int)DocBodySize(L"a\r\n", 3, 12345, EOL_LF), 0);
    Int(L"null text size is zero", (int)DocBodySize(NULL, 1, ENC_UTF8, EOL_LF), 0);
    Int(L"oversized save rejected before dereferencing text", (int)DocWrite(g_file, L"x", 0x7FFFFFFF, ENC_UTF8, EOL_LF, NULL), ERR_TOO_BIG);
    KeptOriginal();
    Done(L"bounds validation leaves destination untouched");
}

static void TestDoc(void)
{
    if (!TempDir()) {
        Result(L"temp folder", L"GetTempPathW / CreateDirectoryW failed: no doc.c tests");
        return;
    }
    Group(L"util.c isdir");
    FilePut(g_file, LIT("x"));
    Want(IsDir(g_dir), L"the temp folder isn't a dir", 0, 0);
    Want(!IsDir(g_file), L"a file is a dir", 0, 0);
    Want(!IsDir(g_missing), L"a missing path is a dir", 0, 0);
    Done(L"isdir");
    TestDocRoundTrips();
    TestDocCodePages();
    TestDocBytes();
    TestDocDetect();
    TestDocEol();
    TestDocLossy();
    TestDocSize();
    TestDocBodySize();
    TestDocBig();
    TestDocSafety();
    DeleteFileW(g_file);
    RemoveDirectoryW(g_dir);
}

/* ---------------------------------------------------------------- entry -- */
#include "search_extra.c"

/* ------------------------------------------------------------- assoc.c -- */
/* AssocRegisterUser against a throwaway key: HKEY_CURRENT_USER is redirected (RegOverridePredefKey, this process only) to
 * HKCU\Software\notepad-mint-unit-test, which is deleted afterwards. the real file associations of this pc are never touched */
static void TestAssoc(void)
{
    typedef LONG (WINAPI *CreateFn)(void *, LPCWSTR, DWORD, LPWSTR, DWORD, DWORD, void *, void **, DWORD *);
    typedef LONG (WINAPI *OverrideFn)(void *, void *);
    typedef LONG (WINAPI *GetFn)(void *, LPCWSTR, LPCWSTR, DWORD, DWORD *, void *, DWORD *);
    typedef LONG (WINAPI *TreeFn)(void *, LPCWSTR);
    typedef LONG (WINAPI *CloseFn)(void *);
    void *const hkcu = (void *)(ULONG_PTR)0x80000001;
    HMODULE m = LoadLibraryW(L"advapi32.dll");
    CreateFn create = m ? (CreateFn)GetProcAddress(m, "RegCreateKeyExW") : NULL;
    OverrideFn over = m ? (OverrideFn)GetProcAddress(m, "RegOverridePredefKey") : NULL;
    GetFn get = m ? (GetFn)GetProcAddress(m, "RegGetValueW") : NULL;
    TreeFn tree = m ? (TreeFn)GetProcAddress(m, "RegDeleteTreeW") : NULL;
    CloseFn close = m ? (CloseFn)GetProcAddress(m, "RegCloseKey") : NULL;
    void *k = NULL;
    WCHAR v[300];
    DWORD cb, type;
    int i, bad = 0;

    Group(L"assoc.c per-user registration (throwaway key)");
    if (!create || !over || !get || !tree || !close || create(hkcu, L"Software\\notepad-mint-unit-test", 0, NULL, 0, 0xF003F, NULL, &k, NULL)) {
        Result(L"throwaway registry key", L"advapi32 or RegCreateKeyExW failed");
        return;
    }
    over(hkcu, k);
    Want(AssocRegisterUser(L"C:\\a dir\\notepad-mint.exe"), L"AssocRegisterUser failed", 0, 0);
    Done(L"registers without an error");
    cb = sizeof v; v[0] = 0;
    get(hkcu, L"Software\\Classes\\" ASSOC_PROGID L"\\shell\\open\\command", NULL, 2, NULL, v, &cb);
    Str(L"open command: the exe quoted, then \"%1\"", v, L"\"C:\\a dir\\notepad-mint.exe\" \"%1\"");
    cb = sizeof v; v[0] = 0; type = 0;
    get(hkcu, L"Software\\Classes\\" ASSOC_PROGID L"\\DefaultIcon", NULL, 0x0000FFFF | 0x10000000, &type, v, &cb);   /* RRF_RT_ANY | RRF_NOEXPAND */
    Want(type == 2 && wcmp(v, L"%SystemRoot%\\system32\\imageres.dll,-102") == 0, L"icon type %d", (int)type, 0);
    Done(L"the file icon is windows' text icon, as REG_EXPAND_SZ");
    cb = sizeof v; v[0] = 0;
    get(hkcu, L"Software\\RegisteredApplications", ASSOC_REGAPP, 2, NULL, v, &cb);
    Str(L"RegisteredApplications points at the capabilities", v, ASSOC_CAPS);
    for (i = 0; i < ASSOC_EXT_COUNT; i++) {
        WCHAR key[96];
        wcopy(key, L"Software\\Classes\\", 96); wcat(key, g_assocExt[i], 96); wcat(key, L"\\OpenWithProgids", 96);
        cb = sizeof v; if (get(hkcu, key, ASSOC_PROGID, 2, NULL, v, &cb)) bad++;
        cb = sizeof v; v[0] = 0;
        if (get(hkcu, ASSOC_CAPS L"\\FileAssociations", g_assocExt[i], 2, NULL, v, &cb) || wcmp(v, ASSOC_PROGID)) bad++;
        cb = sizeof v; if (get(hkcu, L"Software\\Classes\\Applications\\notepad-mint.exe\\SupportedTypes", g_assocExt[i], 2, NULL, v, &cb)) bad++;
    }
    Int(L"every extension: an open-with entry, a capability and a supported type", bad, 0);
    cb = sizeof v; v[0] = 0;
    get(hkcu, L"Software\\Classes\\.txt", NULL, 2, NULL, v, &cb);
    Str(L"the extension's own default is not set (only the user picks it)", v, L"");
    over(hkcu, NULL);
    close(k);
    tree(hkcu, L"Software\\notepad-mint-unit-test");
    cb = sizeof v;
    Want(get(hkcu, L"Software\\notepad-mint-unit-test", NULL, 0x0000FFFF, NULL, v, &cb) != 0, L"throwaway key still there", 0, 0);
    Done(L"the throwaway key is gone again");
}

void start(void)
{
    WCHAR b[160];
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    wsprintfW(b, L"notepad mint unit tests (system ansi code page %u)\r\n", GetACP());
    Out(b);
    TestStrings();
    TestDocName();
    TestPaths();
    TestRt();
    TestFind();
    TestReplace();
    TestSearchExtra();
    TestAssoc();
    TestEncList();
    TestDoc();
    wsprintfW(b, L"\r\nunit: %d passed, %d failed, %d skipped\r\n", g_pass, g_fail, g_skip);
    Out(b);
    ExitProcess((UINT)(g_fail ? 1 : (g_pass ? 0 : 2)));
}
