#include "mp.h"

enum { ID_LIST = 1301, ID_MONO, ID_SIZE, ID_BOLD, ID_ITALIC };
#define ID_PRESET  1310
#define ID_PREVIEW 1360
#define ID_RESET   1370

typedef struct {
    DlgBase base;
    HWND    list, edit, preview;
    HFONT   font;
    WCHAR   face[32];
    int     pt, bold, italic, mono;
    int     busy;
} FontSt;

typedef struct { WCHAR name[32]; int mono; } FontEnt;

static FontEnt *g_fonts;
static int      g_nfonts, g_capfonts;
static int      g_monoOnly = 1;

static const int g_sizes[24] = { 80, 85, 90, 95, 100, 105,  110, 115, 120, 125, 130, 135,  140, 160, 180, 200, 220, 240,  260, 280, 320, 360, 400, 480 };
static const WCHAR g_sample[] = L"sphinx of black quartz, judge my vow. 0123456789";


static int FaceFind(const WCHAR *name, int *index) {
    int lo = 0, hi = g_nfonts, mid, cmp;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        cmp = wcmpi(name, g_fonts[mid].name);
        if (!cmp) { *index = mid; return mid; }
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    *index = lo;
    return -1;
}

static void FaceAdd(const WCHAR *name) {
    int index;
    if (!name[0] || name[0] == '@' || FaceFind(name, &index) >= 0) return;
    if (g_nfonts == g_capfonts) {
        int newCapacity = g_capfonts ? g_capfonts * 2 : 256;
        FontEnt *newFonts = (FontEnt *)mem_realloc(g_fonts, (size_t)newCapacity * sizeof(FontEnt));
        if (!newFonts) return;
        g_fonts = newFonts;
        g_capfonts = newCapacity;
    }
    memmove(g_fonts + index + 1, g_fonts + index, (size_t)(g_nfonts - index) * sizeof(FontEnt));
    wcopy(g_fonts[index].name, name, 32);
    g_fonts[index].mono = 0;
    g_nfonts++;
}

static int CALLBACK EnumCb(const void *logFont, const void *metrics, DWORD type, LPARAM lParam) {
    (void)metrics; (void)lParam;
    if (!(type & RASTER_FONTTYPE)) FaceAdd(((const ENUMLOGFONTEXW *)logFont)->elfLogFont.lfFaceName);
    return 1;
}

static void FaceEnum(void) {
    LOGFONTW logFont;
    HDC hdc = GetDC(NULL);
    if (!hdc) return;
    memset(&logFont, 0, sizeof logFont);
    logFont.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &logFont, EnumCb, 0, 0);
    ReleaseDC(NULL, hdc);
}

static BOOL IsMono(HDC hdc, int index) {
    FontEnt *entry = &g_fonts[index];
    if (!entry->mono) {
        HFONT font = CreateFontW(-32, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, entry->name);
        entry->mono = 2;
        if (font) {
            SIZE narrow, wide;
            HGDIOBJ oldFont = SelectObject(hdc, font);
            LONG widthDiff;
            narrow.cx = 0; wide.cx = 0;
            GetTextExtentPoint32W(hdc, L"iiiiiiii", 8, &narrow);
            GetTextExtentPoint32W(hdc, L"WWWWWWWW", 8, &wide);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
            widthDiff = narrow.cx - wide.cx;
            if (widthDiff < 0) widthDiff = -widthDiff;
            if (narrow.cx > 0 && widthDiff * 32 <= wide.cx) entry->mono = 1;
        }
    }
    return entry->mono == 1;
}

static int Checked(HWND hwnd, int id) { return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

static void SetCheck(HWND hwnd, int id, int on) {
    SendMessageW(GetDlgItem(hwnd, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static BOOL ItemText(HWND list, int index, WCHAR *out) {
    LRESULT length;
    if (index < 0) return FALSE;
    length = SendMessageW(list, LB_GETTEXTLEN, (WPARAM)index, 0);
    if (length < 0 || length >= 32) return FALSE;
    SendMessageW(list, LB_GETTEXT, (WPARAM)index, (LPARAM)out);
    return TRUE;
}

static void FillList(FontSt *state) {
    HCURSOR oldCursor = NULL;
    HDC hdc = NULL;
    int i;
    if (state->mono) {
        oldCursor = SetCursor(LoadCursorW(NULL, IDC_WAIT));
        hdc = CreateCompatibleDC(NULL);
    }
    SendMessageW(state->list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(state->list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_nfonts; i++) if (!state->mono || !hdc || IsMono(hdc, i)) SendMessageW(state->list, LB_ADDSTRING, 0, (LPARAM)g_fonts[i].name);
    if (hdc) DeleteDC(hdc);
    if (state->mono) SetCursor(oldCursor ? oldCursor : LoadCursorW(NULL, IDC_ARROW));
    SendMessageW(state->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(state->list, NULL, TRUE);
}

static void SelectFace(FontSt *state, int force) {
    int index = (int)SendMessageW(state->list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)state->face);
    if (index == LB_ERR && state->mono && force) {
        state->mono = 0;
        SetCheck(state->base.hwnd, ID_MONO, 0);
        FillList(state);
        index = (int)SendMessageW(state->list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)state->face);
    }
    SendMessageW(state->list, LB_SETCURSEL, (WPARAM)index, 0);
    if (index < 0) return;
    ItemText(state->list, index, state->face);
    SendMessageW(state->list, LB_SETTOPINDEX, (WPARAM)(index > 3 ? index - 3 : 0), 0);
}

static int SizeText(HWND edit, int fallback) {
    WCHAR text[16];
    const WCHAR *cursor = text;
    int whole = 0, frac = 0;
    text[0] = 0;
    GetWindowTextW(edit, text, 16);
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor < '0' || *cursor > '9') return fallback;
    while (*cursor >= '0' && *cursor <= '9') {
        whole = whole * 10 + (*cursor++ - '0');
        if (whole > FONT_MAX / 10) return FONT_MAX;
    }
    if ((*cursor == '.' || *cursor == ',') && cursor[1] >= '0' && cursor[1] <= '9') frac = cursor[1] - '0';
    return ClampInt(whole * 10 + frac, FONT_MIN, FONT_MAX);
}

static void FormatPt(WCHAR *text, int tenths) {
    if (tenths % 10) wsprintfW(text, L"%d.%d", tenths / 10, tenths % 10);
    else wsprintfW(text, L"%d", tenths / 10);
}

static void SetSizeText(FontSt *state) {
    WCHAR text[16], current[16];
    FormatPt(text, state->pt);
    current[0] = 0;
    GetWindowTextW(state->edit, current, 16);
    if (wcmp(current, text)) {
        state->busy = 1;
        SetWindowTextW(state->edit, text);
        state->busy = 0;
    }
}

static void Sync(FontSt *state) {
    ItemText(state->list, (int)SendMessageW(state->list, LB_GETCURSEL, 0, 0), state->face);
    state->pt = SizeText(state->edit, state->pt);
    state->bold = Checked(state->base.hwnd, ID_BOLD);
    state->italic = Checked(state->base.hwnd, ID_ITALIC);
}

static HFONT MakeFont(const FontSt *state, int pixelHeight) {
    return CreateFontW(-(pixelHeight < 1 ? 1 : pixelHeight), 0, 0, 0, state->bold ? FW_BOLD : FW_NORMAL, (DWORD)(state->italic ? 1 : 0), 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, state->face);
}

#define PV_L   S(10)
#define PV_R   S(6)
static void PreviewFont(FontSt *state) {
    HFONT newFont;
    if (!state->preview) return;
    newFont = MakeFont(state, MulDiv(state->pt, g_dpi, 720));
    if (!newFont) return;
    if (state->font) DeleteObject(state->font);
    state->font = newFont;
    InvalidateRect(state->preview, NULL, FALSE);
}

static void PreviewDraw(HDC hdc, const RECT *client, const FontSt *state) {
    RECT line = *client;
    HGDIOBJ oldFont;
    FillC(hdc, client, C_EDIT_BG);
    if (!state || !state->font) return;
    oldFont = SelectObject(hdc, state->font);
    line.left += PV_L; line.right -= PV_R;
    TextC(hdc, g_sample, -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, C_EDIT_FG);
    if (oldFont) SelectObject(hdc, oldFont);
}

static void PreviewPaint(HWND hwnd, const FontSt *state) {
    PAINTSTRUCT ps;
    RECT client;
    HDC hdc, memDc;
    HBITMAP bitmap = NULL;
    HGDIOBJ oldBitmap = NULL;
    memset(&ps, 0, sizeof ps);
    hdc = BeginPaint(hwnd, &ps);
    if (!hdc) { EndPaint(hwnd, &ps); return; }
    if (!GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0) { EndPaint(hwnd, &ps); return; }
    memDc = CreateCompatibleDC(hdc);
    if (memDc) bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    if (bitmap) oldBitmap = SelectObject(memDc, bitmap);
    if (oldBitmap && oldBitmap != (HGDIOBJ)(LONG_PTR)-1) {
        PreviewDraw(memDc, &client, state);
        if (!BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY)) PreviewDraw(hdc, &client, state);
        SelectObject(memDc, oldBitmap);
    } else {
        PreviewDraw(hdc, &client, state);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memDc) DeleteDC(memDc);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lParam)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_GETFONT) {
        const FontSt *state = (const FontSt *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        return state ? (LRESULT)state->font : 0;
    }
    if (msg == WM_PRINTCLIENT) {
        RECT client;
        GetClientRect(hwnd, &client);
        PreviewDraw((HDC)wParam, &client, (const FontSt *)GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return 0;
    }
    if (msg == WM_PAINT) { PreviewPaint(hwnd, (const FontSt *)GetWindowLongPtrW(hwnd, GWLP_USERDATA)); return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowState(FontSt *state) {
    SetCheck(state->base.hwnd, ID_BOLD, state->bold);
    SetCheck(state->base.hwnd, ID_ITALIC, state->italic);
    SetSizeText(state);
    SelectFace(state, 1);
    PreviewFont(state);
}

static void DrawItem(const DRAWITEMSTRUCT *item) {
    WCHAR text[32];
    RECT rect = item->rcItem;
    BOOL hasText = ItemText(item->hwndItem, (int)item->itemID, text), selected = hasText && (item->itemState & ODS_SELECTED);
    HGDIOBJ oldFont;
    FillC(item->hDC, &rect, selected ? C_ACCENT : C_FIELD);
    if (hasText) {
        oldFont = SelectObject(item->hDC, g_fontUI);
        rect.left += S(6); rect.right -= S(2);
        TextC(item->hDC, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, selected ? C_ON_ACCENT : C_TEXT);
        SelectObject(item->hDC, oldFont);
    }
    if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
}

static void Opt(HWND parent, const WCHAR *text, int x, int y, int width, int id, DWORD extra, int on) {
    HWND checkbox = UiButton(parent, text, x, y, width, 20, id, BS_AUTOCHECKBOX | extra);
    SendMessageW(checkbox, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void FontLayout(FontSt *state) {
    DlgBase *base = &state->base;
    HWND hwnd = base->hwnd, button;
    WCHAR text[8];
    int i;
    UiLabel(hwnd, L"&font:", 12, 10, 236, 16, 0, 0);
    UiLabel(hwnd, L"7 to 200 pt", 328, 32, 180, 16, IDC_DIM, SS_NOPREFIX);
    UiLabel(hwnd, L"preview:", 12, 228, 200, 16, 0, SS_NOPREFIX);
    DlgFrame(base, 12, 246, 504, 146, BV_SUNKEN);
    state->preview = CreateWindowExW(0, L"mp_fontpv", g_sample, WS_CHILD | WS_VISIBLE, S(12) + 2, S(246) + 2, S(504) - 4, S(146) - 4, hwnd, (HMENU)(ULONG_PTR)ID_PREVIEW, g_hinst, state);
    DlgFrame(base, 12, 28, 236, 166, BV_SUNKEN);
    state->list = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, S(12) + 2, S(28) + 2, S(236) - 4, S(166) - 4, hwnd, (HMENU)(ULONG_PTR)ID_LIST, g_hinst, NULL);
    SendMessageW(state->list, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    DarkScroll(state->list);
    Opt(hwnd, L"&monospaced fonts only", 12, 200, 236, ID_MONO, WS_GROUP, state->mono);
    UiLabel(hwnd, L"&size:", 264, 10, 120, 16, 0, 0);
    state->edit = UiEdit(base, L"", 264, 28, 56, 23, ID_SIZE, WS_GROUP);
    SendMessageW(state->edit, EM_LIMITTEXT, 5, 0);
    for (i = 0; i < COUNTOF(g_sizes); i++) {
        FormatPt(text, g_sizes[i]);
        button = UiButton(hwnd, text, 264 + (i % 6) * 42, 60 + (i / 6) * 26, 40, 22, ID_PRESET + i, BS_PUSHBUTTON | (i ? 0 : WS_GROUP));
        if (i) SetWindowLongPtrW(button, GWL_STYLE, GetWindowLongPtrW(button, GWL_STYLE) & ~WS_TABSTOP);
    }
    Opt(hwnd, L"&bold", 264, 170, 80, ID_BOLD, WS_GROUP, state->bold);
    Opt(hwnd, L"&italic", 352, 170, 80, ID_ITALIC, 0, state->italic);
    UiButton(hwnd, L"&reset", 12, 404, 88, 24, ID_RESET, BS_PUSHBUTTON | WS_GROUP);
    UiButton(hwnd, L"ok", 336, 404, 86, 24, IDOK, BS_DEFPUSHBUTTON | WS_GROUP);
    UiButton(hwnd, L"cancel", 430, 404, 86, 24, IDCANCEL, BS_PUSHBUTTON);
    FillList(state);
    ShowState(state);
    base->focus = state->list;
}

static void FontCmd(FontSt *state, int id, int code) {
    HWND hwnd = state->base.hwnd;
    if (state->base.done) return;
    switch (id) {
    case IDOK:
        Sync(state);
        state->base.result = IDOK;
        state->base.done = 1;
        return;
    case IDCANCEL:
        state->base.result = IDCANCEL;
        state->base.done = 1;
        return;
    case ID_LIST:
        if (code != LBN_SELCHANGE || !ItemText(state->list, (int)SendMessageW(state->list, LB_GETCURSEL, 0, 0), state->face)) return;
        break;
    case ID_MONO:
        state->mono = g_monoOnly = Checked(hwnd, ID_MONO);
        FillList(state);
        SelectFace(state, 0);
        return;
    case ID_SIZE:
        if (state->busy || (code != EN_CHANGE && code != EN_KILLFOCUS)) return;
        state->pt = SizeText(state->edit, state->pt);
        if (code == EN_KILLFOCUS) SetSizeText(state);
        break;
    case ID_BOLD:
    case ID_ITALIC:
        state->bold = Checked(hwnd, ID_BOLD);
        state->italic = Checked(hwnd, ID_ITALIC);
        break;
    case ID_RESET:
        wcopy(state->face, DEFAULT_FACE, 32);
        FontResolve(state->face);
        state->pt = 110;
        state->bold = 0;
        state->italic = 0;
        ShowState(state);
        return;
    default:
        if (id < ID_PRESET || id >= ID_PRESET + COUNTOF(g_sizes)) return;
        state->pt = g_sizes[id - ID_PRESET];
        SetSizeText(state);
        break;
    }
    PreviewFont(state);
}

static LRESULT CALLBACK FontProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgBase *base = DlgFromHwnd(hwnd, msg, lParam);
    FontSt *state = (FontSt *)base;
    LRESULT result;
    if (!base) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE:
        FontLayout(state);
        return 0;
    case WM_COMMAND:
        FontCmd(state, LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_MEASUREITEM:
        if (((MEASUREITEMSTRUCT *)lParam)->CtlID == ID_LIST) { ((MEASUREITEMSTRUCT *)lParam)->itemHeight = (UINT)S(18); return TRUE; }
        break;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lParam)->CtlID == ID_LIST) { DrawItem((DRAWITEMSTRUCT *)lParam); return TRUE; }
        break;
    case WM_NCDESTROY:
        if (state->font) { DeleteObject(state->font); state->font = NULL; }
        state->preview = NULL;
        break;
    }
    if (DlgCommon(base, msg, wParam, lParam, &result)) return result;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL FontDlg(HWND owner) {
    static BOOL reg;
    FontSt state;
    if (!reg) {
        RegClass(L"mp_font", FontProc, 0, NULL);
        RegClass(L"mp_fontpv", PreviewProc, 0, NULL);
        reg = TRUE;
    }
    memset(&state, 0, sizeof state);
    DlgBaseInit(&state.base, owner);
    wcopy(state.face, g_pf.font, 32);
    state.pt = ClampInt(g_pf.pt, FONT_MIN, FONT_MAX);
    state.bold = g_pf.bold != 0;
    state.italic = g_pf.italic != 0;
    state.mono = g_monoOnly;
    FaceEnum();
    FaceAdd(state.face);
    if (!DlgOpen(&state.base, L"mp_font", L"font", 528, 440, 0)) return FALSE;
    DlgRunModal(&state.base);
    if (state.base.result != IDOK) return FALSE;
    wcopy(g_pf.font, state.face, 32);
    g_pf.pt = ClampInt(state.pt, FONT_MIN, FONT_MAX);
    g_pf.bold = state.bold;
    g_pf.italic = state.italic;
    return TRUE;
}
