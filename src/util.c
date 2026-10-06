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

WCHAR wlow(WCHAR c)
{
    if (c < 128) return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    return (WCHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)c);   /* single-char form */
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

static int hexval(WCHAR c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "rrggbb", "#rrggbb", "rgb" */
BOOL ParseColor(const WCHAR *s, COLORREF *out)
{
    int v[6], n = 0;
    while (*s == ' ') s++;
    if (*s == '#') s++;
    while (n < 6 && hexval(*s) >= 0) v[n++] = hexval(*s++);
    while (*s == ' ') s++;
    if (*s) return FALSE;
    if (n == 3) {
        *out = RGB(v[0] * 17, v[1] * 17, v[2] * 17);
        return TRUE;
    }
    if (n == 6) {
        *out = RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
        return TRUE;
    }
    return FALSE;
}

void FormatColor(COLORREF c, WCHAR *out)
{
    static const WCHAR hx[] = L"0123456789abcdef";
    BYTE b[3];
    int i;
    b[0] = GetRValue(c); b[1] = GetGValue(c); b[2] = GetBValue(c);
    for (i = 0; i < 3; i++) {
        out[i * 2]     = hx[b[i] >> 4];
        out[i * 2 + 1] = hx[b[i] & 15];
    }
    out[6] = 0;
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
