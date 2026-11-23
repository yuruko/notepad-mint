#ifndef EDIT_INTERNAL_H
#define EDIT_INTERNAL_H

#include "mp.h"

void EditDeleteWord(int dir);
int  EditCaretIndex(void);
void EditCaretHint(int index);
void EditCaretDisplayAdjusted(BOOL adjusted);

int  EditCaretInView(HWND hwnd, int height);
void EditCaretRemember(HWND hwnd, HFONT font);
void EditCaretInvalidate(void);
void EditCaretFit(HWND hwnd, HFONT font, WNDPROC nativeProc, int *tall);
void EditViewRect(HWND hwnd, RECT *viewRect);
int  EditCaretEndX(HWND hwnd, HFONT font, int index, const RECT *formatRect);

LRESULT EditCtl(UINT msg, WPARAM wParam, LPARAM lParam);
BOOL EditIndexPos(int index, POINT *pos);
const int *EditRowXs(HDC hdc, const WCHAR *text, int rowStart, int rowEnd, int originX);
HFONT EditFont(void);
int  EditHScroll(void);
int  EditCellWidth(void);
void EditWordBounds(int floor, int ceil);
BOOL EditMultiRoute(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result);
void EditMultiPaint(HWND hwnd, HDC hdc, const RECT *clip);
BOOL EditMultiSelected(int index);
void EditMultiNormalize(void);

#endif
