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

#endif
