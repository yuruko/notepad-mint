#include "edit_internal.h"

#define MULTI_TIMER 0x4D51

typedef struct Caret { int anchor, active, primary; } Caret;

static Caret *g_extra;
static int g_extraCount, g_extraCap, g_busy, g_altClick, g_blinkOn = 1, g_freeze, g_frozenLine, g_frozenX;

static int CaretStart(const Caret *caret) { return caret->anchor < caret->active ? caret->anchor : caret->active; }
static int CaretEnd(const Caret *caret) { return caret->anchor < caret->active ? caret->active : caret->anchor; }

int EditMultiCount(void) { return g_extraCount; }

void EditFreeze(BOOL on) {
    HWND hwnd = g_edit;
    int line;
    if (!hwnd) return;
    if (on) {
        if (g_freeze++) return;
        g_frozenLine = (int)EditCtl(EM_GETFIRSTVISIBLELINE, 0, 0);
        g_frozenX = EditHScroll();
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        return;
    }
    if (!g_freeze || --g_freeze) return;
    line = (int)EditCtl(EM_GETFIRSTVISIBLELINE, 0, 0);
    if (line != g_frozenLine) EditCtl(EM_LINESCROLL, 0, (LPARAM)(g_frozenLine - line));
    if (EditHScroll() != g_frozenX) EditCtl(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, g_frozenX), 0);
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    EditScrollSoon();
    SbarSync(hwnd);
    AppUpdateStatus();
}

static void VisibleRange(int *firstIndex, int *lastIndex) {
    RECT formatRect;
    int first = (int)EditCtl(EM_GETFIRSTVISIBLELINE, 0, 0), lines = (int)EditCtl(EM_GETLINECOUNT, 0, 0), pitch = EditLineHeight(), last;
    EditCtl(EM_GETRECT, 0, (LPARAM)&formatRect);
    last = first + (formatRect.bottom - formatRect.top) / (pitch > 0 ? pitch : 1) + 2;
    if (first > 0) first--;
    *firstIndex = (int)EditCtl(EM_LINEINDEX, (WPARAM)first, 0);
    *lastIndex = last < lines ? (int)EditCtl(EM_LINEINDEX, (WPARAM)last, 0) : (int)EditCtl(WM_GETTEXTLENGTH, 0, 0);
}

static void CaretsInvalidate(void) {
    int i, pitch = EditLineHeight(), firstIndex, lastIndex;
    if (!g_edit || !g_extraCount) return;
    VisibleRange(&firstIndex, &lastIndex);
    for (i = 0; i < g_extraCount; i++) {
        POINT pos;
        RECT box;
        if (g_extra[i].active < firstIndex || g_extra[i].active > lastIndex || !EditIndexPos(g_extra[i].active, &pos)) continue;
        box.left = pos.x - S(2); box.top = pos.y; box.right = pos.x + S(3); box.bottom = pos.y + pitch;
        InvalidateRect(g_edit, &box, FALSE);
    }
}

static void BlinkReset(void) {
    UINT blink = GetCaretBlinkTime();
    g_blinkOn = 1;
    if (!g_edit) return;
    if (g_extraCount && blink && blink < 0x7FFFFFFF) SetTimer(g_edit, MULTI_TIMER, blink, NULL);
    else KillTimer(g_edit, MULTI_TIMER);
}

void EditMultiClear(void) {
    if (!g_extraCount) return;
    g_extraCount = 0;
    BlinkReset();
    if (g_edit) InvalidateRect(g_edit, NULL, FALSE);
}

static void NativeGet(Caret *caret) {
    DWORD selStart = 0, selEnd = 0;
    int active = EditCaretIndex();
    EditCtl(EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (active != (int)selStart && active != (int)selEnd) active = (int)selEnd;
    caret->active = active;
    caret->anchor = active == (int)selStart ? (int)selEnd : (int)selStart;
    caret->primary = 1;
}

static void NativeSet(const Caret *caret) {
    int busy = g_busy;
    g_busy = 1;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)caret->anchor, (LPARAM)caret->active);
    g_busy = busy;
}

static void SortCarets(Caret *list, int count) {
    int gap, i, j;
    for (gap = count / 2; gap > 0; gap /= 2) {
        for (i = gap; i < count; i++) {
            Caret item = list[i];
            for (j = i; j >= gap && CaretStart(&list[j - gap]) > CaretStart(&item); j -= gap) list[j] = list[j - gap];
            list[j] = item;
        }
    }
}

static int MergeList(Caret *list, int count) {
    int i, out = 0;
    SortCarets(list, count);
    for (i = 0; i < count; i++) {
        int start = CaretStart(&list[i]), end = CaretEnd(&list[i]);
        if (out) {
            Caret *last = &list[out - 1];
            int lastStart = CaretStart(last), lastEnd = CaretEnd(last);
            if (start < lastEnd || (start == lastEnd && (start == end || lastStart == lastEnd))) {
                const Caret *keep = list[i].primary ? &list[i] : last;
                int forward = keep->active >= keep->anchor, newStart = lastStart < start ? lastStart : start, newEnd = lastEnd > end ? lastEnd : end;
                last->primary |= list[i].primary;
                last->anchor = forward ? newStart : newEnd;
                last->active = forward ? newEnd : newStart;
                continue;
            }
        }
        list[out++] = list[i];
    }
    return out;
}

static Caret *Gather(int *count) {
    Caret *list = (Caret *)mem_alloc(((size_t)g_extraCount + 1) * sizeof(Caret));
    if (!list) return NULL;
    NativeGet(&list[0]);
    memcpy(list + 1, g_extra, (size_t)g_extraCount * sizeof(Caret));
    *count = g_extraCount + 1;
    return list;
}

static int PrimaryOf(const Caret *list, int count) {
    int i;
    for (i = 0; i < count; i++) if (list[i].primary) return i;
    return count - 1;
}

static int *Pairs(const Caret *list, int count) {
    int *pairs = (int *)mem_alloc((size_t)count * 2 * sizeof(int)), i, k = 2, primary = PrimaryOf(list, count);
    if (!pairs) return NULL;
    pairs[0] = list[primary].anchor; pairs[1] = list[primary].active;
    for (i = 0; i < count; i++) {
        if (i == primary) continue;
        pairs[k++] = list[i].anchor; pairs[k++] = list[i].active;
    }
    return pairs;
}

static void Store(Caret *list, int count) {
    int i, k = 0, primary = PrimaryOf(list, count);
    if (count - 1 > g_extraCap) {
        Caret *grown = (Caret *)mem_realloc(g_extra, (size_t)(count - 1) * sizeof(Caret));
        if (grown) { g_extra = grown; g_extraCap = count - 1; }
    }
    NativeSet(&list[primary]);
    for (i = 0; i < count && k < g_extraCap; i++) {
        if (i == primary) continue;
        g_extra[k] = list[i];
        g_extra[k].primary = 0;
        k++;
    }
    g_extraCount = k;
    BlinkReset();
}

void EditMultiRestore(const int *pairs, int count) {
    Caret *list;
    int i;
    if (!g_edit) return;
    if (count < 2 || !pairs) {
        EditMultiClear();
        if (pairs && count == 1) {
            Caret caret;
            caret.anchor = pairs[0]; caret.active = pairs[1]; caret.primary = 1;
            NativeSet(&caret);
        }
        return;
    }
    list = (Caret *)mem_alloc((size_t)count * sizeof(Caret));
    if (!list) return;
    for (i = 0; i < count; i++) {
        int length = (int)EditCtl(WM_GETTEXTLENGTH, 0, 0);
        list[i].anchor = ClampInt(pairs[2 * i], 0, length);
        list[i].active = ClampInt(pairs[2 * i + 1], 0, length);
        list[i].primary = i == 0;
    }
    count = MergeList(list, count);
    Store(list, count);
    mem_free(list);
    InvalidateRect(g_edit, NULL, FALSE);
}

void EditMultiNormalize(void) {
    Caret *list;
    int count = 0, merged;
    if (!g_extraCount) return;
    list = Gather(&count);
    if (!list) return;
    merged = MergeList(list, count);
    Store(list, merged);
    mem_free(list);
    InvalidateRect(g_edit, NULL, FALSE);
}

static void Apply(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WCHAR **texts) {
    Caret *list;
    int count = 0, beforeCount, i, delta = 0, floor = 0, *before, *after;
    list = Gather(&count);
    if (!list) return;
    count = MergeList(list, count);
    beforeCount = count;
    before = Pairs(list, count);
    g_busy = 1;
    EditFreeze(TRUE);
    HistGroupBegin();
    for (i = 0; i < count; i++) {
        int length = GetWindowTextLengthW(hwnd), anchor = list[i].anchor + delta, active = list[i].active + delta;
        DWORD selStart = 0, selEnd = 0;
        anchor = ClampInt(anchor, floor, length);
        active = ClampInt(active, floor, length);
        EditWordBounds(floor, i + 1 < count ? CaretStart(&list[i + 1]) + delta : -1);
        SendMessageW(hwnd, EM_SETSEL, (WPARAM)anchor, (LPARAM)active);
        if (texts) SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)(texts[i] ? texts[i] : L""));
        else SendMessageW(hwnd, msg, wParam, lParam);
        delta += GetWindowTextLengthW(hwnd) - length;
        EditCtl(EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        list[i].anchor = (int)selStart;
        list[i].active = (int)selEnd;
        floor = (int)selEnd;
    }
    EditWordBounds(-1, -1);
    count = MergeList(list, count);
    after = Pairs(list, count);
    HistGroupEnd(before, before ? beforeCount : 0, after, after ? count : 0);
    Store(list, count);
    EditFreeze(FALSE);
    g_busy = 0;
    mem_free(before);
    mem_free(after);
    mem_free(list);
}

static void Move(UINT msg, WPARAM wParam, LPARAM lParam) {
    Caret *list;
    int count = 0, i;
    list = Gather(&count);
    if (!list) return;
    HistBreak();
    g_busy = 1;
    EditFreeze(TRUE);
    for (i = 0; i < count; i++) {
        DWORD selStart = 0, selEnd = 0;
        int anchor = list[i].anchor;
        EditCtl(EM_SETSEL, (WPARAM)list[i].anchor, (LPARAM)list[i].active);
        EditCtl(msg, wParam, lParam);
        EditCtl(EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selStart == selEnd || (int)selStart == anchor) { list[i].anchor = (int)selStart; list[i].active = (int)selEnd; }
        else { list[i].anchor = (int)selEnd; list[i].active = (int)selStart; }
    }
    count = MergeList(list, count);
    Store(list, count);
    EditFreeze(FALSE);
    g_busy = 0;
    mem_free(list);
}

static void Copy(void) {
    Caret *list;
    void *lock = NULL;
    const WCHAR *text;
    int count = 0, i, length = 0, total = 0, pieces = 0;
    HGLOBAL memory;
    WCHAR *out;
    list = Gather(&count);
    if (!list) return;
    count = MergeList(list, count);
    for (i = 0; i < count; i++) if (CaretEnd(&list[i]) > CaretStart(&list[i])) { total += CaretEnd(&list[i]) - CaretStart(&list[i]); pieces++; }
    if (!pieces) { mem_free(list); return; }
    total += 2 * (pieces - 1);
    memory = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)total + 1) * sizeof(WCHAR));
    out = memory ? (WCHAR *)GlobalLock(memory) : NULL;
    text = out ? EditLockText(&lock, &length) : NULL;
    if (!out || !text) {
        EditUnlockText(lock);
        if (out) GlobalUnlock(memory);
        if (memory) GlobalFree(memory);
        mem_free(list);
        return;
    }
    for (i = 0, total = 0; i < count; i++) {
        int start = ClampInt(CaretStart(&list[i]), 0, length), end = ClampInt(CaretEnd(&list[i]), 0, length);
        if (end <= start) continue;
        if (total) { out[total++] = '\r'; out[total++] = '\n'; }
        memcpy(out + total, text + start, (size_t)(end - start) * sizeof(WCHAR));
        total += end - start;
    }
    out[total] = 0;
    EditUnlockText(lock);
    GlobalUnlock(memory);
    if (OpenClipboard(g_edit)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        CloseClipboard();
    } else GlobalFree(memory);
    mem_free(list);
}

static void Cut(HWND hwnd) {
    Copy();
    Apply(hwnd, WM_CLEAR, 0, 0, NULL);
}

static void InsertEach(HWND hwnd, const WCHAR *text) {
    WCHAR **texts;
    WCHAR *copy = NULL;
    int count = g_extraCount + 1, i, lines = 1, length = wlen(text);
    texts = (WCHAR **)mem_zalloc((size_t)count * sizeof(WCHAR *));
    if (!texts) return;
    for (i = 0; i + 1 < length; i++) if (text[i] == '\r' && text[i + 1] == '\n') lines++;
    if (count > 1 && lines == count && (copy = (WCHAR *)mem_alloc(((size_t)length + 1) * sizeof(WCHAR))) != NULL) {
        int k = 0, start = 0;
        memcpy(copy, text, ((size_t)length + 1) * sizeof(WCHAR));
        for (i = 0; i <= length && k < count; i++) {
            if (i < length && !(copy[i] == '\r' && i + 1 < length && copy[i + 1] == '\n')) continue;
            if (i < length) copy[i] = 0;
            texts[k++] = copy + start;
            start = i + 2;
            i++;
        }
    } else {
        for (i = 0; i < count; i++) texts[i] = (WCHAR *)text;
    }
    Apply(hwnd, EM_REPLACESEL, TRUE, 0, texts);
    mem_free(copy);
    mem_free(texts);
}

static void Paste(HWND hwnd) {
    HANDLE data;
    const WCHAR *source;
    WCHAR *text = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_edit)) return;
    data = GetClipboardData(CF_UNICODETEXT);
    source = data ? (const WCHAR *)GlobalLock(data) : NULL;
    if (source) { text = TextToCrlf(source, (int)(GlobalSize(data) / sizeof(WCHAR)), NULL); GlobalUnlock(data); }
    CloseClipboard();
    if (!text) return;
    InsertEach(hwnd, text);
    mem_free(text);
}

BOOL EditMultiInsert(const WCHAR *text) {
    if (!g_extraCount || g_busy || !g_edit) return FALSE;
    InsertEach(g_edit, text);
    return TRUE;
}

BOOL EditMultiHasSel(void) {
    int i;
    for (i = 0; i < g_extraCount; i++) if (g_extra[i].anchor != g_extra[i].active) return TRUE;
    return FALSE;
}

BOOL EditMultiSelected(int index) {
    int lo = 0, hi = g_extraCount;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (CaretStart(&g_extra[mid]) <= index) lo = mid + 1;
        else hi = mid;
    }
    return lo > 0 && index < CaretEnd(&g_extra[lo - 1]);
}

static int SnapIndex(int index) {
    void *lock = NULL;
    int length = 0;
    const WCHAR *text = EditLockText(&lock, &length);
    if (text && index > 0 && index < length) {
        if (text[index - 1] == '\r' && text[index] == '\n') index--;
        else if (text[index] >= 0xDC00 && text[index] <= 0xDFFF && text[index - 1] >= 0xD800 && text[index - 1] <= 0xDBFF) index--;
    }
    EditUnlockText(lock);
    return index;
}

static BOOL AltClick(HWND hwnd, LPARAM lParam) {
    POINT pt;
    Caret native;
    int index, i;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    g_altClick = 1;
    HistBreak();
    if (GetFocus() != hwnd) return FALSE;
    index = SnapIndex(EditIndexFromPoint(pt));
    for (i = 0; i < g_extraCount; i++) {
        if (g_extra[i].anchor != index || g_extra[i].active != index) continue;
        memmove(g_extra + i, g_extra + i + 1, (size_t)(g_extraCount - i - 1) * sizeof(Caret));
        g_extraCount--;
        BlinkReset();
        InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
    }
    NativeGet(&native);
    if (native.anchor == index && native.active == index) {
        if (g_extraCount) {
            Caret last = g_extra[--g_extraCount];
            last.primary = 1;
            NativeSet(&last);
            BlinkReset();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return TRUE;
    }
    if (g_extraCount + 1 > g_extraCap) {
        int cap = g_extraCap ? g_extraCap * 2 : 16;
        Caret *grown = (Caret *)mem_realloc(g_extra, (size_t)cap * sizeof(Caret));
        if (!grown) return FALSE;
        g_extra = grown;
        g_extraCap = cap;
    }
    native.primary = 0;
    g_extra[g_extraCount++] = native;
    SortCarets(g_extra, g_extraCount);
    BlinkReset();
    InvalidateRect(hwnd, NULL, FALSE);
    return FALSE;
}

static int LineStartOf(const WCHAR *text, int index) {
    while (index > 0 && text[index - 1] != '\n' && text[index - 1] != '\r') index--;
    return index;
}

static int LineEndOf(const WCHAR *text, int length, int index) {
    while (index < length && text[index] != '\n' && text[index] != '\r') index++;
    return index;
}

static int BreakEnd(const WCHAR *text, int length, int index) {
    if (index < length && text[index] == '\r') return index + 1 < length && text[index + 1] == '\n' ? index + 2 : index + 1;
    return index < length && text[index] == '\n' ? index + 1 : index;
}

static int BreakStart(const WCHAR *text, int index) {
    if (index > 0 && text[index - 1] == '\n') return index > 1 && text[index - 2] == '\r' ? index - 2 : index - 1;
    return index > 0 && text[index - 1] == '\r' ? index - 1 : index;
}

typedef struct Block { int start, end, from, to, shift; WCHAR *put; } Block;

void EditMoveLines(int dir) {
    Caret *list;
    Block *blocks;
    void *lock = NULL;
    const WCHAR *text;
    WCHAR *buffer = NULL;
    int count = 0, length = 0, i, j, blockCount = 0, beforeCount, *before, *after, used = 0;
    HWND hwnd = g_edit;
    if (!hwnd) return;
    list = Gather(&count);
    if (!list) return;
    count = MergeList(list, count);
    beforeCount = count;
    before = Pairs(list, count);
    blocks = (Block *)mem_alloc((size_t)count * sizeof(Block));
    text = blocks ? EditLockText(&lock, &length) : NULL;
    if (!text || !before) {
        EditUnlockText(lock);
        mem_free(before);
        mem_free(blocks);
        mem_free(list);
        return;
    }
    for (i = 0; i < count; i++) {
        int start = CaretStart(&list[i]), end = CaretEnd(&list[i]);
        if (end > start && end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r')) {
            end = BreakStart(text, end);
            if (end < start) end = start;
        }
        start = LineStartOf(text, start);
        end = LineEndOf(text, length, end);
        if (blockCount && start <= BreakEnd(text, length, blocks[blockCount - 1].end)) {
            if (end > blocks[blockCount - 1].end) blocks[blockCount - 1].end = end;
            continue;
        }
        blocks[blockCount].start = start;
        blocks[blockCount].end = end;
        blockCount++;
    }
    if (!blockCount || (dir < 0 && blocks[0].start == 0) || (dir > 0 && BreakEnd(text, length, blocks[blockCount - 1].end) == blocks[blockCount - 1].end)) {
        EditUnlockText(lock);
        mem_free(before);
        mem_free(blocks);
        mem_free(list);
        return;
    }
    for (i = 0; i < blockCount; i++) {
        Block *block = &blocks[i];
        if (dir < 0) {
            int breakStart = BreakStart(text, block->start), lineStart = LineStartOf(text, breakStart);
            block->from = lineStart;
            block->to = block->end;
            block->shift = lineStart - block->start;
        } else {
            int breakEnd = BreakEnd(text, length, block->end), lineEnd = LineEndOf(text, length, breakEnd);
            block->from = block->start;
            block->to = lineEnd;
            block->shift = lineEnd - block->end;
        }
        used += block->to - block->from + 1;
    }
    buffer = (WCHAR *)mem_alloc((size_t)used * sizeof(WCHAR));
    if (!buffer) {
        EditUnlockText(lock);
        mem_free(before);
        mem_free(blocks);
        mem_free(list);
        return;
    }
    for (i = 0, used = 0; i < blockCount; i++) {
        Block *block = &blocks[i];
        WCHAR *put = buffer + used;
        int k = 0;
        if (dir < 0) {
            int breakStart = BreakStart(text, block->start);
            memcpy(put, text + block->start, (size_t)(block->end - block->start) * sizeof(WCHAR)); k += block->end - block->start;
            memcpy(put + k, text + breakStart, (size_t)(block->start - breakStart) * sizeof(WCHAR)); k += block->start - breakStart;
            memcpy(put + k, text + block->from, (size_t)(breakStart - block->from) * sizeof(WCHAR)); k += breakStart - block->from;
        } else {
            int breakEnd = BreakEnd(text, length, block->end);
            memcpy(put, text + breakEnd, (size_t)(block->to - breakEnd) * sizeof(WCHAR)); k += block->to - breakEnd;
            memcpy(put + k, text + block->end, (size_t)(breakEnd - block->end) * sizeof(WCHAR)); k += breakEnd - block->end;
            memcpy(put + k, text + block->start, (size_t)(block->end - block->start) * sizeof(WCHAR)); k += block->end - block->start;
        }
        put[k] = 0;
        block->put = put;
        used += k + 1;
    }
    for (i = 0; i < count; i++) {
        for (j = 0; j < blockCount; j++) {
            int last = BreakEnd(text, length, blocks[j].end);
            if (CaretStart(&list[i]) < blocks[j].start || CaretEnd(&list[i]) > last) continue;
            list[i].anchor = ClampInt(list[i].anchor + blocks[j].shift, 0, length);
            list[i].active = ClampInt(list[i].active + blocks[j].shift, 0, length);
            break;
        }
    }
    EditUnlockText(lock);
    g_busy = 1;
    EditFreeze(TRUE);
    HistBreak();
    HistGroupBegin();
    for (i = 0; i < blockCount; i++) {
        SendMessageW(hwnd, EM_SETSEL, (WPARAM)blocks[i].from, (LPARAM)blocks[i].to);
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)blocks[i].put);
    }
    after = Pairs(list, count);
    HistGroupEnd(before, beforeCount, after, after ? count : 0);
    HistBreak();
    Store(list, count);
    EditFreeze(FALSE);
    g_busy = 0;
    mem_free(before);
    mem_free(after);
    mem_free(buffer);
    mem_free(blocks);
    mem_free(list);
}

BOOL EditMultiRoute(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result) {
    int ctrl, shift;
    if (g_busy || hwnd != g_edit) return FALSE;
    *result = 0;
    switch (msg) {
    case WM_SYSKEYDOWN:
        if (wParam == VK_MENU && !(lParam & 0x40000000)) g_altClick = 0;
        return FALSE;
    case WM_SYSKEYUP:
        if (wParam == VK_MENU && g_altClick) { g_altClick = 0; return TRUE; }
        return FALSE;
    case WM_LBUTTONDOWN:
        if ((GetKeyState(VK_MENU) & 0x8000) && !(wParam & (MK_SHIFT | MK_CONTROL))) return AltClick(hwnd, lParam);
        EditMultiClear();
        return FALSE;
    case WM_LBUTTONDBLCLK:
        if (!(GetKeyState(VK_MENU) & 0x8000)) EditMultiClear();
        return FALSE;
    case WM_TIMER:
        if (wParam != MULTI_TIMER) return FALSE;
        if (!g_extraCount || GetFocus() != hwnd) {
            KillTimer(hwnd, MULTI_TIMER);
            return TRUE;
        }
        g_blinkOn = !g_blinkOn;
        CaretsInvalidate();
        return TRUE;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        if (g_extraCount) { BlinkReset(); InvalidateRect(hwnd, NULL, FALSE); }
        return FALSE;
    }
    if (!g_extraCount) return FALSE;
    switch (msg) {
    case WM_CHAR:
        if (wParam == 3) { Copy(); return TRUE; }
        if (wParam == 0x18) { Cut(hwnd); return TRUE; }
        if (wParam == 0x16) { Paste(hwnd); return TRUE; }
        if (wParam == 8 || wParam == 9 || wParam == 10 || wParam == 13 || wParam >= 0x20) {
            Apply(hwnd, msg, wParam, lParam, NULL);
            return TRUE;
        }
        return FALSE;
    case WM_KEYDOWN:
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wParam) {
        case VK_ESCAPE:
            EditMultiClear();
            return TRUE;
        case VK_DELETE:
            if (shift && !ctrl) Cut(hwnd);
            else Apply(hwnd, msg, wParam, lParam, NULL);
            return TRUE;
        case VK_INSERT:
            if (shift && !ctrl) { Paste(hwnd); return TRUE; }
            if (ctrl && !shift) { Copy(); return TRUE; }
            return FALSE;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
            Move(msg, wParam, lParam);
            return TRUE;
        case VK_PRIOR: case VK_NEXT:
            EditMultiClear();
            return FALSE;
        }
        BlinkReset();
        return FALSE;
    case WM_COPY:
        Copy();
        return TRUE;
    case WM_CUT:
        Cut(hwnd);
        return TRUE;
    case WM_PASTE:
        Paste(hwnd);
        return TRUE;
    case WM_CLEAR:
        Apply(hwnd, WM_CLEAR, 0, 0, NULL);
        return TRUE;
    case WM_SETTEXT: case EM_SETSEL: case EM_REPLACESEL: case WM_IME_STARTCOMPOSITION:
        EditMultiClear();
        return FALSE;
    }
    return FALSE;
}

void EditMultiPaint(HWND hwnd, HDC hdc, const RECT *clip) {
    RECT view, box;
    HDC ownDc = NULL;
    void *lock = NULL;
    const WCHAR *text;
    COLORREF selectedFg = GetSysColor(COLOR_HIGHLIGHTTEXT), selectedBg = GetSysColor(COLOR_HIGHLIGHT);
    int saved, length = 0, pitch, first, last, lineCount, i, cell, caretWidth = 1, firstIndex, lastIndex, caretHidden = 0;
    if (!g_extraCount || hwnd != g_edit) return;
    EditViewRect(hwnd, &view);
    if (clip && !IntersectRect(&view, &view, clip)) return;
    if (view.right <= view.left || view.bottom <= view.top) return;
    text = EditLockText(&lock, &length);
    if (!text) { EditUnlockText(lock); return; }
    if (!hdc) {
        ownDc = hdc = GetDC(hwnd);
        if (!hdc) { EditUnlockText(lock); return; }
        caretHidden = GetFocus() == hwnd && HideCaret(hwnd);
    }
    saved = SaveDC(hdc);
    if (!saved) goto done;
    IntersectClipRect(hdc, view.left, view.top, view.right, view.bottom);
    SelectObject(hdc, EditFont());
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, selectedFg);
    pitch = EditLineHeight();
    cell = EditCellWidth();
    lineCount = (int)EditCtl(EM_GETLINECOUNT, 0, 0);
    first = (int)EditCtl(EM_GETFIRSTVISIBLELINE, 0, 0);
    {
        RECT formatRect;
        EditCtl(EM_GETRECT, 0, (LPARAM)&formatRect);
        last = first + (view.bottom - formatRect.top + pitch - 1) / pitch + 1;
    }
    if (last > lineCount) last = lineCount;
    if (first > 0) first--;
    firstIndex = (int)EditCtl(EM_LINEINDEX, (WPARAM)first, 0);
    lastIndex = last < lineCount ? (int)EditCtl(EM_LINEINDEX, (WPARAM)last, 0) : length;
    for (i = 0; i < g_extraCount && !EditIsRtl(); i++) {
        int start = CaretStart(&g_extra[i]), end = CaretEnd(&g_extra[i]), row;
        if (end <= start || end <= firstIndex || start >= lastIndex) continue;
        row = (int)EditCtl(EM_LINEFROMCHAR, (WPARAM)(start > firstIndex ? start : firstIndex), 0);
        for (; row < last; row++) {
            int rowStart = (int)EditCtl(EM_LINEINDEX, (WPARAM)row, 0), rowEnd = row + 1 < lineCount ? (int)EditCtl(EM_LINEINDEX, (WPARAM)(row + 1), 0) : length;
            int from, to, textEnd, k, segment;
            const int *xs;
            POINT origin, at;
            if (rowStart < 0 || rowStart >= end || rowEnd > length) break;
            from = start > rowStart ? start : rowStart;
            to = end < rowEnd ? end : rowEnd;
            if (from >= to || !EditIndexPos(rowStart, &origin)) continue;
            if (origin.y >= view.bottom || origin.y + pitch <= view.top) continue;
            for (textEnd = rowEnd; textEnd > rowStart && (text[textEnd - 1] == '\n' || text[textEnd - 1] == '\r'); textEnd--) {}
            xs = EditRowXs(hdc, text, rowStart, rowEnd, origin.x);
            box.top = origin.y; box.bottom = origin.y + pitch;
            if (xs && to > from && textEnd > from) {
                box.left = xs[from - rowStart];
                box.right = xs[(to < textEnd ? to : textEnd) - rowStart];
                FillC(hdc, &box, selectedBg);
            }
            for (k = xs && textEnd > from ? textEnd : from; k < to;) {
                int x0, x1;
                if (xs) { x0 = xs[k - rowStart]; x1 = x0 + cell; }
                else {
                    POINT p0, p1;
                    if (!EditIndexPos(k, &p0)) { k++; continue; }
                    x0 = p0.x;
                    x1 = k + 1 < rowEnd && EditIndexPos(k + 1, &p1) && p1.y == p0.y ? p1.x : x0 + cell;
                }
                if (k >= textEnd) {
                    if (k == textEnd && textEnd < rowEnd && row + 1 < lineCount) { box.left = x0; box.right = x0 + cell; FillC(hdc, &box, selectedBg); }
                    break;
                }
                box.left = x0 < x1 ? x0 : x1; box.right = x0 < x1 ? x1 : x0;
                FillC(hdc, &box, selectedBg);
                k++;
            }
            for (k = from; k < to && k < textEnd;) {
                if (text[k] < 0x20) { k++; continue; }
                for (segment = k; segment < to && segment < textEnd && text[segment] >= 0x20; segment++) {}
                if (xs) { at.x = xs[k - rowStart]; at.y = origin.y; }
                else if (!EditIndexPos(k, &at)) { k = segment; continue; }
                TextOutW(hdc, at.x, origin.y, text + k, segment - k);
                k = segment;
            }
        }
    }
    if (GetFocus() == hwnd && g_blinkOn) {
        SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &caretWidth, 0);
        if (caretWidth < 1) caretWidth = 1;
        for (i = 0; i < g_extraCount; i++) {
            POINT pos;
            if (g_extra[i].active < firstIndex || g_extra[i].active > lastIndex || !EditIndexPos(g_extra[i].active, &pos)) continue;
            if (pos.y >= view.bottom || pos.y + pitch <= view.top) continue;
            box.left = pos.x; box.right = pos.x + caretWidth; box.top = pos.y; box.bottom = pos.y + pitch;
            FillC(hdc, &box, C_EDIT_FG);
        }
    }
    RestoreDC(hdc, saved);
done:
    if (caretHidden) ShowCaret(hwnd);
    if (ownDc) ReleaseDC(hwnd, ownDc);
    EditUnlockText(lock);
}
