/* doc.c - reading / writing plain text: encoding detection + line ending handling.
 *
 * the edit control always holds utf-16 with CRLF line breaks, so DocRead normalises
 * whatever is on disk into that form (remembering the original encoding / line
 * ending) and DocWrite converts back to the encoding + line ending the user chose.
 * an encoding id is ENC_UTF8..ENC_ANSI (0..4) or, from ENC_CP_MIN up, a windows code page. */
#include "mp.h"

#define WC_NO_BEST_FIT_CHARS 0x00000400

const WCHAR *const g_encName[ENC_COUNT] = { L"utf-8", L"utf-8 with bom", L"utf-16 le", L"utf-16 be", L"ansi" };
const WCHAR *const g_eolName[EOL_COUNT] = { L"windows (crlf)", L"unix (lf)", L"macintosh (cr)" };
const WCHAR *const g_encShort[ENC_COUNT] = { L"utf8", L"utf8 bom", L"utf16 le", L"utf16 be", L"ansi" };         /* the status bar: as short as it gets, no hyphens */
const WCHAR *const g_eolShort[EOL_COUNT] = { L"crlf", L"lf", L"cr" };

/* ------------------------------------------------------ code page table -- */
typedef struct { int cp; const WCHAR *name, *desc; } CpInfo;

static const CpInfo g_cpTab[] = {
    { 1250,  L"windows-1250",   L"central european" },
    { 1251,  L"windows-1251",   L"cyrillic" },
    { 1252,  L"windows-1252",   L"western european" },
    { 1253,  L"windows-1253",   L"greek" },
    { 1254,  L"windows-1254",   L"turkish" },
    { 1255,  L"windows-1255",   L"hebrew" },
    { 1256,  L"windows-1256",   L"arabic" },
    { 1257,  L"windows-1257",   L"baltic" },
    { 1258,  L"windows-1258",   L"vietnamese" },
    { 874,   L"windows-874",    L"thai" },
    { 28591, L"iso-8859-1",     L"latin 1, western european" },
    { 28592, L"iso-8859-2",     L"latin 2, central european" },
    { 28593, L"iso-8859-3",     L"latin 3, south european" },
    { 28594, L"iso-8859-4",     L"latin 4, baltic" },
    { 28595, L"iso-8859-5",     L"cyrillic" },
    { 28596, L"iso-8859-6",     L"arabic" },
    { 28597, L"iso-8859-7",     L"greek" },
    { 28598, L"iso-8859-8",     L"hebrew, visual" },
    { 38598, L"iso-8859-8-i",   L"hebrew, logical" },
    { 28599, L"iso-8859-9",     L"latin 5, turkish" },
    { 28603, L"iso-8859-13",    L"latin 7, estonian" },
    { 28605, L"iso-8859-15",    L"latin 9" },
    { 932,   L"shift-jis",      L"japanese" },
    { 51932, L"euc-jp",         L"japanese" },
    { 50220, L"iso-2022-jp",    L"japanese" },
    { 936,   L"gbk",            L"simplified chinese" },
    { 54936, L"gb18030",        L"simplified chinese" },
    { 51936, L"euc-cn",         L"simplified chinese" },
    { 950,   L"big5",           L"traditional chinese" },
    { 949,   L"ks_c_5601-1987", L"korean" },
    { 51949, L"euc-kr",         L"korean" },
    { 1361,  L"johab",          L"korean" },
    { 50225, L"iso-2022-kr",    L"korean" },
    { 20866, L"koi8-r",         L"russian" },
    { 21866, L"koi8-u",         L"ukrainian" },
    { 437,   L"ibm437",         L"us dos" },
    { 720,   L"dos-720",        L"arabic dos" },
    { 737,   L"ibm737",         L"greek dos" },
    { 775,   L"ibm775",         L"baltic dos" },
    { 850,   L"ibm850",         L"western european dos" },
    { 852,   L"ibm852",         L"central european dos" },
    { 855,   L"ibm855",         L"cyrillic dos" },
    { 857,   L"ibm857",         L"turkish dos" },
    { 858,   L"ibm00858",       L"western european dos, euro" },
    { 860,   L"ibm860",         L"portuguese dos" },
    { 861,   L"ibm861",         L"icelandic dos" },
    { 862,   L"dos-862",        L"hebrew dos" },
    { 863,   L"ibm863",         L"canadian french dos" },
    { 864,   L"ibm864",         L"arabic dos" },
    { 865,   L"ibm865",         L"nordic dos" },
    { 866,   L"cp866",          L"cyrillic dos" },
    { 869,   L"ibm869",         L"greek dos" },
    { 10000, L"macintosh",      L"mac roman" },
    { 10029, L"x-mac-ce",       L"mac central european" },
    { 10007, L"x-mac-cyrillic", L"mac cyrillic" },
    { 10006, L"x-mac-greek",    L"mac greek" },
    { 10081, L"x-mac-turkish",  L"mac turkish" },
    { 10079, L"x-mac-icelandic", L"mac icelandic" },
    { 20127, L"us-ascii",       L"7-bit ascii" },
    { 65000, L"utf-7",          L"unicode (utf-7)" },
};
#define NCP COUNTOF(g_cpTab)

static const CpInfo *CpFind(int cp)
{
    int i;
    for (i = 0; i < (int)NCP; i++)
        if (g_cpTab[i].cp == cp) return &g_cpTab[i];
    return NULL;
}

void EncLabel(int enc, WCHAR *out, int cap)
{
    const CpInfo *c;
    WCHAR buf[48];
    if (enc >= 0 && enc < ENC_COUNT) { wcopy(out, g_encName[enc], cap); return; }
    c = CpFind(enc);
    if (c) { wcopy(out, c->name, cap); return; }
    wsprintfW(buf, L"code page %d", enc);
    wcopy(out, buf, cap);
}

/* the status bar's text for any encoding id: the short names, a code page by its name without the hyphens ("windows1252", "shiftjis";
 * "cp 1234" when the table has none) */
void EncShort(int enc, WCHAR *out, int cap)
{
    const CpInfo *c;
    WCHAR buf[24];
    int i, n = 0;
    if (enc >= 0 && enc < ENC_COUNT) { wcopy(out, g_encShort[enc], cap); return; }
    c = CpFind(enc);
    if (c) {
        for (i = 0; c->name[i] && n < cap - 1; i++) if (c->name[i] != '-') out[n++] = c->name[i];
        out[n] = 0;
        return;
    }
    wsprintfW(buf, L"cp %d", enc);
    wcopy(out, buf, cap);
}

int EncListCount(void) { return ENC_COUNT + (int)NCP; }

int EncListGet(int i, WCHAR *label, int cap)
{
    if (i < ENC_COUNT) {
        wcopy(label, i == ENC_ANSI ? L"ansi  system default code page" : g_encName[i], cap);
        return i;
    }
    i -= ENC_COUNT;
    if (i >= (int)NCP) i = (int)NCP - 1;
    wcopy(label, g_cpTab[i].name, cap);
    wcat(label, L"  ", cap);
    wcat(label, g_cpTab[i].desc, cap);
    return g_cpTab[i].cp;
}

/* utf-8 / utf-16 / 8-bit: which codepage does MultiByteToWideChar use? */
static UINT CpOf(int enc)
{
    if (enc == ENC_UTF8 || enc == ENC_UTF8BOM) return CP_UTF8;
    if (enc == ENC_ANSI) return CP_ACP;
    return (UINT)enc;
}

/* code pages whose converters reject lpUsedDefaultChar / flags */
static BOOL CpHasDefaultChar(UINT cp)
{
    if (cp == CP_UTF8 || cp == 65000 || cp == 54936 || cp == 42) return FALSE;
    if (cp >= 50220 && cp <= 50229) return FALSE;
    if (cp >= 57002 && cp <= 57011) return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------ detection -- */
/* strict utf-8 check (no overlongs, no surrogates, <= U+10FFFF) */
static BOOL Utf8Valid(const BYTE *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        BYTE c = p[i];
        int need, k;
        unsigned cp;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF)      { need = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; }
        else return FALSE;
        if (i + need >= n) return FALSE;
        for (k = 1; k <= need; k++) {
            if ((p[i + k] & 0xC0) != 0x80) return FALSE;
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if (need == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return FALSE;
        if (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return FALSE;
        i += (size_t)need + 1;
    }
    return TRUE;
}

/* bom-less utf-16 that is mostly ascii: every other byte is zero. 0 = no, 1 = le, 2 = be */
static int Utf16Guess(const BYTE *p, size_t n)
{
    size_t lim = n < 4096 ? n : 4096, i, ze = 0, zo = 0, pairs = lim / 2;
    if (lim < 4) return 0;
    for (i = 0; i + 1 < lim; i += 2) {
        if (!p[i]) ze++;
        if (!p[i + 1]) zo++;
    }
    if (zo * 4 >= pairs && ze == 0) return 1;
    if (ze * 4 >= pairs && zo == 0) return 2;
    return 0;
}

/* ----------------------------------------------------------------- read -- */
/* forceEnc >= 0 skips detection ("reopen with encoding"); *enc reports what was used */
DWORD DocRead(const WCHAR *path, WCHAR **text, int *len, int *enc, int *eol, int forceEnc)
{
    HANDLE f;
    __int64 sz;
    BYTE *buf;
    DWORD rd;
    size_t n, got, off = 0, i, nCRLF = 0, nLF = 0, nCR = 0, nNul = 0, first = 0, outn;
    WCHAR *w, *out;
    int wn, e, k;
    BOOL bom8, bomLE, bomBE;

    *text = NULL; *len = 0; *enc = ENC_UTF8; *eol = EOL_CRLF;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx(f, &sz)) { DWORD er = GetLastError(); CloseHandle(f); return er; }
    if (sz > 0x30000000) { CloseHandle(f); return ERR_TOO_BIG; }
    n = (size_t)sz;
    buf = (BYTE *)mem_alloc(n + 4);
    if (!buf) { CloseHandle(f); return ERR_NOMEM; }
    for (got = 0; got < n; got += rd) {
        if (!ReadFile(f, buf + got, (DWORD)(n - got), &rd, NULL) || rd == 0) break;
    }
    CloseHandle(f);
    n = got;
    buf[n] = buf[n + 1] = buf[n + 2] = buf[n + 3] = 0;

    /* 1. which encoding? */
    bom8  = (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF);
    bomLE = (n >= 2 && buf[0] == 0xFF && buf[1] == 0xFE);
    bomBE = (n >= 2 && buf[0] == 0xFE && buf[1] == 0xFF);
    if (forceEnc >= 0) {
        e = forceEnc;
        if (e == ENC_UTF8 || e == ENC_UTF8BOM) { e = bom8 ? ENC_UTF8BOM : ENC_UTF8; if (bom8) off = 3; }
        else if (e == ENC_UTF16LE && bomLE) off = 2;
        else if (e == ENC_UTF16BE && bomBE) off = 2;
    } else {
        e = ENC_UTF8;
        if (bom8) { e = ENC_UTF8BOM; off = 3; }
        else if (bomLE) { e = ENC_UTF16LE; off = 2; }
        else if (bomBE) { e = ENC_UTF16BE; off = 2; }
        else {
            k = Utf16Guess(buf, n);
            if (k == 1) e = ENC_UTF16LE;
            else if (k == 2) e = ENC_UTF16BE;
            else if (!Utf8Valid(buf, n)) e = ENC_ANSI;
        }
    }

    /* 2. to utf-16 */
    if (e == ENC_UTF16LE || e == ENC_UTF16BE) {
        wn = (int)((n - off) / 2);
        w = (WCHAR *)mem_alloc(((size_t)wn + 1) * sizeof(WCHAR));
        if (w) {
            memcpy(w, buf + off, (size_t)wn * sizeof(WCHAR));
            if (e == ENC_UTF16BE)
                for (k = 0; k < wn; k++) w[k] = (WCHAR)((w[k] << 8) | (w[k] >> 8));
        }
    } else {
        UINT cp = CpOf(e);
        int src = (int)(n - off);
        w = (WCHAR *)mem_alloc(((size_t)src + 1) * sizeof(WCHAR));  /* one conversion, no size query: no code page makes more characters than it has bytes */
        wn = (w && src) ? MultiByteToWideChar(cp, 0, (LPCSTR)(buf + off), src, w, src) : 0;
        if (w && src && !wn && GetLastError() == 122) {              /* (ERROR_INSUFFICIENT_BUFFER: one that does: ask for the size) */
            wn = MultiByteToWideChar(cp, 0, (LPCSTR)(buf + off), src, NULL, 0);
            mem_free(w);
            if (!wn) { mem_free(buf); return ERR_BADCP; }
            w = (WCHAR *)mem_alloc(((size_t)wn + 1) * sizeof(WCHAR));
            if (w) wn = MultiByteToWideChar(cp, 0, (LPCSTR)(buf + off), src, w, wn);
        }
        if (w && src && !wn) { mem_free(w); mem_free(buf); return ERR_BADCP; }     /* code page not installed */
        if (w && (size_t)wn < (size_t)src) { WCHAR *s = (WCHAR *)mem_realloc(w, ((size_t)wn + 1) * sizeof(WCHAR)); if (s) w = s; }   /* (multi-byte text: give the unused room back) */
    }
    mem_free(buf);
    if (!w) return ERR_NOMEM;
    w[wn] = 0;

    /* 3. count line breaks, then normalise to CRLF (and blank out nul characters) */
    for (i = 0; i < (size_t)wn; i++) {
        WCHAR c = w[i];
        if (c == '\r') {
            if (i + 1 < (size_t)wn && w[i + 1] == '\n') { nCRLF++; i++; if (!first) first = 1; }
            else { nCR++; if (!first) first = 3; }
        } else if (c == '\n') {
            nLF++; if (!first) first = 2;
        } else if (c == 0) {
            nNul++;
        }
    }
    if (nLF + nCR + nNul == 0) {
        out = w; outn = (size_t)wn;                                   /* already clean */
    } else {
        outn = (size_t)wn + nLF + nCR;
        out = (WCHAR *)mem_alloc((outn + 1) * sizeof(WCHAR));
        if (!out) { mem_free(w); return ERR_NOMEM; }
        {
            WCHAR *d = out;
            for (i = 0; i < (size_t)wn; i++) {
                WCHAR c = w[i];
                if (c == '\r') {
                    *d++ = '\r'; *d++ = '\n';
                    if (i + 1 < (size_t)wn && w[i + 1] == '\n') i++;
                } else if (c == '\n') {
                    *d++ = '\r'; *d++ = '\n';
                } else {
                    *d++ = c ? c : ' ';
                }
            }
        }
        mem_free(w);
    }
    out[outn] = 0;

    /* majority line ending wins; ties go to whichever kind came first; none => windows */
    {
        int best = EOL_CRLF;
        size_t bc = nCRLF;
        if (nLF > bc) { best = EOL_LF; bc = nLF; }
        if (nCR > bc) { best = EOL_CR; bc = nCR; }
        if (first == 1 && nCRLF == bc) best = EOL_CRLF;
        else if (first == 2 && nLF == bc) best = EOL_LF;
        else if (first == 3 && nCR == bc) best = EOL_CR;
        *eol = best;
    }
    *text = out; *len = (int)outn; *enc = e;
    return 0;
}

/* ---------------------------------------------------------------- write -- */
/* rewrite line breaks (any of CRLF / LF / CR) as the requested style. allocates (*owned = 1), except when nothing has to change (CRLF wanted and every
 * break already is one: the editor's own text): then it hands back `t` itself and the caller must not free it */
static WCHAR *EolConvert(const WCHAR *t, int len, int eol, int *outLen, int *owned)
{
    size_t el = (eol == EOL_CRLF) ? 2 : 1, i, breaks = 0, consumed = 0, total;
    WCHAR *out, *d;
    for (i = 0; i < (size_t)len; i++) {
        if (t[i] == '\r') {
            breaks++; consumed++;
            if (i + 1 < (size_t)len && t[i + 1] == '\n') { i++; consumed++; }
        } else if (t[i] == '\n') {
            breaks++; consumed++;
        }
    }
    total = (size_t)len - consumed + breaks * el;
    if (t && eol == EOL_CRLF && consumed == breaks * 2) { *outLen = len; *owned = 0; return (WCHAR *)t; }
    *owned = 1;
    out = (WCHAR *)mem_alloc((total + 1) * sizeof(WCHAR));
    if (!out) return NULL;
    d = out;
    for (i = 0; i < (size_t)len; i++) {
        WCHAR c = t[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < (size_t)len && t[i + 1] == '\n') i++;
            if (eol == EOL_CRLF) { *d++ = '\r'; *d++ = '\n'; }
            else *d++ = (eol == EOL_LF) ? '\n' : '\r';
        } else {
            *d++ = c;
        }
    }
    *d = 0;
    *outLen = (int)total;
    return out;
}

/* the size in bytes DocWrite would produce for this text (the byte order mark not counted), without converting anything into a buffer: the status bar
 * shows it, for the document and for the selection. `text` is what the edit control holds (every line break CR LF); a LF / CR file has one byte
 * (unit) less per break. the code page conversions are asked for their length only, with the same flags DocWrite uses, so the number is exact
 * (a lossy '?' is still one byte) */
DWORD DocBodySize(const WCHAR *text, int len, int enc, int eol)
{
    size_t breaks = len > 0 ? mp_count_lf(text, (size_t)len) : 0;
    size_t cut = (eol == EOL_CRLF) ? 0 : breaks;
    if (len < 0) len = 0;
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) return (DWORD)(((size_t)len - cut) * 2);
    {
        UINT cp = CpOf(enc);
        DWORD fl = CpHasDefaultChar(cp) ? WC_NO_BEST_FIT_CHARS : 0;
        size_t need = len ? (size_t)WideCharToMultiByte(cp, fl, text, len, NULL, 0, NULL, NULL) : 0;
        return (DWORD)(need - cut);
    }
}

/* a whole file: the byte order mark (utf-8 with bom 3 bytes, utf-16 2) + its text. a piece of the text, like the selection, has no mark: DocBodySize */
DWORD DocEncodedSize(const WCHAR *text, int len, int enc, int eol)
{
    DWORD bom = (enc == ENC_UTF16LE || enc == ENC_UTF16BE) ? 2 : (enc == ENC_UTF8BOM ? 3 : 0);
    return bom + DocBodySize(text, len, enc, eol);
}

/* *lossy: in = "allowed to lose characters", out = "characters were (or would be) lost" */
DWORD DocWrite(const WCHAR *path, const WCHAR *text, int len, int enc, int eol, BOOL *lossy)
{
    WCHAR *w;
    BYTE *out = NULL;
    size_t on = 0, done;
    int wn = 0, k, own = 0;
    BOOL allow = lossy ? *lossy : FALSE, used = FALSE;
    HANDLE f;
    DWORD wr;

    if (lossy) *lossy = FALSE;
    w = EolConvert(text, len < 0 ? 0 : len, eol, &wn, &own);
    if (!w) return ERR_NOMEM;

    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        on = 2 + (size_t)wn * 2;
        out = (BYTE *)mem_alloc(on);
        if (out) {
            WCHAR *o = (WCHAR *)out;
            o[0] = (enc == ENC_UTF16LE) ? 0xFEFF : 0xFFFE;
            for (k = 0; k < wn; k++)
                o[1 + k] = (enc == ENC_UTF16LE) ? w[k] : (WCHAR)((w[k] << 8) | (w[k] >> 8));
        }
    } else if (CpOf(enc) == CP_UTF8) {                        /* utf-8 loses nothing: one conversion, into the worst case of 3 bytes per unit */
        size_t bom = (enc == ENC_UTF8BOM) ? 3 : 0;
        int need = 0;
        out = (BYTE *)mem_alloc(bom + (size_t)wn * 3 + 1);
        if (out) {
            if (bom) { out[0] = 0xEF; out[1] = 0xBB; out[2] = 0xBF; }
            need = wn ? WideCharToMultiByte(CP_UTF8, 0, w, wn, (LPSTR)(out + bom), wn * 3, NULL, NULL) : 0;
            if (wn && !need) { mem_free(out); if (own) mem_free(w); return ERR_BADCP; }
            on = bom + (size_t)need;
        }
    } else {
        UINT cp = CpOf(enc);
        size_t bom = 0;
        BOOL probe = CpHasDefaultChar(cp);
        DWORD fl = probe ? WC_NO_BEST_FIT_CHARS : 0;      /* no best-fit guessing: anything not 1:1 becomes '?' + flags loss */
        int need = wn ? WideCharToMultiByte(cp, fl, w, wn, NULL, 0, NULL, NULL) : 0;

        if (wn && !need) { if (own) mem_free(w); return ERR_BADCP; }
        on = bom + (size_t)need;
        out = (BYTE *)mem_alloc(on + 1);
        if (out) {
            if (bom) { out[0] = 0xEF; out[1] = 0xBB; out[2] = 0xBF; }
            if (need) WideCharToMultiByte(cp, fl, w, wn, (LPSTR)(out + bom), need, NULL, probe ? &used : NULL);
        }
    }
    if (own) mem_free(w);
    if (!out) return ERR_NOMEM;
    if (used) {
        if (lossy) *lossy = TRUE;
        if (!allow) { mem_free(out); return ERR_LOSSY; }
    }

    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { DWORD er = GetLastError(); mem_free(out); return er; }
    for (done = 0; done < on; done += wr) {
        if (!WriteFile(f, out + done, (DWORD)(on - done), &wr, NULL) || wr == 0) {
            DWORD er = GetLastError();
            CloseHandle(f);
            mem_free(out);
            return er ? er : 29;
        }
    }
    CloseHandle(f);
    mem_free(out);
    return 0;
}
