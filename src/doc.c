#include "mp.h"

#define WC_NO_BEST_FIT_CHARS 0x00000400
#define WC_ERR_INVALID_CHARS 0x00000080
#define DOC_MAX_CHARS 0x3FFFFFFE

#ifdef DOC_IO_TEST
static int g_ioFault, g_ioCalls;
void DocTestFault(int fault) { g_ioFault = fault; g_ioCalls = 0; }
static BOOL DocReadFile(HANDLE file, LPVOID buffer, DWORD length, DWORD *bytesRead, void *overlap) {
    if (g_ioFault == 1) { *bytesRead = 0; SetLastError(30); return FALSE; }
    if (g_ioFault == 2) {
        if (g_ioCalls++) { *bytesRead = 0; return TRUE; }
        if (length > 1) length = 1;
    }
    return ReadFile(file, buffer, length, bytesRead, overlap);
}
static BOOL DocWriteFile(HANDLE file, LPCVOID buffer, DWORD length, DWORD *bytesWritten, void *overlap) {
    if (g_ioFault == 3) {
        WriteFile(file, buffer, length ? 1 : 0, bytesWritten, overlap);
        SetLastError(29); return FALSE;
    }
    if (g_ioFault == 4) { *bytesWritten = 0; SetLastError(0); return TRUE; }
    return WriteFile(file, buffer, length, bytesWritten, overlap);
}
static BOOL DocFlushFile(HANDLE file) {
    if (g_ioFault == 5) { SetLastError(29); return FALSE; }
    return FlushFileBuffers(file);
}
static BOOL DocReplaceFile(LPCWSTR path, LPCWSTR temp, LPCWSTR backup) {
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
static BOOL DocMoveFile(LPCWSTR from, LPCWSTR to) {
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
const WCHAR *const g_encShort[ENC_COUNT] = { L"utf8", L"utf8 bom", L"utf16 le", L"utf16 be", L"ansi" };
const WCHAR *const g_eolShort[EOL_COUNT] = { L"crlf", L"lf", L"cr" };

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

static const CpInfo *CpFind(int cp) {
    int i;
    for (i = 0; i < (int)NCP; i++) if (g_cpTab[i].cp == cp) return &g_cpTab[i];
    return NULL;
}

void EncLabel(int enc, WCHAR *out, int cap) {
    const CpInfo *info;
    WCHAR buffer[48];
    if (enc >= 0 && enc < ENC_COUNT) { wcopy(out, g_encName[enc], cap); return; }
    info = CpFind(enc);
    if (info) { wcopy(out, info->name, cap); return; }
    wsprintfW(buffer, L"code page %d", enc);
    wcopy(out, buffer, cap);
}

void EncShort(int enc, WCHAR *out, int cap) {
    const CpInfo *info;
    WCHAR buffer[24];
    int i, length = 0;
    if (cap <= 0) return;
    if (enc >= 0 && enc < ENC_COUNT) { wcopy(out, g_encShort[enc], cap); return; }
    info = CpFind(enc);
    if (info) {
        for (i = 0; info->name[i] && length < cap - 1; i++) if (info->name[i] != '-') out[length++] = info->name[i];
        out[length] = 0;
        return;
    }
    wsprintfW(buffer, L"cp %d", enc);
    wcopy(out, buffer, cap);
}

int EncListCount(void) { return ENC_COUNT + (int)NCP; }

int EncListGet(int index, WCHAR *label, int cap) {
    if (index < 0) index = 0;
    if (index < ENC_COUNT) {
        wcopy(label, index == ENC_ANSI ? L"ansi  system default code page" : g_encName[index], cap);
        return index;
    }
    index -= ENC_COUNT;
    if (index >= (int)NCP) index = (int)NCP - 1;
    wcopy(label, g_cpTab[index].name, cap);
    wcat(label, L"  ", cap);
    wcat(label, g_cpTab[index].desc, cap);
    return g_cpTab[index].cp;
}

static UINT CpOf(int enc) {
    if (enc == ENC_UTF8 || enc == ENC_UTF8BOM) return CP_UTF8;
    if (enc == ENC_ANSI) return GetACP();
    return (UINT)enc;
}

static BOOL CpHasDefaultChar(UINT cp) {
    if (cp == CP_UTF8 || cp == 65000 || cp == 54936 || cp == 42) return FALSE;
    if (cp >= 50220 && cp <= 50229) return FALSE;
    if (cp >= 57002 && cp <= 57011) return FALSE;
    return TRUE;
}

static BOOL Utf8Valid(const BYTE *bytes, size_t length) {
    size_t i = 0;
    while (i < length) {
        BYTE lead = bytes[i];
        int need, k;
        unsigned codePoint;
        if (lead < 0x80) { i++; continue; }
        if (lead >= 0xC2 && lead <= 0xDF)      { need = 1; codePoint = lead & 0x1F; }
        else if (lead >= 0xE0 && lead <= 0xEF) { need = 2; codePoint = lead & 0x0F; }
        else if (lead >= 0xF0 && lead <= 0xF4) { need = 3; codePoint = lead & 0x07; }
        else return FALSE;
        if (i + need >= length) return FALSE;
        for (k = 1; k <= need; k++) {
            if ((bytes[i + k] & 0xC0) != 0x80) return FALSE;
            codePoint = (codePoint << 6) | (bytes[i + k] & 0x3F);
        }
        if (need == 2 && (codePoint < 0x800 || (codePoint >= 0xD800 && codePoint <= 0xDFFF))) return FALSE;
        if (need == 3 && (codePoint < 0x10000 || codePoint > 0x10FFFF)) return FALSE;
        i += (size_t)need + 1;
    }
    return TRUE;
}

static int Utf16Guess(const BYTE *bytes, size_t length) {
    size_t limit = length < 4096 ? length : 4096, i, zerosEven = 0, zerosOdd = 0, pairs = limit / 2;
    if (limit < 4) return 0;
    for (i = 0; i + 1 < limit; i += 2) {
        if (!bytes[i]) zerosEven++;
        if (!bytes[i + 1]) zerosOdd++;
    }
    if (zerosOdd * 4 >= pairs && zerosEven == 0) return 1;
    if (zerosEven * 4 >= pairs && zerosOdd == 0) return 2;
    return 0;
}

static BOOL g_lossyOk;
void DocAllowLossy(BOOL on) { g_lossyOk = on; }

DWORD DocRead(const WCHAR *path, WCHAR **text, int *len, int *enc, int *eol, int forceEnc) {
    HANDLE file;
    __int64 fileSize;
    BYTE *bytes;
    DWORD bytesRead;
    size_t byteLen, totalRead, offset = 0, i, nCRLF = 0, nLF = 0, nCR = 0, nNul = 0, firstEol = 0, outLen;
    WCHAR *wide, *out;
    int wideLen, encoding, k;
    BOOL bom8, bomLE, bomBE, validUtf8 = FALSE;
    *text = NULL; *len = 0; *enc = ENC_UTF8; *eol = EOL_CRLF;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx(file, &fileSize)) { DWORD err = GetLastError(); CloseHandle(file); return err; }
    if (fileSize < 0 || fileSize > 0x30000000) { CloseHandle(file); return ERR_TOO_BIG; }
    byteLen = (size_t)fileSize;
    bytes = (BYTE *)mem_alloc(byteLen + 4);
    if (!bytes) { CloseHandle(file); return ERR_NOMEM; }
    for (totalRead = 0; totalRead < byteLen; totalRead += bytesRead) {
        if (!DocReadFile(file, bytes + totalRead, (DWORD)(byteLen - totalRead), &bytesRead, NULL)) {
            DWORD err = GetLastError();
            CloseHandle(file); mem_free(bytes); return err ? err : 30;
        }
        if (!bytesRead || bytesRead > byteLen - totalRead) { CloseHandle(file); mem_free(bytes); return 38; }
    }
    CloseHandle(file);
    byteLen = totalRead;
    bytes[byteLen] = bytes[byteLen + 1] = bytes[byteLen + 2] = bytes[byteLen + 3] = 0;
    bom8  = (byteLen >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF);
    bomLE = (byteLen >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE);
    bomBE = (byteLen >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF);
    if (forceEnc >= 0) {
        encoding = forceEnc;
        if (encoding == ENC_UTF8 || encoding == ENC_UTF8BOM) { encoding = bom8 ? ENC_UTF8BOM : ENC_UTF8; if (bom8) offset = 3; }
        else if (encoding == ENC_UTF16LE && bomLE) offset = 2;
        else if (encoding == ENC_UTF16BE && bomBE) offset = 2;
    } else {
        encoding = ENC_UTF8;
        if (bom8) { encoding = ENC_UTF8BOM; offset = 3; }
        else if (bomLE) { encoding = ENC_UTF16LE; offset = 2; }
        else if (bomBE) { encoding = ENC_UTF16BE; offset = 2; }
        else {
            k = Utf16Guess(bytes, byteLen);
            if (k == 1) encoding = ENC_UTF16LE;
            else if (k == 2) encoding = ENC_UTF16BE;
            else if (!Utf8Valid(bytes, byteLen)) encoding = ENC_ANSI;
            else validUtf8 = TRUE;
        }
    }
    if (encoding == ENC_UTF16LE || encoding == ENC_UTF16BE) {
        int odd = (int)((byteLen - offset) & 1);
        if (odd && !g_lossyOk) { mem_free(bytes); return ERR_LOSSY; }
        wideLen = (int)((byteLen - offset) / 2);
        wide = (WCHAR *)mem_alloc(((size_t)wideLen + 2) * sizeof(WCHAR));
        if (wide) {
            memcpy(wide, bytes + offset, (size_t)wideLen * sizeof(WCHAR));
            if (encoding == ENC_UTF16BE) for (k = 0; k < wideLen; k++) wide[k] = (WCHAR)((wide[k] << 8) | (wide[k] >> 8));
            if (odd) wide[wideLen++] = 0xFFFD;
        }
    } else {
        UINT cp = CpOf(encoding);
        int srcLen = (int)(byteLen - offset);
        if (cp == CP_UTF8 && !validUtf8 && !g_lossyOk && !Utf8Valid(bytes + offset, byteLen - offset)) { mem_free(bytes); return ERR_LOSSY; }
        wide = (WCHAR *)mem_alloc(((size_t)srcLen + 1) * sizeof(WCHAR));
        wideLen = (wide && srcLen) ? MultiByteToWideChar(cp, 0, (LPCSTR)(bytes + offset), srcLen, wide, srcLen) : 0;
        if (wide && srcLen && !wideLen && GetLastError() == 122) {
            wideLen = MultiByteToWideChar(cp, 0, (LPCSTR)(bytes + offset), srcLen, NULL, 0);
            mem_free(wide);
            if (!wideLen) { mem_free(bytes); return ERR_BADCP; }
            if (wideLen > DOC_MAX_CHARS) { mem_free(bytes); return ERR_TOO_BIG; }
            wide = (WCHAR *)mem_alloc(((size_t)wideLen + 1) * sizeof(WCHAR));
            if (wide) wideLen = MultiByteToWideChar(cp, 0, (LPCSTR)(bytes + offset), srcLen, wide, wideLen);
        }
        if (wide && srcLen && !wideLen) { mem_free(wide); mem_free(bytes); return ERR_BADCP; }
        if (wide && (size_t)wideLen < (size_t)srcLen) { WCHAR *resized = (WCHAR *)mem_realloc(wide, ((size_t)wideLen + 1) * sizeof(WCHAR)); if (resized) wide = resized; }
    }
    mem_free(bytes);
    if (!wide) return ERR_NOMEM;
    wide[wideLen] = 0;
    for (i = mp_find3(wide, (size_t)wideLen, '\r', '\n', 0); i < (size_t)wideLen; i += 1 + mp_find3(wide + i + 1, (size_t)wideLen - i - 1, '\r', '\n', 0)) {
        WCHAR c = wide[i];
        if (c == '\r') {
            if (i + 1 < (size_t)wideLen && wide[i + 1] == '\n') { nCRLF++; i++; if (!firstEol) firstEol = 1; }
            else { nCR++; if (!firstEol) firstEol = 3; }
        } else if (c == '\n') {
            nLF++; if (!firstEol) firstEol = 2;
        } else {
            nNul++;
        }
    }
    if (nLF + nCR + nNul == 0) {
        out = wide; outLen = (size_t)wideLen;
    } else {
        outLen = (size_t)wideLen + nLF + nCR;
        if (outLen > DOC_MAX_CHARS) { mem_free(wide); return ERR_TOO_BIG; }
        out = (WCHAR *)mem_alloc((outLen + 1) * sizeof(WCHAR));
        if (!out) { mem_free(wide); return ERR_NOMEM; }
        {
            WCHAR *dst = out;
            for (i = 0; i < (size_t)wideLen; i++) {
                size_t run = mp_find3(wide + i, (size_t)wideLen - i, '\r', '\n', 0);
                WCHAR c;
                memcpy(dst, wide + i, run * sizeof(WCHAR));
                dst += run; i += run;
                if (i >= (size_t)wideLen) break;
                c = wide[i];
                if (c == '\r') {
                    *dst++ = '\r'; *dst++ = '\n';
                    if (i + 1 < (size_t)wideLen && wide[i + 1] == '\n') i++;
                } else if (c == '\n') {
                    *dst++ = '\r'; *dst++ = '\n';
                } else {
                    *dst++ = ' ';
                }
            }
        }
        mem_free(wide);
    }
    out[outLen] = 0;
    {
        int bestEol = EOL_CRLF;
        size_t bestCount = nCRLF;
        if (nLF > bestCount) { bestEol = EOL_LF; bestCount = nLF; }
        if (nCR > bestCount) { bestEol = EOL_CR; bestCount = nCR; }
        if (firstEol == 1 && nCRLF == bestCount) bestEol = EOL_CRLF;
        else if (firstEol == 2 && nLF == bestCount) bestEol = EOL_LF;
        else if (firstEol == 3 && nCR == bestCount) bestEol = EOL_CR;
        *eol = bestEol;
    }
    *text = out; *len = (int)outLen; *enc = encoding;
    return 0;
}

static WCHAR *EolConvert(const WCHAR *text, int len, int eol, int *outLen, int *owned) {
    size_t eolLen = (eol == EOL_CRLF) ? 2 : 1, i, breaks = 0, consumed = 0, total;
    WCHAR *out, *dst;
    for (i = mp_find3(text, (size_t)len, '\r', '\n', '\n'); i < (size_t)len; i += 1 + mp_find3(text + i + 1, (size_t)len - i - 1, '\r', '\n', '\n')) {
        if (text[i] == '\r') {
            breaks++; consumed++;
            if (i + 1 < (size_t)len && text[i + 1] == '\n') { i++; consumed++; }
        } else {
            breaks++; consumed++;
        }
    }
    total = (size_t)len - consumed + breaks * eolLen;
    if (total > DOC_MAX_CHARS) { *outLen = -1; return NULL; }
    if (text && eol == EOL_CRLF && consumed == breaks * 2) { *outLen = len; *owned = 0; return (WCHAR *)text; }
    *owned = 1;
    out = (WCHAR *)mem_alloc((total + 1) * sizeof(WCHAR));
    if (!out) return NULL;
    dst = out;
    for (i = 0; i < (size_t)len; i++) {
        size_t run = mp_find3(text + i, (size_t)len - i, '\r', '\n', '\n');
        WCHAR c;
        memcpy(dst, text + i, run * sizeof(WCHAR));
        dst += run; i += run;
        if (i >= (size_t)len) break;
        c = text[i];
        if (c == '\r' && i + 1 < (size_t)len && text[i + 1] == '\n') i++;
        if (eol == EOL_CRLF) { *dst++ = '\r'; *dst++ = '\n'; }
        else *dst++ = (eol == EOL_LF) ? '\n' : '\r';
    }
    *dst = 0;
    *outLen = (int)total;
    return out;
}

DWORD DocBodySize(const WCHAR *text, int len, int enc, int eol) {
    size_t cut;
    if (!text || len <= 0) return 0;
    cut = (eol == EOL_CRLF) ? 0 : mp_count_lf(text, (size_t)len);
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) return (DWORD)(((size_t)len - cut) * 2);
    {
        UINT cp = CpOf(enc);
        DWORD flags = CpHasDefaultChar(cp) ? WC_NO_BEST_FIT_CHARS : 0;
        size_t need = (size_t)WideCharToMultiByte(cp, flags, text, len, NULL, 0, NULL, NULL);
        return need >= cut ? (DWORD)(need - cut) : 0;
    }
}

DWORD DocEncodedSize(const WCHAR *text, int len, int enc, int eol) {
    DWORD bom = (enc == ENC_UTF16LE || enc == ENC_UTF16BE) ? 2 : (enc == ENC_UTF8BOM ? 3 : 0);
    return bom + DocBodySize(text, len, enc, eol);
}

static HANDLE SaveSibling(const WCHAR *full, WCHAR *out, const WCHAR *extension) {
    static DWORD serial;
    const WCHAR *name = PathName(full);
    size_t prefix = (size_t)(name - full);
    HANDLE file;
    DWORD err;
    int attempt;
    memcpy(out, full, prefix * sizeof(WCHAR));
    for (attempt = 0; attempt < 100; attempt++) {
        wsprintfW(out + prefix, L".mint-%08x-%08x-%08x.%s", GetCurrentThreadId(), GetTickCount(), ++serial, extension);
        file = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) return file;
        err = GetLastError();
        if (err != 80 && err != 183) return INVALID_HANDLE_VALUE;
    }
    SetLastError(80);
    return INVALID_HANDLE_VALUE;
}

static DWORD WriteAll(HANDLE file, const BYTE *out, size_t outLen) {
    DWORD bytesWritten, err;
    size_t done;
    for (done = 0; done < outLen; done += bytesWritten) {
        if (!DocWriteFile(file, out + done, (DWORD)(outLen - done), &bytesWritten, NULL)) { err = GetLastError(); return err ? err : 29; }
        if (!bytesWritten || bytesWritten > outLen - done) return 29;
    }
    return 0;
}

static DWORD SaveInPlace(HANDLE file, const BYTE *out, size_t outLen) {
    DWORD err;
    if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return GetLastError();
    err = WriteAll(file, out, outLen);
    if (err) return err;
    if (!SetEndOfFile(file)) return GetLastError();
    if (!DocFlushFile(file)) { err = GetLastError(); return err ? err : 29; }
    return 0;
}

static DWORD SaveBytes(const WCHAR *path, const BYTE *out, size_t outLen) {
    WCHAR full[PATH_CAP], temp[PATH_CAP + 64], backup[PATH_CAP + 64];
    HANDLE file = INVALID_HANDLE_VALUE, guard = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD size, attrs, err = 0, basicAttrs;
    BOOL existed, inPlace = FALSE, haveTemp = FALSE, haveBackup = FALSE;
    size = GetFullPathNameW(path, PATH_CAP, full, NULL);
    if (!size) return GetLastError();
    if (size >= PATH_CAP) return 206;
    attrs = GetFileAttributesW(full);
    existed = attrs != INVALID_FILE_ATTRIBUTES;
    if (!existed) {
        err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND) return err;
    } else {
        if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) return 5;
        guard = CreateFileW(full, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (guard == INVALID_HANDLE_VALUE) return GetLastError();
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            size = GetFinalPathNameByHandleW(guard, full, PATH_CAP, 0);
            if (!size || size >= PATH_CAP) { err = size ? 206 : GetLastError(); goto finish; }
            attrs = GetFileAttributesW(full);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) { err = 5; goto finish; }
        }
        inPlace = GetFileInformationByHandle(guard, &info) && info.nNumberOfLinks > 1;
    }
    err = 0;
    if (!inPlace) {
        file = SaveSibling(full, temp, L"tmp");
        if (file == INVALID_HANDLE_VALUE) {
            err = GetLastError();
            if (err != 5 || !existed) goto finish;
            inPlace = TRUE;
        }
    }
    if (inPlace) { err = SaveInPlace(guard, out, outLen); goto finish; }
    haveTemp = TRUE;
    err = WriteAll(file, out, outLen);
    if (err) goto finish;
    if (!DocFlushFile(file)) { err = GetLastError(); if (!err) err = 29; goto finish; }
    if (!CloseHandle(file)) { err = GetLastError(); file = INVALID_HANDLE_VALUE; goto finish; }
    file = INVALID_HANDLE_VALUE;
    if (existed) {
        basicAttrs = attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
        if (!SetFileAttributesW(temp, basicAttrs ? basicAttrs : FILE_ATTRIBUTE_NORMAL)) { err = GetLastError(); goto finish; }
        file = SaveSibling(full, backup, L"bak");
        if (file == INVALID_HANDLE_VALUE) { err = GetLastError(); goto finish; }
        haveBackup = TRUE;
        if (!CloseHandle(file)) { err = GetLastError(); file = INVALID_HANDLE_VALUE; goto finish; }
        file = INVALID_HANDLE_VALUE;
        if (!DocReplaceFile(full, temp, backup)) {
            err = GetLastError();
            if (!err) err = 29;
            if (err == 1177) {
                if (!MoveFileExW(backup, full, MOVEFILE_WRITE_THROUGH)) haveBackup = FALSE;
            }
            goto finish;
        }
    } else if (!DocMoveFile(temp, full)) {
        err = GetLastError();
        if (!err) err = 29;
        goto finish;
    }
    haveTemp = FALSE;
finish:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (guard != INVALID_HANDLE_VALUE) CloseHandle(guard);
    if (haveTemp) DeleteFileW(temp);
    if (haveBackup) DeleteFileW(backup);
    return err;
}

DWORD DocWrite(const WCHAR *path, const WCHAR *text, int len, int enc, int eol, BOOL *lossy) {
    WCHAR *wide;
    BYTE *out = NULL;
    size_t outLen = 0;
    int wideLen = 0, k, own = 0;
    BOOL allowLossy = lossy ? *lossy : FALSE, isLossy = FALSE;
    DWORD err;
    if (lossy) *lossy = FALSE;
    if (!text && len > 0) return ERR_BADCP;
    if (len > DOC_MAX_CHARS) return ERR_TOO_BIG;
    wide = EolConvert(text, len < 0 ? 0 : len, eol, &wideLen, &own);
    if (!wide) return wideLen < 0 ? ERR_TOO_BIG : ERR_NOMEM;
    if (enc == ENC_UTF16LE || enc == ENC_UTF16BE) {
        outLen = 2 + (size_t)wideLen * 2;
        out = (BYTE *)mem_alloc(outLen);
        if (out) {
            WCHAR *outWide = (WCHAR *)out;
            outWide[0] = (enc == ENC_UTF16LE) ? 0xFEFF : 0xFFFE;
            for (k = 0; k < wideLen; k++) outWide[1 + k] = (enc == ENC_UTF16LE) ? wide[k] : (WCHAR)((wide[k] << 8) | (wide[k] >> 8));
        }
    } else if (CpOf(enc) == CP_UTF8) {
        size_t bom = (enc == ENC_UTF8BOM) ? 3 : 0;
        int need = 0;
        if (wideLen > 0x2AAAAAAA) { err = ERR_TOO_BIG; goto fail; }
        out = (BYTE *)mem_alloc(bom + (size_t)wideLen * 3 + 1);
        if (out) {
            if (bom) { out[0] = 0xEF; out[1] = 0xBB; out[2] = 0xBF; }
            need = wideLen ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideLen, (LPSTR)(out + bom), wideLen * 3, NULL, NULL) : 0;
            if (wideLen && !need && GetLastError() == ERR_LOSSY) {
                isLossy = TRUE;
                if (lossy) *lossy = TRUE;
                if (!allowLossy) { err = ERR_LOSSY; goto fail; }
                need = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, (LPSTR)(out + bom), wideLen * 3, NULL, NULL);
            }
            if (wideLen && !need) { err = ERR_BADCP; goto fail; }
            outLen = bom + (size_t)need;
        }
    } else {
        UINT cp = CpOf(enc);
        BOOL probe = CpHasDefaultChar(cp);
        DWORD flags = probe ? WC_NO_BEST_FIT_CHARS : 0;
        int need = wideLen ? WideCharToMultiByte(cp, flags, wide, wideLen, NULL, 0, NULL, NULL) : 0;
        if (wideLen && !need) { err = ERR_BADCP; goto fail; }
        outLen = (size_t)need;
        out = (BYTE *)mem_alloc(outLen + 1);
        if (out) {
            if (need && WideCharToMultiByte(cp, flags, wide, wideLen, (LPSTR)out, need, NULL, probe ? &isLossy : NULL) != need) { err = ERR_BADCP; goto fail; }
            if (need && !probe) {
                int backLen = MultiByteToWideChar(cp, 0, (LPCSTR)out, need, NULL, 0);
                WCHAR *back;
                if (!backLen || backLen > DOC_MAX_CHARS) { err = ERR_BADCP; goto fail; }
                back = (WCHAR *)mem_alloc((size_t)backLen * sizeof(WCHAR));
                if (!back) { err = ERR_NOMEM; goto fail; }
                if (MultiByteToWideChar(cp, 0, (LPCSTR)out, need, back, backLen) != backLen) { mem_free(back); err = ERR_BADCP; goto fail; }
                isLossy = backLen != wideLen || memcmp(back, wide, (size_t)wideLen * sizeof(WCHAR)) != 0;
                mem_free(back);
            }
        }
    }
    if (own) mem_free(wide);
    if (!out) return ERR_NOMEM;
    if (isLossy) {
        if (lossy) *lossy = TRUE;
        if (!allowLossy) { mem_free(out); return ERR_LOSSY; }
    }
    err = SaveBytes(path, out, outLen);
    mem_free(out);
    return err;
fail:
    mem_free(out);
    if (own) mem_free(wide);
    return err;
}
