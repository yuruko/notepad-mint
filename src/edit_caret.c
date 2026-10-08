/* edit_caret.c - native caret geometry across font and viewport changes.
 * EDIT keeps selection and undo ownership. These helpers observe its caret and,
 * only when a row is taller than the viewport, position that same native object. */
#include "edit_internal.h"

static struct { HFONT font; DWORD rev; int n, dx, start, width, tab, rtl, ok; } g_endCaret;

void EditCaretInvalidate(void) { g_endCaret.ok = 0; }

/* During an expensive drag we can keep the previous wrap only while its caret is
 * still visible in the new viewport. In particular, the old formatting rectangle
 * can extend beyond a shrinking window until the deferred layout catches up. */
int EditCaretInView(HWND h, int height)
{
    POINT p;
    RECT r, fr;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    if (GetFocus() != h || !GetCaretPos(&p)) return 0;
    GetClientRect(h, &r);
    if (!(st & WS_VSCROLL)) r.right -= S(SBAR_TRIM);
    if (!(st & WS_HSCROLL)) r.bottom -= S(SBAR_TRIM);
    SendMessageW(h, EM_GETRECT, 0, (LPARAM)&fr);
    if (!IntersectRect(&r, &r, &fr)) return 0;
    if (r.bottom - r.top < height) height = r.bottom - r.top;
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y + height <= r.bottom;
}

void EditCaretRemember(HWND h, HFONT font)
{
    GUITHREADINFO gi;
    RECT view, fr;
    LRESULT previous;
    int n = GetWindowTextLengthW(h);
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (n <= 0 || EditCaretIndex() != n || !GetGUIThreadInfo(GetCurrentThreadId(), &gi) ||
        gi.hwndCaret != h || gi.hwndFocus != h || gi.rcCaret.top <= -20000) return;
    GetClientRect(h, &view);
    if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_HSCROLL)) view.bottom -= S(SBAR_TRIM);
    if (gi.rcCaret.bottom - gi.rcCaret.top > view.bottom - view.top) return; /* never cache a position we clipped ourselves */
    previous = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)(n - 1), 0);
    if (previous == -1) return;
    SendMessageW(h, EM_GETRECT, 0, (LPARAM)&fr);
    g_endCaret.font = font; g_endCaret.rev = g_textRev; g_endCaret.n = n;
    g_endCaret.dx = gi.rcCaret.left - (short)LOWORD(previous);
    g_endCaret.start = (int)SendMessageW(h, EM_LINEINDEX, (WPARAM)SendMessageW(h, EM_LINEFROMCHAR, (WPARAM)n, 0), 0);
    g_endCaret.width = fr.right - fr.left; g_endCaret.tab = g_pf.tab; g_endCaret.rtl = EditIsRtl();
    g_endCaret.ok = 1;
}

/* EM_POSFROMCHAR excludes EOF. Its previous UTF-16 position already includes a
 * trailing low surrogate or combining mark; otherwise add that final glyph's
 * advance, in its native reading direction. A bounded suffix supplies shaping
 * context without measuring or allocating a whole, potentially huge line. */
static int CaretEndX(HWND h, HFONT font, int index, const RECT *fr)
{
    void *lock;
    const WCHAR *text;
    int n, start = (int)SendMessageW(h, EM_LINEINDEX,
        (WPARAM)SendMessageW(h, EM_LINEFROMCHAR, (WPARAM)index, 0), 0);
    int x = EditIsRtl() ? fr->right - 1 : fr->left;
    LRESULT pos;
    if (index <= start || index <= 0) return x;
    pos = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)(index - 1), 0);
    if (pos == -1) return x;
    x = (short)LOWORD(pos);
    if (g_endCaret.ok && g_endCaret.font == font && g_endCaret.rev == g_textRev && g_endCaret.n == index &&
        g_endCaret.start == start && g_endCaret.width == fr->right - fr->left && g_endCaret.tab == g_pf.tab && g_endCaret.rtl == EditIsRtl())
        return x + g_endCaret.dx;
    text = EditLockText(&lock, &n);
    if (text && index <= n) {
        WCHAR last = text[index - 1];
        WORD type = 0, direction = 0;
        int advance = 0;
        GetStringTypeW(CT_CTYPE3, &last, 1, &type);
        GetStringTypeW(2, &last, 1, &direction);    /* CT_CTYPE2; C2_RIGHTTOLEFT = 2 */
        if (last == '\t' && g_pf.tab > 0) {
            HDC dc = GetDC(h);
            TEXTMETRICW tm;
            HGDIOBJ old;
            memset(&tm, 0, sizeof tm);
            if (dc) {
                old = SelectObject(dc, font);
                GetTextMetricsW(dc, &tm);
                SelectObject(dc, old);
                ReleaseDC(h, dc);
            }
            advance = g_pf.tab * tm.tmAveCharWidth;
            if (advance > 0) {
                LRESULT origin = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)start, 0);
                int offset = x - (origin == -1 ? fr->left : (short)LOWORD(origin));
                offset %= advance;
                if (offset < 0) offset += advance;
                advance -= offset;
            }
        } else if (!(last >= 0xDC00 && last <= 0xDFFF) && !(type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK))) {
            HDC dc = GetDC(h);
            SIZE size = { 0, 0 };
            if (dc) {
                HGDIOBJ old = SelectObject(dc, font);
                INT widths[32], fit = 0;
                int a = index - start > COUNTOF(widths) ? index - COUNTOF(widths) : start, count = index - a;
                if (GetTextExtentExPointW(dc, text + a, count, 0x7FFFFFFF, &fit, widths, &size))
                    advance = widths[count - 1] - (count > 1 ? widths[count - 2] : 0);
                SelectObject(dc, old);
                ReleaseDC(h, dc);
            }
        }
        if (advance < 0) advance = 0;
        x += direction == 2 || (direction != 1 && direction != 3 && direction != 6 && EditIsRtl()) ? -advance : advance;
    }
    EditUnlockText(lock);
    return x;
}

void EditCaretFit(HWND h, HFONT font, WNDPROC nativeProc, int *tall)
{
    static int fitting;
    GUITHREADINFO gi;
    RECT view, fr;
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    int index, x, y, width;
    LRESULT pos;
    if (fitting) return;
    fitting = 1;
    memset(&gi, 0, sizeof gi);
    gi.cbSize = sizeof gi;
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &gi) || gi.hwndCaret != h || gi.hwndFocus != h) goto done;
    GetClientRect(h, &view);
    if (!(style & WS_VSCROLL)) view.right -= S(SBAR_TRIM);
    if (!(style & WS_HSCROLL)) view.bottom -= S(SBAR_TRIM);
    if (gi.rcCaret.bottom - gi.rcCaret.top <= view.bottom - view.top) {
        *tall = 0;
        EditCaretRemember(h, font);
        EditCaretDisplayAdjusted(FALSE);
        goto done;
    }
    *tall = 1;
    if (view.right <= view.left || view.bottom <= view.top) goto done;
    if (gi.rcCaret.right > view.left && gi.rcCaret.left < view.right &&
        gi.rcCaret.bottom > view.top && gi.rcCaret.top < view.bottom) goto done;
    index = EditCaretIndex();
    CallWindowProcW(nativeProc, h, EM_SCROLLCARET, 0, 0);
    SendMessageW(h, EM_GETRECT, 0, (LPARAM)&fr);
    pos = SendMessageW(h, EM_POSFROMCHAR, (WPARAM)index, 0);
    x = pos == -1 ? CaretEndX(h, font, index, &fr) : (short)LOWORD(pos);
    y = pos == -1 ? fr.top : (short)HIWORD(pos);
    width = gi.rcCaret.right - gi.rcCaret.left;
    if (width < 1) width = 1;
    if (x > view.right - width) x = view.right - width;
    if (x < view.left) x = view.left;
    if (y >= view.bottom || y < view.top) y = view.top;
    /* Keep the native caret object. EDIT parks it off screen when its height
     * exceeds the client; positioning that same object exposes its visible top. */
    if (SetCaretPos(x, y)) {
        EditCaretHint(index);
        EditCaretDisplayAdjusted(TRUE);          /* status / Ctrl+K must use the logical endpoint, not the clipped display position */
    }
done:
    fitting = 0;
}
