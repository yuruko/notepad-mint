/* Inspect resource ownership in the app's thread, without remote pointers. */
#include "ui_probe.h"
#ifdef SHOTDC
LRESULT UiProbeFontAndDialogs(WPARAM operation, LPARAM font)
{
    if (operation == 0) return (LRESULT)g_fontUI;
    if (operation == 1) return (LRESULT)g_fontUIB;
    if (operation == 4) {
        HDC dc = GetDC(g_hwnd);
        HGDIOBJ old;
        TEXTMETRICW tm;
        int height = 0;
        if (!dc || !font) { if (dc) ReleaseDC(g_hwnd, dc); return 0; }
        old = SelectObject(dc, (HFONT)font);
        if (old && old != (HGDIOBJ)(INT_PTR)-1) {
            if (GetTextMetricsW(dc, &tm)) height = tm.tmHeight;
            SelectObject(dc, old);
        }
        ReleaseDC(g_hwnd, dc);
        return height;
    }
    if (operation == 2 || operation == 3) {
        HWND find = FindDlgHwnd(), outer, inner;
        int mask = 0;
        BOOL wasEnabled;
        if (!find || !IsWindow(find)) return -1;
        wasEnabled = IsWindowEnabled(find);
        if (operation == 2) {
            if (wasEnabled) mask |= 1;
            outer = DialogHoldFind();
            if (outer == find) mask |= 2;
            if (!IsWindowEnabled(find)) mask |= 4;
            inner = DialogHoldFind();
            if (!inner) mask |= 8;
            DialogReleaseFind(inner);
            if (!IsWindowEnabled(find)) mask |= 16;
            DialogReleaseFind(outer);
            if (IsWindowEnabled(find)) mask |= 32;
        } else {
            if (wasEnabled) EnableWindow(find, FALSE);
            outer = DialogHoldFind();
            if (!outer) mask |= 1;
            DialogReleaseFind(outer);
            if (!IsWindowEnabled(find)) mask |= 2;
            if (wasEnabled) EnableWindow(find, TRUE);
            if (IsWindowEnabled(find) == wasEnabled) mask |= 4;
        }
        return mask;
    }
    return 0;
}
#endif
