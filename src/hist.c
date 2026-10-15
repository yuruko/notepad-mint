#include "mp.h"

#ifndef HIST_BYTES
#define HIST_BYTES (192u * 1024u * 1024u)
#endif
#define HIST_PEEK 2
#define HIST_SMALL 64

enum { HK_OTHER, HK_TYPE, HK_BACK, HK_DEL };

typedef struct HistItem {
    WCHAR *text;
    int pos, textLen, replaceLen;
    int undoStart, undoEnd, redoStart, redoEnd;
    int kind, endsLine;
} HistItem;

static HistItem g_undo[HIST_MAX], g_redo[HIST_MAX];
static int g_undoCount, g_redoCount, g_open, g_busy;
static size_t g_bytes;
static struct { int active, failed, lo, hi, length, selStart, selEnd, kind; WCHAR *old; WCHAR small[HIST_SMALL]; } g_cap;

static size_t ItemBytes(const HistItem *item) {
    return (size_t)item->textLen * sizeof(WCHAR);
}

static void ItemFree(HistItem *item) {
    g_bytes -= ItemBytes(item);
    mem_free(item->text);
    item->text = NULL;
    item->textLen = 0;
}

static void ClearStack(HistItem *stack, int *count) {
    while (*count > 0) ItemFree(&stack[--*count]);
}

static void DropOldest(void) {
    ItemFree(&g_undo[0]);
    memmove(g_undo, g_undo + 1, (size_t)(g_undoCount - 1) * sizeof(HistItem));
    g_undoCount--;
}

static void Trim(void) {
    while (g_bytes > HIST_BYTES && g_undoCount > 1) DropOldest();
}

void HistClear(void) {
    ClearStack(g_undo, &g_undoCount);
    ClearStack(g_redo, &g_redoCount);
    g_open = 0;
}

void HistBreak(void) {
    g_open = 0;
}

BOOL HistCanUndo(void) {
    return g_undoCount > 0;
}

BOOL HistCanRedo(void) {
    return g_redoCount > 0;
}

static int KindOf(UINT msg, WPARAM wParam) {
    switch (msg) {
    case WM_CHAR:
        if (wParam == 8) return HK_BACK;
        if (wParam == 0x16 || wParam == 0x18) return HK_OTHER;
        if (wParam == 9 || wParam == 10 || wParam == 13 || (wParam >= 0x20 && wParam != 0x7F)) return HK_TYPE;
        return -1;
    case WM_KEYDOWN:
        if (wParam == VK_DELETE) {
            int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl && !shift) return -1;
            return shift ? HK_OTHER : HK_DEL;
        }
        return wParam == VK_INSERT ? HK_OTHER : -1;
    case WM_IME_CHAR:
        return HK_TYPE;
    case WM_IME_COMPOSITION: case WM_CUT: case WM_CLEAR: case WM_PASTE: case EM_REPLACESEL:
        return HK_OTHER;
    }
    return -1;
}

static void CapFree(void) {
    if (g_cap.old && g_cap.old != g_cap.small) mem_free(g_cap.old);
    g_cap.old = NULL;
}

int HistEnter(UINT msg, WPARAM wParam) {
    DWORD selStart = 0, selEnd = 0;
    int kind, length, lo, hi;
    if (g_busy || g_cap.active || !g_edit) return 0;
    kind = KindOf(msg, wParam);
    if (kind < 0) return 0;
    length = GetWindowTextLengthW(g_edit);
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if ((int)selEnd > length) selEnd = (DWORD)length;
    if (selStart > selEnd) selStart = selEnd;
    lo = (int)selStart - HIST_PEEK;
    if (lo < 0) lo = 0;
    hi = (int)selEnd + HIST_PEEK;
    if (hi > length) hi = length;
    g_cap.active = 1;
    g_cap.failed = 0;
    g_cap.lo = lo;
    g_cap.hi = hi;
    g_cap.length = length;
    g_cap.selStart = (int)selStart;
    g_cap.selEnd = (int)selEnd;
    g_cap.kind = kind;
    g_cap.old = g_cap.small;
    if (hi - lo >= HIST_SMALL) g_cap.old = (WCHAR *)mem_alloc(((size_t)(hi - lo) + 1) * sizeof(WCHAR));
    if (!g_cap.old) g_cap.failed = 1;
    if (!g_cap.failed && hi > lo) {
        void *lock = NULL;
        int got = 0;
        const WCHAR *text = EditLockText(&lock, &got);
        if (!text || got != length) g_cap.failed = 1;
        else memcpy(g_cap.old, text + lo, (size_t)(hi - lo) * sizeof(WCHAR));
        EditUnlockText(lock);
    }
    return 1;
}

static BOOL Merge(HistItem *top, HistItem *item) {
    WCHAR *joined;
    if (item->kind == HK_OTHER || top->kind != item->kind) return FALSE;
    if (item->kind == HK_TYPE) {
        if (item->textLen != 0 || top->endsLine || item->pos != top->pos + top->replaceLen) return FALSE;
        top->replaceLen += item->replaceLen;
        top->endsLine = item->endsLine;
    } else {
        int back = item->kind == HK_BACK;
        if (back ? item->pos + item->textLen != top->pos : item->pos != top->pos) return FALSE;
        joined = (WCHAR *)mem_alloc(((size_t)item->textLen + top->textLen + 1) * sizeof(WCHAR));
        if (!joined) return FALSE;
        memcpy(joined, back ? item->text : top->text, (size_t)(back ? item->textLen : top->textLen) * sizeof(WCHAR));
        memcpy(joined + (back ? item->textLen : top->textLen), back ? top->text : item->text, (size_t)(back ? top->textLen : item->textLen) * sizeof(WCHAR));
        joined[item->textLen + top->textLen] = 0;
        g_bytes += ItemBytes(item);
        mem_free(top->text);
        top->text = joined;
        top->textLen += item->textLen;
        if (back) top->pos = item->pos;
    }
    top->redoStart = item->redoStart;
    top->redoEnd = item->redoEnd;
    mem_free(item->text);
    item->text = NULL;
    return TRUE;
}

static void Push(HistItem *item) {
    ClearStack(g_redo, &g_redoCount);
    if (g_open && g_undoCount && Merge(&g_undo[g_undoCount - 1], item)) {
        Trim();
        return;
    }
    if (g_undoCount == HIST_MAX) DropOldest();
    g_undo[g_undoCount++] = *item;
    g_bytes += ItemBytes(item);
    g_open = item->kind != HK_OTHER;
    Trim();
}

void HistLeave(void) {
    void *lock = NULL;
    DWORD selStart = 0, selEnd = 0;
    HistItem item;
    const WCHAR *text = NULL;
    int length, oldLen, newLen, head = 0, tail = 0, cutLen, putLen, hint, got = 0, kind;
    if (!g_cap.active) return;
    g_cap.active = 0;
    length = GetWindowTextLengthW(g_edit);
    oldLen = g_cap.hi - g_cap.lo;
    newLen = oldLen + length - g_cap.length;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (g_cap.failed || newLen < 0 || g_cap.lo + newLen > length) {
        if (g_cap.failed || length != g_cap.length) HistClear();
        CapFree();
        return;
    }
    if (newLen > 0 || length > 0) text = EditLockText(&lock, &got);
    if (newLen > 0 && (!text || got != length)) {
        EditUnlockText(lock);
        CapFree();
        HistClear();
        return;
    }
    kind = g_cap.kind;
    hint = (kind == HK_BACK ? (int)selStart : g_cap.selStart) - g_cap.lo;
    if (hint < 0) hint = 0;
    TextTrim(g_cap.old, oldLen, text ? text + g_cap.lo : NULL, newLen, hint, &head, &tail);
    cutLen = oldLen - head - tail;
    putLen = newLen - head - tail;
    memset(&item, 0, sizeof item);
    if (cutLen == 0 && putLen == 0) {
        EditUnlockText(lock);
        CapFree();
        return;
    }
    if (cutLen > 0) {
        item.text = (WCHAR *)mem_alloc(((size_t)cutLen + 1) * sizeof(WCHAR));
        if (!item.text) {
            EditUnlockText(lock);
            CapFree();
            HistClear();
            return;
        }
        memcpy(item.text, g_cap.old + head, (size_t)cutLen * sizeof(WCHAR));
        item.text[cutLen] = 0;
    }
    if (kind == HK_TYPE && putLen == 0) kind = HK_OTHER;
    if ((kind == HK_BACK || kind == HK_DEL) && (putLen != 0 || cutLen == 0)) kind = HK_OTHER;
    item.pos = g_cap.lo + head;
    item.textLen = cutLen;
    item.replaceLen = putLen;
    item.undoStart = g_cap.selStart;
    item.undoEnd = g_cap.selEnd;
    item.redoStart = (int)selStart;
    item.redoEnd = (int)selEnd;
    item.kind = kind;
    item.endsLine = putLen > 0 && text[item.pos + putLen - 1] == '\n';
    EditUnlockText(lock);
    CapFree();
    Push(&item);
}

static BOOL Replay(HistItem *item, BOOL undoing) {
    void *lock = NULL;
    const WCHAR *text;
    WCHAR *current = NULL, *swap;
    int length = 0, after, selStart, selEnd, tempLen;
    text = EditLockText(&lock, &length);
    if (!text || item->pos < 0 || item->pos + item->replaceLen > length) {
        EditUnlockText(lock);
        return FALSE;
    }
    if (item->replaceLen > 0) {
        current = (WCHAR *)mem_alloc(((size_t)item->replaceLen + 1) * sizeof(WCHAR));
        if (!current) {
            EditUnlockText(lock);
            return FALSE;
        }
        memcpy(current, text + item->pos, (size_t)item->replaceLen * sizeof(WCHAR));
        current[item->replaceLen] = 0;
    }
    EditUnlockText(lock);
    g_busy++;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)item->pos, (LPARAM)(item->pos + item->replaceLen));
    SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)(item->text ? item->text : L""));
    EditNativeUndoClear();
    after = GetWindowTextLengthW(g_edit);
    if (after != length - item->replaceLen + item->textLen) {
        g_busy--;
        mem_free(current);
        return FALSE;
    }
    swap = item->text;
    item->text = current;
    mem_free(swap);
    tempLen = item->textLen;
    item->textLen = item->replaceLen;
    item->replaceLen = tempLen;
    selStart = undoing ? item->undoStart : item->redoStart;
    selEnd = undoing ? item->undoEnd : item->redoEnd;
    if (selStart > after) selStart = after;
    if (selEnd > after) selEnd = after;
    if (selEnd < selStart) selEnd = selStart;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selEnd);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    g_busy--;
    return TRUE;
}

static BOOL Step(HistItem *from, int *fromCount, HistItem *to, int *toCount, BOOL undoing) {
    HistItem item;
    if (!*fromCount || g_cap.active) return FALSE;
    item = from[--*fromCount];
    g_bytes -= ItemBytes(&item);
    g_open = 0;
    if (!Replay(&item, undoing)) {
        g_bytes += ItemBytes(&item);
        from[(*fromCount)++] = item;
        HistClear();
        return FALSE;
    }
    item.kind = HK_OTHER;
    to[(*toCount)++] = item;
    g_bytes += ItemBytes(&item);
    Trim();
    return TRUE;
}

BOOL HistUndo(void) {
    return Step(g_undo, &g_undoCount, g_redo, &g_redoCount, TRUE);
}

BOOL HistRedo(void) {
    return Step(g_redo, &g_redoCount, g_undo, &g_undoCount, FALSE);
}
