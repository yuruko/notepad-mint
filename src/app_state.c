/* app_state.c - compare the current document with its last clean state.
 * Cache content comparisons by text revision; encoding and line ending changes
 * still count as edits even when the native control's modified flag is clear. */
#include "app_internal.h"

/* ------------------------------------------------------------ dirty state -- */
/* "modified" means: different from what was last loaded / saved (or from blank for a new document). the stock edit
 * only has a sticky flag, so typing something and deleting it again, or undoing back to the original, would still ask
 * "save changes?". the clean state is kept as length + two 32-bit hashes (no 64-bit math, nothing to copy for big files);
 * a content compare only happens when the flag is set and the length is back to the clean one */
static struct { int n; DWORD h1, h2; int enc, eol; } g_clean;
static struct { DWORD rev; int valid, changed; } g_dirtyCache;

/* two units at a time (one 32-bit word) in two independent lanes per hash: a quarter of the dependent multiplies of one unit at a time (the
 * hashes only ever meet each other in this process, so the exact function is free) */
static void HashText(const WCHAR *t, int n, DWORD *a, DWORD *b)
{
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

void AppMarkClean(void)                         /* the document as it is now is the clean state: just loaded, created or saved */
{
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

static BOOL TextChanged(void)
{
    void *h = NULL;
    int n = 0;
    const WCHAR *p;
    DWORD a, b;
    BOOL same;
    if (!SendMessageW(g_edit, EM_GETMODIFY, 0, 0)) return FALSE;     /* untouched since the last load / save */
    if (g_clean.n < 0) return TRUE;                              /* unavailable baseline: never discard edits */
    if (GetWindowTextLengthW(g_edit) != g_clean.n) return TRUE;      /* another length: certainly changed (cheap) */
    if (g_dirtyCache.valid && g_dirtyCache.rev == g_textRev) return g_dirtyCache.changed;
    p = EditLockText(&h, &n);
    if (!p) { EditUnlockText(h); return TRUE; }
    HashText(p, n, &a, &b);
    EditUnlockText(h);
    same = (a == g_clean.h1 && b == g_clean.h2);
    g_dirtyCache.rev = g_textRev; g_dirtyCache.valid = 1; g_dirtyCache.changed = !same;
    if (same) SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);          /* back to the clean text: the control's own flag follows */
    return !same;
}

BOOL AppIsDirty(void)
{
    if (!g_edit) return FALSE;
    if ((g_doc.enc != g_clean.enc || g_doc.eol != g_clean.eol) &&    /* a new encoding / line ending needs a save ... */
        (g_doc.path[0] || GetWindowTextLengthW(g_edit) > 0))         /* ... unless it is an empty unsaved document: nothing to save */
        return TRUE;
    return TextChanged();
}
