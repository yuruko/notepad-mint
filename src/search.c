/* search.c - bounded UTF-16 find / replace; no ui or mutable global state. */
#include "mp.h"

#define RESULT_MAX 0x3FFFFFFF /* result including its terminator must fit in 32-bit byte arithmetic */
#define LOCAL_PATTERN 256

typedef struct {
    const WCHAR *raw;
    WCHAR *folded;
    int *failure;
    int m, up, matchCase;
    void *heap;
    WCHAR localText[LOCAL_PATTERN];
    int localFailure[LOCAL_PATTERN];
} SearchPattern;

static int High(WCHAR c) { return c >= 0xD800 && c <= 0xDBFF; }
static int Low(WCHAR c) { return c >= 0xDC00 && c <= 0xDFFF; }

static int WordChar(WCHAR c)
{
    WORD type = 0;
    if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    /* NLS classifies UTF-16 units, not supplementary code points. Keeping both
     * surrogate kinds in words avoids inventing a boundary beside an unknown letter. */
    if (High(c) || Low(c)) return 1;
    if (GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & (C1_ALPHA | C1_DIGIT))) return 1;
    return GetStringTypeW(CT_CTYPE3, &c, 1, &type) && (type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK));
}

/* Always use the complete document: slicing at the selection invents word boundaries. */
static int Boundary(const WCHAR *t, int n, int at, int m, int wholeWord)
{
    int end = at + m;
    if ((at > 0 && Low(t[at]) && High(t[at - 1])) || (end < n && High(t[end - 1]) && Low(t[end]))) return 0;
    return !wholeWord || ((at == 0 || !WordChar(t[at - 1])) && (end == n || !WordChar(t[end])));
}

/* Compare against a pre-folded pattern unit. ASCII needles can reject non-ASCII
 * units without a user32 call except for the two lowercase mappings to ASCII. */
static int Equal(WCHAR c, WCHAR f, int matchCase)
{
    if (c == f) return 1;
    if (matchCase) return 0;
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A') == f;
    if (c < 128 || (f < 128 && c != 0x212A && c != 0x0130)) return 0;
    return wlow(c) == f;
}

/* KMP failure links make repeated-prefix searches linear. Ordinary dialog
 * patterns fit on the stack; replace-all prepares once for both passes. */
static void Prepare(SearchPattern *p, const WCHAR *pat, int m, int up, int matchCase)
{
    int i, q;
    p->raw = pat; p->m = m; p->up = up; p->matchCase = matchCase; p->heap = NULL;
    p->folded = p->localText; p->failure = p->localFailure;
    if (m > LOCAL_PATTERN) {
        if ((size_t)m <= ((size_t)-1) / (sizeof(int) + sizeof(WCHAR)))
            p->heap = mem_alloc((size_t)m * (sizeof(int) + sizeof(WCHAR)));
        if (!p->heap) { p->folded = NULL; return; } /* bounded direct scan still works under memory pressure */
        p->failure = (int *)p->heap;
        p->folded = (WCHAR *)(p->failure + m);
    }
    for (i = 0; i < m; i++) p->folded[i] = matchCase ? pat[up ? m - 1 - i : i] : wlow(pat[up ? m - 1 - i : i]);
    p->failure[0] = 0;
    for (i = 1, q = 0; i < m; i++) {
        while (q && p->folded[i] != p->folded[q]) q = p->failure[q - 1];
        if (p->folded[i] == p->folded[q]) q++;
        p->failure[i] = q;
    }
}

static int Scan(const SearchPattern *p, const WCHAR *t, int n, int from, int wholeWord)
{
    int i, k, q = 0, at, left, step = p->up ? -1 : 1;
    WCHAR f, fu;
    if (from < 0) from = 0;
    if (from > n) from = n;
    if (!p->folded) {
        for (i = p->up ? from - p->m : from; i >= 0 && i <= n - p->m; i += step) {
            for (k = 0; k < p->m; k++) {
                WCHAR f = p->matchCase ? p->raw[k] : wlow(p->raw[k]);
                if (!Equal(t[i + k], f, p->matchCase)) break;
            }
            if (k == p->m && Boundary(t, n, i, p->m, wholeWord)) return i;
        }
        return -1;
    }
    i = p->up ? from - 1 : from;
    left = p->up ? from : n - from;
    f = p->folded[0];
    fu = !p->matchCase && f >= 'a' && f <= 'z' ? (WCHAR)(f - 32) : f;
    while (left-- > 0) {
        WCHAR c = t[i];
        if (!q && c != f && c != fu && (p->matchCase || c < 128 || (f < 128 && c != 0x212A && c != 0x0130) || wlow(c) != f)) goto next;
        while (q && !Equal(t[i], p->folded[q], p->matchCase)) q = p->failure[q - 1];
        if (Equal(t[i], p->folded[q], p->matchCase)) q++;
        if (q == p->m) {
            at = p->up ? i : i - p->m + 1;
            if (Boundary(t, n, at, p->m, wholeWord)) return at;
            q = p->failure[q - 1];
        }
next:
        i += step;
    }
    return -1;
}

int FindInTextEx(const WCHAR *t, int n, const WCHAR *pat, int m, int from, int up, int matchCase, int wholeWord)
{
    SearchPattern p;
    int at;
    if (m <= 0 || n < m || !t || !pat) return -1;
    Prepare(&p, pat, m, up, matchCase);
    at = Scan(&p, t, n, from, wholeWord);
    mem_free(p.heap);
    return at;
}

int FindInText(const WCHAR *t, int n, const WCHAR *pat, int m, int from, int up, int matchCase)
{
    return FindInTextEx(t, n, pat, m, from, up, matchCase, 0);
}

/* Non-overlapping, left to right. Size first without overflowing, allocate once. */
WCHAR *ReplaceAllTextEx(const WCHAR *t, int n, const WCHAR *pat, int m, const WCHAR *with, int wn, int matchCase,
                       int wholeWord, int *outLen, int *count)
{
    SearchPattern plan;
    WCHAR *r = NULL, *o;
    int len, k = 0, i = 0, p, valid;

    if (outLen) *outLen = 0;
    if (count) *count = 0;
    if (!t || n < 0) n = 0;
    if (!with || wn < 0) { with = L""; wn = 0; }
    if (n > RESULT_MAX) return NULL;
    len = n;
    valid = pat && m > 0 && m <= n;
    plan.heap = NULL;
    if (valid) {
        Prepare(&plan, pat, m, 0, matchCase);
        for (; (p = Scan(&plan, t, n, i, wholeWord)) >= 0; i = p + m) {
            if (wn > m && len > RESULT_MAX - (wn - m)) goto done;
            len += wn - m;
            k++;
        }
    }
    r = (WCHAR *)mem_alloc(((size_t)len + 1) * sizeof(WCHAR));
    if (!r) goto done;
    o = r;
    for (i = 0; k > 0 && (p = Scan(&plan, t, n, i, wholeWord)) >= 0; i = p + m) {
        memcpy(o, t + i, (size_t)(p - i) * sizeof(WCHAR));
        o += p - i;
        memcpy(o, with, (size_t)wn * sizeof(WCHAR));
        o += wn;
    }
    if (n > i) memcpy(o, t + i, (size_t)(n - i) * sizeof(WCHAR));
    r[len] = 0;
    if (outLen) *outLen = len;
    if (count) *count = k;
done:
    mem_free(plan.heap);
    return r;
}

WCHAR *ReplaceAllText(const WCHAR *t, int n, const WCHAR *pat, int m, const WCHAR *with, int wn, int matchCase,
                      int *outLen, int *count)
{
    return ReplaceAllTextEx(t, n, pat, m, with, wn, matchCase, 0, outLen, count);
}
