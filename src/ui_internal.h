/* ui_internal.h - private contracts among UI modules and native-dialog callers. */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "mp.h"

void ButtonInit(void);
void DialogInit(void);
void UiFrame(HDC dc, const RECT *r, COLORREF topLeft, COLORREF bottomRight);

/* Return only the find window whose enabled state this call changed. A nested
 * dialog receives NULL and must not enable a window held by an outer dialog. */
HWND DialogHoldFind(void);
void DialogReleaseFind(HWND held);

#endif
