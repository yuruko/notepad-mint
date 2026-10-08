/* Private, test-only UI inspection. Release builds expose no probe messages. */
#ifndef UI_PROBE_H
#define UI_PROBE_H
#ifdef SHOTDC
#include "ui_internal.h"
LRESULT UiProbeFontAndDialogs(WPARAM operation, LPARAM font);
#endif
#endif
