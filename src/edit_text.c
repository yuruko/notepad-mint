#include "edit_internal.h"

static const WCHAR *TextLock(HLOCAL *h, int *n) {
    *h = (HLOCAL)SendMessageW(g_edit, EM_GETHANDLE, 0, 0);
    *n = GetWindowTextLengthW(g_edit);
    return *h ? (const WCHAR *)LocalLock(*h) : NULL;
}

const WCHAR *EditLockText(void **h, int *n) {
    return TextLock((HLOCAL *)h, n);
}

void EditUnlockText(void *h) {
    if (h) LocalUnlock((HLOCAL)h);
}

BOOL EditHasSel(void) {
    DWORD s = 0, e = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    return s != e;
}

static int g_activeCaret, g_caretAdjusted;

void EditCaretHint(int index) { g_activeCaret = index; }
void EditCaretDisplayAdjusted(BOOL adjusted) { g_caretAdjusted = adjusted; }

int EditCaretIndex(void) {
    DWORD s = 0, e = 0;
    POINT cp;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s == e || (g_activeCaret != (int)s && g_activeCaret != (int)e)) g_activeCaret = (int)e;
    if (s != e && !g_caretAdjusted && GetFocus() == g_edit && GetCaretPos(&cp) && cp.x > -20000 && cp.y > -20000) {
        LRESULT ps = SendMessageW(g_edit, EM_POSFROMCHAR, s, 0), pe = SendMessageW(g_edit, EM_POSFROMCHAR, e, 0);
        if (ps != -1 && pe != -1) {
            int ds = (cp.x - (short)LOWORD(ps)), dy = (cp.y - (short)HIWORD(ps));
            int de = (cp.x - (short)LOWORD(pe)), dz = (cp.y - (short)HIWORD(pe));
            ds = (ds < 0 ? -ds : ds) + 64 * (dy < 0 ? -dy : dy);
            de = (de < 0 ? -de : de) + 64 * (dz < 0 ? -dz : dz);
            g_activeCaret = (int)(ds < de ? s : e);
        }
    }
    return g_activeCaret;
}

void EditCaretPos(int *line, int *col) {
    static struct { DWORD rev; int n, idx, line, col, ok; } c;
    HLOCAL h;
    int n, idx = EditCaretIndex(), i;
    const WCHAR *p;
    *line = 1; *col = 1;
    n = GetWindowTextLengthW(g_edit);
    if (c.ok && c.rev == g_textRev && c.n == n && c.idx == idx) { *line = c.line; *col = c.col; return; }
    p = TextLock(&h, &n);
    if (!p) return;
    if (idx > n) idx = n;
    if (c.ok && c.rev == g_textRev && c.n == n && (idx > c.idx ? idx - c.idx : c.idx - idx) < idx) {
        int lo = idx < c.idx ? idx : c.idx, span = idx > c.idx ? idx - c.idx : c.idx - idx;
        int breaks = (int)mp_count_lf(p + lo, (size_t)span);
        *line = c.line + (idx < c.idx ? -breaks : breaks);
        if (!breaks) *col = c.col + (idx - c.idx);
        else {
            for (i = idx; i > 0 && p[i - 1] != '\n'; i--) {}
            *col = idx - i + 1;
        }
    } else {
        *line = 1 + (int)mp_count_lf(p, (size_t)idx);
        for (i = idx; i > 0 && p[i - 1] != '\n'; i--) {}
        *col = idx - i + 1;
    }
    LocalUnlock(h);
    c.rev = g_textRev; c.n = n; c.idx = idx; c.line = *line; c.col = *col; c.ok = 1;
}

BOOL EditSelStats(int enc, int eol, int *lines, DWORD *bytes) {
    static struct { DWORD s, e, rev; int len, enc, eol, lines, ok; DWORD bytes; } c;
    DWORD s = 0, e = 0;
    HLOCAL h;
    int n;
    const WCHAR *p;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e <= s) return FALSE;
    if (!c.ok || c.s != s || c.e != e || c.rev != g_textRev || c.len != GetWindowTextLengthW(g_edit) || c.enc != enc || c.eol != eol) {
        p = TextLock(&h, &n);
        if (!p) return FALSE;
        if (e > (DWORD)n) e = (DWORD)n;
        if (s >= e) { LocalUnlock(h); return FALSE; }
        c.lines = (int)mp_count_lf(p + s, (size_t)(e - s)) + (p[e - 1] == '\n' ? 0 : 1);
        c.bytes = DocBodySize(p + s, (int)(e - s), enc, eol);
        LocalUnlock(h);
        c.s = s; c.e = e; c.rev = g_textRev; c.len = n; c.enc = enc; c.eol = eol; c.ok = 1;
    }
    *lines = c.lines;
    *bytes = c.bytes;
    return TRUE;
}

BOOL EditGotoLine(int line) {
    HLOCAL h;
    int n, cur = 1, i = 0;
    const WCHAR *p = TextLock(&h, &n);
    if (!p) return line <= 1;
    if (line < 1) line = 1;
    while (cur < line) {
        while (i < n && p[i] != '\n') i++;
        if (i >= n) { LocalUnlock(h); return FALSE; }
        i++; cur++;
    }
    LocalUnlock(h);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)i, (LPARAM)i);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}

void EditClearLine(void) {
    HLOCAL h;
    int n, idx = EditCaretIndex(), a, b;
    const WCHAR *p = TextLock(&h, &n);
    if (!p) return;
    if (idx > n) idx = n;
    for (a = idx; a > 0 && p[a - 1] != '\n'; a--) {}
    for (b = idx; b < n && p[b] != '\n'; b++) {}
    if (b < n && b > a && p[b - 1] == '\r') b--;
    LocalUnlock(h);
    if (a == b) return;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

BOOL EditSetDocText(const WCHAR *t) {
    BOOL ok;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    ok = SetWindowTextW(g_edit, t ? t : L"");
    if (ok) {
        SendMessageW(g_edit, EM_SETSEL, 0, 0);
        SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(g_edit, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(g_edit, EM_SETMODIFY, FALSE, 0);
    }
    SendMessageW(g_edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_edit, NULL, TRUE);
    return ok;
}

WCHAR *EditGetDocText(int *len) {
    int n = GetWindowTextLengthW(g_edit), got;
    WCHAR *buf = (WCHAR *)mem_alloc(((size_t)n + 1) * sizeof(WCHAR));
    if (!buf) return NULL;
    got = GetWindowTextW(g_edit, buf, n + 1);
    if (got != n) { mem_free(buf); return NULL; }
    buf[got] = 0;
    if (len) *len = got;
    return buf;
}

void EditInsert(const WCHAR *s) {
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)s);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

int WordClass(WCHAR c) {
    if (c == ' ' || c == '\t') return 0;
    if (c >= 0x80) return 1;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return 2;
}

void EditDeleteWord(int dir) {
    DWORD s = 0, e = 0;
    HLOCAL h;
    int n, a, b;
    const WCHAR *p;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (s != e) { SendMessageW(g_edit, WM_CLEAR, 0, 0); return; }
    p = TextLock(&h, &n);
    if (!p) return;
    a = b = (int)s;
    if (a > n) a = b = n;
    if (dir < 0) {
        if (a > 0 && p[a - 1] == '\n') {
            a--;
            if (a > 0 && p[a - 1] == '\r') a--;
        } else {
            while (a > 0 && WordClass(p[a - 1]) == 0) a--;
            if (a > 0 && p[a - 1] != '\n') {
                int k = WordClass(p[a - 1]);
                while (a > 0 && p[a - 1] != '\n' && WordClass(p[a - 1]) == k) a--;
            }
        }
    } else {
        if (b < n && p[b] == '\r') {
            b++;
            if (b < n && p[b] == '\n') b++;
        } else if (b < n && p[b] == '\n') {
            b++;
        } else {
            if (b < n && WordClass(p[b]) != 0) {
                int k = WordClass(p[b]);
                while (b < n && p[b] != '\r' && WordClass(p[b]) == k) b++;
            } while (b < n && WordClass(p[b]) == 0) b++;
        }
    }
    LocalUnlock(h);
    if (a != b) {
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    }
}
