#include "app_internal.h"

static struct { int n; DWORD h1, h2; int enc, eol; } g_clean;
static struct { DWORD rev; int valid, changed; } g_dirtyCache;

static void HashText(const WCHAR *t, int n, DWORD *a, DWORD *b) {
    const DWORD *w = (const DWORD *)t;
    DWORD h1 = 2166136261u, h2 = 5381u, h3 = 2166136261u ^ 0x5BD1E995u, h4 = 5381u * 31u;
    int i, m = n / 4;
    for (i = 0; i < m; i++, w += 2) {
        h1 = (h1 ^ w[0]) * 16777619u;
        h3 = (h3 ^ w[1]) * 16777619u;
        h2 = (h2 * 33u) ^ w[0];
        h4 = (h4 * 33u) ^ w[1];
    }
    for (i = m * 4; i < n; i++) {
        h1 = (h1 ^ t[i]) * 16777619u;
        h2 = (h2 * 33u) ^ t[i];
    }
    *a = h1 ^ (h3 * 0x9E3779B1u);
    *b = h2 + h4 * 0x85EBCA6Bu;
}

void AppMarkClean(void) {
    void *h = NULL;
    int n = 0;
    const WCHAR *p = EditLockText(&h, &n);
    g_dirtyCache.valid = 0;
    if (!p) { EditUnlockText(h); g_clean.n = -1; return; }
    HashText(p, n, &g_clean.h1, &g_clean.h2);
    EditUnlockText(h);
    g_clean.n = n;
    g_clean.enc = g_doc.enc;
    g_clean.eol = g_doc.eol;
    SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
}

static BOOL TextChanged(void) {
    void *h = NULL;
    int n = 0;
    const WCHAR *p;
    DWORD a, b;
    BOOL same;
    if (!SendMessageW(g_edit, EM_GETMODIFY, 0, 0)) return FALSE;
    if (g_clean.n < 0) return TRUE;
    if (GetWindowTextLengthW(g_edit) != g_clean.n) return TRUE;
    if (g_dirtyCache.valid && g_dirtyCache.rev == g_textRev) return g_dirtyCache.changed;
    p = EditLockText(&h, &n);
    if (!p) { EditUnlockText(h); return TRUE; }
    HashText(p, n, &a, &b);
    EditUnlockText(h);
    same = (a == g_clean.h1 && b == g_clean.h2);
    g_dirtyCache.rev = g_textRev; g_dirtyCache.valid = 1; g_dirtyCache.changed = !same;
    if (same) SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    return !same;
}

BOOL AppIsDirty(void) {
    if (!g_edit) return FALSE;
    if ((g_doc.enc != g_clean.enc || g_doc.eol != g_clean.eol) && (g_doc.path[0] || GetWindowTextLengthW(g_edit) > 0)) return TRUE;
    return TextChanged();
}
