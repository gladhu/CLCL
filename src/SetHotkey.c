/*
 * CLCL
 *
 * SetHotkey.c
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <commctrl.h>

#include "General.h"
#include "Data.h"
#include "Ini.h"
#include "DarkMode.h"
#include "Hotkey.h"

#include "resource.h"

/* Define */

/* Global Variables */
extern OPTION_INFO option;
extern DATA_INFO regist_data;

/* Local Function Prototypes */
static BOOL confirm_item_hotkey(const HWND hDlg, const UINT modifiers, const UINT virtkey, const void *self);
static INT_PTR CALLBACK set_hotkey_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

/*
 * confirm_item_hotkey - テンプレートのホットキーが他と衝突しないか確認する
 */
static BOOL confirm_item_hotkey(const HWND hDlg, const UINT modifiers, const UINT virtkey, const void *self)
{
	HOTKEY_BIND list[HOTKEY_BIND_MAX];
	int i;
	int n = 0;

	for (i = 0; i < option.action_cnt; i++) {
		n = hotkey_push_action(list, n, HOTKEY_BIND_MAX, option.action_info + i);
	}
	for (i = 0; i < option.tool_cnt; i++) {
		n = hotkey_push_tool(list, n, HOTKEY_BIND_MAX, option.tool_info + i);
	}
	n = hotkey_push_items(list, n, HOTKEY_BIND_MAX, regist_data.child);
	return hotkey_confirm(hDlg, modifiers, virtkey, list, n, self);
}

/*
 * set_hotkey_proc - ホットキー設定ウィンドウプロシージャ
 */
static INT_PTR CALLBACK set_hotkey_proc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	DATA_INFO *di;
	int i;

	switch (uMsg) {
	case WM_INITDIALOG:
		// ダークモードの設定
		dark_mode_set_dialog(hDlg);
		if ((di = (DATA_INFO *)lParam) == NULL) {
			EndDialog(hDlg, FALSE);
			break;
		}
		// ホットキー
		i = 0;
		if (di->op_modifiers & MOD_SHIFT) {
			i |= HOTKEYF_SHIFT;
		}
		if (di->op_modifiers & MOD_CONTROL) {
			i |= HOTKEYF_CONTROL;
		}
		if (di->op_modifiers & MOD_ALT) {
			i |= HOTKEYF_ALT;
		}
		if (di->op_modifiers & MOD_WIN) {
			CheckDlgButton(hDlg, IDC_CHECK_WIN, 1);
		}
		if (di->op_virtkey != 0) {
			SendDlgItemMessage(hDlg, IDC_HOTKEY_ITEM, HKM_SETHOTKEY,
				(WPARAM)MAKEWORD(di->op_virtkey, i), 0);
			// 貼り付け
			CheckDlgButton(hDlg, IDC_CHECK_PASTE, di->op_paste);
		} else {
			CheckDlgButton(hDlg, IDC_CHECK_PASTE, 1);
		}

		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
		break;

	case WM_CLOSE:
		EndDialog(hDlg, FALSE);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCANCEL:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
			break;

		case IDOK:
			di = (DATA_INFO *)GetWindowLongPtr(hDlg, GWLP_USERDATA);
			if (di == NULL) {
				EndDialog(hDlg, FALSE);
				break;
			}
			{
				UINT old_mod = di->op_modifiers;
				UINT old_vk = di->op_virtkey;
				int old_paste = di->op_paste;

				// ホットキー
				i = (int)SendDlgItemMessage(hDlg, IDC_HOTKEY_ITEM, HKM_GETHOTKEY, 0, 0);
				di->op_virtkey = LOBYTE(i);
				i = HIBYTE(i);
				di->op_modifiers = ((i & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) |
					((i & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
					((i & HOTKEYF_ALT) ? MOD_ALT : 0);
				if (IsDlgButtonChecked(hDlg, IDC_CHECK_WIN) == 1) {
					di->op_modifiers |= MOD_WIN;
				}
				// 貼り付け
				di->op_paste = IsDlgButtonChecked(hDlg, IDC_CHECK_PASTE);
				if (di->op_virtkey != 0 &&
					confirm_item_hotkey(hDlg, di->op_modifiers, di->op_virtkey, di) == FALSE) {
					di->op_modifiers = old_mod;
					di->op_virtkey = old_vk;
					di->op_paste = old_paste;
					break;
				}
			}
			EndDialog(hDlg, TRUE);
			break;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/*
 * set_hotkey - ホットキー設定
 */
BOOL set_hotkey(const HINSTANCE hInst, const HWND hWnd, DATA_INFO *di)
{
	return DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_DIALOG_SET_HOTKEY), hWnd, set_hotkey_proc, (LPARAM)di);
}
/* End of source */
