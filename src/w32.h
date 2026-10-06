/* w32.h - hand-written win32 declarations for notepad mint
 *
 * there is no windows.h, no crt and no third-party code in this project. every
 * type, constant and prototype the program needs is declared right here, and
 * the only things the exe links against are the os's own kernel32/user32/gdi32.
 * this is a 32-bit (x86) program: one exe runs on 32-bit windows, 64-bit windows
 * and windows on arm. struct layouts mirror the 32-bit sdk exactly (tools/layout_check).
 */
#ifndef W32_H
#define W32_H

#define API       __declspec(dllimport)
#define WINAPI    __stdcall
#define CALLBACK  __stdcall
#define NULL      ((void *)0)
#define TRUE      1
#define FALSE     0
#define COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---------------------------------------------------------------- types -- */
typedef unsigned char       BYTE;
typedef unsigned short      WORD, WCHAR, ATOM;
typedef unsigned long       DWORD, ULONG;
typedef long                LONG;
typedef short               SHORT;
typedef int                 BOOL, INT;
typedef unsigned int        UINT;
typedef unsigned __int64    ULONGLONG;
typedef __int64             LONGLONG;
typedef unsigned int        ULONG_PTR, UINT_PTR, SIZE_T, DWORD_PTR, WPARAM, size_t;      /* 32-bit: pointer sized == 32 bits */
typedef int                 LONG_PTR, INT_PTR, LPARAM, LRESULT;
typedef void               *PVOID, *LPVOID, *HANDLE, *HGDIOBJ, *HLOCAL, *HGLOBAL;
typedef const void         *LPCVOID;
typedef WCHAR              *LPWSTR;
typedef const WCHAR        *LPCWSTR;
typedef char               *LPSTR;
typedef const char         *LPCSTR;
typedef DWORD               COLORREF;
typedef DWORD              *LPDWORD;
typedef BOOL               *LPBOOL;

#define DECLARE_HANDLE(n) typedef struct n##__ { int unused; } *n
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HHOOK);
typedef HINSTANCE HMODULE;
typedef HICON     HCURSOR;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef int     (CALLBACK *FONTENUMPROCW)(const void *, const void *, DWORD, LPARAM);

#define LOWORD(l)   ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)   ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define LOBYTE(w)   ((BYTE)((DWORD_PTR)(w) & 0xff))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define MAKELPARAM(a, b) ((LPARAM)MAKELONG(a, b))
#define MAKEWPARAM(a, b) ((WPARAM)MAKELONG(a, b))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)((c) & 0xff))
#define GetGValue(c) ((BYTE)(((c) >> 8) & 0xff))
#define GetBValue(c) ((BYTE)(((c) >> 16) & 0xff))

/* -------------------------------------------------------------- structs -- */
typedef struct { LONG x, y; } POINT;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;

typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;

typedef struct {
    UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm;
} WNDCLASSEXW;

typedef struct {
    LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
    int cy, cx, y, x; LONG style; LPCWSTR lpszName, lpszClass; DWORD dwExStyle;
} CREATESTRUCTW;

typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { HWND hwnd, hwndInsertAfter; int x, y, cx, cy; UINT flags; } WINDOWPOS;
typedef struct { RECT rgrc[3]; WINDOWPOS *lppos; } NCCALCSIZE_PARAMS;
typedef struct { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { UINT CtlType, CtlID, itemID, itemAction, itemState; HWND hwndItem; HDC hDC; RECT rcItem; ULONG_PTR itemData; } DRAWITEMSTRUCT;
typedef struct { UINT CtlType, CtlID, itemID, itemWidth, itemHeight; ULONG_PTR itemData; } MEASUREITEMSTRUCT;
typedef struct { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT;
typedef struct { HWND hwndFrom; UINT_PTR idFrom; UINT code; } NMHDR;
typedef struct { BYTE fVirt; WORD key; WORD cmd; } ACCEL;
typedef struct { UINT cbSize, fMask; int nMin, nMax; UINT nPage; int nPos, nTrackPos; } SCROLLINFO;

typedef struct {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[32];
} LOGFONTW;
typedef struct { LOGFONTW elfLogFont; WCHAR elfFullName[64]; WCHAR elfStyle[32]; WCHAR elfScript[32]; } ENUMLOGFONTEXW;
typedef struct {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth,
         tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICW;

typedef struct {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[260]; WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct { int cbSize; LPCWSTR lpszDocName, lpszOutput, lpszDatatype; DWORD fwType; } DOCINFOW;

typedef struct {
    DWORD cb; LPWSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2; BYTE *lpReserved2; HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;

#pragma pack(push, 2)
typedef struct { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant; } BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;

/* printdlg / pagesetupdlg (comdlg32, loaded only when the user prints). the 32-bit sdk packs PRINTDLG to 1 byte */
#pragma pack(push, 1)
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HGLOBAL hDevMode, hDevNames; HDC hDC; DWORD Flags;
    WORD nFromPage, nToPage, nMinPage, nMaxPage, nCopies; HINSTANCE hInstance; LPARAM lCustData;
    void *lpfnPrintHook, *lpfnSetupHook; LPCWSTR lpPrintTemplateName, lpSetupTemplateName;
    HGLOBAL hPrintTemplate, hSetupTemplate;
} PRINTDLGW;
#pragma pack(pop)
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HGLOBAL hDevMode, hDevNames; DWORD Flags; POINT ptPaperSize;
    RECT rtMinMargin, rtMargin; HINSTANCE hInstance; LPARAM lCustData;
    void *lpfnPageSetupHook, *lpfnPagePaintHook; LPCWSTR lpPageSetupTemplateName; HGLOBAL hPageSetupTemplate;
} PAGESETUPDLGW;

/* openfilename (comdlg32 GetOpenFileNameW / GetSaveFileNameW, loaded when the user opens / saves). commdlg.h packs the whole struct to 1 byte;
 * this is the windows 2000+ form (the last three fields) and lStructSize must be sizeof */
#pragma pack(push, 1)
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter; DWORD nMaxCustFilter, nFilterIndex;
    LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags;
    WORD nFileOffset, nFileExtension; LPCWSTR lpstrDefExt; LPARAM lCustData; void *lpfnHook; LPCWSTR lpTemplateName;
    void *pvReserved; DWORD dwReserved, FlagsEx;
} OPENFILENAMEW;
#pragma pack(pop)

/* ------------------------------------------------------------ constants -- */
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define CW_USEDEFAULT        ((int)0x80000000)
#define HWND_TOP        ((HWND)0)
#define HWND_TOPMOST    ((HWND)(LONG_PTR)-1)
#define HWND_NOTOPMOST  ((HWND)(LONG_PTR)-2)
#define HWND_DESKTOP    ((HWND)0)

/* window styles */
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP      0x80000000L
#define WS_CHILD      0x40000000L
#define WS_MINIMIZE   0x20000000L
#define WS_VISIBLE    0x10000000L
#define WS_DISABLED   0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE   0x01000000L
#define WS_CAPTION    0x00C00000L
#define WS_BORDER     0x00800000L
#define WS_DLGFRAME   0x00400000L
#define WS_VSCROLL    0x00200000L
#define WS_HSCROLL    0x00100000L
#define WS_SYSMENU    0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP      0x00020000L
#define WS_TABSTOP    0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_RIGHT         0x00001000L
#define WS_EX_RTLREADING    0x00002000L
#define WS_EX_LEFTSCROLLBAR 0x00004000L
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_TOPMOST       0x00000008L
#define WS_EX_ACCEPTFILES   0x00000010L
#define WS_EX_TOOLWINDOW    0x00000080L
#define WS_EX_CLIENTEDGE    0x00000200L
#define WS_EX_CONTROLPARENT 0x00010000L
#define WS_EX_APPWINDOW     0x00040000L
#define WS_EX_NOACTIVATE    0x08000000L

#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_DROPSHADOW 0x00020000

/* messages */
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SETTINGCHANGE 0x001A
#define WM_QUERYENDSESSION 0x0011
#define WM_ENDSESSION 0x0016
#define WM_ACTIVATEAPP 0x001C
#define WM_CANCELMODE 0x001F
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_DRAWITEM 0x002B
#define WM_MEASUREITEM 0x002C
#define WM_VKEYTOITEM 0x002E
#define WM_CHARTOITEM 0x002F
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_CONTEXTMENU 0x007B
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCACTIVATE 0x0086
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCRBUTTONDOWN 0x00A4
#define WM_NCRBUTTONUP 0x00A5
#define WM_GETDLGCODE 0x0087
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLORDLG 0x0136
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MOUSEWHEEL 0x020A
#define WM_CAPTURECHANGED 0x0215
#define WM_PRINTCLIENT 0x0318
#define PRF_CLIENT 0x00000004L
#define PRF_ERASEBKGND 0x00000008L
#define WM_DROPFILES 0x0233
#define WM_DPICHANGED 0x02E0
#define WM_MOUSELEAVE 0x02A3
#define WM_CUT 0x0300
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
#define WM_UNDO 0x0304
#define WM_APP 0x8000

#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SIZE_MINIMIZED 1
#define WA_INACTIVE 0
#define HTCLIENT 1
#define HTCAPTION 2
#define HTTOP 12
#define HTTOPLEFT 13
#define HTTOPRIGHT 14
#define HTBOTTOMRIGHT 17
#define MA_NOACTIVATE 3
#define ICON_SMALL 0
#define ICON_BIG 1

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040

#define GCL_HBRBACKGROUND (-10)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)
#define GWLP_ID (-12)
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4

#define SB_HORZ 0
#define SB_VERT 1
#define SIF_RANGE 0x0001
#define SIF_PAGE 0x0002
#define SIF_POS 0x0004
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CYFRAME 33
#define SM_CXPADDEDBORDER 92
#define DI_NORMAL 3
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXSMICON 49
#define SM_CYSMICON 50

#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_IBEAM MAKEINTRESOURCEW(32513)
#define IDC_WAIT  MAKEINTRESOURCEW(32514)
#define IDC_HAND  MAKEINTRESOURCEW(32649)
#define IMAGE_ICON 1
#define LR_DEFAULTCOLOR 0
#define LR_SHARED 0x8000

#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define MB_OK 0
#define MB_ICONERROR 0x10

#define RDW_INVALIDATE 0x0001
#define RDW_ERASE 0x0004
#define RDW_FRAME 0x0400
#define RDW_ALLCHILDREN 0x0080
#define PM_NOREMOVE 0
#define PM_REMOVE 1
#define TME_LEAVE 2
#define MONITOR_DEFAULTTONULL 0
#define MONITOR_DEFAULTTONEAREST 2
#define SPI_GETCARETWIDTH 0x2006
#define COLOR_HIGHLIGHT 13
#define SPI_GETWORKAREA 0x0030
#define SPI_GETNONCLIENTMETRICS 0x0029

#define FVIRTKEY 1
#define FSHIFT 4
#define FCONTROL 8
#define FALT 0x10

/* virtual keys */
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_F1 0x70
#define VK_F3 0x72
#define VK_F5 0x74
#define VK_F10 0x79
#define VK_OEM_PLUS 0xBB
#define VK_OEM_MINUS 0xBD

/* edit control */
#define ES_LEFT 0x0000
#define ES_CENTER 0x0001
#define ES_RIGHT 0x0002
#define ES_MULTILINE 0x0004
#define ES_AUTOVSCROLL 0x0040
#define ES_AUTOHSCROLL 0x0080
#define ES_NOHIDESEL 0x0100
#define ES_READONLY 0x0800
#define ES_WANTRETURN 0x1000
#define ES_NUMBER 0x2000
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_GETRECT 0x00B2
#define EM_SETRECTNP 0x00B4
#define EM_SCROLL 0x00B5
#define EM_LINESCROLL 0x00B6
#define EM_SCROLLCARET 0x00B7
#define EM_GETMODIFY 0x00B8
#define EM_SETMODIFY 0x00B9
#define EM_GETLINECOUNT 0x00BA
#define EM_LINEINDEX 0x00BB
#define EM_GETHANDLE 0x00BD
#define EM_LINELENGTH 0x00C1
#define EM_REPLACESEL 0x00C2
#define EM_LIMITTEXT 0x00C5
#define EM_CANUNDO 0x00C6
#define EM_UNDO 0x00C7
#define EM_LINEFROMCHAR 0x00C9
#define EM_SETTABSTOPS 0x00CB
#define EM_EMPTYUNDOBUFFER 0x00CD
#define EM_GETFIRSTVISIBLELINE 0x00CE
#define EM_SETREADONLY 0x00CF
#define EM_SETMARGINS 0x00D3
#define EM_POSFROMCHAR 0x00D6
#define EM_CHARFROMPOS 0x00D7
#define EC_LEFTMARGIN 1
#define EC_RIGHTMARGIN 2
#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400
#define EN_ERRSPACE 0x0500
#define EN_MAXTEXT 0x0501
#define EN_HSCROLL 0x0601
#define EN_VSCROLL 0x0602

/* buttons / statics / list boxes */
#define BS_PUSHBUTTON 0x00
#define BS_DEFPUSHBUTTON 0x01
#define BS_AUTOCHECKBOX 0x03
#define BS_AUTORADIOBUTTON 0x09
#define BS_TYPEMASK 0x0F
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BM_GETSTATE 0x00F2
#define BM_SETSTYLE 0x00F4
#define BM_CLICK 0x00F5
#define BN_CLICKED 0
#define BST_UNCHECKED 0
#define BST_CHECKED 1
#define DLGC_WANTARROWS 0x0001
#define DLGC_WANTTAB 0x0002
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_WANTCHARS 0x0080
#define DLGC_BUTTON 0x2000
#define DLGC_DEFPUSHBUTTON 0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_RADIOBUTTON 0x0040
#define WM_USER 0x0400
#define DM_GETDEFID (WM_USER + 0)
#define SS_LEFT 0x00
#define SS_CENTER 0x01
#define SS_RIGHT 0x02
#define SS_NOPREFIX 0x80
#define SS_NOTIFY 0x0100
#define LB_ADDSTRING 0x0180
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018A
#define LB_GETCOUNT 0x018B
#define LB_FINDSTRING 0x018F
#define LB_GETTOPINDEX 0x018E
#define LB_SETTOPINDEX 0x0197
#define LB_GETITEMRECT 0x0198
#define LB_GETITEMDATA 0x0199
#define LB_SETITEMDATA 0x019A
#define LB_SETITEMHEIGHT 0x01A0
#define LB_FINDSTRINGEXACT 0x01A2
#define LB_INITSTORAGE 0x01A8
#define LB_ITEMFROMPOINT 0x01A9
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2
#define LBS_NOTIFY 0x0001
#define LBS_SORT 0x0002
#define LBS_OWNERDRAWFIXED 0x0010
#define LBS_HASSTRINGS 0x0040
#define LBS_NOINTEGRALHEIGHT 0x0100
#define LBS_WANTKEYBOARDINPUT 0x0400L
#define LB_ERR (-1)
#define ODS_SELECTED 0x0001
#define ODS_FOCUS 0x0010
#define ODT_LISTBOX 2

/* drawing */
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_RIGHT 0x0002
#define DT_VCENTER 0x0004
#define DT_WORDBREAK 0x0010
#define DT_SINGLELINE 0x0020
#define DT_NOCLIP 0x0100
#define DT_CALCRECT 0x0400
#define DT_NOPREFIX 0x0800
#define DT_PATH_ELLIPSIS 0x4000
#define DT_END_ELLIPSIS 0x8000
#define TRANSPARENT 1
#define OPAQUE 2
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define SRCCOPY 0x00CC0020
#define PS_SOLID 0
#define FW_NORMAL 400
#define FW_BOLD 700
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define OUT_TT_PRECIS 4
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define FF_DONTCARE 0
#define FF_MODERN 0x30
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define HORZRES 8
#define VERTRES 10
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113
#define PHYSICALWIDTH 110
#define PHYSICALHEIGHT 111
#define NULL_BRUSH 5
#define NULL_PEN 8
#define DEFAULT_GUI_FONT 17
#define DC_BRUSH 18
#define DC_PEN 19
#define RASTER_FONTTYPE 0x0001
#define TRUETYPE_FONTTYPE 4
#define MM_TEXT 1

/* files / strings */
#define GENERIC_READ  0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_HIDDEN 0x02
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define CP_ACP 0
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 0x08
#define WC_ERR_INVALID_CHARS 0x80
#define HEAP_ZERO_MEMORY 8
#define MOVEFILE_REPLACE_EXISTING 1
#define LOCALE_USER_DEFAULT 0x0400
#define TIME_NOSECONDS 2
#define DATE_SHORTDATE 1
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define CF_UNICODETEXT 13
#define CREATE_UNICODE_ENVIRONMENT 0x400
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define SEM_FAILCRITICALERRORS 0x0001
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PRINT_CANCELLED 63L
#define ERROR_ALREADY_EXISTS 183
#define ERROR_CANCELLED 1223L
#define CSTR_EQUAL 2

/* dwm (dwmapi.dll, loaded at run time) */
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWA_BORDER_COLOR 34
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_TEXT_COLOR 36
#define DWMWCP_DONOTROUND 1

/* comdlg32 printing */
#define PD_PAGENUMS 0x00000002
#define PD_DISABLEPRINTTOFILE 0x00080000
#define PD_HIDEPRINTTOFILE 0x00100000
#define PD_RETURNDC 0x00000100
#define PD_NOSELECTION 0x00000004
#define PD_NOPAGENUMS 0x00000008
#define PD_USEDEVMODECOPIESANDCOLLATE 0x00040000
#define PSD_MARGINS 0x00000002
#define PSD_INTHOUSANDTHSOFINCHES 0x00000004
#define PSD_DISABLEPRINTER 0x00000020
#define PDERR_NODEVICES 0x1007                          /* cderr.h */
#define PDERR_NODEFAULTPRN 0x1008
#define PDERR_DNDMMISMATCH 0x1009
#define PDERR_PRINTERNOTFOUND 0x100B
#define PDERR_DEFAULTDIFFERENT 0x100C

/* comdlg32 open / save as (OPENFILENAMEW.Flags, and CommDlgExtendedError after a failed call: cderr.h) */
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY 0x00000004
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_EXPLORER 0x00080000
#define FNERR_BUFFERTOOSMALL 0x3003

/* ------------------------------------------------------------- kernel32 -- */
API void    WINAPI ExitProcess(UINT);
API HMODULE WINAPI GetModuleHandleW(LPCWSTR);
API LPWSTR  WINAPI GetCommandLineW(void);
API DWORD   WINAPI GetLastError(void);
API void    WINAPI Sleep(DWORD);
API HANDLE  WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
API BOOL    WINAPI ReadFile(HANDLE, LPVOID, DWORD, DWORD *, void *);
API BOOL    WINAPI WriteFile(HANDLE, LPCVOID, DWORD, DWORD *, void *);
API BOOL    WINAPI CloseHandle(HANDLE);
API BOOL    WINAPI GetFileSizeEx(HANDLE, __int64 *);
API DWORD   WINAPI GetFileAttributesW(LPCWSTR);
API HANDLE  WINAPI FindFirstFileW(LPCWSTR, WIN32_FIND_DATAW *);
API BOOL    WINAPI FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
API BOOL    WINAPI FindClose(HANDLE);
API DWORD   WINAPI GetCurrentDirectoryW(DWORD, LPWSTR);
API DWORD   WINAPI GetFullPathNameW(LPCWSTR, DWORD, LPWSTR, LPWSTR *);
API DWORD   WINAPI GetLogicalDrives(void);
API UINT    WINAPI GetDriveTypeW(LPCWSTR);
API BOOL    WINAPI CreateDirectoryW(LPCWSTR, void *);
API BOOL    WINAPI DeleteFileW(LPCWSTR);
API BOOL    WINAPI MoveFileExW(LPCWSTR, LPCWSTR, DWORD);
API HANDLE  WINAPI GetProcessHeap(void);
API LPVOID  WINAPI HeapAlloc(HANDLE, DWORD, SIZE_T);
API LPVOID  WINAPI HeapReAlloc(HANDLE, DWORD, LPVOID, SIZE_T);
API BOOL    WINAPI HeapFree(HANDLE, DWORD, LPVOID);
API int     WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
API int     WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, BOOL *);
API int     WINAPI lstrlenW(LPCWSTR);
API DWORD   WINAPI GetEnvironmentVariableW(LPCWSTR, LPWSTR, DWORD);
API BOOL    WINAPI WritePrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR);
API DWORD   WINAPI GetPrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
API UINT    WINAPI GetPrivateProfileIntW(LPCWSTR, LPCWSTR, INT, LPCWSTR);
API int     WINAPI GetTimeFormatW(DWORD, DWORD, const SYSTEMTIME *, LPCWSTR, LPWSTR, int);
API int     WINAPI GetDateFormatW(DWORD, DWORD, const SYSTEMTIME *, LPCWSTR, LPWSTR, int);
API void    WINAPI GetLocalTime(SYSTEMTIME *);
API int     WINAPI MulDiv(int, int, int);
API HMODULE WINAPI LoadLibraryW(LPCWSTR);
API void *  WINAPI GetProcAddress(HMODULE, LPCSTR);
API LPVOID  WINAPI LocalLock(HLOCAL);
API BOOL    WINAPI LocalUnlock(HLOCAL);
API int     WINAPI CompareStringOrdinal(LPCWSTR, int, LPCWSTR, int, BOOL);
API DWORD   WINAPI GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
API void    WINAPI OutputDebugStringW(LPCWSTR);
API DWORD   WINAPI GetTickCount(void);
API BOOL    WINAPI CreateProcessW(LPCWSTR, LPWSTR, void *, void *, BOOL, DWORD, LPVOID, LPCWSTR, STARTUPINFOW *, PROCESS_INFORMATION *);
API BOOL    WINAPI SetCurrentDirectoryW(LPCWSTR);
API DWORD   WINAPI FormatMessageW(DWORD, LPCVOID, DWORD, DWORD, LPWSTR, DWORD, void *);
API DWORD   WINAPI GetCurrentThreadId(void);
API HGLOBAL WINAPI GlobalFree(HGLOBAL);
API UINT    WINAPI SetErrorMode(UINT);

/* --------------------------------------------------------------- user32 -- */
API ATOM    WINAPI RegisterClassExW(const WNDCLASSEXW *);
API HWND    WINAPI CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
API BOOL    WINAPI DestroyWindow(HWND);
API BOOL    WINAPI ShowWindow(HWND, int);
API BOOL    WINAPI UpdateWindow(HWND);
API BOOL    WINAPI GetMessageW(MSG *, HWND, UINT, UINT);
API BOOL    WINAPI PeekMessageW(MSG *, HWND, UINT, UINT, UINT);
API BOOL    WINAPI TranslateMessage(const MSG *);
API LRESULT WINAPI DispatchMessageW(const MSG *);
API LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
API LRESULT WINAPI CallWindowProcW(WNDPROC, HWND, UINT, WPARAM, LPARAM);
API LRESULT WINAPI SendMessageW(HWND, UINT, WPARAM, LPARAM);
API BOOL    WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM);
API void    WINAPI PostQuitMessage(int);
API LONG    WINAPI SetWindowLongW(HWND, int, LONG);              /* 32-bit user32 has no ...LongPtr exports */
API LONG    WINAPI GetWindowLongW(HWND, int);
#define SetWindowLongPtrW SetWindowLongW
#define GetWindowLongPtrW GetWindowLongW
API BOOL    WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
API BOOL    WINAPI MoveWindow(HWND, int, int, int, int, BOOL);
API BOOL    WINAPI GetClientRect(HWND, RECT *);
API BOOL    WINAPI GetWindowRect(HWND, RECT *);
API BOOL    WINAPI ClientToScreen(HWND, POINT *);
API BOOL    WINAPI ScreenToClient(HWND, POINT *);
API BOOL    WINAPI InvalidateRect(HWND, const RECT *, BOOL);
API BOOL    WINAPI ValidateRect(HWND, const RECT *);
API HDC     WINAPI BeginPaint(HWND, PAINTSTRUCT *);
API BOOL    WINAPI EndPaint(HWND, const PAINTSTRUCT *);
API HDC     WINAPI GetDC(HWND);
API int     WINAPI ReleaseDC(HWND, HDC);
API int     WINAPI FillRect(HDC, const RECT *, HBRUSH);
API int     WINAPI DrawTextW(HDC, LPCWSTR, int, RECT *, UINT);
API BOOL    WINAPI DrawFocusRect(HDC, const RECT *);
API int     WINAPI GetSystemMetrics(int);
API DWORD   WINAPI GetSysColor(int);
API BOOL    WINAPI SystemParametersInfoW(UINT, UINT, PVOID, UINT);
API HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR);
API HCURSOR WINAPI SetCursor(HCURSOR);
API HICON   WINAPI LoadIconW(HINSTANCE, LPCWSTR);
API HANDLE  WINAPI LoadImageW(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
API BOOL    WINAPI DestroyIcon(HICON);
API HWND    WINAPI SetCapture(HWND);
API BOOL    WINAPI ReleaseCapture(void);
API HWND    WINAPI GetCapture(void);
API HWND    WINAPI GetFocus(void);
API HWND    WINAPI SetFocus(HWND);
API HWND    WINAPI GetForegroundWindow(void);
API BOOL    WINAPI SetForegroundWindow(HWND);
API HWND    WINAPI SetActiveWindow(HWND);
API BOOL    WINAPI EnableWindow(HWND, BOOL);
API BOOL    WINAPI IsWindowEnabled(HWND);
API BOOL    WINAPI IsWindow(HWND);
API BOOL    WINAPI IsWindowVisible(HWND);
API BOOL    WINAPI IsIconic(HWND);
API BOOL    WINAPI IsZoomed(HWND);
API UINT_PTR WINAPI SetTimer(HWND, UINT_PTR, UINT, void *);
API BOOL    WINAPI KillTimer(HWND, UINT_PTR);
API short   WINAPI GetKeyState(int);
API short   WINAPI GetAsyncKeyState(int);
API HACCEL  WINAPI CreateAcceleratorTableW(ACCEL *, int);
API int     WINAPI TranslateAcceleratorW(HWND, HACCEL, MSG *);
API BOOL    WINAPI IsDialogMessageW(HWND, MSG *);
API HWND    WINAPI GetDlgItem(HWND, int);
API BOOL    WINAPI SetWindowTextW(HWND, LPCWSTR);
API int     WINAPI GetWindowTextW(HWND, LPWSTR, int);
API int     WINAPI GetWindowTextLengthW(HWND);
API int     WINAPI MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
API BOOL    WINAPI GetCursorPos(POINT *);
API BOOL    WINAPI TrackMouseEvent(TRACKMOUSEEVENT *);
API HWND    WINAPI GetParent(HWND);
API HWND    WINAPI GetWindow(HWND, UINT);
API BOOL    WINAPI IsChild(HWND, HWND);
API HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
API HMONITOR WINAPI MonitorFromPoint(POINT, DWORD);
API BOOL    WINAPI GetMonitorInfoW(HMONITOR, MONITORINFO *);
API BOOL    WINAPI GetWindowPlacement(HWND, WINDOWPLACEMENT *);
API BOOL    WINAPI SetWindowPlacement(HWND, const WINDOWPLACEMENT *);
API BOOL    WINAPI AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD);
/* GetDpiForWindow / GetDpiForSystem / AdjustWindowRectExForDpi / GetSystemMetricsForDpi are windows 10 only:
 * ui.c looks them up at run time (UiDpiForWindow ...) so the exe still starts on older windows */
API BOOL    WINAPI SetRect(RECT *, int, int, int, int);
API BOOL    WINAPI OffsetRect(RECT *, int, int);
API BOOL    WINAPI InflateRect(RECT *, int, int);
API BOOL    WINAPI PtInRect(const RECT *, POINT);
API BOOL    WINAPI IntersectRect(RECT *, const RECT *, const RECT *);
API LPWSTR  WINAPI CharLowerW(LPWSTR);
API LPWSTR  WINAPI CharUpperW(LPWSTR);
API int     WINAPI wsprintfW(LPWSTR, LPCWSTR, ...);
API BOOL    WINAPI MessageBeep(UINT);
API BOOL    WINAPI RedrawWindow(HWND, const RECT *, HRGN, UINT);
API int     WINAPI GetClassNameW(HWND, LPWSTR, int);
API BOOL    WINAPI EnumChildWindows(HWND, BOOL (CALLBACK *)(HWND, LPARAM), LPARAM);
API BOOL    WINAPI EnumThreadWindows(DWORD, BOOL (CALLBACK *)(HWND, LPARAM), LPARAM);
API HWND    WINAPI GetActiveWindow(void);
API DWORD   WINAPI SetClassLongW(HWND, int, LONG);              /* SetClassLongPtrW on 32-bit */
API LRESULT WINAPI SendDlgItemMessageW(HWND, int, UINT, WPARAM, LPARAM);
API BOOL    WINAPI GetCaretPos(POINT *);
API BOOL    WINAPI SetCaretPos(int, int);
API BOOL    WINAPI CreateCaret(HWND, HBITMAP, int, int);
API BOOL    WINAPI DestroyCaret(void);
API BOOL    WINAPI ShowCaret(HWND);
API BOOL    WINAPI GetScrollInfo(HWND, int, SCROLLINFO *);
API BOOL    WINAPI ShowScrollBar(HWND, int, BOOL);
API BOOL    WINAPI IsClipboardFormatAvailable(UINT);
API HWND    WINAPI FindWindowExW(HWND, HWND, LPCWSTR, LPCWSTR);
API BOOL    WINAPI GetKeyboardState(BYTE *);
API BOOL    WINAPI DrawIconEx(HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT);

/* --------------------------------------------------------------- gdi32 ---- */
API HFONT    WINAPI CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
API HFONT    WINAPI CreateFontIndirectW(const LOGFONTW *);
API HGDIOBJ  WINAPI GetStockObject(int);
API HBRUSH   WINAPI CreateSolidBrush(COLORREF);
API HPEN     WINAPI CreatePen(int, int, COLORREF);
API BOOL     WINAPI DeleteObject(HGDIOBJ);
API HGDIOBJ  WINAPI SelectObject(HDC, HGDIOBJ);
API COLORREF WINAPI SetTextColor(HDC, COLORREF);
API COLORREF WINAPI SetBkColor(HDC, COLORREF);
API int      WINAPI SetBkMode(HDC, int);
API BOOL     WINAPI TextOutW(HDC, int, int, LPCWSTR, int);
API BOOL     WINAPI ExtTextOutW(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *);
API BOOL     WINAPI GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE *);
API BOOL     WINAPI GetTextExtentExPointW(HDC, LPCWSTR, int, int, INT *, INT *, SIZE *);
API BOOL     WINAPI GetTextMetricsW(HDC, TEXTMETRICW *);
API int      WINAPI GetTextFaceW(HDC, int, LPWSTR);
API int      WINAPI GetDeviceCaps(HDC, int);
API int      WINAPI EnumFontFamiliesExW(HDC, LOGFONTW *, FONTENUMPROCW, LPARAM, DWORD);
API HDC      WINAPI CreateCompatibleDC(HDC);
API HBITMAP  WINAPI CreateCompatibleBitmap(HDC, int, int);
API BOOL     WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
API BOOL     WINAPI DeleteDC(HDC);
API BOOL     WINAPI MoveToEx(HDC, int, int, POINT *);
API BOOL     WINAPI LineTo(HDC, int, int);
API BOOL     WINAPI Polygon(HDC, const POINT *, int);
API BOOL     WINAPI Ellipse(HDC, int, int, int, int);
API int      WINAPI SaveDC(HDC);
API BOOL     WINAPI RestoreDC(HDC, int);
API int      WINAPI IntersectClipRect(HDC, int, int, int, int);
API int      WINAPI SetMapMode(HDC, int);
API BOOL     WINAPI SetViewportOrgEx(HDC, int, int, POINT *);
API HDC      WINAPI CreateDCW(LPCWSTR, LPCWSTR, LPCWSTR, const void *);
API int      WINAPI StartDocW(HDC, const DOCINFOW *);
API int      WINAPI EndDoc(HDC);
API int      WINAPI StartPage(HDC);
API int      WINAPI EndPage(HDC);
API int      WINAPI AbortDoc(HDC);
API COLORREF WINAPI SetDCBrushColor(HDC, COLORREF);
API COLORREF WINAPI SetDCPenColor(HDC, COLORREF);
API COLORREF WINAPI GetPixel(HDC, int, int);
API HBITMAP  WINAPI CreateBitmap(int, int, UINT, UINT, const void *);
API HBRUSH   WINAPI CreatePatternBrush(HBITMAP);

/* mem* (provided by rt.asm) */
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int   memcmp(const void *, const void *, size_t);

#endif
