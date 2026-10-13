#include "mp.h"
#include "ui_internal.h"

enum { ID_ENCLIST = 1201 };

typedef char SzOfn[sizeof(OPENFILENAMEW) == 88 ? 1 : -1];

typedef BOOL  (WINAPI *OfnFn)(OPENFILENAMEW *);
typedef DWORD (WINAPI *CdErrFn)(void);
static HMODULE g_cd;
static OfnFn   pOpen, pSave;
static CdErrFn pCdErr;
static WCHAR   g_last[PATH_CAP];

typedef struct { DlgBase base; HWND list; int enc, reopen; } EncSt;

static BOOL CdLoad(void) {
    if (!g_cd && (g_cd = LoadLibraryW(L"comdlg32.dll")) != NULL) {
        pOpen  = (OfnFn)GetProcAddress(g_cd, "GetOpenFileNameW");
        pSave  = (OfnFn)GetProcAddress(g_cd, "GetSaveFileNameW");
        pCdErr = (CdErrFn)GetProcAddress(g_cd, "CommDlgExtendedError");
    }
    return pOpen && pSave && pCdErr;
}

static BOOL PickFile(HWND owner, WCHAR *path, int capacity, BOOL save) {
    OPENFILENAMEW ofn;
    WCHAR buffer[PATH_CAP], dir[PATH_CAP];
    HWND held;
    DWORD err = 0;
    BOOL ok;
    if (!path || capacity <= 0) return FALSE;
    if (!CdLoad()) {
        MpNote(owner, APP_NAME, L"cannot show the file dialog.");
        return FALSE;
    }
    PathDir(path, dir, PATH_CAP);
    if (!IsDir(dir)) wcopy(dir, g_last, PATH_CAP);
    if (!IsDir(dir)) dir[0] = 0;
    wcopy(buffer, save ? PathName(path) : L"", PATH_CAP);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"text documents (*.txt)\0*.txt\0all files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = PATH_CAP;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrTitle = save ? L"save as" : L"open";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    held = DialogHoldFind();
    ok = save ? pSave(&ofn) : pOpen(&ofn);
    if (!ok) err = pCdErr();
    DialogReleaseFind(held);
    if (!ok) {
        if (err) MpNote(owner, APP_NAME, err == FNERR_BUFFERTOOSMALL ? L"the file name is too long." : L"cannot show the file dialog.");
        return FALSE;
    }
    if (wlen(buffer) >= capacity) {
        MpNote(owner, APP_NAME, L"the file name is too long.");
        return FALSE;
    }
    wcopy(path, buffer, capacity);
    PathDir(buffer, g_last, PATH_CAP);
    return TRUE;
}

BOOL FileDlgOpen(HWND owner, WCHAR *path, int capacity) {
    return PickFile(owner, path, capacity, FALSE);
}

BOOL FileDlgSave(HWND owner, WCHAR *path, int capacity) {
    return PickFile(owner, path, capacity, TRUE);
}

static int AddItem(HWND list, const WCHAR *text, int data) {
    int index = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)text);
    if (index >= 0) SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, (LPARAM)data);
    return index;
}

static void DrawItem(const DRAWITEMSTRUCT *item) {
    WCHAR text[PATH_CAP];
    RECT rect = item->rcItem;
    HDC hdc = item->hDC;
    BOOL selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF textColor = selected ? C_ON_ACCENT : C_TEXT, dimColor = selected ? C_ON_ACCENT : C_DIM;
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    int length = -1, split = 0;
    HGDIOBJ oldFont;
    FillC(hdc, &rect, selected ? C_ACCENT : C_FIELD);
    if (item->itemID != (UINT)-1) length = (int)SendMessageW(item->hwndItem, LB_GETTEXTLEN, item->itemID, 0);
    if (length >= 0 && length < PATH_CAP) {
        SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)text);
        oldFont = SelectObject(hdc, g_fontUI);
        while (split < length && !(text[split] == ' ' && text[split + 1] == ' ')) split++;
        rect.left += S(8);
        TextC(hdc, text, split, &rect, format, textColor);
        if (split < length) {
            rect.left = item->rcItem.left + S(140);
            TextC(hdc, text + split + 2, -1, &rect, format, dimColor);
        }
        SelectObject(hdc, oldFont);
    }
    if (item->itemState & ODS_FOCUS) DrawFocusRect(hdc, &item->rcItem);
}

static HWND ListBox(DlgBase *base, int x, int y, int width, int height, int id, DWORD extra) {
    HWND list;
    DlgFrame(base, x, y, width, height, BV_SUNKEN);
    list = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | extra, S(x) + 2, S(y) + 2, S(width) - 4, S(height) - 4, base->hwnd, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(list, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    DarkScroll(list);
    return list;
}

static BOOL FocusClick(HWND dlg) {
    WCHAR className[16];
    HWND focus = GetFocus();
    if (!focus || GetParent(focus) != dlg || GetWindowLongPtrW(focus, GWLP_ID) == IDOK) return FALSE;
    GetClassNameW(focus, className, 16);
    if (wcmp(className, L"mp_btn") != 0) return FALSE;
    SendMessageW(focus, BM_CLICK, 0, 0);
    return TRUE;
}

static LRESULT CALLBACK EncProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    EncSt *state = (EncSt *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        WCHAR label[96];
        int i, index, encoding, selIndex = -1, count = EncListCount();
        UiLabel(hwnd, state->reopen ? L"pick the encoding to read the file with:" : L"pick the encoding to save the file with:", 16, 14, 368, 16, 0, 0);
        state->list = ListBox(base, 16, 36, 368, 272, ID_ENCLIST, 0);
        for (i = 0; i < count; i++) {
            encoding = EncListGet(i, label, COUNTOF(label));
            index = AddItem(state->list, label, encoding);
            if (encoding == state->enc && selIndex < 0) selIndex = index;
        }
        if (selIndex < 0 && state->enc >= ENC_CP_MIN) {
            EncLabel(state->enc, label, COUNTOF(label));
            selIndex = AddItem(state->list, label, state->enc);
        }
        if (selIndex >= 0) {
            SendMessageW(state->list, LB_SETTOPINDEX, (WPARAM)(selIndex > 5 ? selIndex - 5 : 0), 0);
            SendMessageW(state->list, LB_SETCURSEL, (WPARAM)selIndex, 0);
        }
        UiButton(hwnd, L"ok", 200, 320, 88, 24, IDOK, BS_DEFPUSHBUTTON);
        UiButton(hwnd, L"cancel", 296, 320, 88, 24, IDCANCEL, BS_PUSHBUTTON);
        base->focus = state->list;
        return 0; }
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)lParam)->itemHeight = (UINT)S(20);
        return TRUE;
    case WM_DRAWITEM:
        DrawItem((const DRAWITEMSTRUCT *)lParam);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK && FocusClick(hwnd)) return 0;
        if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == ID_ENCLIST && HIWORD(wParam) == LBN_DBLCLK)) {
            int index = (int)SendMessageW(state->list, LB_GETCURSEL, 0, 0);
            if (index >= 0) {
                state->enc = (int)SendMessageW(state->list, LB_GETITEMDATA, (WPARAM)index, 0);
                base->result = 1; base->done = 1;
            }
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { base->result = 0; base->done = 1; return 0; }
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL EncDlg(HWND owner, int *enc, int reopen) {
    static BOOL reg;
    EncSt state;
    if (!enc) return FALSE;
    if (!reg) { RegClass(L"mp_enc", EncProc, 0, NULL); reg = TRUE; }
    memset(&state, 0, sizeof state);
    DlgBaseInit(&state.base, owner);
    state.enc = *enc;
    state.reopen = reopen;
    if (!DlgOpen(&state.base, L"mp_enc", reopen ? L"reopen with encoding" : L"other code page", 400, 360, 0)) return FALSE;
    DlgRunModal(&state.base);
    if (state.base.result != 1) return FALSE;
    *enc = state.enc;
    return TRUE;
}
