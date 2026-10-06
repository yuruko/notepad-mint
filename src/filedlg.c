/* filedlg.c - open / save as / encoding picker. custom dialogs (the native ones can't follow the theme):
 * path edit + up, an owner-draw list (folders first, then the files matching the filter, or the drive list at the top),
 * file name, and mp_btn dropdowns that pop MenuPopup (files of type; save adds encoding + line ending) */
#include "mp.h"

enum { ID_PATH = 1101, ID_UP, ID_LIST, ID_NAME, ID_TYPE, ID_ENC, ID_EOL, ID_ENCLIST = 1201 };
enum { MI_TYPE = 2000, MI_ENC = 2100, MI_ENC_CUR = MI_ENC + ENC_COUNT, MI_ENC_OTHER = 2110, MI_EOL = 2200 };   /* popup ids */
enum { K_DIR = 1, K_FILE, K_DRIVE };                  /* list item data (low 4 bits; a drive keeps its GetDriveTypeW above) */
#define FILTER_CAP 260

static const WCHAR *const g_typeName[2] = { L"text documents (*.txt)", L"all files (*.*)" };
static const WCHAR *const g_typePat[2]  = { L"*.txt", L"*.*" };
static const WCHAR *const g_drvName[7]  = { L"", L"", L"removable disk", L"local disk", L"network drive", L"cd drive", L"ram disk" };
static WCHAR   g_last[PATH_CAP];                      /* the folder of the last file picked in this process */
static WNDPROC g_btnProc;

typedef struct {
    DlgBase b;
    int  save, type, enc, eol, cap;
    const WCHAR *in;
    HWND path, up, list, name, typeBtn, encBtn, eolBtn;
    WCHAR dir[PATH_CAP], res[PATH_CAP], filter[FILTER_CAP], cpLab[48];   /* dir "" = the drive list */
    MenuItem mi[ENC_COUNT + 3];
    MenuDef md;
} FileDlg;

typedef struct { DlgBase b; HWND list; int enc, reopen; } EncSt;

/* -------------------------------------------------------------- paths -- */
static BOOL IsSep(WCHAR c) { return c == '\\' || c == '/'; }

/* length of the root without its trailing separator: "c:" = 2, "\\server\share" = all of it, else 0 */
static int RootLen(const WCHAR *p)
{
    int i, k = 0;
    if (p[0] && p[1] == ':') return 2;
    if (!IsSep(p[0]) || !IsSep(p[1])) return 0;
    for (i = 2; p[i]; i++)
        if (IsSep(p[i]) && ++k == 2) return i;
    return i;
}

static BOOL IsRoot(const WCHAR *p) { return p[0] && wlen(p) <= RootLen(p) + 1; }

static BOOL HasAny(const WCHAR *s, const WCHAR *set)
{
    const WCHAR *c;
    for (; *s; s++)
        for (c = set; *c; c++)
            if (*s == *c) return TRUE;
    return FALSE;
}

static int Trim(WCHAR *t)
{
    int n = wlen(t), i = 0;
    while (n > 0 && t[n - 1] == ' ') t[--n] = 0;
    while (t[i] == ' ') i++;
    if (i) memmove(t, t + i, (size_t)(n - i + 1) * sizeof(WCHAR));
    return n - i;
}

/* a typed name -> full path: drive / unc names as they are, "\x" on the current folder's root, the rest joined to
 * the current folder. GetFullPathNameW then folds "." / "..". FALSE when it would not fit PATH_CAP */
static BOOL Resolve(const FileDlg *d, const WCHAR *in, WCHAR *out)
{
    WCHAR t[PATH_CAP];
    const WCHAR *src = t;
    int n = wlen(in), rl;
    DWORD r;

    if (!d->dir[0] || (in[0] && in[1] == ':') || (IsSep(in[0]) && IsSep(in[1]))) {
        src = in;
    } else if (IsSep(in[0])) {
        rl = RootLen(d->dir);
        if (rl + n >= PATH_CAP) return FALSE;
        memcpy(t, d->dir, (size_t)rl * sizeof(WCHAR));
        wcopy(t + rl, in, PATH_CAP - rl);
    } else {
        if (wlen(d->dir) + 1 + n >= PATH_CAP) return FALSE;
        wcopy(t, d->dir, PATH_CAP);
        PathJoin(t, in, PATH_CAP);
    }
    r = GetFullPathNameW(src, PATH_CAP, out, NULL);
    return r > 0 && r < PATH_CAP;
}

static int Ask(FileDlg *d, const WCHAR *what, const WCHAR *msg, BOOL yesNo)
{
    WCHAR t[PATH_CAP + 160];
    wcopy(t, what, PATH_CAP);
    wcat(t, L"\n\n", COUNTOF(t));
    wcat(t, msg, COUNTOF(t));
    return MpAsk(d->b.hwnd, d->save ? L"save as" : L"open", t, yesNo ? L"yes" : L"ok", yesNo ? L"no" : NULL, NULL, yesNo ? 2 : 1);
}

/* ------------------------------------------------------------ listing -- */
/* entries packed in one growing buffer: kind char, name, nul */
typedef struct { WCHAR *pool; int n, used, cap; } Ents;

static BOOL EntAdd(Ents *e, int kind, const WCHAR *nm)
{
    int len = wlen(nm), need = e->used + len + 2;
    if (need > e->cap) {
        int nc = e->cap ? e->cap * 2 : 4096;
        WCHAR *q;
        while (nc < need) nc *= 2;
        q = (WCHAR *)mem_realloc(e->pool, (size_t)nc * sizeof(WCHAR));
        if (!q) return FALSE;
        e->pool = q; e->cap = nc;
    }
    e->pool[e->used] = (WCHAR)kind;
    memcpy(e->pool + e->used + 1, nm, ((size_t)len + 1) * sizeof(WCHAR));
    e->used = need;
    e->n++;
    return TRUE;
}

static int EntCmp(const WCHAR *a, const WCHAR *b)
{
    int r = (int)a[0] - (int)b[0];                    /* folders first */
    return r ? r : wcmpi(a + 1, b + 1);
}

/* bottom-up merge sort (t = scratch for n): n log n compares however big the folder is */
static void Sort(WCHAR **a, WCHAR **t, int n)
{
    WCHAR **src = a, **dst = t, **x;
    int w, l, m, r, i, j, k;
    for (w = 1; w < n; w *= 2) {
        for (l = 0; l < n; l += 2 * w) {
            m = l + w < n ? l + w : n;
            r = l + 2 * w < n ? l + 2 * w : n;
            for (i = l, j = m, k = l; k < r; k++)
                dst[k] = (j >= r || (i < m && EntCmp(src[i], src[j]) <= 0)) ? src[i++] : src[j++];
        }
        x = src; src = dst; dst = x;
    }
    if (src != a) memcpy(a, src, (size_t)n * sizeof(WCHAR *));
}

static int AddItem(HWND list, const WCHAR *t, int data)
{
    int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)t);
    if (k >= 0) SendMessageW(list, LB_SETITEMDATA, (WPARAM)k, (LPARAM)data);
    return k;
}

/* lists `dir` ("" = the drives). the folder is read completely before the list is touched,
 * so a folder that can't be read leaves the old listing in place */
static BOOL Fill(FileDlg *d, const WCHAR *dir)
{
    WCHAR pat[PATH_CAP], s[4], **ix = NULL, *p;
    WIN32_FIND_DATAW fd;
    HANDLE f;
    Ents e;
    DWORD mask;
    UINT t;
    int i;

    memset(&e, 0, sizeof e);
    if (!dir[0]) {
        mask = GetLogicalDrives();
        s[1] = ':'; s[2] = '\\'; s[3] = 0;
        for (i = 0; i < 26; i++) {
            if (!(mask & (1u << i))) continue;
            s[0] = (WCHAR)('a' + i);
            t = GetDriveTypeW(s);
            if (t <= DRIVE_NO_ROOT_DIR) continue;
            if (!EntAdd(&e, K_DRIVE | (int)(t << 4), s)) break;
        }
    } else {
        if (wlen(dir) + 2 >= PATH_CAP) return FALSE;
        wcopy(pat, dir, PATH_CAP);
        PathJoin(pat, L"*", PATH_CAP);
        f = FindFirstFileW(pat, &fd);
        if (f == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;       /* (an empty drive root has no entries at all) */
        } else {
            do {
                const WCHAR *nm = fd.cFileName;
                BOOL isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2])))) continue;
                if (!isDir && !WildMatch(d->filter, nm)) continue;
                if (!EntAdd(&e, isDir ? K_DIR : K_FILE, nm)) break;
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    if (e.n) {
        ix = (WCHAR **)mem_alloc((size_t)e.n * 2 * sizeof(WCHAR *));
        if (!ix) { mem_free(e.pool); return FALSE; }
        for (p = e.pool, i = 0; i < e.n; i++) { ix[i] = p; p += wlen(p + 1) + 2; }
        if (dir[0]) Sort(ix, ix + e.n, e.n);
    }
    SendMessageW(d->list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(d->list, LB_RESETCONTENT, 0, 0);
    SendMessageW(d->list, LB_INITSTORAGE, (WPARAM)e.n, (LPARAM)((size_t)e.used * sizeof(WCHAR)));
    for (i = 0; i < e.n; i++)
        if (AddItem(d->list, ix[i] + 1, ix[i][0]) < 0) break;
    SendMessageW(d->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->list, NULL, TRUE);
    mem_free(ix);
    mem_free(e.pool);
    return TRUE;
}

/* shows `path` ("" = the drive list) and selects `sel` if it is listed. quiet: no message on failure */
static BOOL Go(FileDlg *d, const WCHAR *path, const WCHAR *sel, BOOL quiet)
{
    WCHAR nd[PATH_CAP];
    DWORD r;
    int n;

    nd[0] = 0;
    if (path[0]) {
        r = GetFullPathNameW(path, PATH_CAP, nd, NULL);
        if (!r || r >= PATH_CAP || !IsDir(nd)) {
            if (!quiet) Ask(d, path, L"path does not exist. check the path and try again.", FALSE);
            return FALSE;
        }
        n = wlen(nd);
        while (n > RootLen(nd) + 1 && IsSep(nd[n - 1])) nd[--n] = 0;
    }
    if (!Fill(d, nd)) {
        if (!quiet) Ask(d, nd, L"this folder can't be opened. it may be unavailable, or access may be denied.", FALSE);
        return FALSE;
    }
    wcopy(d->dir, nd, PATH_CAP);
    SetWindowTextW(d->path, nd);
    if (!nd[0] && GetFocus() == d->up) SetFocus(d->list);
    EnableWindow(d->up, nd[0] != 0);
    if (sel && sel[0]) {
        n = (int)SendMessageW(d->list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)sel);
        if (n >= 0) SendMessageW(d->list, LB_SETCURSEL, (WPARAM)n, 0);
    }
    return TRUE;
}

static void Up(FileDlg *d)
{
    WCHAR par[PATH_CAP], from[PATH_CAP];
    if (!d->dir[0]) return;
    if (IsRoot(d->dir)) { par[0] = 0; wcopy(from, d->dir, PATH_CAP); }        /* a root goes up to the drive list */
    else { PathDir(d->dir, par, PATH_CAP); wcopy(from, PathName(d->dir), PATH_CAP); }
    Go(d, par, from, FALSE);
}

/* the look-in edit. force = 0: only when its text differs from the folder shown (it was typed / WM_SETTEXT'd) */
static BOOL PathGo(FileDlg *d, BOOL force)
{
    WCHAR t[PATH_CAP], full[PATH_CAP];
    GetWindowTextW(d->path, t, PATH_CAP);
    Trim(t);
    if (!force && wcmpi(t, d->dir) == 0) return TRUE;
    if (!t[0]) return Go(d, L"", NULL, FALSE);
    if (!Resolve(d, t, full)) { Ask(d, t, L"path does not exist. check the path and try again.", FALSE); return FALSE; }
    return Go(d, full, NULL, FALSE);
}

/* the selected item's text (PATH_CAP buffer) and kind, 0 = nothing selected */
static int SelItem(FileDlg *d, WCHAR *t)
{
    int i = (int)SendMessageW(d->list, LB_GETCURSEL, 0, 0), n;
    t[0] = 0;
    if (i < 0) return 0;
    n = (int)SendMessageW(d->list, LB_GETTEXTLEN, (WPARAM)i, 0);
    if (n < 0 || n >= PATH_CAP) return 0;
    SendMessageW(d->list, LB_GETTEXT, (WPARAM)i, (LPARAM)t);
    return (int)SendMessageW(d->list, LB_GETITEMDATA, (WPARAM)i, 0) & 15;
}

/* "*.log" or "c:\logs\*.log": a new filter (and folder) */
static void Filter(FileDlg *d, const WCHAR *t)
{
    WCHAR part[PATH_CAP], full[PATH_CAP], old[FILTER_CAP];
    const WCHAR *nm = PathName(t);
    int n = (int)(nm - t);

    if (n > 0) {
        memcpy(part, t, (size_t)n * sizeof(WCHAR));
        part[n] = 0;
        if (!Resolve(d, part, full)) { Ask(d, part, L"path does not exist. check the path and try again.", FALSE); return; }
    } else {
        wcopy(full, d->dir, PATH_CAP);
    }
    wcopy(old, d->filter, FILTER_CAP);
    wcopy(d->filter, nm, FILTER_CAP);
    if (!Go(d, full, NULL, FALSE)) { wcopy(d->filter, old, FILTER_CAP); return; }
    SetWindowTextW(d->name, nm);
    SendMessageW(d->name, EM_SETSEL, 0, -1);
}

/* ok on a file name: wildcards filter, a folder is entered, else the checks open / save as need */
static void Accept(FileDlg *d, const WCHAR *raw)
{
    WCHAR t[PATH_CAP], full[PATH_CAP], dir[PATH_CAP];
    const WCHAR *nm;
    BOOL quoted = FALSE;
    int n;

    wcopy(t, raw, PATH_CAP);
    n = Trim(t);
    if (n >= 2 && t[0] == '"' && t[n - 1] == '"') {            /* "name": taken as it is (no .txt added) */
        t[n - 1] = 0;
        memmove(t, t + 1, (size_t)(n - 1) * sizeof(WCHAR));
        quoted = TRUE;
    }
    if (!t[0]) { SetFocus(d->name); return; }
    if (HasAny(t, L"*?")) { Filter(d, t); return; }
    if (!Resolve(d, t, full)) { Ask(d, t, L"the file name is not valid.", FALSE); return; }
    if (IsDir(full)) {
        if (Go(d, full, NULL, FALSE)) SetWindowTextW(d->name, L"");
        return;
    }
    nm = PathName(full);
    if (!nm[0] || HasAny(nm, L"<>|\":")) { Ask(d, t, L"the file name is not valid.", FALSE); return; }

    if (d->save) {
        if (!quoted && !HasAny(nm, L".")) {                     /* notepad: no dot in the name => .txt */
            if (wlen(full) + 4 >= PATH_CAP) { Ask(d, t, L"the file name is too long.", FALSE); return; }
            wcat(full, L".txt", PATH_CAP);
        }
        PathDir(full, dir, PATH_CAP);
        if (!IsDir(dir)) { Ask(d, dir, L"path does not exist. check the path and try again.", FALSE); return; }
        if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES &&
            Ask(d, full, L"already exists. do you want to replace it?", TRUE) != 1) return;
    } else if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        if (!HasAny(nm, L".") && wlen(full) + 4 < PATH_CAP) wcat(full, L".txt", PATH_CAP);   /* "readme" finds readme.txt */
        if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES || IsDir(full)) {
            Ask(d, t, L"file not found. check the file name and try again.", FALSE);
            return;
        }
    }
    if (wlen(full) >= d->cap) { Ask(d, t, L"the file name is too long.", FALSE); return; }
    wcopy(d->res, full, PATH_CAP);
    d->b.result = 1;
    d->b.done = 1;
}

/* double click / enter on a list item */
static void Activate(FileDlg *d)
{
    WCHAR t[PATH_CAP], p[PATH_CAP];
    int k = SelItem(d, t);
    if (k == K_DRIVE) {
        Go(d, t, NULL, FALSE);
    } else if (k == K_DIR) {
        if (wlen(d->dir) + wlen(t) + 2 > PATH_CAP) return;
        wcopy(p, d->dir, PATH_CAP);
        PathJoin(p, t, PATH_CAP);
        Go(d, p, NULL, FALSE);
    } else if (k == K_FILE) {
        SetWindowTextW(d->name, t);
        Accept(d, t);
    }
}

/* ------------------------------------------------------------ drawing -- */
/* 16x16 (96-dpi) line glyphs: folder, file, drive */
static void Glyph(HDC dc, int kind, int x, int y, COLORREF c)
{
    static const POINT dirP[6] = { {1, 3}, {6, 3}, {8, 5}, {15, 5}, {15, 14}, {1, 14} };
    static const POINT fileP[5] = { {3, 1}, {10, 1}, {13, 4}, {13, 15}, {3, 15} };
    static const POINT drvP[4] = { {1, 5}, {15, 5}, {15, 12}, {1, 12} };
    const POINT *s = kind == K_DIR ? dirP : (kind == K_DRIVE ? drvP : fileP);
    int n = kind == K_DIR ? 6 : (kind == K_DRIVE ? 4 : 5), i;
    POINT p[6];
    HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetDCPenColor(dc, c);
    for (i = 0; i < n; i++) { p[i].x = x + S(s[i].x); p[i].y = y + S(s[i].y); }
    Polygon(dc, p, n);
    if (kind == K_DIR) {                                        /* the front flap */
        MoveToEx(dc, x + S(1), y + S(7), NULL); LineTo(dc, x + S(15), y + S(7));
    } else if (kind == K_DRIVE) {                               /* the activity light */
        MoveToEx(dc, x + S(10), y + S(9), NULL); LineTo(dc, x + S(13), y + S(9));
    } else {                                                    /* the folded corner */
        MoveToEx(dc, x + S(10), y + S(1), NULL); LineTo(dc, x + S(10), y + S(4)); LineTo(dc, x + S(13), y + S(4));
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

/* both lists: selection = mint fill + on-accent text. files: glyph + name (+ the drive kind, dim).
 * encodings: "name  description" in two columns */
static void DrawItem(const DRAWITEMSTRUCT *di, int files)
{
    WCHAR t[PATH_CAP];
    RECT r = di->rcItem;
    HDC dc = di->hDC;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    COLORREF fg = sel ? C_ON_ACCENT : C_TEXT, dim = sel ? C_ON_ACCENT : C_DIM;
    UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    int n = -1, k = 0, kind = (int)(di->itemData & 15);
    HGDIOBJ of;

    FillC(dc, &r, sel ? C_ACCENT : C_FIELD);
    if (di->itemID != (UINT)-1) n = (int)SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    if (n >= 0 && n < PATH_CAP) {
        SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)t);
        of = SelectObject(dc, g_fontUI);
        if (files) {
            Glyph(dc, kind, r.left + S(5), r.top + (r.bottom - r.top - S(16)) / 2,
                  sel ? C_ON_ACCENT : (kind == K_FILE ? C_DIM : C_ACCENT_FG));
            r.left += S(28);
            TextC(dc, t, n, &r, fmt, fg);
            if (kind == K_DRIVE) {
                r.left += TextW(dc, t, n) + S(16);
                k = (int)(di->itemData >> 4);
                TextC(dc, g_drvName[k < 7 ? k : 0], -1, &r, fmt, dim);
            }
        } else {
            while (k < n && !(t[k] == ' ' && t[k + 1] == ' ')) k++;
            r.left += S(8);
            TextC(dc, t, k, &r, fmt, fg);
            if (k < n) {
                r.left = di->rcItem.left + S(140);
                TextC(dc, t + k + 2, -1, &r, fmt, dim);
            }
        }
        SelectObject(dc, of);
    }
    if (di->itemState & ODS_FOCUS) DrawFocusRect(dc, &di->rcItem);
}

/* owner-draw list inside a drawn 2px sunken frame (like UiEdit) */
static HWND ListBox(DlgBase *b, int x, int y, int w, int h, int id, DWORD extra)
{
    HWND c;
    DlgFrame(b, x, y, w, h, BV_SUNKEN);
    c = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                        LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | extra,
                        S(x) + 2, S(y) + 2, S(w) - 4, S(h) - 4, b->hwnd, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    DarkScroll(c);
    return c;
}

/* a dropdown = an mp_btn showing the current pick, with a down arrow painted over its face */
static LRESULT CALLBACK DropProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r = CallWindowProcW(g_btnProc, h, m, w, l);
    if (m == WM_PAINT) {
        RECT rc;
        HDC dc = GetDC(h);
        GetClientRect(h, &rc);
        Tri(dc, rc.right - S(16), (rc.bottom - S(4)) / 2, S(4), 1, IsWindowEnabled(h) ? C_TEXT : C_DIM);
        ReleaseDC(h, dc);
    }
    return r;
}

static HWND DropBtn(HWND p, const WCHAR *text, int x, int y, int id)
{
    HWND c = UiButton(p, text, x, y, 220, 24, id, BS_PUSHBUTTON);
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(c, GWLP_WNDPROC, (LONG_PTR)DropProc);
    if (!g_btnProc) g_btnProc = old;
    return c;
}

/* IsDialogMessage turns enter into IDOK whatever has the focus: a focused push button other than ok gets the click */
static BOOL FocusClick(HWND dlg)
{
    WCHAR cn[16];
    HWND f = GetFocus();
    if (!f || GetParent(f) != dlg || GetWindowLongPtrW(f, GWLP_ID) == IDOK) return FALSE;
    GetClassNameW(f, cn, 16);
    if (wcmp(cn, L"mp_btn") != 0) return FALSE;
    SendMessageW(f, BM_CLICK, 0, 0);
    return TRUE;
}

/* -------------------------------------------------------- file dialog -- */
static void Item(MenuItem *m, const WCHAR *label, int id)
{
    m->label = label; m->accel = NULL; m->id = id; m->sub = NULL;
}

static void Drop(FileDlg *d, int id)
{
    HWND btn = id == ID_TYPE ? d->typeBtn : (id == ID_ENC ? d->encBtn : d->eolBtn);
    RECT r;
    int i, n = 0;
    if (id == ID_TYPE) {
        for (i = 0; i < 2; i++) Item(&d->mi[n++], g_typeName[i], MI_TYPE + i);
    } else if (id == ID_ENC) {
        for (i = 0; i < ENC_COUNT; i++) Item(&d->mi[n++], g_encName[i], MI_ENC + i);
        if (d->enc >= ENC_CP_MIN) { EncLabel(d->enc, d->cpLab, COUNTOF(d->cpLab)); Item(&d->mi[n++], d->cpLab, MI_ENC_CUR); }
        Item(&d->mi[n++], NULL, 0);
        Item(&d->mi[n++], L"other code page...", MI_ENC_OTHER);
    } else {
        for (i = 0; i < EOL_COUNT; i++) Item(&d->mi[n++], g_eolName[i], MI_EOL + i);
    }
    d->md.items = d->mi;
    d->md.n = n;
    GetWindowRect(btn, &r);
    MenuPopup(d->b.hwnd, &d->md, r.left, r.bottom, 0);          /* the pick comes back as a posted WM_COMMAND */
}

static void SetEnc(FileDlg *d, int e)
{
    WCHAR t[48];
    d->enc = e;
    EncLabel(e, t, COUNTOF(t));
    SetWindowTextW(d->encBtn, t);
}

static void OnOk(FileDlg *d)
{
    WCHAR t[PATH_CAP];
    HWND f = GetFocus();
    int k;
    if (f == d->path) { PathGo(d, TRUE); return; }
    if (f == d->list) {
        k = SelItem(d, t);
        if (k == K_DIR || k == K_DRIVE) { Activate(d); return; }
    }
    if (FocusClick(d->b.hwnd) || !PathGo(d, FALSE)) return;     /* a look-in path typed but not entered yet counts */
    GetWindowTextW(d->name, t, PATH_CAP);
    Accept(d, t);
}

static void FileCmd(FileDlg *d, int id, int code)
{
    WCHAR t[PATH_CAP];
    int e;
    switch (id) {
    case IDOK:     OnOk(d); return;
    case IDCANCEL: d->b.result = 0; d->b.done = 1; return;
    case ID_UP:    Up(d); return;
    case ID_LIST:
        if (code == LBN_SELCHANGE && SelItem(d, t) == K_FILE) SetWindowTextW(d->name, t);
        else if (code == LBN_DBLCLK) Activate(d);
        return;
    case ID_TYPE: case ID_ENC: case ID_EOL:
        if (code == BN_CLICKED) Drop(d, id);
        return;
    case MI_ENC_OTHER:
        e = d->enc;
        if (EncDlg(d->b.hwnd, &e, 0)) SetEnc(d, e);
        return;
    }
    if (id >= MI_TYPE && id < MI_TYPE + 2) {
        d->type = id - MI_TYPE;
        wcopy(d->filter, g_typePat[d->type], FILTER_CAP);
        SetWindowTextW(d->typeBtn, g_typeName[d->type]);
        Go(d, d->dir, NULL, FALSE);
    } else if (id >= MI_ENC && id < MI_ENC + ENC_COUNT) {
        SetEnc(d, id - MI_ENC);
    } else if (id >= MI_EOL && id < MI_EOL + EOL_COUNT) {
        d->eol = id - MI_EOL;
        SetWindowTextW(d->eolBtn, g_eolName[d->eol]);
    }
}

static void FileCreate(FileDlg *d)
{
    HWND h = d->b.hwnd;
    WCHAR t[PATH_CAP];
    DWORD n;

    UiLabel(h, L"look in:", 12, 16, 72, 16, 0, 0);
    d->path = UiEdit(&d->b, L"", 86, 12, 396, 24, ID_PATH, 0);
    d->up = UiButton(h, L"up", 490, 12, 78, 24, ID_UP, BS_PUSHBUTTON);
    d->list = ListBox(&d->b, 12, 44, 556, 292, ID_LIST, LBS_WANTKEYBOARDINPUT);
    UiLabel(h, L"file name:", 12, 352, 96, 16, 0, 0);
    d->name = UiEdit(&d->b, d->save ? PathName(d->in) : L"", 112, 348, 356, 24, ID_NAME, 0);
    UiLabel(h, L"files of type:", 12, 384, 96, 16, 0, 0);
    d->typeBtn = DropBtn(h, g_typeName[d->type], 112, 380, ID_TYPE);
    if (d->save) {
        EncLabel(d->enc, t, 48);
        UiLabel(h, L"encoding:", 12, 416, 96, 16, 0, 0);
        d->encBtn = DropBtn(h, t, 112, 412, ID_ENC);
        UiLabel(h, L"line ending:", 12, 448, 96, 16, 0, 0);
        d->eolBtn = DropBtn(h, g_eolName[d->eol], 112, 444, ID_EOL);
    }
    UiButton(h, d->save ? L"save" : L"open", 480, 348, 88, 24, IDOK, BS_DEFPUSHBUTTON);
    UiButton(h, L"cancel", 480, 380, 88, 24, IDCANCEL, BS_PUSHBUTTON);
    SendMessageW(d->path, EM_LIMITTEXT, PATH_CAP - 1, 0);
    SendMessageW(d->name, EM_LIMITTEXT, PATH_CAP - 1, 0);
    SendMessageW(d->name, EM_SETSEL, 0, -1);
    d->b.focus = d->name;

    /* start folder: the file's own folder, else the last one used, else the current directory, else the drives */
    PathDir(d->in, t, PATH_CAP);
    if (t[0] && Go(d, t, PathName(d->in), TRUE)) return;
    if (g_last[0] && Go(d, g_last, NULL, TRUE)) return;
    n = GetCurrentDirectoryW(PATH_CAP, t);
    if (n && n < PATH_CAP && Go(d, t, NULL, TRUE)) return;
    Go(d, L"", NULL, TRUE);
}

static LRESULT CALLBACK FileProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    FileDlg *d = (FileDlg *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE:
        FileCreate(d);
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)l)->itemHeight = (UINT)S(20);
        return TRUE;
    case WM_DRAWITEM:
        DrawItem((const DRAWITEMSTRUCT *)l, 1);
        return TRUE;
    case WM_VKEYTOITEM:                                          /* backspace in the list = up (after the list's own key handling) */
        if ((HWND)l == d->list && LOWORD(w) == VK_BACK) { PostMessageW(h, WM_COMMAND, ID_UP, 0); return -2; }
        return -1;
    case WM_COMMAND:
        FileCmd(d, LOWORD(w), HIWORD(w));
        return 0;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

/* enc / eol NULL = the open dialog. path is only written on ok */
static BOOL FileRun(HWND owner, WCHAR *path, int cap, int *enc, int *eol)
{
    static BOOL reg;
    FileDlg d;
    if (!path || cap <= 0) return FALSE;
    if (!reg) { RegClass(L"mp_file", FileProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    d.save = enc && eol;
    d.in = path;
    d.cap = cap;
    d.enc = d.save ? *enc : ENC_UTF8;
    d.eol = d.save && *eol >= 0 && *eol < EOL_COUNT ? *eol : EOL_CRLF;
    wcopy(d.filter, g_typePat[0], FILTER_CAP);
    if (!DlgOpen(&d.b, L"mp_file", d.save ? L"save as" : L"open", 580, d.save ? 484 : 420, 0)) return FALSE;
    DlgRunModal(&d.b);
    if (d.b.result != 1) return FALSE;
    wcopy(path, d.res, cap);
    PathDir(d.res, g_last, PATH_CAP);
    if (d.save) { *enc = d.enc; *eol = d.eol; }
    return TRUE;
}

BOOL FileDlgOpen(HWND owner, WCHAR *path, int cap)
{
    return FileRun(owner, path, cap, NULL, NULL);
}

BOOL FileDlgSave(HWND owner, WCHAR *path, int cap, int *enc, int *eol)
{
    if (!enc || !eol) return FALSE;
    return FileRun(owner, path, cap, enc, eol);
}

/* ---------------------------------------------------- encoding picker -- */
static LRESULT CALLBACK EncProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DlgBase *b = DlgFromHwnd(h, m, l);
    EncSt *d = (EncSt *)b;
    LRESULT r;
    if (!b) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        WCHAR t[96];
        int i, k, e, sel = -1, n = EncListCount();
        UiLabel(h, d->reopen ? L"pick the encoding to read the file with:" : L"pick the encoding to save the file with:",
                16, 14, 368, 16, 0, 0);
        d->list = ListBox(b, 16, 36, 368, 272, ID_ENCLIST, 0);
        for (i = 0; i < n; i++) {
            e = EncListGet(i, t, COUNTOF(t));
            k = AddItem(d->list, t, e);
            if (e == d->enc && sel < 0) sel = k;
        }
        if (sel < 0 && d->enc >= ENC_CP_MIN) {                   /* a code page the table doesn't list */
            EncLabel(d->enc, t, COUNTOF(t));
            sel = AddItem(d->list, t, d->enc);
        }
        if (sel >= 0) {
            SendMessageW(d->list, LB_SETTOPINDEX, (WPARAM)(sel > 5 ? sel - 5 : 0), 0);
            SendMessageW(d->list, LB_SETCURSEL, (WPARAM)sel, 0);
        }
        UiButton(h, L"ok", 200, 320, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        UiButton(h, L"cancel", 296, 320, 88, 24, IDCANCEL, BS_PUSHBUTTON);
        b->focus = d->list;
        return 0; }
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)l)->itemHeight = (UINT)S(20);
        return TRUE;
    case WM_DRAWITEM:
        DrawItem((const DRAWITEMSTRUCT *)l, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK && FocusClick(h)) return 0;
        if (LOWORD(w) == IDOK || (LOWORD(w) == ID_ENCLIST && HIWORD(w) == LBN_DBLCLK)) {
            int i = (int)SendMessageW(d->list, LB_GETCURSEL, 0, 0);
            if (i >= 0) {
                d->enc = (int)SendMessageW(d->list, LB_GETITEMDATA, (WPARAM)i, 0);
                b->result = 1; b->done = 1;
            }
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) { b->result = 0; b->done = 1; return 0; }
        break;
    }
    if (DlgCommon(b, m, w, l, &r)) return r;
    return DefWindowProcW(h, m, w, l);
}

BOOL EncDlg(HWND owner, int *enc, int reopen)
{
    static BOOL reg;
    EncSt d;
    if (!enc) return FALSE;
    if (!reg) { RegClass(L"mp_enc", EncProc, 0, NULL); reg = TRUE; }
    memset(&d, 0, sizeof d);
    DlgBaseInit(&d.b, owner);
    d.enc = *enc;
    d.reopen = reopen;
    if (!DlgOpen(&d.b, L"mp_enc", reopen ? L"reopen with encoding" : L"other code page", 400, 360, 0)) return FALSE;
    DlgRunModal(&d.b);
    if (d.b.result != 1) return FALSE;
    *enc = d.enc;
    return TRUE;
}
