/* Reproducible 1.0.3/current comparisons; no CRT, same optimization flags as the app. */
#include "mp.h"

API HANDLE WINAPI GetStdHandle(DWORD);
size_t mp_count_lf_before(const WCHAR *, size_t);
typedef size_t (*CountFn)(const WCHAR *, size_t);
typedef int (*FindFn)(const WCHAR *, int, const WCHAR *, int, int, int, int);
static HANDLE g_out;
static volatile int g_result;

static void Out(const WCHAR *s)
{
    char bytes[512];
    DWORD wrote;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, bytes, sizeof bytes, NULL, NULL);
    if (n > 1) WriteFile(g_out, bytes, (DWORD)(n - 1), &wrote, NULL);
}

/* The prior bounded direct scan, including its first-character filter. */
static int FindBefore(const WCHAR *t, int n, const WCHAR *p, int m, int from, int up, int mc)
{
    WCHAR f = mc ? p[0] : wlow(p[0]), fu = !mc && f >= 'a' && f <= 'z' ? (WCHAR)(f - 32) : f;
    int i, k, step = up ? -1 : 1;
    for (i = up ? from - m : from; i >= 0 && i <= n - m; i += step) {
        WCHAR c = t[i];
        if (c != f && c != fu && (mc || c < 128 || (f < 128 && c != 0x212A && c != 0x130) || wlow(c) != f)) continue;
        if (mc) { if (!memcmp(t + i + 1, p + 1, (size_t)(m - 1) * sizeof(WCHAR))) return i; }
        else {
            for (k = 1; k < m; k++) if (t[i + k] != p[k] && wlow(t[i + k]) != wlow(p[k])) break;
            if (k == m) return i;
        }
    }
    return -1;
}

static void CountTime(const WCHAR *label, CountFn fn, const WCHAR *t, int n, int want)
{
    WCHAR line[160];
    DWORD start = GetTickCount(), elapsed;
    int loops = 0;
    do { g_result = (int)fn(t, (size_t)n); loops++; elapsed = GetTickCount() - start; } while (elapsed < 300);
    if (g_result != want) { Out(L"FAIL newline count mismatch\r\n"); ExitProcess(1); }
    wsprintfW(line, L"%s: %d us/scan (%d calls, %u ms)\r\n", label, MulDiv((int)elapsed, 1000, loops), loops, elapsed);
    Out(line);
}

/* 1.0.9's case folding: the system call for every non-ascii unit (util.c now looks the units up in a table) */
static WCHAR LowSys(WCHAR c)
{
    if (c < 128) return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    return (WCHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)c);
}

static int FindSysLower(const WCHAR *t, int n, const WCHAR *p, int m, int from, int up, int mc)
{
    WCHAR f = mc ? p[0] : LowSys(p[0]), fu = !mc && f >= 'a' && f <= 'z' ? (WCHAR)(f - 32) : f;
    int i, k;
    (void)up;
    for (i = from; i <= n - m; i++) {
        WCHAR c = t[i];
        if (c != f && c != fu && (mc || c < 128 || (f < 128 && c != 0x212A && c != 0x130) || LowSys(c) != f)) continue;
        for (k = 1; k < m; k++) if (t[i + k] != p[k] && LowSys(t[i + k]) != LowSys(p[k])) break;
        if (k == m) return i;
    }
    return -1;
}

static void FindTime(const WCHAR *label, FindFn fn, const WCHAR *t, int n, const WCHAR *pat, int m, int mc)
{
    WCHAR line[160];
    DWORD start = GetTickCount(), elapsed;
    int loops = 0;
    do { g_result = fn(t, n, pat, m, 0, 0, mc); loops++; elapsed = GetTickCount() - start; } while (elapsed < 300);
    if (g_result != -1) { Out(L"FAIL missing pattern matched\r\n"); ExitProcess(1); }
    wsprintfW(line, L"%s: %d us/scan (%d calls, %u ms)\r\n", label, MulDiv((int)elapsed, 1000, loops), loops, elapsed);
    Out(line);
}

void start(void)
{
    static const int density[] = { 0, 80, 2, 1 };
    WCHAR *text = (WCHAR *)mem_alloc(2000000), pattern[256], label[96];
    int d, i, want, mc;
    g_out = GetStdHandle((DWORD)-11);
    if (!text) { Out(L"allocation failed\r\n"); ExitProcess(1); }
    Out(L"1,000,000 UTF-16 units, unaligned pointer; elapsed batches >=300 ms\r\n");
    for (d = 0; d < COUNTOF(density); d++) {
        for (want = 0, i = 0; i < 1000000; i++) { text[i] = density[d] && i % density[d] == 0 ? '\n' : 'a'; if (i && text[i] == '\n') want++; }
        wsprintfW(label, L"count before, density 1/%d", density[d]); CountTime(label, mp_count_lf_before, text + 1, 999999, want);
        wsprintfW(label, L"count after, density 1/%d", density[d]); CountTime(label, mp_count_lf, text + 1, 999999, want);
    }
    for (i = 0; i < 1000000; i++) text[i] = 'a';
    for (i = 0; i < 256; i++) pattern[i] = i == 255 ? 'b' : 'a';
    for (mc = 0; mc <= 1; mc++) {
        wsprintfW(label, L"find before repeated prefix, case %d", mc); FindTime(label, FindBefore, text, 1000000, pattern, 256, mc);
        wsprintfW(label, L"find after repeated prefix, case %d", mc); FindTime(label, FindInText, text, 1000000, pattern, 256, mc);
        wsprintfW(label, L"find before ordinary miss, case %d", mc); FindTime(label, FindBefore, text, 1000000, L"missing", 7, mc);
        wsprintfW(label, L"find after ordinary miss, case %d", mc); FindTime(label, FindInText, text, 1000000, L"missing", 7, mc);
    }
    {                                                   /* realistic text: the first letter of the pattern turns up every 44 units */
        static const WCHAR fox[] = L"the quick brown fox jumps over the lazy dog ";
        for (i = 0; i < 1000000; i++) text[i] = fox[i % 44];
        for (mc = 0; mc <= 1; mc++) {
            wsprintfW(label, L"find before english miss, case %d", mc); FindTime(label, FindBefore, text, 1000000, L"missing", 7, mc);
            wsprintfW(label, L"find after english miss, case %d", mc); FindTime(label, FindInText, text, 1000000, L"missing", 7, mc);
        }
    }
    {                                                   /* cyrillic, ignoring case: 1.0.9 called the system for every unit */
        static const WCHAR yabloko[] = { 0x44F, 0x431, 0x43B, 0x43E, 0x43A, 0x43E, 0 };
        for (i = 0; i < 1000000; i++) text[i] = (WCHAR)(i % 33 == 32 ? ' ' : 0x430 + i % 33);
        FindTime(L"find 1.0.9 cyrillic miss ignoring case (system lower case per unit)", FindSysLower, text, 1000000, yabloko, 6, 0);
        FindTime(L"find now cyrillic miss ignoring case (lower case table)", FindInText, text, 1000000, yabloko, 6, 0);
    }
    mem_free(text);
    ExitProcess(0);
}
