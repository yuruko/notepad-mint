/* fontdlg.c - the font dialog: family (optionally monospaced only), size 9..70, bold / italic, live preview.
 * the editor colours come from the theme (view > theme), so there are no colour settings here */
#include "mp.h"

enum { ID_LIST = 1301, ID_MONO, ID_SIZE, ID_BOLD, ID_ITALIC };
#define ID_PRESET  1310                             /* + index into g_sizes */
#define ID_PREVIEW 1360
#define ID_RESET   1370

typedef struct {
    DlgBase b;
    HWND    list, edit, pv;
    HFONT   font;                                   /* the preview's font (rebuilt on every change) */
    WCHAR   face[32];
    int     pt, bold, italic, mono;
    int     busy;                                   /* SetSizeText is writing the size edit: its EN_CHANGE echo is ignored */
} FontSt;

typedef struct { WCHAR name[32]; int mono; } FontEnt;   /* mono: 0 not measured yet, 1 monospaced, 2 proportional */

static FontEnt *g_fonts;                            /* every family, sorted with wcmpi, no duplicates. kept for the */
static int      g_nfonts, g_capfonts;               /* whole process: measuring hundreds of fonts is slow */
static int      g_monoOnly = 1;                     /* the filter's last explicit setting */

static const int g_sizes[15] = { 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36, 48, 70 };
static const WCHAR g_sample[] = L"sphinx of black quartz, judge my vow. 0123456789";

static int Clamp(int v) { return v < FONT_MIN ? FONT_MIN : (v > FONT_MAX ? FONT_MAX : v); }

/* ----------------------------------------------------------- families -- */
/* binary search: the index, or -1 with *at = where it would go */
static int FaceFind(const WCHAR *name, int *at)
{
    int lo = 0, hi = g_nfonts, mid, c;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        c = wcmpi(name, g_fonts[mid].name);
        if (!c) { *at = mid; return mid; }
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    *at = lo;
    return -1;
}

static void FaceAdd(const WCHAR *name)
{
    int at;
    if (!name[0] || name[0] == '@' || FaceFind(name, &at) >= 0) return;     /* '@' = the vertical (cjk) variants */
    if (g_nfonts == g_capfonts) {
        int cap = g_capfonts ? g_capfonts * 2 : 256;
        FontEnt *p = (FontEnt *)mem_realloc(g_fonts, (size_t)cap * sizeof(FontEnt));
        if (!p) return;
        g_fonts = p;
        g_capfonts = cap;
    }
    memmove(g_fonts + at + 1, g_fonts + at, (size_t)(g_nfonts - at) * sizeof(FontEnt));
    wcopy(g_fonts[at].name, name, 32);
    g_fonts[at].mono = 0;
    g_nfonts++;
}

/* one call per family and charset. bitmap fonts are left out: they don't scale over 9..70 pt */
static int CALLBACK EnumCb(const void *lf, const void *tm, DWORD type, LPARAM lp)
{
    (void)tm; (void)lp;
    if (!(type & RASTER_FONTTYPE)) FaceAdd(((const ENUMLOGFONTEXW *)lf)->elfLogFont.lfFaceName);
    return 1;
}

/* every open: fonts installed meanwhile show up, the measurements of the known ones stay */
static void FaceEnum(void)
{
    LOGFONTW lf;
    HDC dc = GetDC(NULL);
    if (!dc) return;
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, EnumCb, 0, 0);
    ReleaseDC(NULL, dc);
}

/* monospaced = "iiiiiiii" as wide as "WWWWWWWW" (the pitch flags lie too often). measured once, then cached */
static BOOL IsMono(HDC dc, int i)
{
    FontEnt *e = &g_fonts[i];
    if (!e->mono) {
        HFONT f = CreateFontW(-32, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, e->name);
        e->mono = 2;
        if (f) {
            SIZE a, b;
            HGDIOBJ of = SelectObject(dc, f);
            LONG dw;
            a.cx = 0; b.cx = 0;
            GetTextExtentPoint32W(dc, L"iiiiiiii", 8, &a);
            GetTextExtentPoint32W(dc, L"WWWWWWWW", 8, &b);
            SelectObject(dc, of);
            DeleteObject(f);
            dw = a.cx - b.cx;
            if (dw < 0) dw = -dw;
            if (a.cx > 0 && dw * 32 <= b.cx) e->mono = 1;
        }
    }
    return e->mono == 1;
}

/* ---------------------------------------------------------- controls --- */
static int Checked(HWND h, int id) { return SendMessageW(GetDlgItem(h, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

static void SetCheck(HWND h, int id, int on)
{
    SendMessageW(GetDlgItem(h, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* list item i -> out (32 chars). FALSE for no / a bad item */
static BOOL ItemText(HWND list, int i, WCHAR *out)
{
    LRESULT n;
    if (i < 0) return FALSE;
    n = SendMessageW(list, LB_GETTEXTLEN, (WPARAM)i, 0);
    if (n < 0 || n >= 32) return FALSE;
    SendMessageW(list, LB_GETTEXT, (WPARAM)i, (LPARAM)out);
    return TRUE;
}

static void FillList(FontSt *d)
{
    HCURSOR cur = NULL;
    HDC dc = NULL;
    int i;
    if (d->mono) {                                  /* the first time this measures every family */
        cur = SetCursor(LoadCursorW(NULL, IDC_WAIT));
        dc = CreateCompatibleDC(NULL);
    }
    SendMessageW(d->list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(d->list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_nfonts; i++)
        if (!d->mono || !dc || IsMono(dc, i)) SendMessageW(d->list, LB_ADDSTRING, 0, (LPARAM)g_fonts[i].name);
    if (dc) DeleteDC(dc);
    if (d->mono) SetCursor(cur ? cur : LoadCursorW(NULL, IDC_ARROW));     /* SetCursor(NULL) would hide the cursor */
    SendMessageW(d->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->list, NULL, TRUE);
}

/* select d->face, scrolled into view. force: a face the filter hides switches the filter off (open, reset);
 * otherwise (the filter was just switched on) nothing is selected and the face stays what it was */
static void SelectFace(FontSt *d, int force)
{
    int i = (int)SendMessageW(d->list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)d->face);
    if (i == LB_ERR && d->mono && force) {
        d->mono = 0;
        SetCheck(d->b.hwnd, ID_MONO, 0);
        FillList(d);
        i = (int)SendMessageW(d->list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)d->face);
    }
    SendMessageW(d->list, LB_SETCURSEL, (WPARAM)i, 0);
    if (i < 0) return;
    ItemText(d->list, i, d->face);                  /* the installed spelling */
    SendMessageW(d->list, LB_SETTOPINDEX, (WPARAM)(i > 3 ? i - 3 : 0), 0);
}

/* the size edit's number, clamped; def when it doesn't start with one (empty) */
static int SizeText(HWND e, int def)
{
    WCHAR t[16];
    const WCHAR *p = t;
    t[0] = 0;
    GetWindowTextW(e, t, 16);
    while (*p == ' ' || *p == '\t') p++;
    return (*p >= '0' && *p <= '9') ? Clamp(wtoi(p)) : def;
}

/* d->pt -> the size edit. WM_SETTEXT on an edit does send EN_CHANGE, synchronously: FontCmd re-enters from inside
 * SetWindowTextW. busy makes that echo return early (every caller rebuilds the preview itself afterwards), and the text is
 * only written when it differs, so this cannot loop */
static void SetSizeText(FontSt *d)
{
    WCHAR t[16], cur[16];
    wsprintfW(t, L"%d", d->pt);
    cur[0] = 0;
    GetWindowTextW(d->edit, cur, 16);
    if (wcmp(cur, t)) {
        d->busy = 1;
        SetWindowTextW(d->edit, t);
        d->busy = 0;
    }
}

/* the dialog's controls -> the local state. ok runs it because a scripted LB_SETCURSEL / BM_SETCHECK sends no notification
 * (WM_SETTEXT on the size edit does notify with EN_CHANGE, but the edit is read here anyway) */
static void Sync(FontSt *d)
{
    ItemText(d->list, (int)SendMessageW(d->list, LB_GETCURSEL, 0, 0), d->face);
    d->pt = SizeText(d->edit, d->pt);
    d->bold = Checked(d->b.hwnd, ID_BOLD);
    d->italic = Checked(d->b.hwnd, ID_ITALIC);
}

/* ----------------------------------------------------------- preview --- */
static HFONT MakeFont(const FontSt *d, int px)
{
    return CreateFontW(-(px < 1 ? 1 : px), 0, 0, 0, d->bold ? FW_BOLD : FW_NORMAL, (DWORD)(d->italic ? 1 : 0), 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, d->face);
}

/* one line at the actual selected size. long samples are clipped horizontally rather than shrinking the font */
#define PV_L   S(10)
#define PV_R   S(6)
static void PreviewFont(FontSt *d)
{
    HFONT f;
    if (!d->pv) return;
    f = MakeFont(d, MulDiv(d->pt, g_dpi, 72));
    if (!f) return;                                 /* preserve the last preview if a GDI allocation fails */
    if (d->font) DeleteObject(d->font);
    d->font = f;
    InvalidateRect(d->pv, NULL, FALSE);
}

static void PreviewDraw(HDC dc, const RECT *rc, const FontSt *d)
{
    RECT line = *rc;
    HGDIOBJ of;
    FillC(dc, rc, C_EDIT_BG);
    if (!d || !d->font) return;
    of = SelectObject(dc, d->font);
    line.left += PV_L; line.right -= PV_R;
    TextC(dc, g_sample, -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, C_EDIT_FG);
    if (of) SelectObject(dc, of);
}

/* double buffered, in the editor colours of the current theme (read here, never cached) */
static void PreviewPaint(HWND h, const FontSt *d)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc, mdc;
    HBITMAP bmp = NULL;
    HGDIOBJ ob = NULL;
    memset(&ps, 0, sizeof ps);
    dc = BeginPaint(h, &ps);
    if (!dc) { EndPaint(h, &ps); return; }
    if (!GetClientRect(h, &rc) || rc.right <= 0 || rc.bottom <= 0) { EndPaint(h, &ps); return; }
    mdc = CreateCompatibleDC(dc);
    if (mdc) bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    if (bmp) ob = SelectObject(mdc, bmp);
    if (ob && ob != (HGDIOBJ)(LONG_PTR)-1) {
        PreviewDraw(mdc, &rc, d);
        if (!BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY)) PreviewDraw(dc, &rc, d);
        SelectObject(mdc, ob);
    } else {
        PreviewDraw(dc, &rc, d);
    }
    if (bmp) DeleteObject(bmp);
    if (mdc) DeleteDC(mdc);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK PreviewProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_GETFONT) {
        const FontSt *d = (const FontSt *)GetWindowLongPtrW(h, GWLP_USERDATA);
        return d ? (LRESULT)d->font : 0;
    }
    if (m == WM_PRINTCLIENT) {
        RECT rc;
        GetClientRect(h, &rc);
        PreviewDraw((HDC)w, &rc, (const FontSt *)GetWindowLongPtrW(h, GWLP_USERDATA));
        return 0;
    }
    if (m == WM_PAINT) { PreviewPaint(h, (const FontSt *)GetWindowLongPtrW(h, GWLP_USERDATA)); return 0; }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------ dialog --- */
/* local state -> the controls (open, reset) */
static void ShowState(FontSt *d)
{
    SetCheck(d->b.hwnd, ID_BOLD, d->bold);
    SetCheck(d->b.hwnd, ID_ITALIC, d->italic);
    SetSizeText(d);
    SelectFace(d, 1);
    PreviewFont(d);
}

static void DrawItem(const DRAWITEMSTRUCT *di)
{
    WCHAR t[32];
    RECT r = di->rcItem;
    BOOL has = ItemText(di->hwndItem, (int)di->itemID, t), sel = has && (di->itemState & ODS_SELECTED);
    HGDIOBJ of;
    FillC(di->hDC, &r, sel ? C_ACCENT : C_FIELD);
    if (has) {
        of = SelectObject(di->hDC, g_fontUI);
        r.left += S(6); r.right -= S(2);
        TextC(di->hDC, t, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, sel ? C_ON_ACCENT : C_TEXT);
        SelectObject(di->hDC, of);
    }
    if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
}

static void Opt(HWND h, const WCHAR *text, int x, int y, int w, int id, DWORD extra, int on)
{
    HWND c = UiButton(h, text, x, y, w, 20, id, BS_AUTOCHECKBOX | extra);
    SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* client 528 x 440: family list | size + presets + bold / italic, preview below, buttons at the bottom.
 * the labels and the preview come first so they never sit inside a control group (arrow keys) */
static void FontLayout(FontSt *d)
{
    DlgBase *b = &d->b;
    HWND h = b->hwnd, c;
    WCHAR t[8];
    int i;
    UiLabel(h, L"font:", 12, 10, 236, 16, 0, SS_NOPREFIX);
    UiLabel(h, L"size:", 264, 10, 120, 16, 0, SS_NOPREFIX);
    UiLabel(h, L"9 to 70 pt", 328, 32, 180, 16, IDC_DIM, SS_NOPREFIX);
    UiLabel(h, L"preview:", 12, 228, 200, 16, 0, SS_NOPREFIX);
    DlgFrame(b, 12, 246, 504, 146, BV_SUNKEN);
    d->pv = CreateWindowExW(0, L"mp_fontpv", g_sample, WS_CHILD | WS_VISIBLE, S(12) + 2, S(246) + 2, S(504) - 4, S(146) - 4,
                            h, (HMENU)(ULONG_PTR)ID_PREVIEW, g_hinst, d);

    DlgFrame(b, 12, 28, 236, 166, BV_SUNKEN);
    d->list = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | WS_VSCROLL | LBS_NOTIFY |
                              LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                              S(12) + 2, S(28) + 2, S(236) - 4, S(166) - 4, h, (HMENU)(ULONG_PTR)ID_LIST, g_hinst, NULL);
    SendMessageW(d->list, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    DarkScroll(d->list);
    Opt(h, L"monospaced fonts only", 12, 200, 236, ID_MONO, WS_GROUP, d->mono);

    d->edit = UiEdit(b, L"", 264, 28, 56, 23, ID_SIZE, ES_NUMBER | WS_GROUP);
    SendMessageW(d->edit, EM_LIMITTEXT, 3, 0);
    for (i = 0; i < COUNTOF(g_sizes); i++) {                       /* one tab stop: the arrow keys move along */
        wsprintfW(t, L"%d", g_sizes[i]);
        c = UiButton(h, t, 264 + (i % 5) * 51, 60 + (i / 5) * 26, 48, 22, ID_PRESET + i, BS_PUSHBUTTON | (i ? 0 : WS_GROUP));
        if (i) SetWindowLongPtrW(c, GWL_STYLE, GetWindowLongPtrW(c, GWL_STYLE) & ~WS_TABSTOP);
    }
    Opt(h, L"bold", 264, 150, 80, ID_BOLD, WS_GROUP, d->bold);
    Opt(h, L"italic", 352, 150, 80, ID_ITALIC, 0, d->italic);

    UiButton(h, L"reset", 12, 404, 88, 24, ID_RESET, BS_PUSHBUTTON | WS_GROUP);
    UiButton(h, L"ok", 336, 404, 86, 24, IDOK, BS_DEFPUSHBUTTON | WS_GROUP);
    UiButton(h, L"cancel", 430, 404, 86, 24, IDCANCEL, BS_PUSHBUTTON);

    FillList(d);
    ShowState(d);
    b->focus = d->list;
}

static void FontCmd(FontSt *d, int id, int code)
{
    HWND h = d->b.hwnd;
    if (d->b.done) return;                                          /* closing: the focus moving away still notifies */
    switch (id) {
    case IDOK:
        Sync(d);
        d->b.result = IDOK;
        d->b.done = 1;
        return;
    case IDCANCEL:
        d->b.result = IDCANCEL;
        d->b.done = 1;
        return;
    case ID_LIST:
        if (code != LBN_SELCHANGE || !ItemText(d->list, (int)SendMessageW(d->list, LB_GETCURSEL, 0, 0), d->face)) return;
        break;
    case ID_MONO:
        d->mono = g_monoOnly = Checked(h, ID_MONO);
        FillList(d);
        SelectFace(d, 0);
        return;
    case ID_SIZE:
        if (d->busy || (code != EN_CHANGE && code != EN_KILLFOCUS)) return;     /* busy: the echo of SetSizeText */
        d->pt = SizeText(d->edit, d->pt);
        if (code == EN_KILLFOCUS) SetSizeText(d);                  /* the clamped value replaces what was typed */
        break;
    case ID_BOLD:
    case ID_ITALIC:
        d->bold = Checked(h, ID_BOLD);
        d->italic = Checked(h, ID_ITALIC);
        break;
    case ID_RESET:
        wcopy(d->face, L"Consolas", 32);
        FontResolve(d->face);
        d->pt = 11;
        d->bold = 0;
        d->italic = 0;
        ShowState(d);
        return;
    default:
        if (id < ID_PRESET || id >= ID_PRESET + COUNTOF(g_sizes)) return;
        d->pt = g_sizes[id - ID_PRESET];
        SetSizeText(d);
        break;
    }
    PreviewFont(d);
}

static LRESULT CALLBACK FontProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    FontSt *d = (FontSt *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE:
        FontLayout(d);
        return 0;
    case WM_COMMAND:
        FontCmd(d, LOWORD(w), HIWORD(w));
        return 0;
    case WM_MEASUREITEM:
        if (((MEASUREITEMSTRUCT *)l)->CtlID == ID_LIST) { ((MEASUREITEMSTRUCT *)l)->itemHeight = (UINT)S(18); return TRUE; }
        break;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)l)->CtlID == ID_LIST) { DrawItem((DRAWITEMSTRUCT *)l); return TRUE; }
        break;
    case WM_NCDESTROY:
        if (d->font) { DeleteObject(d->font); d->font = NULL; }
        d->pv = NULL;
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

/* works on a copy of g_pf's face / size / bold / italic; ok copies it back (main then applies it) */
BOOL FontDlg(HWND owner)
{
    static BOOL reg;
    FontSt d;
    if (!reg) {
        RegClass(L"mp_font", FontProc, 0, NULL);
        RegClass(L"mp_fontpv", PreviewProc, 0, NULL);
        reg = TRUE;
    }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    wcopy(d.face, g_pf.font, 32);
    d.pt = Clamp(g_pf.pt);
    d.bold = g_pf.bold != 0;
    d.italic = g_pf.italic != 0;
    d.mono = g_monoOnly;                            /* SelectFace switches it off when the current face isn't monospaced */
    FaceEnum();
    FaceAdd(d.face);                                /* the current face is always listed (even a bitmap font) */
    if (!DlgOpen(&d.b, L"mp_font", L"font", 528, 440, 0)) return FALSE;
    DlgRunModal(&d.b);
    if (d.b.result != IDOK) return FALSE;
    wcopy(g_pf.font, d.face, 32);
    g_pf.pt = Clamp(d.pt);
    g_pf.bold = d.bold;
    g_pf.italic = d.italic;
    return TRUE;
}
