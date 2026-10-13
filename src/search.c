#include "mp.h"

#define RESULT_MAX 0x3FFFFFFF
#define LOCAL_PATTERN 256

typedef struct {
    const WCHAR *raw;
    WCHAR *folded;
    int *failure;
    int patLen, up, matchCase;
    void *heap;
    WCHAR localText[LOCAL_PATTERN];
    int localFailure[LOCAL_PATTERN];
} SearchPattern;

static int High(WCHAR c) { return c >= 0xD800 && c <= 0xDBFF; }
static int Low(WCHAR c) { return c >= 0xDC00 && c <= 0xDFFF; }

static int WordChar(WCHAR c) {
    WORD type = 0;
    if (c < 128) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (High(c) || Low(c)) return 1;
    if (GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & (C1_ALPHA | C1_DIGIT))) return 1;
    return GetStringTypeW(CT_CTYPE3, &c, 1, &type) && (type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK));
}

static int Boundary(const WCHAR *text, int length, int start, int patLen, int wholeWord) {
    int end = start + patLen;
    if ((start > 0 && Low(text[start]) && High(text[start - 1])) || (end < length && High(text[end - 1]) && Low(text[end]))) return 0;
    return !wholeWord || ((start == 0 || !WordChar(text[start - 1])) && (end == length || !WordChar(text[end])));
}

static int Equal(WCHAR c, WCHAR folded, int matchCase) {
    if (c == folded) return 1;
    if (matchCase) return 0;
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A') == folded;
    if (c < 128 || (folded < 128 && c != 0x212A && c != 0x0130)) return 0;
    return wlow(c) == folded;
}

static void Prepare(SearchPattern *plan, const WCHAR *pattern, int patLen, int up, int matchCase) {
    int i, matched;
    plan->raw = pattern; plan->patLen = patLen; plan->up = up; plan->matchCase = matchCase; plan->heap = NULL;
    plan->folded = plan->localText; plan->failure = plan->localFailure;
    if (patLen > LOCAL_PATTERN) {
        if ((size_t)patLen <= ((size_t)-1) / (sizeof(int) + sizeof(WCHAR))) plan->heap = mem_alloc((size_t)patLen * (sizeof(int) + sizeof(WCHAR)));
        if (!plan->heap) { plan->folded = NULL; return; }
        plan->failure = (int *)plan->heap;
        plan->folded = (WCHAR *)(plan->failure + patLen);
    }
    for (i = 0; i < patLen; i++) plan->folded[i] = matchCase ? pattern[up ? patLen - 1 - i : i] : wlow(pattern[up ? patLen - 1 - i : i]);
    plan->failure[0] = 0;
    for (i = 1, matched = 0; i < patLen; i++) {
        while (matched && plan->folded[i] != plan->folded[matched]) matched = plan->failure[matched - 1];
        if (plan->folded[i] == plan->folded[matched]) matched++;
        plan->failure[i] = matched;
    }
}

static int Scan(const SearchPattern *plan, const WCHAR *text, int length, int from, int wholeWord) {
    int i, k, matched = 0, found, left, step = plan->up ? -1 : 1, skip;
    WCHAR first, firstUpper, firstSpecial;
    if (from < 0) from = 0;
    if (from > length) from = length;
    if (!plan->folded) {
        for (i = plan->up ? from - plan->patLen : from; i >= 0 && i <= length - plan->patLen; i += step) {
            for (k = 0; k < plan->patLen; k++) {
                WCHAR expected = plan->matchCase ? plan->raw[k] : wlow(plan->raw[k]);
                if (!Equal(text[i + k], expected, plan->matchCase)) break;
            }
            if (k == plan->patLen && Boundary(text, length, i, plan->patLen, wholeWord)) return i;
        }
        return -1;
    }
    i = plan->up ? from - 1 : from;
    left = plan->up ? from : length - from;
    first = plan->folded[0];
    firstUpper = !plan->matchCase && first >= 'a' && first <= 'z' ? (WCHAR)(first - 32) : first;
    firstSpecial = plan->matchCase ? first : first == 'k' ? 0x212A : first == 'i' ? 0x0130 : first;
    skip = !plan->up && (plan->matchCase || first < 128);
    while (left-- > 0) {
        WCHAR c;
        if (!matched && skip) {
            k = (int)mp_find3(text + i, (size_t)left + 1, first, firstUpper, firstSpecial);
            if (k > left) break;
            i += k; left -= k;
        }
        c = text[i];
        if (!matched && c != first && c != firstUpper && (plan->matchCase || c < 128 || (first < 128 && c != 0x212A && c != 0x0130) || wlow(c) != first)) goto next;
        while (matched && !Equal(text[i], plan->folded[matched], plan->matchCase)) matched = plan->failure[matched - 1];
        if (Equal(text[i], plan->folded[matched], plan->matchCase)) matched++;
        if (matched == plan->patLen) {
            found = plan->up ? i : i - plan->patLen + 1;
            if (Boundary(text, length, found, plan->patLen, wholeWord)) return found;
            matched = plan->failure[matched - 1];
        }
next:
        i += step;
    }
    return -1;
}

int FindInTextEx(const WCHAR *text, int length, const WCHAR *pattern, int patLen, int from, int up, int matchCase, int wholeWord) {
    SearchPattern plan;
    int found;
    if (patLen <= 0 || length < patLen || !text || !pattern) return -1;
    Prepare(&plan, pattern, patLen, up, matchCase);
    found = Scan(&plan, text, length, from, wholeWord);
    mem_free(plan.heap);
    return found;
}

WCHAR *ReplaceAllTextEx(const WCHAR *text, int length, const WCHAR *pattern, int patLen, const WCHAR *replacement, int replLen, int matchCase, int wholeWord, int *outLen, int *count) {
    SearchPattern plan;
    WCHAR *result = NULL, *dst;
    int resultLen, matches = 0, i = 0, found, valid;
    if (outLen) *outLen = 0;
    if (count) *count = 0;
    if (!text || length < 0) length = 0;
    if (!replacement || replLen < 0) { replacement = L""; replLen = 0; }
    if (length > RESULT_MAX) return NULL;
    resultLen = length;
    valid = pattern && patLen > 0 && patLen <= length;
    plan.heap = NULL;
    if (valid) {
        Prepare(&plan, pattern, patLen, 0, matchCase);
        for (; (found = Scan(&plan, text, length, i, wholeWord)) >= 0; i = found + patLen) {
            if (replLen > patLen && resultLen > RESULT_MAX - (replLen - patLen)) goto done;
            resultLen += replLen - patLen;
            matches++;
        }
    }
    result = (WCHAR *)mem_alloc(((size_t)resultLen + 1) * sizeof(WCHAR));
    if (!result) goto done;
    dst = result;
    for (i = 0; matches > 0 && (found = Scan(&plan, text, length, i, wholeWord)) >= 0; i = found + patLen) {
        memcpy(dst, text + i, (size_t)(found - i) * sizeof(WCHAR));
        dst += found - i;
        memcpy(dst, replacement, (size_t)replLen * sizeof(WCHAR));
        dst += replLen;
    }
    if (length > i) memcpy(dst, text + i, (size_t)(length - i) * sizeof(WCHAR));
    result[resultLen] = 0;
    if (outLen) *outLen = resultLen;
    if (count) *count = matches;
done:
    mem_free(plan.heap);
    return result;
}
