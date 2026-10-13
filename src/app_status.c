#include "app_internal.h"

#define STATS_BIG   1000000
#define STATS_TIMER 7
static int g_statsNow;

void AppUpdateStatus(void) {
    WCHAR label[64];
    int line, col, selLines;
    DWORD selBytes;
    if (!g_status || !g_pf.statusbar) return;
    EditCaretPos(&line, &col);
    if (EditSelStats(g_doc.enc, g_doc.eol, &selLines, &selBytes)) wsprintfW(label, L"%d:%d [%d L %u B]", line, col, selLines, selBytes);
    else wsprintfW(label, L"%d:%d", line, col);
    StatusSet(g_status, SB_POS, label);
    if (g_pf.cur != g_pf.pt) wsprintfW(label, L"%d%%", MulDiv(g_pf.cur, 100, g_pf.pt));
    else label[0] = 0;
    StatusSet(g_status, SB_ZOOM, label);
    {
        static struct { DWORD rev, bytes; int lines, textLength, enc, eol, valid; } c;
        int textLength = GetWindowTextLengthW(g_edit);
        int due = !c.valid || c.rev != g_textRev || c.textLength != textLength || c.enc != g_doc.enc || c.eol != g_doc.eol;
        if (due && c.valid && textLength > STATS_BIG && !g_statsNow && c.enc == g_doc.enc && c.eol == g_doc.eol) {
            SetTimer(g_hwnd, STATS_TIMER, 250, NULL);
            due = 0;
        }
        if (due) {
            void *lock = NULL;
            int length = 0;
            const WCHAR *text = EditLockText(&lock, &length);
            if (text) {
                c.bytes = DocEncodedSize(text, length, g_doc.enc, g_doc.eol);
                c.lines = 1 + (int)mp_count_lf(text, (size_t)length);
                c.rev = g_textRev; c.textLength = textLength; c.enc = g_doc.enc; c.eol = g_doc.eol; c.valid = 1;
            } else if (!c.valid) {
                c.bytes = 0;
                c.lines = 1;
            }
            EditUnlockText(lock);
        }
        wsprintfW(label, L"%d L", c.lines);
        StatusSet(g_status, SB_LINES, label);
        wsprintfW(label, L"%u B", c.bytes);
        StatusSet(g_status, SB_BYTES, label);
    }
    StatusSet(g_status, SB_EOL, g_eolShort[g_doc.eol]);
    EncShort(g_doc.enc, label, COUNTOF(label));
    StatusSet(g_status, SB_ENC, label);
    {
        RECT client, windowRect;
        int minWidth = StatusMinWidth();
        GetClientRect(g_hwnd, &client);
        if (client.right > 0 && client.right < minWidth && !IsZoomed(g_hwnd) && !IsIconic(g_hwnd)) {
            GetWindowRect(g_hwnd, &windowRect);
            SetWindowPos(g_hwnd, NULL, 0, 0, windowRect.right - windowRect.left + (minWidth - client.right), windowRect.bottom - windowRect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

void AppStatusTimer(HWND hwnd, WPARAM timer) {
    if (timer != STATS_TIMER) return;
    KillTimer(hwnd, STATS_TIMER);
    g_statsNow = 1;
    AppUpdateStatus();
    g_statsNow = 0;
}
