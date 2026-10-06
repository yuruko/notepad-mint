/* filedlg.c - open / save as and the encoding picker.
 * open / save as are the native comdlg32 dialogs (GetOpenFileNameW / GetSaveFileNameW), loaded on first use like the print
 * dialogs. they follow the system theme (light): a native dialog can't be darkened, so there is no custom one any more.
 * the encoding picker at the bottom is ours (an owner-draw list in the app's own theme). */
#include "mp.h"

enum { ID_ENCLIST = 1201 };

typedef char SzOfn[sizeof(OPENFILENAMEW) == 88 ? 1 : -1];   /* the 32-bit sdk size (commdlg.h packs it to 1 byte on x86) */

typedef BOOL  (WINAPI *OfnFn)(OPENFILENAMEW *);
typedef DWORD (WINAPI *CdErrFn)(void);
static HMODULE g_cd;
static OfnFn   pOpen, pSave;
static CdErrFn pCdErr;
static WCHAR   g_last[PATH_CAP];                      /* the folder of the last file picked in this process */

typedef struct { DlgBase b; HWND list; int enc, reopen; } EncSt;

/* ------------------------------------------------------- open / save as -- */
static BOOL CdLoad(void)
{
    if (!g_cd && (g_cd = LoadLibraryW(L"comdlg32.dll")) != NULL) {
        pOpen  = (OfnFn)GetProcAddress(g_cd, "GetOpenFileNameW");
        pSave  = (OfnFn)GetProcAddress(g_cd, "GetSaveFileNameW");
        pCdErr = (CdErrFn)GetProcAddress(g_cd, "CommDlgExtendedError");
    }
    return pOpen && pSave && pCdErr;
}

/* `path` in: what the dialog proposes (the folder and name of the current file; for an unsaved document just its default
 * name; the open dialog only uses the folder). `path` out: the pick, written only on ok (cancel / error / too long: untouched).
 * a ".txt" is added to a name typed without extension (lpstrDefExt). the dialog is modal over `owner`; the modeless find dialog
 * is held too, or replace all could still edit the document while it is up (what DlgRunModal does for our own dialogs) */
static BOOL PickFile(HWND owner, WCHAR *path, int cap, BOOL save)
{
    OPENFILENAMEW o;
    WCHAR buf[PATH_CAP], dir[PATH_CAP];
    HWND fd;
    DWORD er = 0;
    BOOL ok, held = FALSE;

    if (!path || cap <= 0) return FALSE;
    if (!CdLoad()) {
        MpAsk(owner, APP_NAME, L"cannot show the file dialog.", L"ok", NULL, NULL, 1);
        return FALSE;
    }

    PathDir(path, dir, PATH_CAP);                       /* start folder: the file's own, else the last one picked, else the system's choice */
    if (!IsDir(dir)) wcopy(dir, g_last, PATH_CAP);
    if (!IsDir(dir)) dir[0] = 0;
    wcopy(buf, save ? PathName(path) : L"", PATH_CAP);

    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = owner;
    o.lpstrFilter = L"text documents (*.txt)\0*.txt\0all files (*.*)\0*.*\0";     /* (the literal's own nul ends the list) */
    o.nFilterIndex = 1;
    o.lpstrFile = buf;
    o.nMaxFile = PATH_CAP;
    o.lpstrInitialDir = dir[0] ? dir : NULL;
    o.lpstrTitle = save ? L"save as" : L"open";
    o.lpstrDefExt = L"txt";
    o.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    fd = FindDlgHwnd();
    if (fd && IsWindow(fd) && IsWindowEnabled(fd)) { EnableWindow(fd, FALSE); held = TRUE; }
    ok = save ? pSave(&o) : pOpen(&o);
    if (!ok) er = pCdErr();                             /* 0 = the user cancelled */
    if (held && IsWindow(fd)) EnableWindow(fd, TRUE);

    if (!ok) {
        if (er) MpAsk(owner, APP_NAME, er == FNERR_BUFFERTOOSMALL ? L"the file name is too long." : L"cannot show the file dialog.",
                      L"ok", NULL, NULL, 1);
        return FALSE;
    }
    if (wlen(buf) >= cap) {
        MpAsk(owner, APP_NAME, L"the file name is too long.", L"ok", NULL, NULL, 1);
        return FALSE;
    }
    wcopy(path, buf, cap);
    PathDir(buf, g_last, PATH_CAP);
    return TRUE;
}

BOOL FileDlgOpen(HWND owner, WCHAR *path, int cap)
{
    return PickFile(owner, path, cap, FALSE);
}

BOOL FileDlgSave(HWND owner, WCHAR *path, int cap)
{
    return PickFile(owner, path, cap, TRUE);
}

/* ---------------------------------------------------- encoding picker -- */
static int AddItem(HWND list, const WCHAR *t, int data)
{
    int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)t);
    if (k >= 0) SendMessageW(list, LB_SETITEMDATA, (WPARAM)k, (LPARAM)data);
    return k;
}

/* "name  description" in two columns; the selection = mint fill + on-accent text */
static void DrawItem(const DRAWITEMSTRUCT *di)
{
    WCHAR t[PATH_CAP];
    RECT r = di->rcItem;
    HDC dc = di->hDC;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    COLORREF fg = sel ? C_ON_ACCENT : C_TEXT, dim = sel ? C_ON_ACCENT : C_DIM;
    UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    int n = -1, k = 0;
    HGDIOBJ of;

    FillC(dc, &r, sel ? C_ACCENT : C_FIELD);
    if (di->itemID != (UINT)-1) n = (int)SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    if (n >= 0 && n < PATH_CAP) {
        SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)t);
        of = SelectObject(dc, g_fontUI);
        while (k < n && !(t[k] == ' ' && t[k + 1] == ' ')) k++;
        r.left += S(8);
        TextC(dc, t, k, &r, fmt, fg);
        if (k < n) {
            r.left = di->rcItem.left + S(140);
            TextC(dc, t + k + 2, -1, &r, fmt, dim);
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
        DrawItem((const DRAWITEMSTRUCT *)l);
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
