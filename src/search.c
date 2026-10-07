/* search.c - the text search behind find / replace. pure: no ui, no globals (unit tested from the mp.h contract) */
#include "mp.h"

#define RESULT_MAX 0x3FFFFFFF                   /* longest ReplaceAllText result: its byte size must fit in 32 bits */

/* t[0..m) == p[0..m)? case-insensitive = wlow on both sides */
static int Same(const WCHAR *t, const WCHAR *p, int m, int matchCase)
{
    int k;
    if (matchCase) return memcmp(t, p, (size_t)m * sizeof(WCHAR)) == 0;
    for (k = 0; k < m; k++)
        if (t[k] != p[k] && wlow(t[k]) != wlow(p[k])) return 0;
    return 1;
}

/* a match at t? f = the pattern's first char (lowered when !matchCase), fu = its ascii upper case twin */
static int At(const WCHAR *t, const WCHAR *pat, int m, WCHAR f, WCHAR fu, int matchCase)
{
    WCHAR c = t[0];
    if (c != f && c != fu && (matchCase || c < 128 || (f < 128 && c != 0x212A && c != 0x130) || wlow(c) != f)) return 0;     /* first char prefilter (no user32 call per char of a non-ascii text: the only non-ascii letters that lower to ascii are the kelvin sign and the dotted capital i) */
    return Same(t + 1, pat + 1, m - 1, matchCase);
}

int FindInText(const WCHAR *t, int n, const WCHAR *pat, int m, int from, int up, int matchCase)
{
    WCHAR f, fu;
    int i;
    if (m <= 0 || n < m || !t || !pat) return -1;
    if (from < 0) from = 0;
    if (from > n) from = n;
    f = matchCase ? pat[0] : wlow(pat[0]);
    fu = (!matchCase && f >= 'a' && f <= 'z') ? (WCHAR)(f - 32) : f;
    if (up) {
        for (i = from - m; i >= 0; i--) if (At(t + i, pat, m, f, fu, matchCase)) return i;
    } else {
        for (i = from; i <= n - m; i++) if (At(t + i, pat, m, f, fu, matchCase)) return i;
    }
    return -1;
}

/* non-overlapping, left to right. counts first (and sizes the result without overflowing an int), then allocates once */
WCHAR *ReplaceAllText(const WCHAR *t, int n, const WCHAR *pat, int m, const WCHAR *with, int wn, int matchCase,
                      int *outLen, int *count)
{
    WCHAR *r, *o;
    int len, k = 0, i, p;

    if (outLen) *outLen = 0;
    if (count) *count = 0;
    if (!t || n < 0) n = 0;
    if (!with || wn < 0) { with = L""; wn = 0; }
    len = n;
    for (i = 0; m > 0 && (p = FindInText(t, n, pat, m, i, 0, matchCase)) >= 0; i = p + m) {
        if (wn > m && len > RESULT_MAX - (wn - m)) return NULL;
        len += wn - m;
        k++;
    }
    if (len > RESULT_MAX) return NULL;
    r = (WCHAR *)mem_alloc(((size_t)len + 1) * sizeof(WCHAR));
    if (!r) return NULL;
    o = r;
    for (i = 0; k > 0 && (p = FindInText(t, n, pat, m, i, 0, matchCase)) >= 0; i = p + m) {
        memcpy(o, t + i, (size_t)(p - i) * sizeof(WCHAR));
        o += p - i;
        memcpy(o, with, (size_t)wn * sizeof(WCHAR));
        o += wn;
    }
    if (n > i) memcpy(o, t + i, (size_t)(n - i) * sizeof(WCHAR));
    r[len] = 0;
    if (outLen) *outLen = len;
    if (count) *count = k;
    return r;
}
