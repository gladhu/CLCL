/*
 * CLCL
 *
 * Caret.h
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_CARET_H
#define _INC_CARET_H

/* Include Files */

/* Define */

/* Struct */

/* Function Prototypes */
#ifdef __cplusplus
extern "C" {
#endif
BOOL caret_get_pos(const HWND active_wnd, const HWND focus_wnd, POINT *pt);
void caret_free(void);
#ifdef __cplusplus
}
#endif

#endif
/* End of source */
