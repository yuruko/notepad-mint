/* util.c - tiny string / memory / path helpers (no crt) */
#include "mp.h"

static HANDLE g_heap;

#ifdef DBGLOG
/* debug builds only: append "tag a b" to dbg.log next to the exe (build\dbg.log) */
void Dbg(const WCHAR *tag, INT_PTR a, INT_PTR b)
{
    WCHAR line[256], exe[PATH_CAP], path[PATH_CAP];
    char u[600];
    HANDLE f;
    DWORD wr;
    int n;
    wsprintfW(line, L"%s %d %d\r\n", tag, (int)a, (int)b);
    n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, 600, NULL, NULL);
    GetModuleFileNameW(NULL, exe, PATH_CAP);
    PathDir(exe, path, PATH_CAP);
    PathJoin(path, L"dbg.log", PATH_CAP);
    f = CreateFileW(path, 4 /* FILE_APPEND_DATA */, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 4 /* OPEN_ALWAYS */, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, u, (DWORD)(n > 0 ? n - 1 : 0), &wr, NULL); CloseHandle(f); }
}
#endif

void *mem_alloc(size_t n)
{
    if (!g_heap) g_heap = GetProcessHeap();
    return HeapAlloc(g_heap, 0, n ? n : 1);
}

void *mem_zalloc(size_t n)
{
    if (!g_heap) g_heap = GetProcessHeap();
    return HeapAlloc(g_heap, HEAP_ZERO_MEMORY, n ? n : 1);
}

void *mem_realloc(void *p, size_t n)
{
    if (!p) return mem_alloc(n);
    return HeapReAlloc(g_heap, 0, p, n ? n : 1);
}

void mem_free(void *p)
{
    if (p) HeapFree(g_heap, 0, p);
}

int wlen(const WCHAR *s)
{
    const WCHAR *p = s;
    while (*p) p++;
    return (int)(p - s);
}

/* copies at most cap-1 chars and always terminates */
void wcopy(WCHAR *d, const WCHAR *s, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    for (; i < cap - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

void wcat(WCHAR *d, const WCHAR *s, int cap)
{
    int n = wlen(d);
    if (n < cap) wcopy(d + n, s, cap - n);
}

int wcmp(const WCHAR *a, const WCHAR *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

int wcmpi(const WCHAR *a, const WCHAR *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) - CSTR_EQUAL;
}

/* the lower case of every utf-16 unit, made on the first non-ascii one: a search that ignores case looks up each character of the text,
 * and the system call costs about 100 ns a character (seconds on a big cyrillic or greek file). CharLowerW of a string maps unit by unit like its
 * single-char form, except that a surrogate pair is mapped as one character: the surrogates stay out of the strings (they map to themselves) */
static WCHAR *g_low;

static void LowInit(void)
{
    WCHAR *t = (WCHAR *)mem_alloc(65537 * sizeof(WCHAR));
    int i;
    if (!t) return;
    for (i = 0; i < 65536; i++) t[i] = (WCHAR)i;
    t[0xD800] = 0; t[0x10000] = 0;                  /* (each range a nul terminated string) */
    CharLowerW(t + 128);
    CharLowerW(t + 0xE000);
    t[0xD800] = 0xD800;
    g_low = t;
}

WCHAR wlow(WCHAR c)
{
    if (c < 128) return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    if (!g_low) LowInit();
    return g_low ? g_low[c] : (WCHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)c);   /* (no memory for the table: the single-char form) */
}

int wtoi(const WCHAR *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        if (v < 100000000) v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* the default name of an unsaved document: "mint-" + four characters from 0-9 a-z (base 36, zero padded) of a number that only grows.
 * DocNameClock = the local date and time as DOCNAME_STEPS steps a day (45: one every 32 minutes) counted from midnight on 1 january 2000
 * (= 0000). only the last two digits of the year count, so 2100 starts at 0000 again: a century is at most 36525 days * 45 = 1643625 steps,
 * below 36^4 = 1679616, so it always fits four characters (today's names start with 9, the first character steps every 2.8 years). */
unsigned DocNameClock(const SYSTEMTIME *st)
{
    static const WORD before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    unsigned y = st->wYear % 100u, m = st->wMonth, d = st->wDay, sec;
    if (m < 1 || m > 12) m = 1;
    if (d < 1 || d > 31) d = 1;
    sec = ((unsigned)st->wHour * 3600u + (unsigned)st->wMinute * 60u + (unsigned)st->wSecond) % 86400u;
    return (y * 365u + (y + 3u) / 4u + before[m - 1] + (m > 2 && y % 4u == 0) + d - 1u) * DOCNAME_STEPS + sec / (86400u / DOCNAME_STEPS);
}

/* the next name: one more than the last one handed out while that is ahead of the clock (several documents within 32 minutes, or the clock
 * was set back), else the clock's. a last name more than DOCNAME_AHEAD steps ahead is stale (a new century, a clock that was far off): the clock wins */
unsigned DocNameNext(unsigned clock, unsigned last)
{
    unsigned next = last + 1u;                      /* (DOCNAME_NONE + 1 = 0: never ahead) */
    return next > clock && next - clock <= DOCNAME_AHEAD ? next : clock;
}

/* out needs room for 10 characters (cap < 10 gives an empty string) */
void DefaultDocName(unsigned v, WCHAR *out, int cap)
{
    static const WCHAR dig[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
    int i;
    if (cap < 10) { if (cap > 0) out[0] = 0; return; }
    v %= 1679616u;
    wcopy(out, L"mint-", cap);
    for (i = 8; i >= 5; i--) { out[i] = dig[v % 36u]; v /= 36u; }
    out[9] = 0;
}

const WCHAR *PathName(const WCHAR *path)
{
    const WCHAR *p = path, *last = path;
    for (; *p; p++)
        if (*p == '\\' || *p == '/') last = p + 1;
    return last;
}

/* directory part without the trailing slash (keeps "c:\" intact) */
void PathDir(const WCHAR *path, WCHAR *out, int cap)
{
    if (cap <= 0) return;
    const WCHAR *name = PathName(path);
    int n = (int)(name - path);
    if (n > 1 && !(n == 3 && path[1] == ':')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(out, path, (size_t)n * sizeof(WCHAR));
    out[n] = 0;
}

void PathJoin(WCHAR *dir, const WCHAR *name, int cap)
{
    int n = wlen(dir);
    if (n > 0 && dir[n - 1] != '\\') wcat(dir, L"\\", cap);
    wcat(dir, name, cap);
}

BOOL IsDir(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
