#include "edit_internal.h"

static const WCHAR *TextLock(HLOCAL *hText, int *length) {
    *hText = (HLOCAL)SendMessageW(g_edit, EM_GETHANDLE, 0, 0);
    *length = GetWindowTextLengthW(g_edit);
    return *hText ? (const WCHAR *)LocalLock(*hText) : NULL;
}

const WCHAR *EditLockText(void **hText, int *length) {
    return TextLock((HLOCAL *)hText, length);
}

void EditUnlockText(void *hText) {
    if (hText) LocalUnlock((HLOCAL)hText);
}

BOOL EditHasSel(void) {
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    return selStart != selEnd;
}

static int g_activeCaret, g_caretAdjusted;

void EditCaretHint(int index) { g_activeCaret = index; }
void EditCaretDisplayAdjusted(BOOL adjusted) { g_caretAdjusted = adjusted; }

int EditCaretIndex(void) {
    DWORD selStart = 0, selEnd = 0;
    POINT caretPos;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selStart == selEnd || (g_activeCaret != (int)selStart && g_activeCaret != (int)selEnd)) g_activeCaret = (int)selEnd;
    if (selStart != selEnd && !g_caretAdjusted && GetFocus() == g_edit && GetCaretPos(&caretPos) && caretPos.x > -20000 && caretPos.y > -20000) {
        LRESULT startPos = SendMessageW(g_edit, EM_POSFROMCHAR, selStart, 0), endPos = SendMessageW(g_edit, EM_POSFROMCHAR, selEnd, 0);
        if (startPos != -1 && endPos != -1) {
            int startDist = (caretPos.x - (short)LOWORD(startPos)), startDy = (caretPos.y - (short)HIWORD(startPos));
            int endDist = (caretPos.x - (short)LOWORD(endPos)), endDy = (caretPos.y - (short)HIWORD(endPos));
            startDist = (startDist < 0 ? -startDist : startDist) + 64 * (startDy < 0 ? -startDy : startDy);
            endDist = (endDist < 0 ? -endDist : endDist) + 64 * (endDy < 0 ? -endDy : endDy);
            g_activeCaret = (int)(startDist < endDist ? selStart : selEnd);
        }
    }
    return g_activeCaret;
}

void EditCaretPos(int *line, int *col) {
    static struct { DWORD textRev; int length, index, line, col, valid; } c;
    HLOCAL hText;
    int length, index = EditCaretIndex(), i;
    const WCHAR *text;
    *line = 1; *col = 1;
    length = GetWindowTextLengthW(g_edit);
    if (c.valid && c.textRev == g_textRev && c.length == length && c.index == index) { *line = c.line; *col = c.col; return; }
    text = TextLock(&hText, &length);
    if (!text) return;
    if (index > length) index = length;
    if (c.valid && c.textRev == g_textRev && c.length == length && (index > c.index ? index - c.index : c.index - index) < index) {
        int spanStart = index < c.index ? index : c.index, span = index > c.index ? index - c.index : c.index - index;
        int breaks = (int)mp_count_lf(text + spanStart, (size_t)span);
        *line = c.line + (index < c.index ? -breaks : breaks);
        if (!breaks) *col = c.col + (index - c.index);
        else {
            for (i = index; i > 0 && text[i - 1] != '\n'; i--) {}
            *col = index - i + 1;
        }
    } else {
        *line = 1 + (int)mp_count_lf(text, (size_t)index);
        for (i = index; i > 0 && text[i - 1] != '\n'; i--) {}
        *col = index - i + 1;
    }
    LocalUnlock(hText);
    c.textRev = g_textRev; c.length = length; c.index = index; c.line = *line; c.col = *col; c.valid = 1;
}

BOOL EditSelStats(int enc, int eol, int *lines, DWORD *bytes) {
    static struct { DWORD selStart, selEnd, textRev; int length, enc, eol, lines, valid; DWORD bytes; } c;
    DWORD selStart = 0, selEnd = 0;
    HLOCAL hText;
    int length;
    const WCHAR *text;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd <= selStart) return FALSE;
    if (!c.valid || c.selStart != selStart || c.selEnd != selEnd || c.textRev != g_textRev || c.length != GetWindowTextLengthW(g_edit) || c.enc != enc || c.eol != eol) {
        text = TextLock(&hText, &length);
        if (!text) return FALSE;
        if (selEnd > (DWORD)length) selEnd = (DWORD)length;
        if (selStart >= selEnd) { LocalUnlock(hText); return FALSE; }
        c.lines = (int)mp_count_lf(text + selStart, (size_t)(selEnd - selStart)) + (text[selEnd - 1] == '\n' ? 0 : 1);
        c.bytes = DocBodySize(text + selStart, (int)(selEnd - selStart), enc, eol);
        LocalUnlock(hText);
        c.selStart = selStart; c.selEnd = selEnd; c.textRev = g_textRev; c.length = length; c.enc = enc; c.eol = eol; c.valid = 1;
    }
    *lines = c.lines;
    *bytes = c.bytes;
    return TRUE;
}

BOOL EditGotoLine(int line) {
    HLOCAL hText;
    int length, currentLine = 1, i = 0;
    const WCHAR *text = TextLock(&hText, &length);
    if (!text) return line <= 1;
    if (line < 1) line = 1;
    while (currentLine < line) {
        while (i < length && text[i] != '\n') i++;
        if (i >= length) { LocalUnlock(hText); return FALSE; }
        i++; currentLine++;
    }
    LocalUnlock(hText);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)i, (LPARAM)i);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
    return TRUE;
}

void EditClearLine(void) {
    HLOCAL hText;
    int length, index = EditCaretIndex(), start, end;
    const WCHAR *text = TextLock(&hText, &length);
    if (!text) return;
    if (index > length) index = length;
    for (start = index; start > 0 && text[start - 1] != '\n'; start--) {}
    for (end = index; end < length && text[end] != '\n'; end++) {}
    if (end < length && end > start && text[end - 1] == '\r') end--;
    LocalUnlock(hText);
    if (start == end) return;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)start, (LPARAM)end);
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

BOOL EditSetDocText(const WCHAR *text) {
    BOOL ok;
    SendMessageW(g_edit, WM_SETREDRAW, FALSE, 0);
    ok = SetWindowTextW(g_edit, text ? text : L"");
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

WCHAR *EditGetDocText(int *length) {
    int textLength = GetWindowTextLengthW(g_edit), got;
    WCHAR *buffer = (WCHAR *)mem_alloc(((size_t)textLength + 1) * sizeof(WCHAR));
    if (!buffer) return NULL;
    got = GetWindowTextW(g_edit, buffer, textLength + 1);
    if (got != textLength) { mem_free(buffer); return NULL; }
    buffer[got] = 0;
    if (length) *length = got;
    return buffer;
}

void EditInsert(const WCHAR *text) {
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)text);
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
}

int WordClass(WCHAR ch) {
    if (ch == ' ' || ch == '\t') return 0;
    if (ch >= 0x80) return 1;
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') return 1;
    return 2;
}

void EditDeleteWord(int dir) {
    DWORD selStart = 0, selEnd = 0;
    HLOCAL hText;
    int length, start, end;
    const WCHAR *text;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selStart != selEnd) { SendMessageW(g_edit, WM_CLEAR, 0, 0); return; }
    text = TextLock(&hText, &length);
    if (!text) return;
    start = end = (int)selStart;
    if (start > length) start = end = length;
    if (dir < 0) {
        if (start > 0 && text[start - 1] == '\n') {
            start--;
            if (start > 0 && text[start - 1] == '\r') start--;
        } else {
            while (start > 0 && WordClass(text[start - 1]) == 0) start--;
            if (start > 0 && text[start - 1] != '\n') {
                int wordClass = WordClass(text[start - 1]);
                while (start > 0 && text[start - 1] != '\n' && WordClass(text[start - 1]) == wordClass) start--;
            }
        }
    } else {
        if (end < length && text[end] == '\r') {
            end++;
            if (end < length && text[end] == '\n') end++;
        } else if (end < length && text[end] == '\n') {
            end++;
        } else {
            if (end < length && WordClass(text[end]) != 0) {
                int wordClass = WordClass(text[end]);
                while (end < length && text[end] != '\r' && WordClass(text[end]) == wordClass) end++;
            } while (end < length && WordClass(text[end]) == 0) end++;
        }
    }
    LocalUnlock(hText);
    if (start != end) {
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)start, (LPARAM)end);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    }
}
