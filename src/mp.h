#ifndef MP_H
#define MP_H

#include "w32.h"

#define APP_NAME     L"notepad mint"
#define TITLE_TAIL   L" - " APP_NAME
#define APP_CLASS    L"notepad_mint"
#define APP_VERSION  L"1.2.0"
#define PATH_CAP     1024

typedef struct Palette {
    COLORREF accent, accentFg, onAccent;
    COLORREF face, face2, field;
    COLORREF hi, hi2, lo, lo2;
    COLORREF text, dim;
    COLORREF editFg, editBg;
} Palette;
extern Palette g_pal;
#define C_ACCENT    (g_pal.accent)
#define C_ACCENT_FG (g_pal.accentFg)
#define C_ON_ACCENT (g_pal.onAccent)
#define C_FACE      (g_pal.face)
#define C_FACE2     (g_pal.face2)
#define C_FIELD     (g_pal.field)
#define C_HI        (g_pal.hi)
#define C_HI2       (g_pal.hi2)
#define C_LO        (g_pal.lo)
#define C_LO2       (g_pal.lo2)
#define C_TEXT      (g_pal.text)
#define C_DIM       (g_pal.dim)
#define PROBE_ID    0x4D494E54
#define DEFAULT_FACE L"Consolas"
#define C_EDIT_FG   (g_pal.editFg)
#define C_EDIT_BG   (g_pal.editBg)
enum { THEME_DARK, THEME_LIGHT };

enum {
    IDM_FILE_NEW = 101, IDM_FILE_NEWWIN, IDM_FILE_OPEN, IDM_FILE_SAVE, IDM_FILE_SAVEAS, IDM_FILE_PAGESETUP, IDM_FILE_PRINT, IDM_FILE_EXIT,
    IDM_EDIT_UNDO = 201, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE, IDM_EDIT_DELETE,
    IDM_EDIT_FIND, IDM_EDIT_FINDNEXT, IDM_EDIT_FINDPREV, IDM_EDIT_REPLACE, IDM_EDIT_GOTO,
    IDM_EDIT_SELALL, IDM_EDIT_TIMEDATE, IDM_EDIT_CLEARLINE,
    IDM_FMT_WRAP = 301, IDM_FMT_FONT, IDM_TAB_2 = 305, IDM_TAB_4, IDM_TAB_8,
    IDM_EOL_CRLF = 311, IDM_EOL_LF, IDM_EOL_CR,
    IDM_ENC_UTF8 = 321, IDM_ENC_UTF8BOM, IDM_ENC_UTF16LE, IDM_ENC_UTF16BE, IDM_ENC_ANSI, IDM_ENC_OTHER, IDM_ENC_REOPEN,
    IDM_RTL = 340, IDM_DIR_LTR = 341, IDM_DIR_RTL = 342, IDM_UCC_BASE = 350,
    IDM_VIEW_STATUS = 401, IDM_ZOOM_IN, IDM_ZOOM_OUT, IDM_ZOOM_RESET, IDM_THEME_DARK, IDM_THEME_LIGHT, IDM_THEME_TOGGLE,
    IDM_HELP_TOPICS = 501, IDM_HELP_ABOUT, IDM_HELP_DEFAULT,
    IDM_RECENT_BASE = 700, IDM_RECENT_NONE = 710, IDM_RECENT_CLEAR = 711,
    IDM_SYS_RESTORE = 601, IDM_SYS_MOVE, IDM_SYS_SIZE, IDM_SYS_MIN, IDM_SYS_MAX, IDM_SYS_CLOSE
};

void  *mem_alloc(size_t size);
void  *mem_zalloc(size_t size);
void  *mem_realloc(void *block, size_t size);
void   mem_free(void *block);
int    wlen(const WCHAR *text);
void   wcopy(WCHAR *dst, const WCHAR *src, int cap);
void   wcat(WCHAR *dst, const WCHAR *src, int cap);
int    wcmp(const WCHAR *a, const WCHAR *b);
int    wcmpi(const WCHAR *a, const WCHAR *b);
WCHAR  wlow(WCHAR c);
int    wtoi(const WCHAR *text);
const WCHAR *PathName(const WCHAR *path);
void   PathDir(const WCHAR *path, WCHAR *out, int cap);
void   PathJoin(WCHAR *dir, const WCHAR *name, int cap);
BOOL   IsDir(const WCHAR *path);
WCHAR *TextToCrlf(const WCHAR *src, int maxLen, int *outLen);
size_t mp_count_lf(const WCHAR *p, size_t n);
size_t mp_find3(const WCHAR *p, size_t n, unsigned a, unsigned b, unsigned c);
int    WordClass(WCHAR ch);
#define DOCNAME_EPOCH 2026u
#define DOCNAME_STEPS 45u
#define DOCNAME_AHEAD (DOCNAME_STEPS * 366u)
#define DOCNAME_NONE  0xFFFFFFFFu
unsigned DocNameClock(const SYSTEMTIME *time);
unsigned DocNameNext(unsigned clock, unsigned last);
void   DefaultDocName(unsigned value, WCHAR *out, int cap);

#define ASSOC_PROGID    L"notepad-mint.text"
#define ASSOC_REGAPP    L"notepad-mint"
#define ASSOC_CAPS      L"Software\\notepad mint\\Capabilities"
#define ASSOC_EXT_COUNT 10
extern const WCHAR *const g_assocExt[ASSOC_EXT_COUNT];
BOOL   AssocRegisterUser(const WCHAR *exe);
BOOL   AssocMachineHas(const WCHAR *exe);

int    FindInTextEx(const WCHAR *text, int length, const WCHAR *pattern, int patLen, int from, int up, int matchCase, int wholeWord);
WCHAR *ReplaceAllTextEx(const WCHAR *text, int length, const WCHAR *pattern, int patLen, const WCHAR *replacement, int replLen, int matchCase, int wholeWord, int *outLen, int *count);
#ifdef DBGLOG
void   Dbg(const WCHAR *tag, INT_PTR a, INT_PTR b);
#define DBG(t, a, b) Dbg(t, (INT_PTR)(a), (INT_PTR)(b))
#else
#define DBG(t, a, b) ((void)0)
#endif

#define IDC_DIM 0xD1

typedef struct DlgBase {
    HWND hwnd, owner, focus;
    int  done, result, modeless;
    int  nframes;
    RECT fr[24];
    BYTE frk[24];
} DlgBase;

enum { BV_RAISED, BV_SUNKEN, BV_ETCHED, BV_FLAT_DN };

extern HINSTANCE g_hinst;
extern int       g_dpi;
extern HFONT     g_fontUI, g_fontUIB;
extern HFONT     g_fontMenu, g_fontMenuB;
#define CHROME_PX 11
extern HBRUSH    g_brFace, g_brField;

int   S(int v);
int   UnS(int px);
void  UiInit(HINSTANCE hInstance);
void  UiSetDpi(int dpi);
void  ThemeSet(int theme);
int   ThemeGet(void);
void  UiSetChromeFont(const WCHAR *face);
int   UiSystemDpi(void);
int   UiDpiForWindow(HWND hwnd);
int   UiMetric(int index);
BOOL  UiAdjustRect(RECT *rect, DWORD style, DWORD exStyle);
void  FillC(HDC hdc, const RECT *rect, COLORREF color);
void  Bevel(HDC hdc, const RECT *rect, int kind);
void  TextC(HDC hdc, const WCHAR *text, int length, RECT *rect, UINT format, COLORREF color);
int   TextW(HDC hdc, const WCHAR *text, int length);
void  Tri(HDC hdc, int x, int y, int size, COLORREF color);
void  CheckGlyph(HDC hdc, int x, int y, COLORREF color);
void  DarkFrame(HWND hwnd, int active);
void  DarkScroll(HWND hwnd);
void  SbarAttach(HWND target);
void  SbarTrim(HWND target, int right, int bottom);
void  SbarSync(HWND target);
void  SbarDetach(HWND target);
void  RegClass(const WCHAR *name, WNDPROC proc, UINT style, HBRUSH bg);

DlgBase *DlgFromHwnd(HWND hwnd, UINT msg, LPARAM lParam);
void  DlgBaseInit(DlgBase *base, HWND owner);
HWND  DlgOpen(DlgBase *base, const WCHAR *className, const WCHAR *title, int clientWidth, int clientHeight, int modeless);
void  DlgRunModal(DlgBase *base);
BOOL  DlgCommon(DlgBase *base, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *ret);
void  DlgFrame(DlgBase *base, int x, int y, int width, int height, int kind);
HWND  UiLabel(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD extra);
HWND  UiEdit(DlgBase *base, const WCHAR *text, int x, int y, int width, int height, int id, DWORD extra);
HWND  UiButton(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD style);
int   MpAsk(HWND owner, const WCHAR *title, const WCHAR *message, const WCHAR *label1, const WCHAR *label2, const WCHAR *label3, int escIdx);
void  MpNote(HWND owner, const WCHAR *title, const WCHAR *message);
int   ClampInt(int value, int lo, int hi);

#define MS_CHECK 1
#define MS_RADIO 2
#define MS_GRAY  4
typedef struct MenuDef MenuDef;
typedef struct MenuItem { const WCHAR *label, *accel; int id; const MenuDef *sub; } MenuItem;
struct MenuDef { const MenuItem *items; int n; };
typedef unsigned (*MenuStateFn)(int id);
#define BAR_BTN_W 31
#define BAR_BTN_ICON 12
#define BAR_BTN_OPACITY_ON 60
#define BAR_BTN_OPACITY 20
#define BAR_WRAP_ICON_DY 1
#define BAR_TIP_DELAY 500
#define BAR_TIP_SHOW 5000

extern const MenuDef g_mdEditCtx, g_mdEol, g_mdEnc, g_mdZoom, g_mdUcc, g_mdSys;
extern const MenuDef g_mdTab;
extern MenuDef g_mdRecent;
#define RECENT_MAX 9
void  MenuSetRecent(const WCHAR (*paths)[PATH_CAP], int n);
HWND  MenuBarCreate(HWND parent, MenuStateFn stateFn);
int   MenuBarHeight(void);
int   MenuBarMinWidth(void);
void  MenuBarRefont(HWND bar);
void  MenuBarActivate(int index, int openPopup);
int   MenuBarMnemonic(WCHAR ch);
void  MenuPopup(HWND owner, const MenuDef *def, int x, int y, int anchorBottom);
void  MenuCancel(void);
void  MenuArmDoubleClose(const RECT *screenRect, DWORD time);
void  MenuShowCues(void);

enum { SB_POS, SB_ZOOM, SB_LINES, SB_BYTES, SB_EOL, SB_ENC, SB_COUNT };
HWND  StatusCreate(HWND parent);
int   StatusHeight(void);
int   StatusMinWidth(void);
void  StatusSet(HWND hwnd, int index, const WCHAR *text);
void  StatusRefont(HWND hwnd);

int      FrameEnabled(void);
int      FrameHeight(void);
void     FrameRefont(HWND hwnd);
void     FrameInvalidate(HWND hwnd);
void     FrameActive(HWND hwnd, int active);
void     FramePaint(HWND hwnd, HDC hdc);
LRESULT  FrameNcCalc(HWND hwnd, WPARAM wParam, LPARAM lParam);
LRESULT  FrameHitTest(HWND hwnd, LPARAM lParam);
BOOL     FrameMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void     FrameSysMenu(HWND hwnd, int x, int y);
unsigned FrameSysState(int id);
void     FrameSysCommand(HWND hwnd, int id);

enum { ENC_UTF8, ENC_UTF8BOM, ENC_UTF16LE, ENC_UTF16BE, ENC_ANSI, ENC_COUNT };
enum { EOL_CRLF, EOL_LF, EOL_CR, EOL_COUNT };
#define ENC_CP_MIN   100
#define ERR_NOMEM    8
#define ERR_TOO_BIG  223
#define ERR_BADCP    0xE0000001u
#define ERR_LOSSY    1113
extern const WCHAR *const g_encName[ENC_COUNT];
extern const WCHAR *const g_encShort[ENC_COUNT];
extern const WCHAR *const g_eolShort[EOL_COUNT];
void  EncLabel(int enc, WCHAR *out, int cap);
void  EncShort(int enc, WCHAR *out, int cap);
int   EncListCount(void);
int   EncListGet(int index, WCHAR *label, int cap);
DWORD DocRead(const WCHAR *path, WCHAR **text, int *len, int *enc, int *eol, int forceEnc);
void  DocAllowLossy(BOOL on);
DWORD DocWrite(const WCHAR *path, const WCHAR *text, int len, int enc, int eol, BOOL *lossy);
DWORD DocEncodedSize(const WCHAR *text, int len, int enc, int eol);
DWORD DocBodySize(const WCHAR *text, int len, int enc, int eol);
extern DWORD g_textRev;

#define FONT_MIN 70
#define FIND_CAP 256
#define PAGE_TEXT_CAP 128
#define FONT_MAX 1000
typedef struct Prefs {
    WCHAR    font[32];
    int      pt, bold, italic;
    int      cur;
    int      theme;
    int      wrap, statusbar;
    int      tab;
    int      winx, winy, winw, winh, maximized;
    int      matchCase, wrapAround, wholeWord, findUp;
    int      marginL, marginT, marginR, marginB;
    WCHAR    findWhat[FIND_CAP], findWith[FIND_CAP];
    WCHAR    header[PAGE_TEXT_CAP], footer[PAGE_TEXT_CAP];
} Prefs;
extern Prefs g_pf;
extern HWND  g_hwnd, g_edit, g_status;

typedef struct DocState {
    WCHAR path[PATH_CAP];
    WCHAR name[12];
    int   enc, eol;
} DocState;
extern DocState g_doc;
const WCHAR *AppDocName(void);
void  AppOpenFiles(const WCHAR *paths);
#define WM_APP_OPENFILES (WM_APP + 62)

void  AppApplyPrefs(void);
void  AppSavePrefs(void);
BOOL  AppIsDirty(void);
void  AppUpdateTitle(void);
void  AppUpdateStatus(void);

#define EDIT_PAD 8
#define SBAR_TRIM 5
#define EDIT_PAD_TOP 4
#define EDIT_BAND_INSET 0
HWND   EditCreate(HWND parent);
void   EditApplyFont(void);
void   EditApplyColors(void);
HBRUSH EditBrush(void);
void   FontResolve(WCHAR *face);
BOOL   EditSetDocText(const WCHAR *text);
WCHAR *EditGetDocText(int *length);
BOOL   EditHasSel(void);
void   EditApplyTabs(void);
void   EditScrollSoon(void);
const WCHAR *EditLockText(void **hText, int *length);
void   EditUnlockText(void *hText);
void   EditCaretPos(int *line, int *col);
BOOL   EditSelStats(int enc, int eol, int *lines, DWORD *bytes);
BOOL   EditGotoLine(int line);
void   EditClearLine(void);
void   EditZoomStep(int dir);
void   EditZoomReset(void);
BOOL   EditZoomCan(int dir);
void   EditInsert(const WCHAR *text);
BOOL   EditSetRtl(BOOL rtl);
void   EditDirKeyCancel(void);
int    EditStrongDir(int limit);
BOOL   EditIsRtl(void);
int    EditLineHeight(void);
int    EditIndexFromPoint(POINT pt);
BOOL   EditCaretRect(int index, RECT *rect);
BOOL   EditHitSelection(POINT pt);

void   DndInit(HWND hwnd);
void   DndShutdown(void);
BOOL   DndReady(void);
void   DndDragSelection(void);
void   DndDropFiles(HANDLE drop);
#ifdef SHOTDC
LRESULT DndProbe(const COPYDATASTRUCT *copy);
#endif

void  FindDlgShow(int replaceMode);
void  FindNext(int reverse);
HWND  FindDlgHwnd(void);
BOOL  FindHasText(void);
void  GotoDlg(HWND owner);
void  AboutDlg(HWND owner);
void  HelpDlg(HWND owner);
BOOL  FontDlg(HWND owner);
BOOL  EncDlg(HWND owner, int *enc, int reopen);
BOOL  FileDlgOpen(HWND owner, WCHAR *path, int capacity);
BOOL  FileDlgSave(HWND owner, WCHAR *path, int capacity);
void  PrintDoc(HWND owner, int quiet, const WCHAR *printer);
void  PageSetup(HWND owner);

#endif
