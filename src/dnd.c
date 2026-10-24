#include "mp.h"

#define DROP_NONE    0
#define DROP_TEXT    1
#define DROP_FILES   2
#define SCROLL_DELAY 300
#define SCROLL_EVERY 50

typedef struct DataObject DataObject;
typedef struct DropTarget DropTarget;
typedef struct DropSource DropSource;
typedef struct DropHelper DropHelper;

typedef struct {
    HRESULT (WINAPI *QueryInterface)(DataObject *self, const GUID *iid, void **out);
    ULONG   (WINAPI *AddRef)(DataObject *self);
    ULONG   (WINAPI *Release)(DataObject *self);
    HRESULT (WINAPI *GetData)(DataObject *self, FORMATETC *format, STGMEDIUM *medium);
    HRESULT (WINAPI *GetDataHere)(DataObject *self, FORMATETC *format, STGMEDIUM *medium);
    HRESULT (WINAPI *QueryGetData)(DataObject *self, FORMATETC *format);
    HRESULT (WINAPI *GetCanonicalFormatEtc)(DataObject *self, FORMATETC *in, FORMATETC *out);
    HRESULT (WINAPI *SetData)(DataObject *self, FORMATETC *format, STGMEDIUM *medium, BOOL release);
    HRESULT (WINAPI *EnumFormatEtc)(DataObject *self, DWORD direction, void **out);
    HRESULT (WINAPI *DAdvise)(DataObject *self, FORMATETC *format, DWORD flags, void *sink, DWORD *connection);
    HRESULT (WINAPI *DUnadvise)(DataObject *self, DWORD connection);
    HRESULT (WINAPI *EnumDAdvise)(DataObject *self, void **out);
} DataObjectVtbl;
struct DataObject { const DataObjectVtbl *vtbl; };

typedef struct {
    HRESULT (WINAPI *QueryInterface)(DropTarget *self, const GUID *iid, void **out);
    ULONG   (WINAPI *AddRef)(DropTarget *self);
    ULONG   (WINAPI *Release)(DropTarget *self);
    HRESULT (WINAPI *DragEnter)(DropTarget *self, DataObject *data, DWORD keys, POINTL pt, DWORD *effect);
    HRESULT (WINAPI *DragOver)(DropTarget *self, DWORD keys, POINTL pt, DWORD *effect);
    HRESULT (WINAPI *DragLeave)(DropTarget *self);
    HRESULT (WINAPI *Drop)(DropTarget *self, DataObject *data, DWORD keys, POINTL pt, DWORD *effect);
} DropTargetVtbl;
struct DropTarget { const DropTargetVtbl *vtbl; };

typedef struct {
    HRESULT (WINAPI *QueryInterface)(DropSource *self, const GUID *iid, void **out);
    ULONG   (WINAPI *AddRef)(DropSource *self);
    ULONG   (WINAPI *Release)(DropSource *self);
    HRESULT (WINAPI *QueryContinueDrag)(DropSource *self, BOOL escape, DWORD keys);
    HRESULT (WINAPI *GiveFeedback)(DropSource *self, DWORD effect);
} DropSourceVtbl;
struct DropSource { const DropSourceVtbl *vtbl; };

typedef struct {
    HRESULT (WINAPI *QueryInterface)(DropHelper *self, const GUID *iid, void **out);
    ULONG   (WINAPI *AddRef)(DropHelper *self);
    ULONG   (WINAPI *Release)(DropHelper *self);
    HRESULT (WINAPI *DragEnter)(DropHelper *self, HWND target, DataObject *data, POINT *pt, DWORD effect);
    HRESULT (WINAPI *DragLeave)(DropHelper *self);
    HRESULT (WINAPI *DragOver)(DropHelper *self, POINT *pt, DWORD effect);
    HRESULT (WINAPI *Drop)(DropHelper *self, DataObject *data, POINT *pt, DWORD effect);
    HRESULT (WINAPI *Show)(DropHelper *self, BOOL show);
} DropHelperVtbl;
struct DropHelper { const DropHelperVtbl *vtbl; };

typedef struct { const DataObjectVtbl *vtbl; WCHAR *text; int length; WCHAR *paths; } TextData;

static const GUID g_iidUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID g_iidDataObject = { 0x0000010E, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID g_iidDropSource = { 0x00000121, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID g_iidDropTarget = { 0x00000122, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID g_clsidDragDropHelper = { 0x4657278A, 0x411B, 0x11D2, { 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };
static const GUID g_iidDropTargetHelper = { 0x4657278B, 0x411B, 0x11D2, { 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };
static const FORMATETC g_textFormats[2] = { { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }, { CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL } };

static struct {
    HRESULT (WINAPI *initialize)(void *reserved);
    void    (WINAPI *uninitialize)(void);
    HRESULT (WINAPI *registerDragDrop)(HWND hwnd, DropTarget *target);
    HRESULT (WINAPI *revokeDragDrop)(HWND hwnd);
    HRESULT (WINAPI *doDragDrop)(DataObject *data, DropSource *source, DWORD okEffects, DWORD *effect);
    void    (WINAPI *releaseStgMedium)(STGMEDIUM *medium);
    HRESULT (WINAPI *createInstance)(const GUID *clsid, void *outer, DWORD context, const GUID *iid, void **out);
} g_ole;

static struct {
    HMODULE module;
    UINT    (WINAPI *dragQueryFile)(HANDLE drop, UINT index, LPWSTR path, UINT capacity);
    void    (WINAPI *dragFinish)(HANDLE drop);
    HRESULT (WINAPI *createFormatEnum)(UINT count, const FORMATETC *formats, void **out);
} g_shell;

static struct {
    DataObject *data;
    int kind, hasFile, internal, index;
    DWORD scrollSince, scrollLast;
} g_drop;

static HWND g_registered, g_dropCaret;
static DropHelper *g_helper;
static int g_oleReady, g_helperTried, g_caretHidden, g_dragging, g_dragDropped, g_dragFrom, g_dragTo;

static BOOL ShellReady(void) {
    if (!g_shell.module && (g_shell.module = LoadLibraryW(L"shell32.dll")) != NULL) {
        *(void **)&g_shell.dragQueryFile = GetProcAddress(g_shell.module, "DragQueryFileW");
        *(void **)&g_shell.dragFinish = GetProcAddress(g_shell.module, "DragFinish");
        *(void **)&g_shell.createFormatEnum = GetProcAddress(g_shell.module, "SHCreateStdEnumFmtEtc");
    }
    return g_shell.dragQueryFile && g_shell.dragFinish;
}

static HRESULT Query(void *self, const GUID *iid, const GUID *own, void **out) {
    if (!memcmp(iid, &g_iidUnknown, sizeof(GUID)) || !memcmp(iid, own, sizeof(GUID))) { *out = self; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}

static HGLOBAL GlobalText(const WCHAR *text, int length, BOOL ansi) {
    int bytes = ansi ? WideCharToMultiByte(CP_ACP, 0, text, length, NULL, 0, NULL, NULL) : length * (int)sizeof(WCHAR);
    HGLOBAL global = GlobalAlloc(GHND, (SIZE_T)bytes + sizeof(WCHAR));
    BYTE *dst = global ? (BYTE *)GlobalLock(global) : NULL;
    if (!dst) { if (global) GlobalFree(global); return NULL; }
    if (ansi) WideCharToMultiByte(CP_ACP, 0, text, length, (LPSTR)dst, bytes, NULL, NULL);
    else memcpy(dst, text, (size_t)bytes);
    GlobalUnlock(global);
    return global;
}

static int PathsLength(const WCHAR *paths) {
    const WCHAR *path = paths;
    while (*path) path += wlen(path) + 1;
    return (int)(path - paths) + 1;
}

#ifdef SHOTDC
static HGLOBAL GlobalDrop(const WCHAR *paths) {
    int length = PathsLength(paths) + 1;
    HGLOBAL global = GlobalAlloc(GHND, 20 + (SIZE_T)length * sizeof(WCHAR));
    BYTE *dst = global ? (BYTE *)GlobalLock(global) : NULL;
    if (!dst) { if (global) GlobalFree(global); return NULL; }
    ((DWORD *)dst)[0] = 20;
    ((DWORD *)dst)[4] = 1;
    memcpy(dst + 20, paths, (size_t)(length - 1) * sizeof(WCHAR));
    GlobalUnlock(global);
    return global;
}
#endif

static int DataFormat(const TextData *data, const FORMATETC *format) {
    if (!format || format->dwAspect != DVASPECT_CONTENT || !(format->tymed & TYMED_HGLOBAL)) return 0;
    if ((format->cfFormat == CF_UNICODETEXT || format->cfFormat == CF_TEXT) && data->text) return format->cfFormat;
#ifdef SHOTDC
    if (format->cfFormat == CF_HDROP && data->paths) return CF_HDROP;
#endif
    return 0;
}

static HRESULT WINAPI DataQueryInterface(DataObject *self, const GUID *iid, void **out) { return Query(self, iid, &g_iidDataObject, out); }
static ULONG WINAPI DataAddRef(DataObject *self) { (void)self; return 2; }
static ULONG WINAPI DataRelease(DataObject *self) { (void)self; return 1; }

static HRESULT WINAPI DataGetData(DataObject *self, FORMATETC *format, STGMEDIUM *medium) {
    const TextData *data = (const TextData *)self;
    int kind = DataFormat(data, format);
    HGLOBAL global = NULL;
    if (!kind) return DV_E_FORMATETC;
    if (kind == CF_UNICODETEXT || kind == CF_TEXT) global = GlobalText(data->text, data->length, kind == CF_TEXT);
#ifdef SHOTDC
    if (kind == CF_HDROP) global = GlobalDrop(data->paths);
#endif
    if (!global) return E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    medium->pUnkForRelease = NULL;
    return S_OK;
}

static HRESULT WINAPI DataGetDataHere(DataObject *self, FORMATETC *format, STGMEDIUM *medium) { (void)self; (void)format; (void)medium; return E_NOTIMPL; }
static HRESULT WINAPI DataQueryGetData(DataObject *self, FORMATETC *format) { return DataFormat((const TextData *)self, format) ? S_OK : DV_E_FORMATETC; }
static HRESULT WINAPI DataGetCanonicalFormatEtc(DataObject *self, FORMATETC *in, FORMATETC *out) { (void)self; (void)in; if (out) out->ptd = NULL; return E_NOTIMPL; }
static HRESULT WINAPI DataSetData(DataObject *self, FORMATETC *format, STGMEDIUM *medium, BOOL release) { (void)self; (void)format; (void)medium; (void)release; return E_NOTIMPL; }

static HRESULT WINAPI DataEnumFormatEtc(DataObject *self, DWORD direction, void **out) {
    *out = NULL;
    if (direction != DATADIR_GET || !((const TextData *)self)->text || !ShellReady() || !g_shell.createFormatEnum) return E_NOTIMPL;
    return g_shell.createFormatEnum(2, g_textFormats, out);
}

static HRESULT WINAPI DataDAdvise(DataObject *self, FORMATETC *format, DWORD flags, void *sink, DWORD *connection) { (void)self; (void)format; (void)flags; (void)sink; (void)connection; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT WINAPI DataDUnadvise(DataObject *self, DWORD connection) { (void)self; (void)connection; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT WINAPI DataEnumDAdvise(DataObject *self, void **out) { (void)self; *out = NULL; return OLE_E_ADVISENOTSUPPORTED; }

static const DataObjectVtbl g_dataVtbl = {
    DataQueryInterface, DataAddRef, DataRelease, DataGetData, DataGetDataHere, DataQueryGetData,
    DataGetCanonicalFormatEtc, DataSetData, DataEnumFormatEtc, DataDAdvise, DataDUnadvise, DataEnumDAdvise
};
static TextData g_dragData = { &g_dataVtbl, NULL, 0, NULL };

static WCHAR *DropPaths(HANDLE drop) {
    UINT count, i;
    size_t total = 1;
    WCHAR *paths, *dst;
    if (!ShellReady()) return NULL;
    count = g_shell.dragQueryFile(drop, 0xFFFFFFFF, NULL, 0);
    for (i = 0; i < count; i++) total += (size_t)g_shell.dragQueryFile(drop, i, NULL, 0) + 1;
    paths = (WCHAR *)mem_alloc(total * sizeof(WCHAR));
    if (!paths) return NULL;
    for (i = 0, dst = paths; i < count; i++) {
        UINT length = g_shell.dragQueryFile(drop, i, dst, (UINT)(total - (size_t)(dst - paths)));
        if (length) dst += length + 1;
    }
    *dst = 0;
    return paths;
}

static WCHAR *PathsText(const WCHAR *paths) {
    const WCHAR *path;
    size_t total = 1;
    WCHAR *text, *dst;
    for (path = paths; *path; path += wlen(path) + 1) total += (size_t)wlen(path) + 2;
    text = (WCHAR *)mem_alloc(total * sizeof(WCHAR));
    if (!text) return NULL;
    for (path = paths, dst = text; *path; path += wlen(path) + 1) {
        int length = wlen(path);
        if (dst != text) { *dst++ = '\r'; *dst++ = '\n'; }
        memcpy(dst, path, (size_t)length * sizeof(WCHAR));
        dst += length;
    }
    *dst = 0;
    return text;
}

static WCHAR *DataPaths(DataObject *data) {
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium;
    WCHAR *paths = NULL;
    if (data->vtbl->GetData(data, &format, &medium) != S_OK) return NULL;
    if (medium.tymed == TYMED_HGLOBAL) paths = DropPaths(medium.hGlobal);
    g_ole.releaseStgMedium(&medium);
    return paths;
}

static WCHAR *DataText(DataObject *data) {
    FORMATETC format = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium;
    WCHAR *text = NULL;
    if (data->vtbl->GetData(data, &format, &medium) == S_OK) {
        const WCHAR *src = medium.tymed == TYMED_HGLOBAL ? (const WCHAR *)GlobalLock(medium.hGlobal) : NULL;
        if (src) { text = TextToCrlf(src, (int)(GlobalSize(medium.hGlobal) / sizeof(WCHAR)), NULL); GlobalUnlock(medium.hGlobal); }
        g_ole.releaseStgMedium(&medium);
        return text;
    }
    format.cfFormat = CF_TEXT;
    if (data->vtbl->GetData(data, &format, &medium) != S_OK) return NULL;
    if (medium.tymed == TYMED_HGLOBAL) {
        const char *src = (const char *)GlobalLock(medium.hGlobal);
        int bytes = 0, cap = (int)GlobalSize(medium.hGlobal), wide;
        if (src) {
            while (bytes < cap && src[bytes]) bytes++;
            wide = MultiByteToWideChar(CP_ACP, 0, src, bytes, NULL, 0);
            WCHAR *raw = (WCHAR *)mem_alloc(((size_t)wide + 1) * sizeof(WCHAR));
            if (raw) { MultiByteToWideChar(CP_ACP, 0, src, bytes, raw, wide); raw[wide] = 0; text = TextToCrlf(raw, wide, NULL); mem_free(raw); }
            GlobalUnlock(medium.hGlobal);
        }
    }
    g_ole.releaseStgMedium(&medium);
    return text;
}

static int DropKind(DataObject *data, int *hasFile) {
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    *hasFile = 0;
    if (data->vtbl->QueryGetData(data, &format) == S_OK) {
        WCHAR *paths = DataPaths(data);
        const WCHAR *path;
        for (path = paths; path && *path && !*hasFile; path += wlen(path) + 1) *hasFile = !IsDir(path);
        mem_free(paths);
        return DROP_FILES;
    }
    format.cfFormat = CF_UNICODETEXT;
    if (data->vtbl->QueryGetData(data, &format) == S_OK) return DROP_TEXT;
    format.cfFormat = CF_TEXT;
    return data->vtbl->QueryGetData(data, &format) == S_OK ? DROP_TEXT : DROP_NONE;
}

static LRESULT CALLBACK DropCaretProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ERASEBKGND) { RECT client; GetClientRect(hwnd, &client); FillC((HDC)wParam, &client, C_EDIT_FG); return 1; }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void DropCaret(int index) {
    RECT rect;
    if (index >= 0 && g_edit && EditCaretRect(index, &rect)) {
        if (!g_dropCaret) {
            RegClass(L"mp_dropcaret", DropCaretProc, 0, NULL);
            g_dropCaret = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"mp_dropcaret", NULL, WS_POPUP, 0, 0, 0, 0, g_hwnd, NULL, g_hinst, NULL);
            if (g_dropCaret) SetLayeredWindowAttributes(g_dropCaret, 0, 255, LWA_ALPHA);
        }
        if (!g_caretHidden && GetFocus() == g_edit) g_caretHidden = HideCaret(g_edit);
        MapWindowPoints(g_edit, NULL, (POINT *)&rect, 2);
        if (g_dropCaret) { SetWindowPos(g_dropCaret, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW); InvalidateRect(g_dropCaret, NULL, TRUE); }
        return;
    }
    if (g_dropCaret) ShowWindow(g_dropCaret, SW_HIDE);
    if (g_caretHidden) { ShowCaret(g_edit); g_caretHidden = 0; }
}

static void DropScroll(POINT pt) {
    RECT view;
    int line = EditLineHeight(), margin = S(16), dx = 0, dy = 0;
    DWORD now = GetTickCount();
    SendMessageW(g_edit, EM_GETRECT, 0, (LPARAM)&view);
    if (pt.y < view.top + line / 2) dy = -1;
    else if (pt.y >= view.bottom - line / 2) dy = 1;
    if (!g_pf.wrap && pt.x < view.left + margin) dx = -2;
    else if (!g_pf.wrap && pt.x >= view.right - margin) dx = 2;
    if (!dx && !dy) { g_drop.scrollSince = 0; return; }
    if (!g_drop.scrollSince) { g_drop.scrollSince = now; return; }
    if (now - g_drop.scrollSince < SCROLL_DELAY || now - g_drop.scrollLast < SCROLL_EVERY) return;
    g_drop.scrollLast = now;
    SendMessageW(g_edit, EM_LINESCROLL, (WPARAM)dx, (LPARAM)dy);
}

static DWORD TextEffect(DWORD keys, DWORD allowed, int index) {
    BOOL copy = (keys & MK_CONTROL) && (allowed & DROPEFFECT_COPY);
    if (g_drop.internal && index > g_dragFrom && index < g_dragTo) return DROPEFFECT_NONE;
    if (g_drop.internal && !copy && (index == g_dragFrom || index == g_dragTo)) return DROPEFFECT_NONE;
    if (copy) return DROPEFFECT_COPY;
    if (allowed & DROPEFFECT_MOVE) return DROPEFFECT_MOVE;
    return allowed & DROPEFFECT_COPY;
}

static DWORD DropTrack(DWORD keys, POINTL screen, DWORD allowed) {
    POINT pt;
    int index = -1, insert = g_drop.kind == DROP_TEXT || (g_drop.kind == DROP_FILES && (keys & MK_SHIFT));
    DWORD effect = DROPEFFECT_NONE;
    pt.x = screen.x;
    pt.y = screen.y;
    if (insert && g_edit && WindowFromPoint(pt) == g_edit) {
        ScreenToClient(g_edit, &pt);
        DropScroll(pt);
        index = EditIndexFromPoint(pt);
        effect = g_drop.kind == DROP_TEXT ? TextEffect(keys, allowed, index) : (allowed & DROPEFFECT_COPY);
    } else if (!insert && g_drop.kind == DROP_FILES && g_drop.hasFile) {
        effect = allowed & DROPEFFECT_COPY;
    }
    if (effect == DROPEFFECT_NONE) index = -1;
    g_drop.index = index;
    DropCaret(index);
    return effect;
}

static void DropReset(void) {
    DropCaret(-1);
    if (g_drop.data) g_drop.data->vtbl->Release(g_drop.data);
    memset(&g_drop, 0, sizeof g_drop);
    g_drop.index = -1;
}

static BOOL CanReplace(int from, int to, int length) {
    int textLen = GetWindowTextLengthW(g_edit), remaining;
    DWORD limit = (DWORD)SendMessageW(g_edit, EM_GETLIMITTEXT, 0, 0);
    if (from < 0 || to < from || to > textLen || length < 0 || (GetWindowLongPtrW(g_edit, GWL_STYLE) & ES_READONLY)) return FALSE;
    remaining = textLen - (to - from);
    return length <= 0x7FFFFFFF - remaining && (DWORD)remaining <= limit && (DWORD)length <= limit - (DWORD)remaining;
}

static BOOL Replaced(int textLen, int from, int removed, const WCHAR *insert, int length) {
    void *hText = NULL;
    int actual = 0;
    const WCHAR *text = EditLockText(&hText, &actual);
    BOOL ok = text && actual == textLen - removed + length && !memcmp(text + from, insert, (size_t)length * sizeof(WCHAR));
    EditUnlockText(hText);
    return ok;
}

static BOOL InsertAt(int index, const WCHAR *text, BOOL select) {
    int length = wlen(text), textLen = GetWindowTextLengthW(g_edit), added;
    DWORD selStart = 0, selEnd = 0;
    if (!length || !CanReplace(index, index, length)) return FALSE;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)index, (LPARAM)index);
    EditInsert(text);
    if (!Replaced(textLen, index, 0, text, length)) {
        added = GetWindowTextLengthW(g_edit) - textLen;
        if (added > 0 && !HistUndo()) {
            SendMessageW(g_edit, EM_SETSEL, (WPARAM)index, (LPARAM)(index + added));
            EditInsert(L"");
        }
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selEnd);
        return FALSE;
    }
    if (select) SendMessageW(g_edit, EM_SETSEL, (WPARAM)index, (LPARAM)(index + length));
    return TRUE;
}

static BOOL MoveSelection(int index, BOOL copy) {
    int from = g_dragFrom, to = g_dragTo, length = to - from, spanFrom, spanTo, spanLen, textLen = 0;
    void *hText = NULL;
    const WCHAR *text;
    WCHAR *span = NULL, *original;
    BOOL ok;
    if (copy) return InsertAt(index, g_dragData.text, TRUE);
    spanFrom = index < from ? index : from;
    spanTo = index < from ? to : index;
    spanLen = spanTo - spanFrom;
    if (spanLen > 0x3FFFFFFE || !CanReplace(spanFrom, spanTo, spanLen)) return FALSE;
    text = EditLockText(&hText, &textLen);
    if (text && spanTo <= textLen) span = (WCHAR *)mem_alloc(((size_t)spanLen + 1) * 2 * sizeof(WCHAR));
    if (span) {
        original = span + spanLen + 1;
        memcpy(original, text + spanFrom, (size_t)spanLen * sizeof(WCHAR));
        original[spanLen] = 0;
    }
    if (span && index < from) {
        memcpy(span, g_dragData.text, (size_t)length * sizeof(WCHAR));
        memcpy(span + length, text + index, (size_t)(from - index) * sizeof(WCHAR));
    } else if (span) {
        memcpy(span, text + to, (size_t)(index - to) * sizeof(WCHAR));
        memcpy(span + (index - to), g_dragData.text, (size_t)length * sizeof(WCHAR));
    }
    EditUnlockText(hText);
    if (!span) return FALSE;
    span[spanLen] = 0;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)spanFrom, (LPARAM)spanTo);
    EditInsert(span);
    ok = Replaced(textLen, spanFrom, spanLen, span, spanLen);
    if (!ok) {
        if (!Replaced(textLen, spanFrom, spanLen, original, spanLen) && !HistUndo()) {
            int currentLen = GetWindowTextLengthW(g_edit) - textLen + spanLen;
            SendMessageW(g_edit, EM_SETSEL, (WPARAM)spanFrom, (LPARAM)(spanFrom + currentLen));
            EditInsert(original);
        }
        SendMessageW(g_edit, EM_SETSEL, (WPARAM)from, (LPARAM)to);
        mem_free(span);
        return FALSE;
    }
    mem_free(span);
    if (index < from) SendMessageW(g_edit, EM_SETSEL, (WPARAM)index, (LPARAM)(index + length));
    else SendMessageW(g_edit, EM_SETSEL, (WPARAM)(index - length), (LPARAM)index);
    return TRUE;
}

static DWORD DropApply(DataObject *data, DWORD keys, int index, DWORD effect) {
    WCHAR *paths, *text;
    if (g_drop.kind == DROP_FILES) {
        paths = DataPaths(data);
        if (!paths) return DROPEFFECT_NONE;
        if (keys & MK_SHIFT) {
            text = PathsText(paths);
            if (!text || !InsertAt(index, text, FALSE)) effect = DROPEFFECT_NONE;
            mem_free(text);
            mem_free(paths);
        } else if (!PostMessageW(g_hwnd, WM_APP_OPENFILES, 0, (LPARAM)paths)) {
            mem_free(paths);
            effect = DROPEFFECT_NONE;
        }
        return effect;
    }
    if (g_drop.internal) {
        if (!MoveSelection(index, effect == DROPEFFECT_COPY)) return DROPEFFECT_NONE;
        g_dragDropped = 1;
        return effect;
    }
    text = DataText(data);
    if (!text) return DROPEFFECT_NONE;
    if (!InsertAt(index, text, TRUE)) effect = DROPEFFECT_NONE;
    mem_free(text);
    return effect;
}

static HRESULT WINAPI TargetQueryInterface(DropTarget *self, const GUID *iid, void **out) { return Query(self, iid, &g_iidDropTarget, out); }
static ULONG WINAPI TargetAddRef(DropTarget *self) { (void)self; return 2; }
static ULONG WINAPI TargetRelease(DropTarget *self) { (void)self; return 1; }

static HRESULT WINAPI TargetDragEnter(DropTarget *self, DataObject *data, DWORD keys, POINTL pt, DWORD *effect) {
    POINT screen;
    (void)self;
    DropReset();
    if (!g_helperTried) { g_helperTried = 1; g_ole.createInstance(&g_clsidDragDropHelper, NULL, CLSCTX_INPROC_SERVER, &g_iidDropTargetHelper, (void **)&g_helper); }
    g_drop.data = data;
    data->vtbl->AddRef(data);
    g_drop.kind = DropKind(data, &g_drop.hasFile);
    g_drop.internal = g_dragging && data == (DataObject *)&g_dragData;
    *effect = DropTrack(keys, pt, *effect);
    screen.x = pt.x;
    screen.y = pt.y;
    if (g_helper) g_helper->vtbl->DragEnter(g_helper, g_hwnd, data, &screen, *effect);
    return S_OK;
}

static HRESULT WINAPI TargetDragOver(DropTarget *self, DWORD keys, POINTL pt, DWORD *effect) {
    POINT screen;
    (void)self;
    *effect = g_drop.data ? DropTrack(keys, pt, *effect) : DROPEFFECT_NONE;
    screen.x = pt.x;
    screen.y = pt.y;
    if (g_helper) g_helper->vtbl->DragOver(g_helper, &screen, *effect);
    return S_OK;
}

static HRESULT WINAPI TargetDragLeave(DropTarget *self) {
    (void)self;
    if (g_helper) g_helper->vtbl->DragLeave(g_helper);
    DropReset();
    return S_OK;
}

static HRESULT WINAPI TargetDrop(DropTarget *self, DataObject *data, DWORD keys, POINTL pt, DWORD *effect) {
    POINT screen;
    DWORD chosen;
    (void)self;
    if (!g_drop.data) {
        g_drop.data = data;
        data->vtbl->AddRef(data);
        g_drop.kind = DropKind(data, &g_drop.hasFile);
        g_drop.internal = g_dragging && data == (DataObject *)&g_dragData;
    }
    chosen = DropTrack(keys, pt, *effect);
    screen.x = pt.x;
    screen.y = pt.y;
    if (g_helper) g_helper->vtbl->Drop(g_helper, data, &screen, chosen);
    DropCaret(-1);
    if (chosen != DROPEFFECT_NONE) chosen = DropApply(data, keys, g_drop.index, chosen);
    DropReset();
    *effect = chosen;
    return S_OK;
}

static const DropTargetVtbl g_targetVtbl = { TargetQueryInterface, TargetAddRef, TargetRelease, TargetDragEnter, TargetDragOver, TargetDragLeave, TargetDrop };
static DropTarget g_target = { &g_targetVtbl };

static HRESULT WINAPI SourceQueryInterface(DropSource *self, const GUID *iid, void **out) { return Query(self, iid, &g_iidDropSource, out); }
static ULONG WINAPI SourceAddRef(DropSource *self) { (void)self; return 2; }
static ULONG WINAPI SourceRelease(DropSource *self) { (void)self; return 1; }

static HRESULT WINAPI SourceQueryContinueDrag(DropSource *self, BOOL escape, DWORD keys) {
    (void)self;
    if (escape || (keys & MK_RBUTTON)) return DRAGDROP_S_CANCEL;
    return keys & MK_LBUTTON ? S_OK : DRAGDROP_S_DROP;
}

static HRESULT WINAPI SourceGiveFeedback(DropSource *self, DWORD effect) { (void)self; (void)effect; return DRAGDROP_S_USEDEFAULTCURSORS; }

static const DropSourceVtbl g_sourceVtbl = { SourceQueryInterface, SourceAddRef, SourceRelease, SourceQueryContinueDrag, SourceGiveFeedback };
static DropSource g_source = { &g_sourceVtbl };

static BOOL DragSnapshot(void) {
    DWORD selStart = 0, selEnd = 0;
    void *hText = NULL;
    int textLen = 0, length;
    const WCHAR *text;
    WCHAR *copy = NULL;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    text = EditLockText(&hText, &textLen);
    if (text && (int)selEnd > textLen) selEnd = (DWORD)textLen;
    length = (int)selEnd - (int)selStart;
    if (text && length > 0) copy = (WCHAR *)mem_alloc(((size_t)length + 1) * sizeof(WCHAR));
    if (copy) { memcpy(copy, text + selStart, (size_t)length * sizeof(WCHAR)); copy[length] = 0; }
    EditUnlockText(hText);
    if (!copy) return FALSE;
    mem_free(g_dragData.text);
    g_dragData.text = copy;
    g_dragData.length = length;
    g_dragFrom = (int)selStart;
    g_dragTo = (int)selEnd;
    return TRUE;
}

static void DragFinished(HRESULT hr, DWORD effect) {
    if (hr != DRAGDROP_S_DROP || !(effect & DROPEFFECT_MOVE) || g_dragDropped) return;
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)g_dragFrom, (LPARAM)g_dragTo);
    EditInsert(L"");
}

BOOL DndReady(void) {
    return g_oleReady && g_ole.doDragDrop != NULL;
}

void DndDragSelection(void) {
    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr;
    if (!DndReady() || g_dragging || !DragSnapshot()) return;
    g_dragging = 1;
    g_dragDropped = 0;
    hr = g_ole.doDragDrop((DataObject *)&g_dragData, &g_source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    g_dragging = 0;
    DragFinished(hr, effect);
}

void DndDropFiles(HANDLE drop) {
    WCHAR *paths = DropPaths(drop), *text;
    if (g_shell.dragFinish) g_shell.dragFinish(drop);
    if (!paths) return;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        text = PathsText(paths);
        if (text && text[0]) EditInsert(text);
        mem_free(text);
    } else {
        AppOpenFiles(paths);
    }
    mem_free(paths);
}

void DndInit(HWND hwnd) {
    HMODULE ole32;
    if (g_oleReady || (ole32 = LoadLibraryW(L"ole32.dll")) == NULL) return;
    *(void **)&g_ole.initialize = GetProcAddress(ole32, "OleInitialize");
    *(void **)&g_ole.uninitialize = GetProcAddress(ole32, "OleUninitialize");
    *(void **)&g_ole.registerDragDrop = GetProcAddress(ole32, "RegisterDragDrop");
    *(void **)&g_ole.revokeDragDrop = GetProcAddress(ole32, "RevokeDragDrop");
    *(void **)&g_ole.doDragDrop = GetProcAddress(ole32, "DoDragDrop");
    *(void **)&g_ole.releaseStgMedium = GetProcAddress(ole32, "ReleaseStgMedium");
    *(void **)&g_ole.createInstance = GetProcAddress(ole32, "CoCreateInstance");
    if (!g_ole.initialize || !g_ole.uninitialize || !g_ole.registerDragDrop || !g_ole.revokeDragDrop || !g_ole.doDragDrop || !g_ole.releaseStgMedium || !g_ole.createInstance) return;
    if (g_ole.initialize(NULL) < 0) return;
    g_oleReady = 1;
    g_drop.index = -1;
    if (g_ole.registerDragDrop(hwnd, &g_target) >= 0) g_registered = hwnd;
}

void DndShutdown(void) {
    if (g_registered) g_ole.revokeDragDrop(g_registered);
    g_registered = NULL;
    if (g_helper) g_helper->vtbl->Release(g_helper);
    g_helper = NULL;
    if (g_dropCaret) DestroyWindow(g_dropCaret);
    g_dropCaret = NULL;
    if (g_oleReady) g_ole.uninitialize();
    g_oleReady = 0;
}

#ifdef SHOTDC
typedef struct { DWORD op, keys, allowed; LONG x, y; } ProbeDrop;
static TextData g_probeData = { &g_dataVtbl, NULL, 0, NULL };

LRESULT DndProbe(const COPYDATASTRUCT *copy) {
    const ProbeDrop *probe = (const ProbeDrop *)copy->lpData;
    const WCHAR *payload;
    WCHAR *data;
    POINTL screen;
    POINT pt;
    DWORD effect;
    int chars;
    if (copy->dwData != 0x444E4450 || copy->cbData < sizeof(ProbeDrop)) return 0;
    if (probe->op == 0) return g_oleReady && g_registered ? PROBE_ID : 1;
    if (!g_oleReady) return -2;
    payload = (const WCHAR *)(probe + 1);
    chars = (int)((copy->cbData - sizeof(ProbeDrop)) / sizeof(WCHAR));
    pt.x = probe->x;
    pt.y = probe->y;
    ClientToScreen(g_edit, &pt);
    screen.x = pt.x;
    screen.y = pt.y;
    switch (probe->op) {
    case 1: case 2: case 4: case 9:
        data = (WCHAR *)mem_zalloc(((size_t)chars + 2) * sizeof(WCHAR));
        if (!data) return -1;
        memcpy(data, payload, (size_t)chars * sizeof(WCHAR));
        mem_free(g_probeData.text);
        mem_free(g_probeData.paths);
        g_probeData.text = probe->op == 2 || probe->op == 9 ? NULL : data;
        g_probeData.length = g_probeData.text ? wlen(data) : 0;
        g_probeData.paths = g_probeData.text ? NULL : data;
        effect = probe->allowed;
        TargetDragEnter(&g_target, (DataObject *)&g_probeData, probe->keys, screen, &effect);
        effect = probe->allowed;
        TargetDragOver(&g_target, probe->keys, screen, &effect);
        if (probe->op == 4 || probe->op == 9) return (LRESULT)effect;
        effect = probe->allowed;
        TargetDrop(&g_target, (DataObject *)&g_probeData, probe->keys, screen, &effect);
        return (LRESULT)effect;
    case 3:
        if (!DragSnapshot()) return -1;
        g_dragging = 1;
        g_dragDropped = 0;
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        TargetDragEnter(&g_target, (DataObject *)&g_dragData, probe->keys, screen, &effect);
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        TargetDragOver(&g_target, probe->keys, screen, &effect);
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        TargetDrop(&g_target, (DataObject *)&g_dragData, probe->keys, screen, &effect);
        g_dragging = 0;
        DragFinished(effect != DROPEFFECT_NONE ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL, effect);
        return (LRESULT)effect;
    case 5:
        TargetDragLeave(&g_target);
        return 0;
    case 6:
        if (!DragSnapshot()) return -1;
        g_dragDropped = 0;
        DragFinished(DRAGDROP_S_DROP, probe->allowed);
        return 0;
    case 7:
        return g_dropCaret && IsWindowVisible(g_dropCaret) ? (LRESULT)g_dropCaret : 0;
    case 8:
        effect = probe->allowed;
        TargetDragOver(&g_target, probe->keys, screen, &effect);
        return (LRESULT)effect;
    }
    return -1;
}
#endif
