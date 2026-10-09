/* Private commands shared by the editor subclass and text helpers. */
#ifndef EDIT_INTERNAL_H
#define EDIT_INTERNAL_H

#include "mp.h"

void EditDeleteWord(int dir);
int  EditCaretIndex(void);
void EditCaretHint(int index);
void EditCaretDisplayAdjusted(BOOL adjusted);

/* Geometry helpers leave selection/undo untouched. The subclass supplies the
 * native procedure and suppresses nested fitting until this transaction ends. */
int  EditCaretInView(HWND edit, int lineHeight);
void EditCaretRemember(HWND edit, HFONT font);
void EditCaretInvalidate(void);
void EditCaretFit(HWND edit, HFONT font, WNDPROC nativeProc, int *tall);
void EditViewRect(HWND edit, RECT *r);      /* the client area minus the overhang that is out of sight (SBAR_TRIM) */

#endif
