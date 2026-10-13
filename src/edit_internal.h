#ifndef EDIT_INTERNAL_H
#define EDIT_INTERNAL_H

#include "mp.h"

void EditDeleteWord(int dir);
int  EditCaretIndex(void);
void EditCaretHint(int index);
void EditCaretDisplayAdjusted(BOOL adjusted);

int  EditCaretInView(HWND edit, int lineHeight);
void EditCaretRemember(HWND edit, HFONT font);
void EditCaretInvalidate(void);
void EditCaretFit(HWND edit, HFONT font, WNDPROC nativeProc, int *tall);
void EditViewRect(HWND edit, RECT *r);

#endif
