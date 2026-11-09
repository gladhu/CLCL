/*
 * CLCL
 *
 * Search.h
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_SEARCH_H
#define _INC_SEARCH_H

/* Include Files */
#include "Data.h"

/* Define */

/* Struct */

/* Function Prototypes */
BOOL search_item_matches(DATA_INFO *item, const TCHAR *needle);
BOOL search_regist(const HINSTANCE hInstance);
void search_show(const HWND main_wnd, const BOOL paste);

#endif
/* End of source */
