/* w32_theme.h - declarations the theme switch needs (to be merged into src/w32.h) */
#ifndef W32_THEME_H
#define W32_THEME_H

#define GCL_HBRBACKGROUND (-10)
#define RDW_INVALIDATE  0x0001
#define RDW_ERASE       0x0004
#define RDW_ALLCHILDREN 0x0080
#define RDW_FRAME       0x0400

API DWORD   WINAPI GetCurrentThreadId(void);                                       /* kernel32 */
API BOOL    WINAPI EnumThreadWindows(DWORD, BOOL (CALLBACK *)(HWND, LPARAM), LPARAM);   /* user32 */
API HWND    WINAPI GetActiveWindow(void);
API DWORD   WINAPI SetClassLongW(HWND, int, LONG);                                /* = SetClassLongPtrW on 32-bit */

#endif
