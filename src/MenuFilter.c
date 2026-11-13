/*
 * CLCL
 *
 * MenuFilter.c
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Message.h"
#include "Menu.h"
#include "Search.h"
#include "DarkMode.h"
#include "dpi.h"

#include "resource.h"

/* Define */
#define FILTER_CLASS			TEXT("CLCLMenuFilter")
#define IDC_FILTER_EDIT			1001
#define IDC_FILTER_LIST			1002
#define FILTER_TIMER			1
#define FILTER_WATCH			2

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER			0x1501
#endif

/* Global Variables */
extern HINSTANCE hInst;

static BOOL filter_class_ready;
static HWND filter_wnd;
static HWND filter_edit;
static HWND filter_list;
static HWND filter_owner;
static HMENU filter_menu;
static MENU_ITEM_INFO *filter_items;
static int filter_item_cnt;
static HFONT filter_font;
static BOOL filter_done;
static BOOL filter_allow_close;
static BOOL filter_in_submenu;
static int filter_result;
static MENU_ITEM_INFO filter_sep;
static MENU_ITEM_INFO filter_none;
static TCHAR filter_none_text[BUF_SIZE];
static WNDPROC filter_edit_proc;
static WNDPROC filter_list_proc;

/* Local Function Prototypes */
static BOOL filter_related(const HWND hWnd);
static BOOL filter_data_match(const MENU_ITEM_INFO *mii, const TCHAR *needle);
static void filter_fill(const TCHAR *needle);
static void filter_move(const int dir);
static void filter_activate(MENU_ITEM_INFO *mii);
static LRESULT CALLBACK filter_edit_subclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK filter_list_subclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK filter_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
 * filter_related - 入力中に閉じない関連ウィンドウか
 */
static BOOL filter_related(const HWND hWnd)
{
	TCHAR cls[64];

	if (hWnd == NULL) {
		return FALSE;
	}
	if (hWnd == filter_wnd || IsChild(filter_wnd, hWnd) != 0) {
		return TRUE;
	}
	if (GetWindow(hWnd, GW_OWNER) == filter_wnd) {
		return TRUE;
	}
	cls[0] = TEXT('\0');
	GetClassName(hWnd, cls, 64);
	if (StrStrI(cls, TEXT("IME")) != NULL ||
		StrStrI(cls, TEXT("MSCTF")) != NULL ||
		StrStrI(cls, TEXT("Cicero")) != NULL ||
		lstrcmp(cls, TEXT("#32768")) == 0) {
		return TRUE;
	}
	return FALSE;
}

/*
 * filter_data_match - 履歴項目が絞り込みに一致するか
 */
static BOOL filter_data_match(const MENU_ITEM_INFO *mii, const TCHAR *needle)
{
	if (mii == NULL) {
		return FALSE;
	}
	if (needle == NULL || *needle == TEXT('\0')) {
		return TRUE;
	}
	if (mii->flag & MF_SEPARATOR) {
		return FALSE;
	}
	if (mii->text != NULL && StrStrI(mii->text, needle) != NULL) {
		return TRUE;
	}
	if (mii->set_di != NULL && search_item_matches(mii->set_di, needle) == TRUE) {
		return TRUE;
	}
	return FALSE;
}

/*
 * filter_add - 一覧へ項目を追加
 */
static void filter_add(MENU_ITEM_INFO *mii)
{
	int index;
	int height;

	if (filter_list == NULL || mii == NULL) {
		return;
	}
	index = (int)SendMessage(filter_list, LB_ADDSTRING, 0, (LPARAM)mii);
	if (index < 0) {
		return;
	}
	height = menu_item_extent(mii, NULL);
	if (height < Scale(8)) {
		height = Scale(22);
	}
	SendMessage(filter_list, LB_SETITEMHEIGHT, index, height);
}

/*
 * filter_fill - 絞り込み結果を一覧へ反映
 */
static void filter_fill(const TCHAR *needle)
{
	MENU_ITEM_INFO *mii;
	BOOL filtering;
	BOOL seen_data;
	BOOL seen_tail;
	BOOL need_sep;
	int i;
	int sel;

	if (filter_list == NULL) {
		return;
	}
	filtering = (needle != NULL && *needle != TEXT('\0')) ? TRUE : FALSE;
	seen_data = FALSE;
	seen_tail = FALSE;
	need_sep = FALSE;
	SendMessage(filter_list, LB_RESETCONTENT, 0, 0);

	for (i = 0; i < filter_item_cnt; i++) {
		mii = filter_items + i;
		if (mii->item == NULL) {
			continue;
		}
		if (mii->flag & MF_SEPARATOR) {
			if (filtering == FALSE) {
				filter_add(mii);
			} else if (seen_tail == FALSE) {
				need_sep = TRUE;
			} else {
				filter_add(mii);
			}
			continue;
		}
		if (mii->set_di != NULL) {
			if (filtering == TRUE && filter_data_match(mii, needle) == FALSE) {
				continue;
			}
			filter_add(mii);
			seen_data = TRUE;
			continue;
		}
		if (filtering == TRUE && seen_tail == FALSE && (need_sep == TRUE || seen_data == TRUE)) {
			filter_add(&filter_sep);
		}
		seen_tail = TRUE;
		filter_add(mii);
	}

	if (filtering == TRUE && seen_data == FALSE) {
		lstrcpyn(filter_none_text, message_get_res(IDS_SEARCH_NONE), BUF_SIZE);
		SendMessage(filter_list, LB_INSERTSTRING, 0, (LPARAM)&filter_none);
		SendMessage(filter_list, LB_SETITEMHEIGHT, 0, Scale(22));
	}

	sel = -1;
	if (filtering == TRUE) {
		for (i = 0; i < (int)SendMessage(filter_list, LB_GETCOUNT, 0, 0); i++) {
			mii = (MENU_ITEM_INFO *)SendMessage(filter_list, LB_GETITEMDATA, i, 0);
			if (mii == NULL || mii == (MENU_ITEM_INFO *)LB_ERR || mii == &filter_none) {
				continue;
			}
			if (mii->flag & MF_SEPARATOR) {
				continue;
			}
			if (mii->set_di == NULL) {
				continue;
			}
			sel = i;
			break;
		}
	}
	SendMessage(filter_list, LB_SETCURSEL, sel, 0);
}

/*
 * filter_move - 選択行を移動
 */
static void filter_move(const int dir)
{
	int cnt;
	int i;
	MENU_ITEM_INFO *mii;

	if (filter_list == NULL || dir == 0) {
		return;
	}
	cnt = (int)SendMessage(filter_list, LB_GETCOUNT, 0, 0);
	i = (int)SendMessage(filter_list, LB_GETCURSEL, 0, 0);
	if (i < 0) {
		i = (dir > 0) ? -1 : cnt;
	}
	for (;;) {
		i += dir;
		if (i < 0 || i >= cnt) {
			SendMessage(filter_list, LB_SETCURSEL, (WPARAM)-1, 0);
			return;
		}
		mii = (MENU_ITEM_INFO *)SendMessage(filter_list, LB_GETITEMDATA, i, 0);
		if (mii == NULL || mii == (MENU_ITEM_INFO *)LB_ERR || mii == &filter_none) {
			continue;
		}
		if (mii->flag & MF_SEPARATOR) {
			continue;
		}
		SendMessage(filter_list, LB_SETCURSEL, i, 0);
		return;
	}
}

/*
 * filter_submenu - ポップアップ項目のサブメニューを取得
 */
static HMENU filter_submenu(MENU_ITEM_INFO *target)
{
	int pos;
	int i;

	if (filter_menu == NULL || target == NULL) {
		return NULL;
	}
	pos = 0;
	for (i = 0; i < filter_item_cnt; i++) {
		if ((filter_items + i)->item == NULL) {
			continue;
		}
		if ((filter_items + i) == target) {
			return GetSubMenu(filter_menu, pos);
		}
		pos++;
	}
	return NULL;
}

/*
 * filter_activate - 項目を実行する
 */
static void filter_activate(MENU_ITEM_INFO *mii)
{
	HMENU sub;
	RECT rc;
	RECT item_rc;
	POINT pt;
	int sel;
	int cmd;

	if (mii == NULL || mii == &filter_none || (mii->flag & MF_SEPARATOR)) {
		return;
	}
	if (mii->flag & MF_POPUP) {
		sub = filter_submenu(mii);
		if (sub == NULL) {
			return;
		}
		sel = (int)SendMessage(filter_list, LB_GETCURSEL, 0, 0);
		GetWindowRect(filter_list, &rc);
		pt.x = rc.right;
		pt.y = rc.top;
		if (sel >= 0 && SendMessage(filter_list, LB_GETITEMRECT, sel, (LPARAM)&item_rc) != LB_ERR) {
			pt.y = rc.top + item_rc.top;
		}
		filter_in_submenu = TRUE;
		cmd = (int)TrackPopupMenu(sub,
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
			pt.x, pt.y, 0, filter_owner, NULL);
		filter_in_submenu = FALSE;
		if (cmd > 0) {
			filter_result = cmd;
			filter_done = TRUE;
		} else if (filter_edit != NULL) {
			SetFocus(filter_edit);
		}
		return;
	}
	if (mii->id == 0) {
		return;
	}
	filter_result = (int)mii->id;
	filter_done = TRUE;
}

/*
 * filter_activate_current - 選択中の項目を実行する
 */
static void filter_activate_current(void)
{
	MENU_ITEM_INFO *mii;
	int sel;

	if (filter_list == NULL) {
		return;
	}
	sel = (int)SendMessage(filter_list, LB_GETCURSEL, 0, 0);
	if (sel < 0) {
		return;
	}
	mii = (MENU_ITEM_INFO *)SendMessage(filter_list, LB_GETITEMDATA, sel, 0);
	filter_activate(mii);
}

/*
 * filter_edit_subclass - 検索ボックスのキー操作
 */
static LRESULT CALLBACK filter_edit_subclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_KEYDOWN:
		switch (wParam) {
		case VK_DOWN:
			filter_move(1);
			return 0;
		case VK_UP:
			filter_move(-1);
			return 0;
		case VK_NEXT:
			filter_move(8);
			return 0;
		case VK_PRIOR:
			filter_move(-8);
			return 0;
		case VK_RETURN:
			filter_activate_current();
			return 0;
		case VK_ESCAPE:
			filter_result = 0;
			filter_done = TRUE;
			return 0;
		}
		break;

	case WM_MOUSEWHEEL:
		if (filter_list != NULL) {
			SendMessage(filter_list, msg, wParam, lParam);
			return 0;
		}
		break;
	}
	return CallWindowProc(filter_edit_proc, hWnd, msg, wParam, lParam);
}

/*
 * filter_list_subclass - 一覧のマウス操作
 */
static LRESULT CALLBACK filter_list_subclass(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	POINT pt;
	int hit;
	int index;
	MENU_ITEM_INFO *mii;
	LRESULT ret;

	switch (msg) {
	case WM_MOUSEMOVE:
		ret = CallWindowProc(filter_list_proc, hWnd, msg, wParam, lParam);
		pt.x = LOWORD(lParam);
		pt.y = HIWORD(lParam);
		hit = (int)SendMessage(hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
		if (HIWORD(hit) == 0) {
			index = LOWORD(hit);
			mii = (MENU_ITEM_INFO *)SendMessage(hWnd, LB_GETITEMDATA, index, 0);
			if (mii != NULL && mii != (MENU_ITEM_INFO *)LB_ERR &&
				mii != &filter_none && !(mii->flag & MF_SEPARATOR)) {
				if ((int)SendMessage(hWnd, LB_GETCURSEL, 0, 0) != index) {
					SendMessage(hWnd, LB_SETCURSEL, index, 0);
				}
			}
		}
		return ret;

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		ret = CallWindowProc(filter_list_proc, hWnd, msg, wParam, lParam);
		pt.x = LOWORD(lParam);
		pt.y = HIWORD(lParam);
		hit = (int)SendMessage(hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
		if (HIWORD(hit) == 0) {
			index = LOWORD(hit);
			mii = (MENU_ITEM_INFO *)SendMessage(hWnd, LB_GETITEMDATA, index, 0);
			filter_activate(mii);
		}
		return ret;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			filter_result = 0;
			filter_done = TRUE;
			return 0;
		}
		if (wParam == VK_RETURN) {
			filter_activate_current();
			return 0;
		}
		break;
	}
	return CallWindowProc(filter_list_proc, hWnd, msg, wParam, lParam);
}

/*
 * filter_proc - 履歴フィルタウィンドウ
 */
static LRESULT CALLBACK filter_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HBRUSH brush;
	RECT rc;
	TCHAR needle[BUF_SIZE];
	HWND other;
	MEASUREITEMSTRUCT *ms;
	DRAWITEMSTRUCT *ds;
	MENU_ITEM_INFO *draw_mii;
	int saved;

	switch (msg) {
	case WM_ERASEBKGND:
		GetClientRect(hWnd, &rc);
		brush = CreateSolidBrush(dark_mode_get_color(COLOR_MENU));
		if (brush != NULL) {
			FillRect((HDC)wParam, &rc, brush);
			DeleteObject(brush);
		}
		return 1;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE) {
			SetTimer(hWnd, FILTER_TIMER, 120, NULL);
		}
		return 0;

	case WM_TIMER:
		if (wParam == FILTER_WATCH) {
			other = GetForegroundWindow();
			if (filter_allow_close == TRUE && filter_in_submenu == FALSE &&
				other != NULL && other != hWnd && filter_related(other) == FALSE) {
				filter_result = 0;
				filter_done = TRUE;
			}
			return 0;
		}
		if (wParam == FILTER_TIMER) {
			KillTimer(hWnd, FILTER_TIMER);
			needle[0] = TEXT('\0');
			GetWindowText(filter_edit, needle, BUF_SIZE);
			needle[BUF_SIZE - 1] = TEXT('\0');
			Trim(needle);
			filter_fill(needle);
		}
		return 0;

	case WM_MEASUREITEM:
		if (wParam == IDC_FILTER_LIST) {
			ms = (MEASUREITEMSTRUCT *)lParam;
			draw_mii = (MENU_ITEM_INFO *)ms->itemData;
			ms->itemHeight = menu_item_extent(draw_mii, NULL);
			if ((int)ms->itemHeight < Scale(8)) {
				ms->itemHeight = Scale(22);
			}
			return TRUE;
		}
		break;

	case WM_DRAWITEM:
		if (wParam == IDC_FILTER_LIST) {
			ds = (DRAWITEMSTRUCT *)lParam;
			if (ds->itemData == 0) {
				return TRUE;
			}
			draw_mii = (MENU_ITEM_INFO *)ds->itemData;
			if (draw_mii != &filter_sep && draw_mii != &filter_none &&
				menu_check_item_info(draw_mii) == FALSE) {
				return TRUE;
			}
			saved = SaveDC(ds->hDC);
			menu_drawitem(ds);
			if (saved != 0) {
				RestoreDC(ds->hDC, saved);
			}
			return TRUE;
		}
		break;

	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		if (dark_mode_ctl_color(msg, wParam, lParam, &brush) == TRUE) {
			return (LRESULT)brush;
		}
		break;

	case WM_ACTIVATE:
		if (filter_allow_close == TRUE && filter_in_submenu == FALSE &&
			LOWORD(wParam) == WA_INACTIVE) {
			other = (HWND)lParam;
			/* 输入法或子菜单不算离开 */
			if (filter_related(other) == TRUE) {
				break;
			}
			/* 失活时 GetForegroundWindow 可能仍是自己，稍后再看一次 */
			if (other != NULL && GetForegroundWindow() != hWnd &&
				filter_related(GetForegroundWindow()) == FALSE) {
				filter_result = 0;
				filter_done = TRUE;
			} else {
				SetTimer(hWnd, FILTER_WATCH, 50, NULL);
			}
		}
		break;

	case WM_ACTIVATEAPP:
		if (wParam == FALSE && filter_allow_close == TRUE && filter_in_submenu == FALSE) {
			other = GetForegroundWindow();
			if (filter_related(other) == FALSE) {
				filter_result = 0;
				filter_done = TRUE;
			}
		}
		break;

	case WM_CLOSE:
		filter_result = 0;
		filter_done = TRUE;
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

/*
 * filter_register - ウィンドウクラスを登録
 */
static BOOL filter_register(void)
{
	WNDCLASS wc;

	if (filter_class_ready == TRUE) {
		return TRUE;
	}
	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = filter_proc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszClassName = FILTER_CLASS;
	if (RegisterClass(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		return FALSE;
	}
	filter_class_ready = TRUE;
	return TRUE;
}

/*
 * filter_layout_size - ウィンドウサイズを履歴の幅に合わせる
 */
static void filter_layout_size(const POINT *mpos, int *x, int *y, int *width, int *height)
{
	MONITORINFO mi;
	HMONITOR mon;
	RECT work;
	RECT wr;
	MENU_ITEM_INFO *mii;
	int i;
	int item_w;
	int list_h;
	int max_list;
	int edit_h;
	int pad;
	DWORD style;
	DWORD exstyle;

	pad = Scale(4);
	edit_h = Scale(26);
	*width = Scale(280);
	list_h = 0;
	for (i = 0; i < filter_item_cnt; i++) {
		mii = filter_items + i;
		if (mii->item == NULL) {
			continue;
		}
		item_w = 0;
		list_h += menu_item_extent(mii, &item_w);
		if (item_w + Scale(24) > *width) {
			*width = item_w + Scale(24);
		}
	}
	if (list_h < Scale(22)) {
		list_h = Scale(22);
	}

	SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	if (mpos != NULL) {
		mon = MonitorFromPoint(*mpos, MONITOR_DEFAULTTONEAREST);
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfo(mon, &mi) != 0) {
			work = mi.rcWork;
		}
	}
	max_list = (work.bottom - work.top) - edit_h - pad * 3 - Scale(16);
	if (max_list < Scale(120)) {
		max_list = Scale(120);
	}
	if (list_h > max_list) {
		list_h = max_list;
	}
	*width += GetSystemMetricsDpi(SM_CXVSCROLL);
	if (*width > (work.right - work.left) - Scale(16)) {
		*width = (work.right - work.left) - Scale(16);
	}

	style = WS_POPUP | WS_BORDER | WS_CLIPCHILDREN;
	exstyle = WS_EX_TOOLWINDOW;
	SetRect(&wr, 0, 0, *width, pad + edit_h + pad + list_h);
	AdjustWindowRectEx(&wr, style, FALSE, exstyle);
	*width = wr.right - wr.left;
	*height = wr.bottom - wr.top;
	*x = (mpos != NULL) ? mpos->x : work.left;
	*y = (mpos != NULL) ? mpos->y : work.top;
	if (*x + *width > work.right) {
		*x = work.right - *width;
	}
	if (*y + *height > work.bottom) {
		*y = work.bottom - *height;
	}
	if (*x < work.left) {
		*x = work.left;
	}
	if (*y < work.top) {
		*y = work.top;
	}
}

/*
 * menu_filter_show - 履歴ポップアップに検索ボックスを付けて表示
 */
int menu_filter_show(const HWND hWnd, const HMENU hMenu, const POINT *mpos,
	MENU_ITEM_INFO *items, const int count)
{
	MSG msg;
	HWND previous;
	RECT rc;
	int x, y, width, height;
	int pad;
	int edit_h;
	int got_msg;

	if (filter_register() == FALSE || items == NULL) {
		return 0;
	}
	previous = GetForegroundWindow();
	filter_owner = hWnd;
	filter_menu = hMenu;
	filter_items = items;
	filter_item_cnt = count;
	filter_done = FALSE;
	filter_allow_close = FALSE;
	filter_in_submenu = FALSE;
	filter_result = 0;
	ZeroMemory(&filter_sep, sizeof(filter_sep));
	ZeroMemory(&filter_none, sizeof(filter_none));
	filter_sep.flag = MF_SEPARATOR | MF_OWNERDRAW;
	filter_sep.item = (LPCTSTR)&filter_sep;
	filter_none.flag = MF_OWNERDRAW;
	filter_none.item = (LPCTSTR)&filter_none;
	filter_none.text = filter_none_text;
	filter_none_text[0] = TEXT('\0');

	filter_layout_size(mpos, &x, &y, &width, &height);
	filter_wnd = CreateWindowEx(WS_EX_TOOLWINDOW, FILTER_CLASS, TEXT(""),
		WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
		x, y, width, height, NULL, NULL, hInst, NULL);
	if (filter_wnd == NULL) {
		return 0;
	}

	pad = Scale(4);
	edit_h = Scale(26);
	GetClientRect(filter_wnd, &rc);
	filter_font = menu_font_create();
	filter_edit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
		pad, pad, rc.right - pad * 2, edit_h,
		filter_wnd, (HMENU)IDC_FILTER_EDIT, hInst, NULL);
	filter_list = CreateWindowEx(0, TEXT("LISTBOX"), TEXT(""),
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
		0, pad + edit_h + pad, rc.right, rc.bottom - (pad + edit_h + pad),
		filter_wnd, (HMENU)IDC_FILTER_LIST, hInst, NULL);
	if (filter_edit == NULL || filter_list == NULL) {
		DestroyWindow(filter_wnd);
		filter_wnd = NULL;
		return 0;
	}
	if (filter_font != NULL) {
		SendMessage(filter_edit, WM_SETFONT, (WPARAM)filter_font, FALSE);
	}
	SendMessage(filter_edit, EM_SETCUEBANNER, TRUE, (LPARAM)message_get_res(IDS_SEARCH_CUE));
	filter_edit_proc = (WNDPROC)GetWindowLongPtr(filter_edit, GWLP_WNDPROC);
	filter_list_proc = (WNDPROC)GetWindowLongPtr(filter_list, GWLP_WNDPROC);
	SetWindowLongPtr(filter_edit, GWLP_WNDPROC, (LONG_PTR)filter_edit_subclass);
	SetWindowLongPtr(filter_list, GWLP_WNDPROC, (LONG_PTR)filter_list_subclass);
	dark_mode_set_window(filter_wnd);
	dark_mode_set_children(filter_wnd);
	filter_fill(TEXT(""));

	menu_key_hook_pause();
	ShowWindow(filter_wnd, SW_SHOW);
	SetForegroundWindow(filter_wnd);
	SetFocus(filter_edit);
	filter_allow_close = TRUE;
	SetTimer(filter_wnd, FILTER_WATCH, 200, NULL);

	got_msg = 1;
	while (filter_done == FALSE) {
		got_msg = GetMessage(&msg, NULL, 0, 0);
		if (got_msg <= 0) {
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	/* GetMessage が WM_QUIT を取り出して 0 を返すと、ここで再投稿しない限り
	   外側のメッセージループが終了せず、ウィンドウのないプロセスが残る */
	if (got_msg == 0) {
		PostQuitMessage((int)msg.wParam);
		filter_result = 0;
	}

	filter_allow_close = FALSE;
	if (filter_wnd != NULL) {
		KillTimer(filter_wnd, FILTER_TIMER);
		KillTimer(filter_wnd, FILTER_WATCH);
		DestroyWindow(filter_wnd);
		filter_wnd = NULL;
	}
	filter_edit = NULL;
	filter_list = NULL;
	if (filter_font != NULL) {
		DeleteObject(filter_font);
		filter_font = NULL;
	}
	if (previous != NULL && IsWindow(previous) != 0) {
		SetForegroundWindow(previous);
	}
	return filter_result;
}

/*
 * menu_filter_is_open - 履歴一覧のメッセージループがまだ終わっていない
 */
BOOL menu_filter_is_open(void)
{
	return (filter_wnd != NULL && filter_done == FALSE) ? TRUE : FALSE;
}

/*
 * menu_filter_dismiss - 表示中の履歴一覧を閉じる
 */
void menu_filter_dismiss(void)
{
	filter_result = 0;
	filter_done = TRUE;
}
/* End of source */
