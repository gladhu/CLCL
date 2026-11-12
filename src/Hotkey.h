/*
 * CLCL
 *
 * Hotkey.h
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

#ifndef _INC_HOTKEY_H
#define _INC_HOTKEY_H

/* Include Files */
#include "Ini.h"
#include "Data.h"

/* Define */
/* String table ids. Both CLCL.rc and CLCLSet.rc define these numbers. */
#define IDS_HOTKEY_HEAD					430
#define IDS_HOTKEY_INTERNAL				431
#define IDS_HOTKEY_EXTERNAL				432
#define IDS_HOTKEY_RESERVED				433
#define IDS_HOTKEY_INVALID				434
#define IDS_HOTKEY_FAIL					435
#define IDS_HOTKEY_ELEVATION			436
#define IDS_HOTKEY_OWNER_ACTION			437
#define IDS_HOTKEY_OWNER_TOOL			438
#define IDS_HOTKEY_OWNER_TEMPLATE		439
#define IDS_HOTKEY_UNTITLED				440
#define IDS_HOTKEY_NOKEY				441
#define IDS_HOTKEY_ACT_MENU				442
#define IDS_HOTKEY_ACT_VIEWER			443
#define IDS_HOTKEY_ACT_OPTION			444
#define IDS_HOTKEY_ACT_WATCH			445
#define IDS_HOTKEY_ACT_EXIT				446
#define IDS_HOTKEY_ACT_SEARCH			447
#define IDS_HOTKEY_ASK_INTERNAL			448
#define IDS_HOTKEY_ASK_EXTERNAL			449
#define IDS_HOTKEY_ASK_RESERVED			450

#define HOTKEY_BIND_MAX					128
#define HOTKEY_OWNER_CCH				128
#define HOTKEY_SLOT_MAX					128

/* Struct */
typedef struct _HOTKEY_BIND {
	UINT modifiers;
	UINT virtkey;
	const void *identity;
	TCHAR owner[HOTKEY_OWNER_CCH];
} HOTKEY_BIND;

typedef struct _HOTKEY_SLOT {
	UINT modifiers;
	UINT virtkey;
	int id;
} HOTKEY_SLOT;

/* Function Prototypes */
BOOL hotkey_has_slots(void);
void hotkey_clear_slots(void);
BOOL hotkey_hook_match(const KBDLLHOOKSTRUCT *kb, int *id);

int hotkey_push_action(HOTKEY_BIND *list, int count, int max, const ACTION_INFO *ai);
int hotkey_push_tool(HOTKEY_BIND *list, int count, int max, const TOOL_INFO *ti);
int hotkey_push_items(HOTKEY_BIND *list, int count, int max, DATA_INFO *di);
DATA_INFO *hotkey_load_regist(void);

/* Ask before keeping a hotkey that conflicts. TRUE = keep it. */
BOOL hotkey_confirm(const HWND hWnd, const UINT modifiers, const UINT virtkey,
	const HOTKEY_BIND *list, const int count, const void *self);

/* Register every enabled hotkey. Returns the number that could not be registered. */
int hotkey_register_all(const HWND hWnd, OPTION_INFO *opt, DATA_INFO *regist,
	TCHAR *report, int report_cch);

#endif
/* End of source */
