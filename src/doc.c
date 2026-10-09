/* doc.c - reading / writing plain text: encoding detection + line ending handling.
 *
 * the edit control always holds utf-16 with CRLF line breaks, so DocRead normalises
 * whatever is on disk into that form (remembering the original encoding / line
 * ending) and DocWrite converts back to the encoding + line ending the user chose.
 * an encoding id is ENC_UTF8..ENC_ANSI (0..4) or, from ENC_CP_MIN up, a windows code page. */
#include "mp.h"

#define WC_NO_BEST_FIT_CHARS 0x00000400
#define WC_ERR_INVALID_CHARS 0x00000080
#define DOC_MAX_CHARS 0x3FFFFFFE

/* Faults exercise real file cleanup in the unit build; production calls Win32 directly. */
#ifdef DOC_IO_TEST
static int g_ioFault, g_ioCalls;
void DocTestFault(int fault) { g_ioFault = fault; g_ioCalls = 0; }
static BOOL DocReadFile(HANDLE f, LPVOID p, DWORD n, DWORD *rd, void *overlap)
{
    if (g_ioFault == 1) { *rd = 0; SetLastError(30); return FALSE; }
    if (g_ioFault == 2) {
        if (g_ioCalls++) { *rd = 0; return TRUE; }
        if (n > 1) n = 1;
    }
    return ReadFile(f, p, n, rd, overlap);
}
static BOOL DocWriteFile(HANDLE f, LPCVOID p, DWORD n, DWORD *wr, void *overlap)
{
    if (g_ioFault == 3) {
        WriteFile(f, p, n ? 1 : 0, wr, overlap);
        SetLastError(29); return FALSE;
    }
    if (g_ioFault == 4) { *wr = 0; SetLastError(0); return TRUE; }
    return WriteFile(f, p, n, wr, overlap);
}
static BOOL DocFlushFile(HANDLE f)
{
    if (g_ioFault == 5) { SetLastError(29); return FALSE; }
    return FlushFileBuffers(f);
}
static BOOL DocReplaceFile(LPCWSTR path, LPCWSTR temp, LPCWSTR backup)
{
    if (g_ioFault == 6) { SetLastError(5); return FALSE; }
    if (g_ioFault == 7 || g_ioFault == 8) {
        if (!DeleteFileW(backup) || !MoveFileExW(path, backup, MOVEFILE_WRITE_THROUGH)) return FALSE;
        if (g_ioFault == 8) {
            HANDLE other = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            DWORD written;
            if (other != INVALID_HANDLE_VALUE) { WriteFile(other, "raced", 5, &written, NULL); CloseHandle(other); }
        }
        SetLastError(1177); return FALSE;
    }
    if (g_ioFault == 9) { SetLastError(0); return FALSE; }
    return ReplaceFileW(path, temp, backup, 0, NULL, NULL);
}
static BOOL DocMoveFile(LPCWSTR from, LPCWSTR to)
{
    if (g_ioFault == 10) { SetLastError(0); return FALSE; }
    return MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH);
}
#else
#define DocReadFile ReadFile
#define DocWriteFile WriteFile
#define DocFlushFile FlushFileBuffers
#define DocReplaceFile(path, temp, backup) ReplaceFileW(path, temp, backup, 0, NULL, NULL)
#define DocMoveFile(from, to) MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH)
#endif

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
    if (cap <= 0) return;
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
    if (i < 0) i = 0;
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
    if (enc == ENC_ANSI) return GetACP();
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
/* a file that is not valid in its encoding (a broken utf-8 sequence after a bom, or an odd number of bytes for utf-16) fails with ERR_LOSSY:
 * it never becomes a clean document by accident. after the user agreed (main.c OpenDoc) DocAllowLossy(TRUE) opens it anyway, the bad bytes
 * as U+FFFD (what a save then writes) */
static BOOL g_lossyOk;
void DocAllowLossy(BOOL on) { g_lossyOk = on; }

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
    BOOL bom8, bomLE, bomBE, validUtf8 = FALSE;

    *text = NULL; *len = 0; *enc = ENC_UTF8; *eol = EOL_CRLF;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx(f, &sz)) { DWORD er = GetLastError(); CloseHandle(f); return er; }
    if (sz < 0 || sz > 0x30000000) { CloseHandle(f); return ERR_TOO_BIG; }
    n = (size_t)sz;
    buf = (BYTE *)mem_alloc(n + 4);
    if (!buf) { CloseHandle(f); return ERR_NOMEM; }
    for (got = 0; got < n; got += rd) {
        if (!DocReadFile(f, buf + got, (DWORD)(n - got), &rd, NULL)) {
            DWORD er = GetLastError();
            CloseHandle(f); mem_free(buf); return er ? er : 30; /* ERROR_READ_FAULT */
        }
        if (!rd || rd > n - got) { CloseHandle(f); mem_free(buf); return 38; } /* ERROR_HANDLE_EOF: never expose a truncated document */
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
            else validUtf8 = TRUE;                        /* detection already checked these exact bytes */
        }
    }

    /* 2. to utf-16 */
    if (e == ENC_UTF16LE || e == ENC_UTF16BE) {
        int odd = (int)((n - off) & 1);
        if (odd && !g_lossyOk) { mem_free(buf); return ERR_LOSSY; }
        wn = (int)((n - off) / 2);
        w = (WCHAR *)mem_alloc(((size_t)wn + 2) * sizeof(WCHAR));
        if (w) {
            memcpy(w, buf + off, (size_t)wn * sizeof(WCHAR));
            if (e == ENC_UTF16BE)
                for (k = 0; k < wn; k++) w[k] = (WCHAR)((w[k] << 8) | (w[k] >> 8));
            if (odd) w[wn++] = 0xFFFD;                    /* (the last byte on its own) */
        }
    } else {
        UINT cp = CpOf(e);
        int src = (int)(n - off);
        if (cp == CP_UTF8 && !validUtf8 && !g_lossyOk && !Utf8Valid(buf + off, n - off)) { mem_free(buf); return ERR_LOSSY; }   /* (allowed: MultiByteToWideChar without MB_ERR_INVALID_CHARS makes U+FFFD of them) */
        w = (WCHAR *)mem_alloc(((size_t)src + 1) * sizeof(WCHAR));  /* one conversion, no size query: no code page makes more characters than it has bytes */
        wn = (w && src) ? MultiByteToWideChar(cp, 0, (LPCSTR)(buf + off), src, w, src) : 0;
        if (w && src && !wn && GetLastError() == 122) {              /* (ERROR_INSUFFICIENT_BUFFER: one that does: ask for the size) */
            wn = MultiByteToWideChar(cp, 0, (LPCSTR)(buf + off), src, NULL, 0);
            mem_free(w);
            if (!wn) { mem_free(buf); return ERR_BADCP; }
            if (wn > DOC_MAX_CHARS) { mem_free(buf); return ERR_TOO_BIG; }
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
    for (i = mp_find3(w, (size_t)wn, '\r', '\n', 0); i < (size_t)wn; i += 1 + mp_find3(w + i + 1, (size_t)wn - i - 1, '\r', '\n', 0)) {   /* (sse2: from one break to the next) */
        WCHAR c = w[i];
        if (c == '\r') {
            if (i + 1 < (size_t)wn && w[i + 1] == '\n') { nCRLF++; i++; if (!first) first = 1; }
            else { nCR++; if (!first) first = 3; }
        } else if (c == '\n') {
            nLF++; if (!first) first = 2;
        } else {
            nNul++;
        }
    }
    if (nLF + nCR + nNul == 0) {
        out = w; outn = (size_t)wn;                                   /* already clean */
    } else {
        outn = (size_t)wn + nLF + nCR;
        if (outn > DOC_MAX_CHARS) { mem_free(w); return ERR_TOO_BIG; }
        out = (WCHAR *)mem_alloc((outn + 1) * sizeof(WCHAR));
        if (!out) { mem_free(w); return ERR_NOMEM; }
        {
            WCHAR *d = out;
            for (i = 0; i < (size_t)wn; i++) {
                size_t k = mp_find3(w + i, (size_t)wn - i, '\r', '\n', 0);   /* the run up to the next break or nul in one copy */
                WCHAR c;
                memcpy(d, w + i, k * sizeof(WCHAR));
                d += k; i += k;
                if (i >= (size_t)wn) break;
                c = w[i];
                if (c == '\r') {
                    *d++ = '\r'; *d++ = '\n';
                    if (i + 1 < (size_t)wn && w[i + 1] == '\n') i++;
                } else if (c == '\n') {
                    *d++ = '\r'; *d++ = '\n';
                } else {
                    *d++ = ' ';
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
    if (total > DOC_MAX_CHARS) { *outLen = -1; return NULL; }
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
    size_t cut;
    if (!text || len <= 0) return 0;
    cut = (eol == EOL_CRLF) ? 0 : mp_count_lf(text, (size_t)len);
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) return (DWORD)(((size_t)len - cut) * 2);
    {
        UINT cp = CpOf(enc);
        DWORD fl = CpHasDefaultChar(cp) ? WC_NO_BEST_FIT_CHARS : 0;
        size_t need = (size_t)WideCharToMultiByte(cp, fl, text, len, NULL, 0, NULL, NULL);
        return need >= cut ? (DWORD)(need - cut) : 0; /* an unavailable converter reports zero, not an unsigned wraparound */
    }
}

/* a whole file: the byte order mark (utf-8 with bom 3 bytes, utf-16 2) + its text. a piece of the text, like the selection, has no mark: DocBodySize */
DWORD DocEncodedSize(const WCHAR *text, int len, int enc, int eol)
{
    DWORD bom = (enc == ENC_UTF16LE || enc == ENC_UTF16BE) ? 2 : (enc == ENC_UTF8BOM ? 3 : 0);
    return bom + DocBodySize(text, len, enc, eol);
}

/* Reserve a sibling with CREATE_NEW: no truncation, predictable-name overwrite or cross-volume move.
 * The directory, rather than the complete target name, leaves room for maximum-length file names. */
static HANDLE SaveSibling(const WCHAR *full, WCHAR *out, const WCHAR *extension)
{
    static DWORD serial;
    const WCHAR *name = PathName(full);
    size_t prefix = (size_t)(name - full);
    HANDLE f;
    DWORD er;
    int attempt;
    memcpy(out, full, prefix * sizeof(WCHAR));
    for (attempt = 0; attempt < 100; attempt++) {
        wsprintfW(out + prefix, L".mint-%08x-%08x-%08x.%s", GetCurrentThreadId(), GetTickCount(), ++serial, extension);
        f = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) return f;
        er = GetLastError();
        if (er != 80 && er != 183) return INVALID_HANDLE_VALUE; /* only a name collision may be retried */
    }
    SetLastError(80);
    return INVALID_HANDLE_VALUE;
}

/* Write and flush a complete sibling before replacing the destination. ReplaceFile preserves its ACL,
 * creation time and named streams. Its documented partial-rename errors require a backup name: without
 * one, ERROR_UNABLE_TO_MOVE_REPLACEMENT can remove the original. Keep recovery data if rollback fails. */
static DWORD SaveBytes(const WCHAR *path, const BYTE *out, size_t on)
{
    WCHAR full[PATH_CAP], temp[PATH_CAP + 64], backup[PATH_CAP + 64];
    HANDLE f = INVALID_HANDLE_VALUE, guard = INVALID_HANDLE_VALUE;
    DWORD size, attrs, er = 0, wr, basic;
    size_t done;
    BOOL existed, haveTemp = FALSE, haveBackup = FALSE;

    size = GetFullPathNameW(path, PATH_CAP, full, NULL);
    if (!size) return GetLastError();
    if (size >= PATH_CAP) return 206; /* ERROR_FILENAME_EXCED_RANGE */
    attrs = GetFileAttributesW(full);
    existed = attrs != INVALID_FILE_ATTRIBUTES;
    if (!existed) {
        er = GetLastError();
        if (er != ERROR_FILE_NOT_FOUND) return er;
    } else {
        if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) return 5;
        /* Respect write permissions and existing readers that forbid writes/deletion. */
        guard = CreateFileW(full, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (guard == INVALID_HANDLE_VALUE) return GetLastError();
    }
    er = 0;
    f = SaveSibling(full, temp, L"tmp");
    if (f == INVALID_HANDLE_VALUE) { er = GetLastError(); goto finish; }
    haveTemp = TRUE;
    for (done = 0; done < on; done += wr) {
        if (!DocWriteFile(f, out + done, (DWORD)(on - done), &wr, NULL)) { er = GetLastError(); if (!er) er = 29; goto finish; }
        if (!wr || wr > on - done) { er = 29; goto finish; }
    }
    if (!DocFlushFile(f)) { er = GetLastError(); if (!er) er = 29; goto finish; }
    if (!CloseHandle(f)) { er = GetLastError(); f = INVALID_HANDLE_VALUE; goto finish; }
    f = INVALID_HANDLE_VALUE;
    if (existed) {
        basic = attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        if (!SetFileAttributesW(temp, basic ? basic : FILE_ATTRIBUTE_NORMAL)) { er = GetLastError(); goto finish; }
        f = SaveSibling(full, backup, L"bak");
        if (f == INVALID_HANDLE_VALUE) { er = GetLastError(); goto finish; }
        haveBackup = TRUE;
        if (!CloseHandle(f)) { er = GetLastError(); f = INVALID_HANDLE_VALUE; goto finish; }
        f = INVALID_HANDLE_VALUE;
        if (!DocReplaceFile(full, temp, backup)) {
            er = GetLastError();
            if (!er) er = 29;
            if (er == 1177) {
                /* The old file was moved to backup. Never overwrite a new file created by another process. */
                if (!MoveFileExW(backup, full, MOVEFILE_WRITE_THROUGH)) haveBackup = FALSE;
            }
            goto finish;
        }
    } else if (!DocMoveFile(temp, full)) {
        er = GetLastError();
        if (!er) er = 29;
        goto finish;
    }
    haveTemp = FALSE;
finish:
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (guard != INVALID_HANDLE_VALUE) CloseHandle(guard);
    if (haveTemp) DeleteFileW(temp);
    if (haveBackup) DeleteFileW(backup);
    return er;
}

/* *lossy: in = "allowed to lose characters", out = "characters were (or would be) lost" */
DWORD DocWrite(const WCHAR *path, const WCHAR *text, int len, int enc, int eol, BOOL *lossy)
{
    WCHAR *w;
    BYTE *out = NULL;
    size_t on = 0;
    int wn = 0, k, own = 0;
    BOOL allow = lossy ? *lossy : FALSE, used = FALSE;
    DWORD er;

    if (lossy) *lossy = FALSE;
    if (!text && len > 0) return ERR_BADCP;
    if (len > DOC_MAX_CHARS) return ERR_TOO_BIG;
    w = EolConvert(text, len < 0 ? 0 : len, eol, &wn, &own);
    if (!w) return wn < 0 ? ERR_TOO_BIG : ERR_NOMEM;

    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        on = 2 + (size_t)wn * 2;
        out = (BYTE *)mem_alloc(on);
        if (out) {
            WCHAR *o = (WCHAR *)out;
            o[0] = (enc == ENC_UTF16LE) ? 0xFEFF : 0xFFFE;
            for (k = 0; k < wn; k++)
                o[1 + k] = (enc == ENC_UTF16LE) ? w[k] : (WCHAR)((w[k] << 8) | (w[k] >> 8));
        }
    } else if (CpOf(enc) == CP_UTF8) {                        /* one conversion, into the worst case of 3 bytes per unit */
        size_t bom = (enc == ENC_UTF8BOM) ? 3 : 0;
        int need = 0;
        if (wn > 0x2AAAAAAA) { if (own) mem_free(w); return ERR_TOO_BIG; }
        out = (BYTE *)mem_alloc(bom + (size_t)wn * 3 + 1);
        if (out) {
            if (bom) { out[0] = 0xEF; out[1] = 0xBB; out[2] = 0xBF; }
            need = wn ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, wn, (LPSTR)(out + bom), wn * 3, NULL, NULL) : 0;
            if (wn && !need && GetLastError() == ERR_LOSSY) {
                used = TRUE;
                if (lossy) *lossy = TRUE;
                if (!allow) { mem_free(out); if (own) mem_free(w); return ERR_LOSSY; }
                need = WideCharToMultiByte(CP_UTF8, 0, w, wn, (LPSTR)(out + bom), wn * 3, NULL, NULL);
            }
            if (wn && !need) { mem_free(out); if (own) mem_free(w); return ERR_BADCP; }
            on = bom + (size_t)need;
        }
    } else {
        UINT cp = CpOf(enc);
        BOOL probe = CpHasDefaultChar(cp);
        DWORD fl = probe ? WC_NO_BEST_FIT_CHARS : 0;      /* no best-fit guessing: anything not 1:1 becomes '?' + flags loss */
        int need = wn ? WideCharToMultiByte(cp, fl, w, wn, NULL, 0, NULL, NULL) : 0;

        if (wn && !need) { if (own) mem_free(w); return ERR_BADCP; }
        on = (size_t)need;
        out = (BYTE *)mem_alloc(on + 1);
        if (out) {
            if (need && WideCharToMultiByte(cp, fl, w, wn, (LPSTR)out, need, NULL, probe ? &used : NULL) != need) {
                mem_free(out); if (own) mem_free(w); return ERR_BADCP;
            }
            if (need && !probe) {
                /* Stateful code pages cannot report a default character. Verify the complete round trip
                 * instead of silently losing an emoji (or accepting a best-fit substitution). */
                int backLen = MultiByteToWideChar(cp, 0, (LPCSTR)out, need, NULL, 0);
                WCHAR *back;
                if (!backLen || backLen > DOC_MAX_CHARS) { mem_free(out); if (own) mem_free(w); return ERR_BADCP; }
                back = (WCHAR *)mem_alloc((size_t)backLen * sizeof(WCHAR));
                if (!back) { mem_free(out); if (own) mem_free(w); return ERR_NOMEM; }
                if (MultiByteToWideChar(cp, 0, (LPCSTR)out, need, back, backLen) != backLen) {
                    mem_free(back); mem_free(out); if (own) mem_free(w); return ERR_BADCP;
                }
                used = backLen != wn || memcmp(back, w, (size_t)wn * sizeof(WCHAR)) != 0;
                mem_free(back);
            }
        }
    }
    if (own) mem_free(w);
    if (!out) return ERR_NOMEM;
    if (used) {
        if (lossy) *lossy = TRUE;
        if (!allow) { mem_free(out); return ERR_LOSSY; }
    }

    er = SaveBytes(path, out, on);
    mem_free(out);
    return er;
}
