/* mp.h - shared declarations for notepad mint */
#ifndef MP_H
#define MP_H

#include "w32.h"

#define APP_NAME     L"notepad mint"
#define APP_CLASS    L"notepad_mint"
#define APP_VERSION  L"1.0"
#define PATH_CAP     1024

/* ------------------------------------------------------------- palette --
 * classic 2000-era chrome (bevels, flat menu bar, sunken fields) in one of two themes: dark (the default:
 * #161418 chrome, black editor) or light (win2000 gray chrome, white editor), both with the #9df5bd mint.
 * the C_* names are RUNTIME values (ThemeSet in ui.c switches them): never use them in a static initializer,
 * a case label or any constant expression, and never keep a brush made from them across a theme switch
 * (g_brFace / g_brField always hold the current theme's brushes: read them at use time). */
typedef struct Palette {
    COLORREF accent, accentFg, onAccent;    /* mint fill / mint as text or a thin line on a surface / text on a mint fill */
    COLORREF face, face2, field;            /* chrome face, raised surfaces (buttons, popups), sunken fields (lists, small edits) */
    COLORREF hi, hi2, lo, lo2;              /* bevels: outer light, inner light, outer dark, inner dark */
    COLORREF text, dim;                     /* chrome text, disabled / secondary text */
    COLORREF editFg, editBg;                /* the editor area */
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
#define C_EDIT_FG   (g_pal.editFg)
#define C_EDIT_BG   (g_pal.editBg)
enum { THEME_DARK, THEME_LIGHT };

/* --------------------------------------------------------- command ids -- */
enum {
    IDM_FILE_NEW = 101, IDM_FILE_NEWWIN, IDM_FILE_OPEN, IDM_FILE_SAVE, IDM_FILE_SAVEAS, IDM_FILE_PAGESETUP, IDM_FILE_PRINT, IDM_FILE_EXIT,
    IDM_EDIT_UNDO = 201, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE, IDM_EDIT_DELETE,
    IDM_EDIT_FIND, IDM_EDIT_FINDNEXT, IDM_EDIT_FINDPREV, IDM_EDIT_REPLACE, IDM_EDIT_GOTO,
    IDM_EDIT_SELALL, IDM_EDIT_TIMEDATE,
    IDM_FMT_WRAP = 301, IDM_FMT_FONT,
    IDM_EOL_CRLF = 311, IDM_EOL_LF, IDM_EOL_CR,
    IDM_ENC_UTF8 = 321, IDM_ENC_UTF8BOM, IDM_ENC_UTF16LE, IDM_ENC_UTF16BE, IDM_ENC_ANSI, IDM_ENC_OTHER, IDM_ENC_REOPEN,
    IDM_RTL = 340, IDM_UCC_BASE = 350,                /* IDM_UCC_BASE + n inserts unicode control char n (0..16) */
    IDM_VIEW_STATUS = 401, IDM_ZOOM_IN, IDM_ZOOM_OUT, IDM_ZOOM_RESET, IDM_THEME_DARK, IDM_THEME_LIGHT,
    IDM_HELP_TOPICS = 501, IDM_HELP_ABOUT,
    IDM_SYS_RESTORE = 601, IDM_SYS_MOVE, IDM_SYS_SIZE, IDM_SYS_MIN, IDM_SYS_MAX, IDM_SYS_CLOSE   /* title bar menu */
};

/* ------------------------------------------------------------ util.c ---- */
void  *mem_alloc(size_t n);
void  *mem_zalloc(size_t n);
void  *mem_realloc(void *p, size_t n);
void   mem_free(void *p);
int    wlen(const WCHAR *s);
void   wcopy(WCHAR *d, const WCHAR *s, int cap);
void   wcat(WCHAR *d, const WCHAR *s, int cap);
int    wcmp(const WCHAR *a, const WCHAR *b);
int    wcmpi(const WCHAR *a, const WCHAR *b);
WCHAR  wlow(WCHAR c);
int    wtoi(const WCHAR *s);
BOOL   ParseColor(const WCHAR *s, COLORREF *out);
void   FormatColor(COLORREF c, WCHAR *out);          /* out >= 7 chars: "rrggbb" */
const WCHAR *PathName(const WCHAR *path);
void   PathDir(const WCHAR *path, WCHAR *out, int cap);
void   PathJoin(WCHAR *dir, const WCHAR *name, int cap);
BOOL   IsDir(const WCHAR *path);
BOOL   WildMatch(const WCHAR *pat, const WCHAR *name);   /* case-insensitive * and ?, "a;b" = either, "*.*" also matches "readme" */
size_t mp_count_lf(const WCHAR *p, size_t n);        /* rt.asm */
int    WordClass(WCHAR c);                           /* edit.c: 0 blank, 1 word char, 2 punctuation (word delete) */
void   DefaultDocName(const SYSTEMTIME *st, WCHAR *out, int cap);   /* "mint" + 4 base-36 chars of year + month*100 + day + seconds of the day */

/* ------------------------------------------------------------ search.c -- */
/* pure text search (no ui, unit tested): pattern pat[0..m) in t[0..n) (neither needs a terminator).
 * down (up = 0): the first match starting at or after `from`. up: the last match ending at or before `from`.
 * returns the match start or -1 (also for m <= 0). no wrap-around: the caller retries from the other end. */
int    FindInText(const WCHAR *t, int n, const WCHAR *pat, int m, int from, int up, int matchCase);
/* replace every match of pat with `with` in one pass. returns a mem_alloc'd, nul terminated result
 * (*outLen chars, *count replacements) or NULL when out of memory. zero matches => a copy and *count = 0. */
WCHAR *ReplaceAllText(const WCHAR *t, int n, const WCHAR *pat, int m, const WCHAR *with, int wn, int matchCase,
                      int *outLen, int *count);
#ifdef DBGLOG
void   Dbg(const WCHAR *tag, INT_PTR a, INT_PTR b);  /* debug builds: append to build\dbg.log */
#define DBG(t, a, b) Dbg(t, (INT_PTR)(a), (INT_PTR)(b))
#else
#define DBG(t, a, b) ((void)0)
#endif

/* -------------------------------------------------------------- ui.c ---- */
#define IDC_DIM 0xD1                                  /* static label id => dim text */

typedef struct DlgBase {
    HWND hwnd, owner, focus;
    int  done, result, modeless;
    int  nframes;
    RECT fr[24];
    BYTE frk[24];
} DlgBase;

enum { BV_RAISED, BV_SUNKEN, BV_ETCHED, BV_FLAT_UP, BV_FLAT_DN };

extern HINSTANCE g_hinst;
extern int       g_dpi;
extern HFONT     g_fontUI, g_fontUIB;                 /* dialogs: segoe ui 9pt */
extern HFONT     g_fontMenu;                          /* menu bar / popups / status bar / title strip: the editor font face at a static CHROME_PX */
#define CHROME_PX 11                                  /* the chrome font height in 96-dpi pixels (em height): static, scaled by the dpi, it does not follow the editor size */
extern HBRUSH    g_brFace, g_brField;

int   S(int v);                                       /* scale 96-dpi px -> device px */
int   UnS(int px);                                    /* device px -> 96-dpi px */
void  UiInit(HINSTANCE hi);
void  UiSetDpi(int dpi);
void  ThemeSet(int theme);                            /* THEME_*: switches g_pal + g_brFace / g_brField (no repaint) */
int   ThemeGet(void);
void  UiSetChromeFont(const WCHAR *face);                /* rebuilds g_fontMenu (the editor font face, CHROME_PX pixels); callers then refont the bar + status bar */
int   UiSystemDpi(void);
int   UiDpiForWindow(HWND h);                         /* falls back to g_dpi before windows 10 */
int   UiMetric(int idx);                              /* GetSystemMetrics at the current dpi */
BOOL  UiAdjustRect(RECT *r, DWORD style, DWORD ex);   /* client rect -> window rect at the current dpi */
void  FillC(HDC dc, const RECT *r, COLORREF c);
void  Bevel(HDC dc, const RECT *r, int kind);
void  TextC(HDC dc, const WCHAR *s, int n, RECT *r, UINT fmt, COLORREF c);
int   TextW(HDC dc, const WCHAR *s, int n);
void  Tri(HDC dc, int x, int y, int sz, int dir, COLORREF c);  /* dir: 0 right, 1 down */
void  CheckGlyph(HDC dc, int x, int y, COLORREF c);
void  DarkFrame(HWND h, int active);
void  DarkScroll(HWND h);                      /* the control's scrollbars: native ones in the classic style, covered by our own (sbar.c) */
void  SbarAttach(HWND target);                  /* (DarkScroll calls it) overlays for the target's native bars; the target gets WS_CLIPSIBLINGS */
void  SbarSync(HWND target);                    /* follow the target's native bars now (they are also polled) */
void  SbarDetach(HWND target);                  /* the target is being replaced */
void  RegClass(const WCHAR *name, WNDPROC proc, UINT style, HBRUSH bg);

DlgBase *DlgFromHwnd(HWND h, UINT m, LPARAM l);
void  DlgBaseInit(DlgBase *b, HWND owner);
HWND  DlgOpen(DlgBase *b, const WCHAR *cls, const WCHAR *title, int cw, int ch, int modeless);
void  DlgRunModal(DlgBase *b);
BOOL  DlgCommon(DlgBase *b, UINT m, WPARAM w, LPARAM l, LRESULT *ret);
void  DlgFrame(DlgBase *b, int x, int y, int w, int h, int kind);
HWND  UiLabel(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra);
HWND  UiEdit(DlgBase *b, const WCHAR *text, int x, int y, int w, int h, int id, DWORD extra);
HWND  UiButton(HWND p, const WCHAR *text, int x, int y, int w, int h, int id, DWORD style);
int   MpAsk(HWND owner, const WCHAR *title, const WCHAR *msg, const WCHAR *b1, const WCHAR *b2, const WCHAR *b3, int escIdx);

/* ------------------------------------------------------------ menu.c ---- */
#define MS_CHECK 1
#define MS_RADIO 2
#define MS_GRAY  4
typedef struct MenuDef MenuDef;
typedef struct MenuItem { const WCHAR *label, *accel; int id; const MenuDef *sub; } MenuItem;
struct MenuDef { const MenuItem *items; int n; };
typedef unsigned (*MenuStateFn)(int id);

extern const MenuDef g_mdEditCtx, g_mdEol, g_mdEnc, g_mdZoom, g_mdUcc, g_mdSys;
HWND  MenuBarCreate(HWND parent, MenuStateFn fn);
int   MenuBarHeight(void);
void  MenuBarRefont(HWND bar);
void  MenuBarActivate(HWND bar, int idx, int openPopup);
int   MenuBarMnemonic(WCHAR ch);
void  MenuPopup(HWND owner, const MenuDef *def, int x, int y, int anchorBottom);
BOOL  MenuActive(void);
void  MenuCancel(void);

/* ------------------------------------------------------------ status.c -- */
enum { SB_POS, SB_EOL, SB_ENC, SB_COUNT };
HWND  StatusCreate(HWND parent);
int   StatusHeight(void);
int   StatusMinWidth(void);
void  StatusSet(HWND sb, int idx, const WCHAR *text);
void  StatusRefont(HWND sb);

/* ------------------------------------------------------------- frame.c ---- */
/* the main window's title strip (drawn by us, in the chrome font; on by default, see FRAME_CUSTOM in frame.c).
 * with FRAME_CUSTOM 0 every function below is a no-op / falls through to the default window behaviour and FrameHeight() is 0 */
int      FrameEnabled(void);
int      FrameHeight(void);
void     FrameRefont(HWND h);
void     FrameInvalidate(HWND h);
void     FrameActive(HWND h, int active);
void     FramePaint(HWND h, HDC dc);
LRESULT  FrameNcCalc(HWND h, WPARAM w, LPARAM l);
LRESULT  FrameHitTest(HWND h, LPARAM l);                 /* WM_NCHITTEST; returns the hit code */
BOOL     FrameMsg(HWND h, UINT m, WPARAM w, LPARAM l);   /* mouse messages in the strip; TRUE = handled */
void     FrameSysMenu(HWND h, int x, int y);             /* screen coords; (-1,-1) = under the icon */
unsigned FrameSysState(int id);                          /* IDM_SYS_* menu states */
void     FrameSysCommand(HWND h, int id);                /* IDM_SYS_*: posts the matching WM_SYSCOMMAND */

/* -------------------------------------------------------------- doc.c ---- */
/* an encoding id is either one of the ENC_* values (0..4) or, when >= ENC_CP_MIN, a windows code page number */
enum { ENC_UTF8, ENC_UTF8BOM, ENC_UTF16LE, ENC_UTF16BE, ENC_ANSI, ENC_COUNT };
enum { EOL_CRLF, EOL_LF, EOL_CR, EOL_COUNT };
#define ENC_CP_MIN   100
#define ERR_NOMEM    8
#define ERR_TOO_BIG  223
#define ERR_BADCP    87
#define ERR_LOSSY    1113                           /* ERROR_NO_UNICODE_TRANSLATION */
extern const WCHAR *const g_encName[ENC_COUNT];
extern const WCHAR *const g_eolName[EOL_COUNT];
extern const WCHAR *const g_encShort[ENC_COUNT];            /* status bar texts: "utf-8 bom", "crlf" ... */
extern const WCHAR *const g_eolShort[EOL_COUNT];
void  EncLabel(int enc, WCHAR *out, int cap);       /* full name of any encoding id (menus, dialogs) */
void  EncShort(int enc, WCHAR *out, int cap);       /* status bar text for any encoding id */
int   EncListCount(void);                           /* every encoding the pickers offer */
int   EncListGet(int i, WCHAR *label, int cap);     /* returns the encoding id, label = "name  description" */
DWORD DocRead(const WCHAR *path, WCHAR **text, int *len, int *enc, int *eol, int forceEnc);   /* forceEnc -1 = detect */
DWORD DocWrite(const WCHAR *path, const WCHAR *text, int len, int enc, int eol, BOOL *lossy);

/* ---------------------------------------------------------- app state ---- */
#define FONT_MIN 10
#define FONT_MAX 96
typedef struct Prefs {
    WCHAR    font[32];
    int      pt, bold, italic;                  /* the size picked in the font dialog (saved); ctrl+0 returns to it */
    int      cur;                               /* working size: ctrl+plus / ctrl+minus / ctrl+wheel move it (not saved) */
    int      theme;                             /* THEME_DARK / THEME_LIGHT (saved) */
    COLORREF fg, bg;                            /* editor colours: always the theme's (C_EDIT_FG / C_EDIT_BG), not saved */
    int      wrap, statusbar;
    int      winx, winy, winw, winh, maximized;
    int      matchCase, wrapAround;
    int      marginL, marginT, marginR, marginB;      /* page setup, 1/1000 inch */
} Prefs;
extern Prefs g_pf;
extern HWND  g_hwnd, g_edit, g_status;

typedef struct DocState {
    WCHAR path[PATH_CAP];                       /* "" = not saved yet: the document is called `name` */
    WCHAR name[12];                             /* the default name of an unsaved document, "mintXXXX" (DefaultDocName) */
    int   enc, eol;                             /* what DocWrite will use on the next save */
} DocState;
extern DocState g_doc;
const WCHAR *AppDocName(void);                  /* main.c: the file name, or the default name while unsaved (title, prompts, print job) */

/* main.c services used by dialogs / the edit module */
void  AppApplyPrefs(void);                      /* re-create font/brush from g_pf and repaint */
void  AppSavePrefs(void);
void  AppOpenPath(const WCHAR *path);
BOOL  AppIsDirty(void);
void  AppUpdateTitle(void);
void  AppUpdateStatus(void);

/* -------------------------------------------------------------- edit.c --- */
#define EDIT_PAD 10                                   /* padding around the text, 96-dpi pixels (no frame around the editor) */
HWND   EditCreate(HWND parent);                 /* (re)creates g_edit for g_pf.wrap, carrying text/selection/rtl over */
void   EditApplyFont(void);                     /* font from g_pf (face, g_pf.cur size, dpi) */
void   EditApplyColors(void);                   /* bg brush from g_pf.bg, repaint */
HBRUSH EditBrush(void);
void   FontResolve(WCHAR *face);                /* swaps a missing face for consolas / lucida console / courier new */
void   EditSetDocText(const WCHAR *t);          /* load a document: resets undo + modified flag */
WCHAR *EditGetDocText(int *len);                /* heap copy (CRLF text), caller mem_free()s */
BOOL   EditHasSel(void);
void   EditScrollSoon(void);                    /* re-check which scrollbars are needed (they only show when the text needs them) */
const WCHAR *EditLockText(void **h, int *n);    /* zero-copy view of the text (CRLF), not nul terminated; pair with EditUnlockText */
void   EditUnlockText(void *h);
void   EditCaretPos(int *line, int *col);       /* 1-based, logical lines (also with word wrap on) */
int    EditLineCount(void);
BOOL   EditGotoLine(int line);
void   EditZoomStep(int dir);                   /* +1 / -1: next bigger / smaller size (10..96pt) */
void   EditZoomReset(void);                     /* back to g_pf.pt, the size picked in the font dialog */
BOOL   EditZoomCan(int dir);                    /* false at the ends of the range */
void   EditInsert(const WCHAR *s);
void   EditToggleRtl(void);
BOOL   EditIsRtl(void);

/* ------------------------------------------------------------- dialogs -- */
void  FindDlgShow(int replaceMode);
void  FindNext(int dirUp);
HWND  FindDlgHwnd(void);
BOOL  FindHasText(void);
void  GotoDlg(HWND owner);
void  AboutDlg(HWND owner);
void  HelpDlg(HWND owner);
BOOL  FontDlg(HWND owner);                      /* edits g_pf on ok; main applies it */
BOOL  EncDlg(HWND owner, int *enc, int reopen);
BOOL  FileDlgOpen(HWND owner, WCHAR *path, int cap);   /* native open dialog; path in: the current file (its folder is the start folder), out: the pick (only on ok) */
BOOL  FileDlgSave(HWND owner, WCHAR *path, int cap);   /* native save as dialog; path in: the file or the default name to propose, out: the pick (only on ok). no encoding / line ending pickers: the document keeps its own */
void  PrintDoc(HWND owner);
void  PageSetup(HWND owner);

#endif
