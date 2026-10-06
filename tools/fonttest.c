/* fonttest.c - throwaway: which face/charset combinations draw '\' as a backslash on this machine?
 * build: see tools\fonttest.bat (links against build\rt.obj from the main build) */
#include "..\src\w32.h"

static const WCHAR *g_faces[] = { L"Tahoma", L"Segoe UI", L"Microsoft Sans Serif", L"Arial", L"Verdana", L"Consolas", L"Lucida Console", L"Courier New", L"MS UI Gothic", L"Meiryo UI" };
static const int g_cs[] = { 0, 1 };

static LRESULT CALLBACK P(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        int y = 4, i, k;
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < 10; i++) {
            for (k = 0; k < 2; k++) {
                WCHAR t[160];
                HFONT f = CreateFontW(-16, 0, 0, 0, 400, 0, 0, 0, g_cs[k], 0, 0, 5, 0, g_faces[i]);
                HGDIOBJ o = SelectObject(dc, f);
                wsprintfW(t, L"%s  charset=%d   C:\\dir\\file.txt   yen-sign: \x00A5", g_faces[i], g_cs[k]);
                TextOutW(dc, 6, y, t, lstrlenW(t));
                SelectObject(dc, o);
                DeleteObject(f);
                y += 22;
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int ft_main(void)
{
    WNDCLASSEXW wc;
    MSG msg;
    HINSTANCE hi = GetModuleHandleW(NULL);
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = P;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(0);       /* WHITE_BRUSH */
    wc.lpszClassName = L"fonttest";
    RegisterClassExW(&wc);
    CreateWindowExW(0, L"fonttest", L"fonttest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 760, 520, NULL, NULL, hi, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

void start(void) { ExitProcess((UINT)ft_main()); }
