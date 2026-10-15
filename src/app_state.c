#include "app_internal.h"

static struct { int length; DWORD hashA, hashB; int enc, eol; } g_clean;
static struct { DWORD rev; int valid, changed; } g_dirtyCache;

static void HashText(const WCHAR *text, int length, DWORD *hashA, DWORD *hashB) {
    const DWORD *dwords = (const DWORD *)text;
    DWORD fnvEven = 2166136261u, djbEven = 5381u, fnvOdd = 2166136261u ^ 0x5BD1E995u, djbOdd = 5381u * 31u;
    int i, blocks = length / 4;
    for (i = 0; i < blocks; i++, dwords += 2) {
        fnvEven = (fnvEven ^ dwords[0]) * 16777619u;
        fnvOdd = (fnvOdd ^ dwords[1]) * 16777619u;
        djbEven = (djbEven * 33u) ^ dwords[0];
        djbOdd = (djbOdd * 33u) ^ dwords[1];
    }
    for (i = blocks * 4; i < length; i++) {
        fnvEven = (fnvEven ^ text[i]) * 16777619u;
        djbEven = (djbEven * 33u) ^ text[i];
    }
    *hashA = fnvEven ^ (fnvOdd * 0x9E3779B1u);
    *hashB = djbEven + djbOdd * 0x85EBCA6Bu;
}

void AppMarkClean(void) {
    void *lock = NULL;
    int length = 0;
    const WCHAR *text = EditLockText(&lock, &length);
    g_dirtyCache.valid = 0;
    HistBreak();
    if (!text) { EditUnlockText(lock); g_clean.length = -1; return; }
    HashText(text, length, &g_clean.hashA, &g_clean.hashB);
    EditUnlockText(lock);
    g_clean.length = length;
    g_clean.enc = g_doc.enc;
    g_clean.eol = g_doc.eol;
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
}

static BOOL TextChanged(void) {
    void *lock = NULL;
    int length = 0;
    const WCHAR *text;
    DWORD hashA, hashB;
    BOOL same;
    if (!SendMessageW(g_edit, EM_GETMODIFY, 0, 0)) return FALSE;
    if (g_clean.length < 0) return TRUE;
    if (GetWindowTextLengthW(g_edit) != g_clean.length) return TRUE;
    if (g_dirtyCache.valid && g_dirtyCache.rev == g_textRev) return g_dirtyCache.changed;
    text = EditLockText(&lock, &length);
    if (!text) { EditUnlockText(lock); return TRUE; }
    HashText(text, length, &hashA, &hashB);
    EditUnlockText(lock);
    same = (hashA == g_clean.hashA && hashB == g_clean.hashB);
    g_dirtyCache.rev = g_textRev; g_dirtyCache.valid = 1; g_dirtyCache.changed = !same;
    if (same) SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    return !same;
}

BOOL AppIsDirty(void) {
    if (!g_edit) return FALSE;
    if ((g_doc.enc != g_clean.enc || g_doc.eol != g_clean.eol) && (g_doc.path[0] || GetWindowTextLengthW(g_edit) > 0)) return TRUE;
    return TextChanged();
}
