/* app_status.c - caret/selection panels and cached document statistics.
 * Large-document recounts wait for a typing pause; position updates remain live.
 * A wider panel may enlarge the main window, so callers must tolerate layout. */
#include "app_internal.h"

#define STATS_BIG   1000000                              /* characters: above this the line / byte counts are recounted when typing pauses, not on every key */
#define STATS_TIMER 7
static int g_statsNow;                                  /* the timer fired: recount now */

void AppUpdateStatus(void)
{
    WCHAR b[64];
    int line, col, sel;
    DWORD selBytes;
    if (!g_status || !g_pf.statusbar) return;
    EditCaretPos(&line, &col);
    if (EditSelStats(g_doc.enc, g_doc.eol, &sel, &selBytes)) wsprintfW(b, L"%d:%d [%d L %u B]", line, col, sel, selBytes);   /* "162:54 [5 L 54 B]": the lines it covers and the bytes a save would write for it */
    else wsprintfW(b, L"%d:%d", line, col);
    StatusSet(g_status, SB_POS, b);
    {                                                   /* the number of lines ("5 L") and the size a save would write ("124 B"), the lines first: recounted only when the text, the encoding or the line ending changed (a big file is not scanned on every caret move) */
        static struct { DWORD rev, bytes; int lines, len, enc, eol, ok; } c;
        int len = GetWindowTextLengthW(g_edit);
        int due = !c.ok || c.rev != g_textRev || c.len != len || c.enc != g_doc.enc || c.eol != g_doc.eol;
        if (due && c.ok && len > STATS_BIG && !g_statsNow && c.enc == g_doc.enc && c.eol == g_doc.eol) {
            SetTimer(g_hwnd, STATS_TIMER, 250, NULL);       /* a big text being typed in: the numbers follow when typing pauses (every recount is a pass over all of it) */
            due = 0;
        }
        if (due) {
            void *h = NULL;
            int n = 0;
            const WCHAR *p = EditLockText(&h, &n);
            if (p) {
                c.bytes = DocEncodedSize(p, n, g_doc.enc, g_doc.eol);
                c.lines = 1 + (int)mp_count_lf(p, (size_t)n); /* a trailing break adds an empty last line */
                c.rev = g_textRev; c.len = len; c.enc = g_doc.enc; c.eol = g_doc.eol; c.ok = 1;
            } else if (!c.ok) {
                c.bytes = 0;
                c.lines = 1;
            }
            EditUnlockText(h);
            /* A failed buffer lock must not cache blank totals as current. Keep
             * the last valid numbers until the next update can retry. */
        }
        wsprintfW(b, L"%d L", c.lines);
        StatusSet(g_status, SB_LINES, b);
        wsprintfW(b, L"%u B", c.bytes);
        StatusSet(g_status, SB_BYTES, b);
    }
    StatusSet(g_status, SB_EOL, g_eolShort[g_doc.eol]);
    EncShort(g_doc.enc, b, COUNTOF(b));
    StatusSet(g_status, SB_ENC, b);
    {                                                   /* nothing is ever cut off: a text wider than the panels were made for (a long code page name) widens a window that is too narrow */
        RECT cr, wr;
        int need = StatusMinWidth();
        GetClientRect(g_hwnd, &cr);
        if (cr.right > 0 && cr.right < need && !IsZoomed(g_hwnd) && !IsIconic(g_hwnd)) {
            GetWindowRect(g_hwnd, &wr);
            SetWindowPos(g_hwnd, NULL, 0, 0, wr.right - wr.left + (need - cr.right), wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

/* Called by the main window for every timer; unrelated timers are ignored. */
void AppStatusTimer(HWND h, WPARAM timer)
{
    if (timer != STATS_TIMER) return;
    KillTimer(h, STATS_TIMER);
    g_statsNow = 1;
    AppUpdateStatus();
    g_statsNow = 0;
}
