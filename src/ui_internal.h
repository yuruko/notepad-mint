#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "mp.h"

void ButtonInit(void);
void DialogInit(void);
void UiFrame(HDC dc, const RECT *r, COLORREF topLeft, COLORREF bottomRight);

HWND DialogHoldFind(void);
void DialogReleaseFind(HWND held);

#endif
