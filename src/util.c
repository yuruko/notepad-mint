#include "mp.h"

static HANDLE g_heap;

#ifdef DBGLOG
void Dbg(const WCHAR *tag, INT_PTR a, INT_PTR b) {
    WCHAR line[256], exe[PATH_CAP], path[PATH_CAP];
    char utf8[600];
    HANDLE file;
    DWORD bytesWritten;
    int utf8Len;
    wsprintfW(line, L"%s %d %d\r\n", tag, (int)a, (int)b);
    utf8Len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, 600, NULL, NULL);
    GetModuleFileNameW(NULL, exe, PATH_CAP);
    PathDir(exe, path, PATH_CAP);
    PathJoin(path, L"dbg.log", PATH_CAP);
    file = CreateFileW(path, 4 , FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 4 , FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) { WriteFile(file, utf8, (DWORD)(utf8Len > 0 ? utf8Len - 1 : 0), &bytesWritten, NULL); CloseHandle(file); }
}
#endif

void *mem_alloc(size_t size) {
    if (!g_heap) g_heap = GetProcessHeap();
    return HeapAlloc(g_heap, 0, size ? size : 1);
}

void *mem_zalloc(size_t size) {
    if (!g_heap) g_heap = GetProcessHeap();
    return HeapAlloc(g_heap, HEAP_ZERO_MEMORY, size ? size : 1);
}

void *mem_realloc(void *block, size_t size) {
    if (!block) return mem_alloc(size);
    return HeapReAlloc(g_heap, 0, block, size ? size : 1);
}

void mem_free(void *block) {
    if (block) HeapFree(g_heap, 0, block);
}

int ClampInt(int value, int lo, int hi) { return value < lo ? lo : (value > hi ? hi : value); }

int wlen(const WCHAR *text) {
    const WCHAR *end = text;
    while (*end) end++;
    return (int)(end - text);
}

void wcopy(WCHAR *dst, const WCHAR *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

void wcat(WCHAR *dst, const WCHAR *src, int cap) {
    int length = wlen(dst);
    if (length < cap) wcopy(dst + length, src, cap - length);
}

int wcmp(const WCHAR *a, const WCHAR *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

int wcmpi(const WCHAR *a, const WCHAR *b) {
    return CompareStringOrdinal(a, -1, b, -1, TRUE) - CSTR_EQUAL;
}

static WCHAR *g_low;

static void LowInit(void) {
    WCHAR *table = (WCHAR *)mem_alloc(65537 * sizeof(WCHAR));
    int i;
    if (!table) return;
    for (i = 0; i < 65536; i++) table[i] = (WCHAR)i;
    table[0xD800] = 0; table[0x10000] = 0;
    CharLowerW(table + 128);
    CharLowerW(table + 0xE000);
    table[0xD800] = 0xD800;
    g_low = table;
}

WCHAR wlow(WCHAR c) {
    if (c < 128) return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    if (!g_low) LowInit();
    return g_low ? g_low[c] : (WCHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)c);
}

int wtoi(const WCHAR *text) {
    int value = 0, neg = 0;
    while (*text == ' ' || *text == '\t') text++;
    if (*text == '-') { neg = 1; text++; }
    else if (*text == '+') text++;
    while (*text >= '0' && *text <= '9') {
        if (value < 100000000) value = value * 10 + (*text - '0');
        text++;
    }
    return neg ? -value : value;
}

static unsigned Leaps(unsigned year) { return year / 4u - year / 100u + year / 400u; }

unsigned DocNameClock(const SYSTEMTIME *time) {
    static const WORD before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    unsigned year = DOCNAME_EPOCH + ((unsigned)time->wYear + 100u * 100u - DOCNAME_EPOCH) % 100u, month = time->wMonth, day = time->wDay, daySeconds, days;
    if (month < 1 || month > 12) month = 1;
    if (day < 1 || day > 31) day = 1;
    daySeconds = ((unsigned)time->wHour * 3600u + (unsigned)time->wMinute * 60u + (unsigned)time->wSecond) % 86400u;
    days = (year - DOCNAME_EPOCH) * 365u + Leaps(year - 1u) - Leaps(DOCNAME_EPOCH - 1u) + before[month - 1] + day - 1u + (month > 2 && year % 4u == 0 && (year % 100u != 0 || year % 400u == 0));
    return days * DOCNAME_STEPS + daySeconds / (86400u / DOCNAME_STEPS);
}

unsigned DocNameNext(unsigned clock, unsigned last) {
    unsigned next = last + 1u;
    return next > clock && next - clock <= DOCNAME_AHEAD ? next : clock;
}

void DefaultDocName(unsigned value, WCHAR *out, int cap) {
    static const WCHAR dig[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
    int i;
    if (cap < 10) { if (cap > 0) out[0] = 0; return; }
    value %= 1679616u;
    wcopy(out, L"mint-", cap);
    for (i = 8; i >= 5; i--) { out[i] = dig[value % 36u]; value /= 36u; }
    out[9] = 0;
}

const WCHAR *PathName(const WCHAR *path) {
    const WCHAR *cursor = path, *last = path;
    for (; *cursor; cursor++) if (*cursor == '\\' || *cursor == '/') last = cursor + 1;
    return last;
}

void PathDir(const WCHAR *path, WCHAR *out, int cap) {
    if (cap <= 0) return;
    const WCHAR *name = PathName(path);
    int length = (int)(name - path);
    if (length > 1 && !(length == 3 && path[1] == ':')) length--;
    if (length >= cap) length = cap - 1;
    memcpy(out, path, (size_t)length * sizeof(WCHAR));
    out[length] = 0;
}

void PathJoin(WCHAR *dir, const WCHAR *name, int cap) {
    int length = wlen(dir);
    if (length > 0 && dir[length - 1] != '\\') wcat(dir, L"\\", cap);
    wcat(dir, name, cap);
}

WCHAR *TextToCrlf(const WCHAR *src, int maxLen, int *outLen) {
    int length = 0, extra = 0, i;
    WCHAR *out, *dst;
    while (length < maxLen && src[length]) length++;
    for (i = 0; i < length; i++) {
        if (src[i] == '\r' && (i + 1 >= length || src[i + 1] != '\n')) extra++;
        else if (src[i] == '\n' && (i == 0 || src[i - 1] != '\r')) extra++;
    }
    out = (WCHAR *)mem_alloc(((size_t)length + extra + 1) * sizeof(WCHAR));
    if (!out) return NULL;
    for (i = 0, dst = out; i < length; i++) {
        if (src[i] == '\n' && (i == 0 || src[i - 1] != '\r')) *dst++ = '\r';
        *dst++ = src[i];
        if (src[i] == '\r' && (i + 1 >= length || src[i + 1] != '\n')) *dst++ = '\n';
    }
    *dst = 0;
    if (outLen) *outLen = (int)(dst - out);
    return out;
}

void TextTrim(const WCHAR *oldText, int oldLen, const WCHAR *newText, int newLen, int limit, int *head, int *tail) {
    int small = oldLen < newLen ? oldLen : newLen, maxHead = limit >= 0 && limit < small ? limit : small, front = 0, back = 0;
    while (front < maxHead && oldText[front] == newText[front]) front++;
    if (front > 0 && ((oldText[front - 1] >= 0xD800 && oldText[front - 1] <= 0xDBFF) || (oldText[front - 1] == '\r' && ((front < oldLen && oldText[front] == '\n') || (front < newLen && newText[front] == '\n'))))) front--;
    while (back < small - front && oldText[oldLen - 1 - back] == newText[newLen - 1 - back]) back++;
    if (back > 0 && ((oldText[oldLen - back] >= 0xDC00 && oldText[oldLen - back] <= 0xDFFF) || (oldText[oldLen - back] == '\n' && ((oldLen - back >= 1 && oldText[oldLen - back - 1] == '\r') || (newLen - back >= 1 && newText[newLen - back - 1] == '\r'))))) back--;
    *head = front;
    *tail = back;
}

BOOL IsDir(const WCHAR *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}
