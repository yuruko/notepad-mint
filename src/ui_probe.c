#include "ui_probe.h"
#ifdef SHOTDC
LRESULT UiProbeFontAndDialogs(WPARAM operation, LPARAM font) {
    if (operation == 0) return (LRESULT)g_fontUI;
    if (operation == 1) return (LRESULT)g_fontUIB;
    if (operation == 4) {
        HDC hdc = GetDC(g_hwnd);
        HGDIOBJ oldFont;
        TEXTMETRICW tm;
        int height = 0;
        if (!hdc || !font) { if (hdc) ReleaseDC(g_hwnd, hdc); return 0; }
        oldFont = SelectObject(hdc, (HFONT)font);
        if (oldFont && oldFont != (HGDIOBJ)(INT_PTR)-1) {
            if (GetTextMetricsW(hdc, &tm)) height = tm.tmHeight;
            SelectObject(hdc, oldFont);
        }
        ReleaseDC(g_hwnd, hdc);
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
