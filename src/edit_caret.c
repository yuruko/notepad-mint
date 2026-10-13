#include "edit_internal.h"

static struct { HFONT font; DWORD textRev; int length, dx, start, width, tab, rtl, valid; } g_endCaret;

void EditCaretInvalidate(void) { g_endCaret.valid = 0; }

int EditCaretInView(HWND hwnd, int height) {
    POINT caretPos;
    RECT view, formatRect;
    if (GetFocus() != hwnd || !GetCaretPos(&caretPos)) return 0;
    EditViewRect(hwnd, &view);
    SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&formatRect);
    if (!IntersectRect(&view, &view, &formatRect)) return 0;
    if (view.bottom - view.top < height) height = view.bottom - view.top;
    return caretPos.x >= view.left && caretPos.x < view.right && caretPos.y >= view.top && caretPos.y + height <= view.bottom;
}

void EditCaretRemember(HWND hwnd, HFONT font) {
    GUITHREADINFO gi;
    RECT view, formatRect;
    LRESULT previous;
    int length = GetWindowTextLengthW(hwnd);
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (length <= 0 || EditCaretIndex() != length || !GetGUIThreadInfo(GetCurrentThreadId(), &gi) || gi.hwndCaret != hwnd || gi.hwndFocus != hwnd || gi.rcCaret.top <= -20000) return;
    EditViewRect(hwnd, &view);
    if (gi.rcCaret.bottom - gi.rcCaret.top > view.bottom - view.top) return;
    previous = SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)(length - 1), 0);
    if (previous == -1) return;
    SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&formatRect);
    g_endCaret.font = font; g_endCaret.textRev = g_textRev; g_endCaret.length = length;
    g_endCaret.dx = gi.rcCaret.left - (short)LOWORD(previous);
    g_endCaret.start = (int)SendMessageW(hwnd, EM_LINEINDEX, (WPARAM)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)length, 0), 0);
    g_endCaret.width = formatRect.right - formatRect.left; g_endCaret.tab = g_pf.tab; g_endCaret.rtl = EditIsRtl();
    g_endCaret.valid = 1;
}

static int CaretEndX(HWND hwnd, HFONT font, int index, const RECT *formatRect) {
    void *lock;
    const WCHAR *text;
    int length, start = (int)SendMessageW(hwnd, EM_LINEINDEX, (WPARAM)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)index, 0), 0);
    int x = EditIsRtl() ? formatRect->right - 1 : formatRect->left;
    LRESULT pos;
    if (index <= start || index <= 0) return x;
    pos = SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)(index - 1), 0);
    if (pos == -1) return x;
    x = (short)LOWORD(pos);
    if (g_endCaret.valid && g_endCaret.font == font && g_endCaret.textRev == g_textRev && g_endCaret.length == index && g_endCaret.start == start && g_endCaret.width == formatRect->right - formatRect->left && g_endCaret.tab == g_pf.tab && g_endCaret.rtl == EditIsRtl()) return x + g_endCaret.dx;
    text = EditLockText(&lock, &length);
    if (text && index <= length) {
        WCHAR last = text[index - 1];
        WORD type = 0, direction = 0;
        int advance = 0;
        GetStringTypeW(CT_CTYPE3, &last, 1, &type);
        GetStringTypeW(2, &last, 1, &direction);
        if (last == '\t' && g_pf.tab > 0) {
            HDC hdc = GetDC(hwnd);
            TEXTMETRICW tm;
            HGDIOBJ oldFont;
            memset(&tm, 0, sizeof tm);
            if (hdc) {
                oldFont = SelectObject(hdc, font);
                GetTextMetricsW(hdc, &tm);
                SelectObject(hdc, oldFont);
                ReleaseDC(hwnd, hdc);
            }
            advance = g_pf.tab * tm.tmAveCharWidth;
            if (advance > 0) {
                LRESULT origin = SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)start, 0);
                int offset = x - (origin == -1 ? formatRect->left : (short)LOWORD(origin));
                offset %= advance;
                if (offset < 0) offset += advance;
                advance -= offset;
            }
        } else if (!(last >= 0xDC00 && last <= 0xDFFF) && !(type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK))) {
            HDC hdc = GetDC(hwnd);
            SIZE size = { 0, 0 };
            if (hdc) {
                HGDIOBJ oldFont = SelectObject(hdc, font);
                INT widths[32];
                int runStart = index - start > COUNTOF(widths) ? index - COUNTOF(widths) : start, count = index - runStart;
                if (GetTextExtentExPointW(hdc, text + runStart, count, 0x7FFFFFFF, NULL, widths, &size)) advance = widths[count - 1] - (count > 1 ? widths[count - 2] : 0);
                SelectObject(hdc, oldFont);
                ReleaseDC(hwnd, hdc);
            }
        }
        if (advance < 0) advance = 0;
        x += direction == 2 || (direction != 1 && direction != 3 && direction != 6 && EditIsRtl()) ? -advance : advance;
    }
    EditUnlockText(lock);
    return x;
}

void EditCaretFit(HWND hwnd, HFONT font, WNDPROC nativeProc, int *tall) {
    static int fitting;
    GUITHREADINFO gi;
    RECT view, formatRect;
    int index, x, y, width;
    LRESULT pos;
    if (fitting) return;
    fitting = 1;
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &gi) || gi.hwndCaret != hwnd || gi.hwndFocus != hwnd) goto done;
    EditViewRect(hwnd, &view);
    if (gi.rcCaret.bottom - gi.rcCaret.top <= view.bottom - view.top) {
        *tall = 0;
        EditCaretRemember(hwnd, font);
        EditCaretDisplayAdjusted(FALSE);
        goto done;
    }
    *tall = 1;
    if (view.right <= view.left || view.bottom <= view.top) goto done;
    if (gi.rcCaret.right > view.left && gi.rcCaret.left < view.right && gi.rcCaret.bottom > view.top && gi.rcCaret.top < view.bottom) goto done;
    index = EditCaretIndex();
    CallWindowProcW(nativeProc, hwnd, EM_SCROLLCARET, 0, 0);
    SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&formatRect);
    pos = SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)index, 0);
    x = pos == -1 ? CaretEndX(hwnd, font, index, &formatRect) : (short)LOWORD(pos);
    y = pos == -1 ? formatRect.top : (short)HIWORD(pos);
    width = gi.rcCaret.right - gi.rcCaret.left;
    if (width < 1) width = 1;
    if (x > view.right - width) x = view.right - width;
    if (x < view.left) x = view.left;
    if (y >= view.bottom || y < view.top) y = view.top;
    if (SetCaretPos(x, y)) {
        EditCaretHint(index);
        EditCaretDisplayAdjusted(TRUE);
    }
done:
    fitting = 0;
}
