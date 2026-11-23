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
    int kind, endsLine, step;
    int *cursors;
} HistItem;

typedef struct HistStack {
    HistItem *items;
    int count, cap, steps;
} HistStack;

static HistStack g_undo, g_redo;
static HistItem *g_pend;
static int g_pendCount, g_pendCap, g_group, g_stepSeq;
static int g_open, g_busy;
static size_t g_bytes;
static struct { int active, failed, lo, hi, length, selStart, selEnd, kind; WCHAR *old; WCHAR small[HIST_SMALL]; } g_cap;

static size_t CursorBytes(const int *cursors) {
    return cursors ? (size_t)(2 + 2 * (cursors[0] + cursors[1])) * sizeof(int) : 0;
}

static size_t ItemBytes(const HistItem *item) {
    return (size_t)item->textLen * sizeof(WCHAR) + CursorBytes(item->cursors);
}

static void ItemFree(HistItem *item) {
    g_bytes -= ItemBytes(item);
    mem_free(item->text);
    mem_free(item->cursors);
    item->text = NULL;
    item->cursors = NULL;
    item->textLen = 0;
}

static void ClearStack(HistStack *stack) {
    while (stack->count > 0) ItemFree(&stack->items[--stack->count]);
    stack->steps = 0;
}

static void PendClear(void) {
    while (g_pendCount > 0) mem_free(g_pend[--g_pendCount].text);
}

static int TopStep(const HistStack *stack) {
    int n = 1;
    if (!stack->count) return 0;
    while (n < stack->count && stack->items[stack->count - 1 - n].step == stack->items[stack->count - 1].step) n++;
    return n;
}

static BOOL StackPush(HistStack *stack, const HistItem *item) {
    if (stack->count == stack->cap) {
        int cap = stack->cap ? stack->cap * 2 : 256;
        HistItem *grown = (HistItem *)mem_realloc(stack->items, (size_t)cap * sizeof(HistItem));
        if (!grown) return FALSE;
        stack->items = grown;
        stack->cap = cap;
    }
    stack->items[stack->count++] = *item;
    return TRUE;
}

static void DropOldest(void) {
    int n = 1, i;
    while (n < g_undo.count && g_undo.items[n].step == g_undo.items[0].step) n++;
    for (i = 0; i < n; i++) ItemFree(&g_undo.items[i]);
    memmove(g_undo.items, g_undo.items + n, (size_t)(g_undo.count - n) * sizeof(HistItem));
    g_undo.count -= n;
    g_undo.steps--;
}

static void Trim(void) {
    while (g_bytes > HIST_BYTES && g_undo.steps > 1) DropOldest();
}

void HistClear(void) {
    ClearStack(&g_undo);
    ClearStack(&g_redo);
    PendClear();
    g_open = 0;
}

void HistBreak(void) {
    g_open = 0;
}

BOOL HistCanUndo(void) {
    return g_undo.count > 0;
}

BOOL HistCanRedo(void) {
    return g_redo.count > 0;
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

static BOOL MergeOk(const HistItem *top, const HistItem *item, int shift) {
    int pos = top->pos + shift;
    if (item->kind == HK_OTHER || top->kind != item->kind) return FALSE;
    if (item->kind == HK_TYPE) return item->textLen == 0 && !top->endsLine && item->pos == pos + top->replaceLen;
    return item->kind == HK_BACK ? item->pos + item->textLen == pos : item->pos == pos;
}

static WCHAR *MergeText(const HistItem *top, const HistItem *item) {
    WCHAR *joined;
    int back = item->kind == HK_BACK;
    if (item->kind == HK_TYPE) return NULL;
    joined = (WCHAR *)mem_alloc(((size_t)item->textLen + top->textLen + 1) * sizeof(WCHAR));
    if (!joined) return NULL;
    memcpy(joined, back ? item->text : top->text, (size_t)(back ? item->textLen : top->textLen) * sizeof(WCHAR));
    memcpy(joined + (back ? item->textLen : top->textLen), back ? top->text : item->text, (size_t)(back ? top->textLen : item->textLen) * sizeof(WCHAR));
    joined[item->textLen + top->textLen] = 0;
    return joined;
}

static void MergeApply(HistItem *top, HistItem *item, int shift, WCHAR *joined) {
    top->pos += shift;
    if (item->kind == HK_TYPE) {
        top->replaceLen += item->replaceLen;
        top->endsLine = item->endsLine;
    } else {
        g_bytes += (size_t)item->textLen * sizeof(WCHAR);
        mem_free(top->text);
        top->text = joined;
        top->textLen += item->textLen;
        if (item->kind == HK_BACK) top->pos = item->pos;
    }
    top->redoStart = item->redoStart;
    top->redoEnd = item->redoEnd;
    mem_free(item->text);
    item->text = NULL;
}

static int *CursorsMake(const int *before, int beforeCount, const int *after, int afterCount) {
    int *cursors = (int *)mem_alloc((size_t)(2 + 2 * (beforeCount + afterCount)) * sizeof(int));
    if (!cursors) return NULL;
    cursors[0] = beforeCount;
    cursors[1] = afterCount;
    memcpy(cursors + 2, before, (size_t)beforeCount * 2 * sizeof(int));
    memcpy(cursors + 2 + 2 * beforeCount, after, (size_t)afterCount * 2 * sizeof(int));
    return cursors;
}

static void HeadSel(HistItem *head, const int *before, int beforeCount, const int *after, int afterCount) {
    int *cursors;
    if (!before || !after || beforeCount < 1 || afterCount < 1) return;
    head->undoStart = before[0] < before[1] ? before[0] : before[1];
    head->undoEnd = before[0] < before[1] ? before[1] : before[0];
    head->redoStart = after[0] < after[1] ? after[0] : after[1];
    head->redoEnd = after[0] < after[1] ? after[1] : after[0];
    if (beforeCount == 1 && afterCount == 1 && !head->cursors) return;
    cursors = CursorsMake(before, beforeCount, after, afterCount);
    g_bytes -= CursorBytes(head->cursors);
    mem_free(head->cursors);
    head->cursors = cursors;
    g_bytes += CursorBytes(cursors);
}

static BOOL MergeStep(const int *after, int afterCount) {
    int n = g_pendCount, base = g_undo.count - n, i, shift = 0, ok = 1;
    HistItem *head;
    WCHAR **joined;
    if (!g_open || n < 1 || base < 0 || TopStep(&g_undo) != n) return FALSE;
    for (i = 0; i < n; i++) {
        if (!MergeOk(&g_undo.items[base + i], &g_pend[i], shift)) return FALSE;
        shift += g_pend[i].replaceLen - g_pend[i].textLen;
    }
    joined = (WCHAR **)mem_zalloc((size_t)n * sizeof(WCHAR *));
    if (!joined) return FALSE;
    for (i = 0; i < n && ok; i++) if (g_pend[i].kind != HK_TYPE && !(joined[i] = MergeText(&g_undo.items[base + i], &g_pend[i]))) ok = 0;
    if (!ok) {
        for (i = 0; i < n; i++) mem_free(joined[i]);
        mem_free(joined);
        return FALSE;
    }
    for (i = 0, shift = 0; i < n; i++) {
        int delta = g_pend[i].replaceLen - g_pend[i].textLen;
        MergeApply(&g_undo.items[base + i], &g_pend[i], shift, joined[i]);
        shift += delta;
    }
    mem_free(joined);
    head = &g_undo.items[g_undo.count - 1];
    if (after && afterCount > 0) {
        if (head->cursors) {
            int *cursors = CursorsMake(head->cursors + 2, head->cursors[0], after, afterCount);
            if (cursors) {
                g_bytes -= CursorBytes(head->cursors);
                mem_free(head->cursors);
                head->cursors = cursors;
                g_bytes += CursorBytes(cursors);
            }
        }
        head->redoStart = after[0] < after[1] ? after[0] : after[1];
        head->redoEnd = after[0] < after[1] ? after[1] : after[0];
    }
    g_pendCount = 0;
    return TRUE;
}

static void Commit(const int *before, int beforeCount, const int *after, int afterCount) {
    int n = g_pendCount, i, step, open = 1;
    if (!n) return;
    ClearStack(&g_redo);
    if (MergeStep(after, afterCount)) {
        Trim();
        return;
    }
    if (g_undo.steps >= HIST_MAX) DropOldest();
    step = ++g_stepSeq;
    for (i = 0; i < n; i++) {
        g_pend[i].step = step;
        if (g_pend[i].kind == HK_OTHER || g_pend[i].kind != g_pend[0].kind) open = 0;
        if (!StackPush(&g_undo, &g_pend[i])) {
            while (g_undo.count > 0 && g_undo.items[g_undo.count - 1].step == step) g_undo.count--;
            PendClear();
            HistClear();
            return;
        }
        g_bytes += ItemBytes(&g_pend[i]);
    }
    g_undo.steps++;
    g_pendCount = 0;
    HeadSel(&g_undo.items[g_undo.count - 1], before, beforeCount, after, afterCount);
    g_open = open;
    Trim();
}

static void Pend(HistItem *item) {
    if (g_pendCount == g_pendCap) {
        int cap = g_pendCap ? g_pendCap * 2 : 16;
        HistItem *grown = (HistItem *)mem_realloc(g_pend, (size_t)cap * sizeof(HistItem));
        if (!grown) {
            mem_free(item->text);
            PendClear();
            HistClear();
            return;
        }
        g_pend = grown;
        g_pendCap = cap;
    }
    g_pend[g_pendCount++] = *item;
}

void HistGroupBegin(void) {
    PendClear();
    g_group = 1;
}

void HistGroupEnd(const int *before, int beforeCount, const int *after, int afterCount) {
    g_group = 0;
    Commit(before, beforeCount, after, afterCount);
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
    Pend(&item);
    if (!g_group) Commit(NULL, 0, NULL, 0);
}

static BOOL Replay(HistItem *item) {
    void *lock = NULL;
    const WCHAR *text;
    WCHAR *current = NULL, *swap;
    int length = 0, after, tempLen;
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
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)item->pos, (LPARAM)(item->pos + item->replaceLen));
    SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)(item->text ? item->text : L""));
    EditNativeUndoClear();
    after = GetWindowTextLengthW(g_edit);
    if (after != length - item->replaceLen + item->textLen) {
        mem_free(current);
        return FALSE;
    }
    g_bytes -= ItemBytes(item);
    swap = item->text;
    item->text = current;
    mem_free(swap);
    tempLen = item->textLen;
    item->textLen = item->replaceLen;
    item->replaceLen = tempLen;
    g_bytes += ItemBytes(item);
    return TRUE;
}

static BOOL Step(HistStack *from, HistStack *to, BOOL undoing) {
    int n, i, selStart, selEnd, length, *cursors;
    HistItem *head;
    if (!from->count || g_cap.active || g_group) return FALSE;
    n = TopStep(from);
    g_open = 0;
    head = &from->items[undoing ? from->count - 1 : from->count - n];
    cursors = head->cursors;
    selStart = undoing ? head->undoStart : head->redoStart;
    selEnd = undoing ? head->undoEnd : head->redoEnd;
    g_busy++;
    EditFreeze(TRUE);
    for (i = 0; i < n; i++) {
        if (!Replay(&from->items[from->count - 1 - i])) {
            EditFreeze(FALSE);
            g_busy--;
            HistClear();
            return FALSE;
        }
    }
    for (i = 0; i < n; i++) {
        HistItem item = from->items[--from->count];
        item.kind = HK_OTHER;
        if (!StackPush(to, &item)) {
            ItemFree(&item);
            EditFreeze(FALSE);
            g_busy--;
            HistClear();
            return FALSE;
        }
    }
    from->steps--;
    to->steps++;
    length = GetWindowTextLengthW(g_edit);
    if (cursors) EditMultiRestore(cursors + 2 + (undoing ? 0 : 2 * cursors[0]), undoing ? cursors[0] : cursors[1]);
    else {
        if (selStart > length) selStart = length;
        if (selEnd > length) selEnd = length;
        if (selEnd < selStart) selEnd = selStart;
        EditMultiRestore(NULL, 0);
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selEnd);
    }
    EditFreeze(FALSE);
    g_busy--;
    Trim();
    return TRUE;
}

BOOL HistUndo(void) {
    return Step(&g_undo, &g_redo, TRUE);
}

BOOL HistRedo(void) {
    return Step(&g_redo, &g_undo, FALSE);
}
