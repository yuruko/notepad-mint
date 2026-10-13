#include "ui_internal.h"

typedef struct { int check, hot, down, captured, keyDown, focus, isDefault; HFONT font; } BtnSt;

static int BtnType(HWND hwnd) { return (int)(GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK); }

static BOOL IsMpRadio(HWND hwnd) {
    WCHAR className[16];
    GetClassNameW(hwnd, className, 16);
    return wcmp(className, L"mp_btn") == 0 && BtnType(hwnd) == BS_AUTORADIOBUTTON;
}

static void BtnClick(HWND hwnd, BtnSt *state) {
    int type = BtnType(hwnd);
    if (type == BS_AUTOCHECKBOX) {
        state->check = !state->check;
    } else if (type == BS_AUTORADIOBUTTON) {
        HWND first = hwnd, other;
        while (!(GetWindowLongPtrW(first, GWL_STYLE) & WS_GROUP)) {
            other = GetWindow(first, GW_HWNDPREV);
            if (!other) break;
            first = other;
        }
        for (other = first; other; other = GetWindow(other, GW_HWNDNEXT)) {
            if (other != first && (GetWindowLongPtrW(other, GWL_STYLE) & WS_GROUP)) break;
            if (other != hwnd && IsMpRadio(other)) SendMessageW(other, BM_SETCHECK, 0, 0);
        }
        state->check = 1;
    }
    InvalidateRect(hwnd, NULL, FALSE);
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetWindowLongPtrW(hwnd, GWLP_ID), BN_CLICKED), (LPARAM)hwnd);
}

static void BtnDraw(HWND hwnd, BtnSt *state, HDC hdc, RECT client) {
    WCHAR text[128];
    int length = GetWindowTextW(hwnd, text, 128), type = BtnType(hwnd);
    BOOL enabled = IsWindowEnabled(hwnd) ? TRUE : FALSE;
    BOOL hot = state->hot && enabled;
    COLORREF textColor = !enabled ? C_DIM : C_TEXT;
    UINT cues = (SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0;
    HGDIOBJ oldFont = SelectObject(hdc, state->font ? state->font : g_fontUI);
    FillC(hdc, &client, C_FACE);
    if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
        RECT outer = client, inner, textRect;
        if (state->isDefault && enabled) { UiFrame(hdc, &outer, C_ACCENT_FG, C_ACCENT_FG); InflateRect(&outer, -1, -1); }
        Bevel(hdc, &outer, state->down ? BV_SUNKEN : BV_RAISED);
        inner = outer;
        InflateRect(&inner, -2, -2);
        FillC(hdc, &inner, state->down ? C_FACE : (hot ? C_ACCENT : C_FACE2));
        textRect = inner;
        if (state->down) OffsetRect(&textRect, 1, 1);
        TextC(hdc, text, length, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | cues, (hot && !state->down) ? C_ON_ACCENT : textColor);
        if (state->focus && enabled) {
            RECT focusRect = inner;
            InflateRect(&focusRect, -S(3), -S(3));
            DrawFocusRect(hdc, &focusRect);
        }
    } else {
        int boxSize = S(13), boxTop = (client.bottom - boxSize) / 2;
        RECT box, textRect, focusRect, inner;
        box.left = 0; box.top = boxTop; box.right = boxSize; box.bottom = boxTop + boxSize;
        if (type == BS_AUTORADIOBUTTON) {
            HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
            SetDCPenColor(hdc, ThemeGet() == THEME_DARK ? C_HI : C_LO2);
            SetDCBrushColor(hdc, C_FIELD);
            Ellipse(hdc, box.left, box.top, box.right, box.bottom);
            if (state->check) {
                SetDCPenColor(hdc, enabled ? C_ACCENT_FG : C_DIM);
                SetDCBrushColor(hdc, enabled ? C_ACCENT_FG : C_DIM);
                Ellipse(hdc, box.left + S(4), box.top + S(4), box.right - S(3), box.bottom - S(3));
            }
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
        } else {
            Bevel(hdc, &box, BV_SUNKEN);
            inner = box;
            InflateRect(&inner, -2, -2);
            FillC(hdc, &inner, C_FIELD);
            if (state->check) CheckGlyph(hdc, box.left, box.top, enabled ? C_ACCENT_FG : C_DIM);
        }
        textRect.left = boxSize + S(7); textRect.top = 0; textRect.right = client.right; textRect.bottom = client.bottom;
        if (hot) {
            RECT highlight = textRect;
            DrawTextW(hdc, text, length, &highlight, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | cues);
            highlight.left = textRect.left - S(4); highlight.right = highlight.right + S(4);
            highlight.top = (client.bottom - (highlight.bottom - highlight.top)) / 2 - S(2); highlight.bottom = highlight.top + (highlight.bottom - highlight.top) + S(4);
            FillC(hdc, &highlight, C_ACCENT);
        }
        TextC(hdc, text, length, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | cues, hot ? C_ON_ACCENT : textColor);
        if (state->focus && enabled) {
            focusRect = textRect;
            DrawTextW(hdc, text, length, &focusRect, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | cues);
            focusRect.left = textRect.left - S(2); focusRect.right = focusRect.left + (focusRect.right - textRect.left) + S(4);
            focusRect.top = (client.bottom - (focusRect.bottom - focusRect.top)) / 2 - S(1);
            focusRect.bottom = focusRect.top + (focusRect.bottom - focusRect.top) + S(2);
            DrawFocusRect(hdc, &focusRect);
        }
    }
    SelectObject(hdc, oldFont);
}

static void BtnPaint(HWND hwnd, BtnSt *state) {
    PAINTSTRUCT ps;
    RECT client;
    HDC hdc = BeginPaint(hwnd, &ps), memDc = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ oldBitmap = NULL;
    BOOL presented = FALSE;
    GetClientRect(hwnd, &client);
    if (hdc && client.right > 0 && client.bottom > 0) {
        memDc = CreateCompatibleDC(hdc);
        if (memDc) bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        if (bitmap) oldBitmap = SelectObject(memDc, bitmap);
        if (oldBitmap && oldBitmap != (HGDIOBJ)(INT_PTR)-1) {
            BtnDraw(hwnd, state, memDc, client);
            presented = BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBitmap);
        }
        if (!presented) BtnDraw(hwnd, state, hdc, client);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memDc) DeleteDC(memDc);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK BtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BtnSt *state = (BtnSt *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    RECT client;
    POINT pt;
    int type;
    if (msg == WM_NCCREATE) {
        state = (BtnSt *)mem_zalloc(sizeof *state);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (state) state->isDefault = (((CREATESTRUCTW *)lParam)->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_NCDESTROY:
        mem_free(state);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        BtnPaint(hwnd, state);
        return 0;
    case WM_GETDLGCODE:
        type = BtnType(hwnd);
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) return DLGC_BUTTON | (state->isDefault ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
        if (type == BS_AUTORADIOBUTTON) return DLGC_BUTTON | DLGC_RADIOBUTTON;
        return DLGC_BUTTON;
    case WM_SETFONT:
        state->font = (HFONT)wParam;
        if (lParam) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)state->font;
    case WM_SETTEXT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return result; }
    case WM_UPDATEUISTATE:
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETFOCUS:
        state->focus = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        state->focus = 0; state->keyDown = 0; state->down = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case BM_GETCHECK:
        return state->check;
    case BM_SETCHECK:
        state->check = wParam ? 1 : 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case BM_GETSTATE:
        return (state->check ? BST_CHECKED : BST_UNCHECKED) | (state->down ? 4 : 0) | (state->focus ? 8 : 0);
    case BM_SETSTYLE:
        type = (int)(wParam & BS_TYPEMASK);
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
            state->isDefault = (type == BS_DEFPUSHBUTTON);
            if (lParam) InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case BM_CLICK:
        BtnClick(hwnd, state);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (IsWindowEnabled(hwnd)) {
            SetFocus(hwnd);
            state->captured = 1; state->down = 1;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (!state->hot) {
            TRACKMOUSEEVENT trackEvent;
            trackEvent.cbSize = sizeof trackEvent; trackEvent.dwFlags = TME_LEAVE; trackEvent.hwndTrack = hwnd; trackEvent.dwHoverTime = 0;
            TrackMouseEvent(&trackEvent);
            state->hot = 1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (state->captured) {
            int inside;
            GetClientRect(hwnd, &client);
            pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
            inside = PtInRect(&client, pt) ? 1 : 0;
            if (inside != state->down) { state->down = inside; InvalidateRect(hwnd, NULL, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        state->hot = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (state->captured) {
            int fire = state->down;
            state->captured = 0; state->down = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            if (fire) BtnClick(hwnd, state);
        }
        return 0;
    case WM_CAPTURECHANGED:
        state->captured = 0; state->down = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && !state->keyDown) { state->keyDown = 1; state->down = 1; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_KEYUP:
        if (wParam == VK_SPACE && state->keyDown) {
            state->keyDown = 0; state->down = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            BtnClick(hwnd, state);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND UiButton(HWND parent, const WCHAR *text, int x, int y, int width, int height, int id, DWORD style) {
    HWND button = CreateWindowExW(0, L"mp_btn", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, S(x), S(y), S(width), S(height), parent, (HMENU)(ULONG_PTR)id, g_hinst, NULL);
    SendMessageW(button, WM_SETFONT, (WPARAM)g_fontUI, FALSE);
    return button;
}

void ButtonInit(void) {
    RegClass(L"mp_btn", BtnProc, CS_DBLCLKS, NULL);
}
