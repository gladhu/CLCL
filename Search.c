/*
 * CLCL
 *
 * Search.c
 *
 * Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <commctrl.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#include "General.h"
#include "Memory.h"
#include "String.h"
#include "Data.h"
#include "Message.h"
#include "SendKey.h"
#include "DarkMode.h"
#include "dpi.h"
#include "Search.h"

#include "resource.h"

/* Define */
#define WINDOW_CLASS					TEXT("CLCLSearch")

#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER				0x00010000
#endif
#ifndef LVS_EX_LABELTIP
#define LVS_EX_LABELTIP					0x00004000
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER					0x1501
#endif

#define IDC_SEARCH_EDIT					1001
#define IDC_SEARCH_LIST					1002
#define IDC_SEARCH_PASTE				1003
#define IDC_SEARCH_COPY					1004
#define IDC_SEARCH_PASTE_BTN			1005
#define IDC_SEARCH_SHOW					1006
#define IDC_SEARCH_CLOSE				1007
#define IDC_SEARCH_COUNT				1008
#define IDC_SEARCH_LABEL				1009
#define IDC_SEARCH_APPLY				1010

#define SEARCH_TIMER					1
#define SEARCH_RESULT_MAX				500
#define SEARCH_TEXT_MAX					(1024 * 1024)
#define SEARCH_SOURCE_HISTORY			0
#define SEARCH_SOURCE_REGIST			1
#define SEARCH_PROP_PROC				TEXT("CLCLSearchProc")

/* Struct */
typedef struct _SEARCH_RESULT {
	DATA_INFO *item;
	int source;

	TCHAR title[192];
	TCHAR window[128];
	TCHAR date[48];
} SEARCH_RESULT;

/* Global Variables */
extern HINSTANCE hInst;
extern DATA_INFO history_data;
extern DATA_INFO regist_data;

/* Local Function Prototypes */
static HFONT search_create_font(void);
static BOOL search_is_own_window(const HWND hWnd);
static void search_oneline(TCHAR *dst, const int dst_len, const TCHAR *src);
static BOOL search_text_contains(DATA_INFO *data, const TCHAR *needle);
static BOOL search_match_item(DATA_INFO *item, const TCHAR *needle);
static BOOL search_add(DATA_INFO *item, const int source);
static void search_walk(DATA_INFO *di, const int source, const TCHAR *needle, const BOOL allow_empty);
static void search_execute(const HWND hWnd);
static DATA_INFO *search_selected(const HWND hWnd);
static void search_wait_keys(void);
static void search_apply(const HWND hWnd, const int mode);
static void search_show_in_viewer(const HWND hWnd);
static void search_layout(const HWND hWnd);
static void search_set_texts(const HWND hWnd);
static void search_subclass(const HWND hCtrl);
static LRESULT CALLBACK search_child_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK search_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND search_wnd;
static HWND search_main;
static HWND search_paste_target;
static HFONT search_font;
static SEARCH_RESULT *search_results;
static int search_count;
static TCHAR search_cue[BUF_SIZE];

/*
 * search_create_font - 検索ウィンドウのフォントを作成
 */
static HFONT search_create_font(void)
{
	NONCLIENTMETRICS ncm;

	ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0) == FALSE) {
		ncm.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(int);
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0) == FALSE) {
			return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		}
	}
	return CreateFontIndirect(&ncm.lfMessageFont);
}

/*
 * search_is_own_window - CLCL 自身のウィンドウか
 */
static BOOL search_is_own_window(const HWND hWnd)
{
	DWORD pid;

	if (hWnd == NULL || IsWindow(hWnd) == 0) {
		return TRUE;
	}
	pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	return (pid == GetCurrentProcessId()) ? TRUE : FALSE;
}

/*
 * search_oneline - 表示用に改行を空白へ置き換える
 */
static void search_oneline(TCHAR *dst, const int dst_len, const TCHAR *src)
{
	int i, j;

	if (dst_len <= 0) {
		return;
	}
	*dst = TEXT('\0');
	if (src == NULL) {
		return;
	}
	for (i = 0, j = 0; src[i] != TEXT('\0') && j < dst_len - 1; i++) {
		if (src[i] == TEXT('\r') || src[i] == TEXT('\n') || src[i] == TEXT('\t')) {
			if (j > 0 && dst[j - 1] == TEXT(' ')) {
				continue;
			}
			dst[j++] = TEXT(' ');
		} else {
			dst[j++] = src[i];
		}
	}
	dst[j] = TEXT('\0');
}

/*
 * search_text_contains - 形式データに検索文字列が含まれるか
 */
static BOOL search_text_contains(DATA_INFO *data, const TCHAR *needle)
{
	BYTE *mem;
	TCHAR *text;
	char *ansi;
	DWORD size;
	DWORD chars;
	BOOL unicode;
	BOOL hit;

	if (data == NULL || data->type != TYPE_DATA || data->data == NULL || data->size == 0 ||
		needle == NULL || *needle == TEXT('\0')) {
		return FALSE;
	}
	if (data->menu_title != NULL && StrStrI(data->menu_title, needle) != NULL) {
		return TRUE;
	}
	unicode = (data->format == CF_UNICODETEXT) ? TRUE : FALSE;
	if (unicode == FALSE && data->format_name != NULL && StrStrI(data->format_name, TEXT("UNICODE")) != NULL) {
		unicode = TRUE;
	}
	if (unicode == FALSE && data->format != CF_TEXT && data->format != CF_OEMTEXT &&
		(data->format_name == NULL || StrStrI(data->format_name, TEXT("TEXT")) == NULL)) {
		return FALSE;
	}

	size = data->size;
	if (size > SEARCH_TEXT_MAX) {
		size = SEARCH_TEXT_MAX;
	}
	if ((mem = (BYTE *)GlobalLock(data->data)) == NULL) {
		return FALSE;
	}
	text = NULL;
	ansi = NULL;
	if (unicode == TRUE) {
		chars = size / sizeof(WCHAR);
		text = (TCHAR *)mem_alloc(sizeof(WCHAR) * (chars + 1));
		if (text != NULL) {
			CopyMemory(text, mem, chars * sizeof(WCHAR));
			text[chars] = TEXT('\0');
		}
	} else {
		ansi = (char *)mem_alloc(size + 1);
		if (ansi != NULL) {
			CopyMemory(ansi, mem, size);
			ansi[size] = '\0';
			text = alloc_char_to_tchar(ansi);
			mem_free(&ansi);
		}
	}
	GlobalUnlock(data->data);
	hit = FALSE;
	if (text != NULL) {
		hit = (StrStrI(text, needle) != NULL) ? TRUE : FALSE;
		mem_free(&text);
	}
	return hit;
}

/*
 * search_match_item - 項目が検索文字列に一致するか
 */
static BOOL search_match_item(DATA_INFO *item, const TCHAR *needle)
{
	DATA_INFO *child;
	TCHAR *title;

	if (item->title != NULL && StrStrI(item->title, needle) != NULL) {
		return TRUE;
	}
	if (item->window_name != NULL && StrStrI(item->window_name, needle) != NULL) {
		return TRUE;
	}
	title = data_get_title(item);
	if (title != NULL && *title != TEXT('\0') && StrStrI(title, needle) != NULL) {
		return TRUE;
	}
	for (child = item->child; child != NULL; child = child->next) {
		if (child->type == TYPE_DATA && search_text_contains(child, needle) == TRUE) {
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * search_item_matches - 項目またはその配下に検索文字列が含まれるか
 */
BOOL search_item_matches(DATA_INFO *item, const TCHAR *needle)
{
	DATA_INFO *child;

	if (item == NULL) {
		return FALSE;
	}
	if (needle == NULL || *needle == TEXT('\0')) {
		return TRUE;
	}
	if (item->type == TYPE_FOLDER) {
		if (item->title != NULL && StrStrI(item->title, needle) != NULL) {
			return TRUE;
		}
		for (child = item->child; child != NULL; child = child->next) {
			if (search_item_matches(child, needle) == TRUE) {
				return TRUE;
			}
		}
		return FALSE;
	}
	if (item->type == TYPE_ITEM) {
		return search_match_item(item, needle);
	}
	if (item->type == TYPE_DATA) {
		return search_text_contains(item, needle);
	}
	return FALSE;
}

/*
 * search_add - 結果を追加
 */
static BOOL search_add(DATA_INFO *item, const int source)
{
	SEARCH_RESULT *result;
	TCHAR *title;
	TCHAR date[BUF_SIZE];

	if (search_results == NULL || search_count >= SEARCH_RESULT_MAX) {
		return FALSE;
	}
	result = search_results + search_count;
	ZeroMemory(result, sizeof(SEARCH_RESULT));
	result->item = item;
	result->source = source;
	title = data_get_title(item);
	search_oneline(result->title, 192, title);
	if (result->title[0] == TEXT('\0')) {
		lstrcpyn(result->title, message_get_res(IDS_SEARCH_EMPTY_ITEM), 192);
	}
	search_oneline(result->window, 128, item->window_name);
	*date = TEXT('\0');
	data_get_modified_string(item, date);
	lstrcpyn(result->date, date, 48);
	search_count++;
	return TRUE;
}

/*
 * search_walk - 履歴またはテンプレートを走査
 */
static void search_walk(DATA_INFO *di, const int source, const TCHAR *needle, const BOOL allow_empty)
{
	for (; di != NULL && search_count < SEARCH_RESULT_MAX; di = di->next) {
		if (di->type == TYPE_FOLDER) {
			search_walk(di->child, source, needle, FALSE);
			continue;
		}
		if (di->type != TYPE_ITEM) {
			continue;
		}
		if (needle == NULL || *needle == TEXT('\0')) {
			if (allow_empty == TRUE) {
				search_add(di, source);
			}
		} else if (search_match_item(di, needle) == TRUE) {
			search_add(di, source);
		}
	}
}

/*
 * search_execute - 検索を実行して一覧を更新
 */
static void search_execute(const HWND hWnd)
{
	HWND hList;
	HWND hCount;
	TCHAR needle[BUF_SIZE];
	TCHAR source_history[64];
	TCHAR source_regist[64];
	TCHAR fmt[BUF_SIZE];
	TCHAR buf[BUF_SIZE];
	LV_ITEM lvi;
	int i;

	if (search_results == NULL) {
		search_results = (SEARCH_RESULT *)mem_calloc(sizeof(SEARCH_RESULT) * SEARCH_RESULT_MAX);
		if (search_results == NULL) {
			return;
		}
	}
	search_count = 0;
	GetWindowText(GetDlgItem(hWnd, IDC_SEARCH_EDIT), needle, BUF_SIZE - 1);
	needle[BUF_SIZE - 1] = TEXT('\0');
	Trim(needle);

	if (*needle == TEXT('\0')) {
		search_walk(history_data.child, SEARCH_SOURCE_HISTORY, needle, TRUE);
	} else {
		search_walk(history_data.child, SEARCH_SOURCE_HISTORY, needle, FALSE);
		search_walk(regist_data.child, SEARCH_SOURCE_REGIST, needle, FALSE);
	}

	lstrcpyn(source_history, message_get_res(IDS_SEARCH_SOURCE_HISTORY), 64);
	lstrcpyn(source_regist, message_get_res(IDS_SEARCH_SOURCE_REGIST), 64);

	hList = GetDlgItem(hWnd, IDC_SEARCH_LIST);
	SendMessage(hList, WM_SETREDRAW, (WPARAM)FALSE, 0);
	ListView_DeleteAllItems(hList);
	for (i = 0; i < search_count; i++) {
		ZeroMemory(&lvi, sizeof(LV_ITEM));
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.iItem = i;
		lvi.pszText = (search_results + i)->title;
		lvi.lParam = i;
		if (ListView_InsertItem(hList, &lvi) == -1) {
			continue;
		}
		ListView_SetItemText(hList, i, 1,
			((search_results + i)->source == SEARCH_SOURCE_REGIST) ? source_regist : source_history);
		ListView_SetItemText(hList, i, 2, (search_results + i)->window);
		ListView_SetItemText(hList, i, 3, (search_results + i)->date);
	}
	if (search_count > 0) {
		ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(hList, 0, FALSE);
	}
	SendMessage(hList, WM_SETREDRAW, (WPARAM)TRUE, 0);
	InvalidateRect(hList, NULL, TRUE);

	hCount = GetDlgItem(hWnd, IDC_SEARCH_COUNT);
	if (search_count == 0) {
		SetWindowText(hCount, message_get_res(IDS_SEARCH_NONE));
	} else {
		lstrcpyn(fmt, message_get_res(IDS_SEARCH_COUNT), BUF_SIZE);
		wnsprintf(buf, BUF_SIZE, fmt, search_count);
		SetWindowText(hCount, buf);
	}
}

/*
 * search_selected - 選択中の項目を取得
 */
static DATA_INFO *search_selected(const HWND hWnd)
{
	HWND hList;
	LV_ITEM lvi;
	int index;
	int result_index;

	hList = GetDlgItem(hWnd, IDC_SEARCH_LIST);
	index = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
	if (index < 0 || search_results == NULL) {
		return NULL;
	}
	ZeroMemory(&lvi, sizeof(LV_ITEM));
	lvi.mask = LVIF_PARAM;
	lvi.iItem = index;
	if (ListView_GetItem(hList, &lvi) == FALSE) {
		return NULL;
	}
	result_index = (int)lvi.lParam;
	if (result_index < 0 || result_index >= search_count) {
		return NULL;
	}
	return (search_results + result_index)->item;
}

/*
 * search_wait_keys - 修飾キーが離されるまで待つ
 */
static void search_wait_keys(void)
{
	DWORD start;

	start = GetTickCount();
	while (GetAsyncKeyState(VK_MENU) < 0 || GetAsyncKeyState(VK_CONTROL) < 0 ||
		GetAsyncKeyState(VK_SHIFT) < 0) {
		if (GetTickCount() - start > 1500) {
			break;
		}
		Sleep(10);
	}
}

/*
 * search_item_alive - 項目が履歴またはテンプレートに残っているか
 */
static BOOL search_item_alive(DATA_INFO *item)
{
	if (item == NULL) {
		return FALSE;
	}
	if (data_check(&history_data, item) == NULL && data_check(&regist_data, item) == NULL) {
		return FALSE;
	}
	return TRUE;
}

/*
 * search_apply - 選択項目をクリップボードへ送る
 *
 *	mode: 0 チェックに従う / 1 コピーのみ / 2 貼り付け
 */
static void search_apply(const HWND hWnd, const int mode)
{
	DATA_INFO *item;
	HWND target;
	BOOL paste;

	item = search_selected(hWnd);
	if (search_item_alive(item) == FALSE) {
		search_execute(hWnd);
		return;
	}
	if (mode == 1) {
		paste = FALSE;
	} else if (mode == 2) {
		paste = TRUE;
	} else {
		paste = (IsDlgButtonChecked(hWnd, IDC_SEARCH_PASTE) == BST_CHECKED) ? TRUE : FALSE;
	}

	target = search_paste_target;
	if (paste == TRUE && target != NULL && IsWindow(target) != 0 && search_is_own_window(target) == FALSE) {
		ShowWindow(hWnd, SW_HIDE);
		search_wait_keys();
		SendMessage(search_main, WM_UNREGIST_HOTKEY, 0, 0);
		_SetForegroundWindow(target);
		SendMessage(search_main, WM_ITEM_TO_CLIPBOARD, 0, (LPARAM)item);
		sendkey_paste(target);
		SendMessage(search_main, WM_REGIST_HOTKEY, 0, 0);
		DestroyWindow(hWnd);
		return;
	}
	SendMessage(search_main, WM_ITEM_TO_CLIPBOARD, 1, (LPARAM)item);
}

/*
 * search_show_in_viewer - 選択項目をビューアで表示
 */
static void search_show_in_viewer(const HWND hWnd)
{
	DATA_INFO *item;
	HWND viewer;

	item = search_selected(hWnd);
	if (search_item_alive(item) == FALSE) {
		search_execute(hWnd);
		return;
	}
	SendMessage(search_main, WM_COMMAND, ID_MENUITEM_VIEWER, 0);
	viewer = (HWND)SendMessage(search_main, WM_VIEWER_GET_HWND, 0, 0);
	if (viewer != NULL) {
		SendMessage(viewer, WM_VIEWER_SELECT_ITEM, 0, (LPARAM)item);
		_SetForegroundWindow(viewer);
	}
}

/*
 * search_layout - コントロールを配置
 */
static void search_layout(const HWND hWnd)
{
	RECT rect;
	HWND hList;
	int margin;
	int gap;
	int label_w;
	int edit_h;
	int check_h;
	int btn_h;
	int btn_w;
	int list_top;
	int list_h;
	int y;
	int x;
	int list_w;

	SetDpiFromWindow(hWnd);
	GetClientRect(hWnd, &rect);
	margin = Scale(8);
	gap = Scale(6);
	label_w = Scale(56);
	edit_h = Scale(24);
	check_h = Scale(20);
	btn_h = Scale(28);

	y = margin;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_LABEL), margin, y + Scale(4), label_w, edit_h, TRUE);
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_EDIT), margin + label_w, y,
		rect.right - margin * 2 - label_w, edit_h, TRUE);

	y += edit_h + gap;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_PASTE), margin, y, (rect.right - margin * 2) / 2, check_h, TRUE);
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_COUNT), margin + (rect.right - margin * 2) / 2, y,
		(rect.right - margin * 2) / 2, check_h, TRUE);

	y += check_h + gap;
	list_top = y;
	btn_w = (rect.right - margin * 2 - gap * 3) / 4;
	if (btn_w < Scale(72)) {
		btn_w = Scale(72);
	}
	list_h = rect.bottom - margin - btn_h - gap - list_top;
	if (list_h < Scale(80)) {
		list_h = Scale(80);
	}
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_LIST), margin, list_top, rect.right - margin * 2, list_h, TRUE);

	x = margin;
	y = list_top + list_h + gap;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_PASTE_BTN), x, y, btn_w, btn_h, TRUE);
	x += btn_w + gap;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_COPY), x, y, btn_w, btn_h, TRUE);
	x += btn_w + gap;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_SHOW), x, y, btn_w, btn_h, TRUE);
	x += btn_w + gap;
	MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_CLOSE), x, y, btn_w, btn_h, TRUE);

	hList = GetDlgItem(hWnd, IDC_SEARCH_LIST);
	list_w = rect.right - margin * 2 - Scale(20);
	if (list_w < Scale(200)) {
		list_w = Scale(200);
	}
	ListView_SetColumnWidth(hList, 0, list_w * 46 / 100);
	ListView_SetColumnWidth(hList, 1, list_w * 14 / 100);
	ListView_SetColumnWidth(hList, 2, list_w * 22 / 100);
	ListView_SetColumnWidth(hList, 3, list_w * 18 / 100);
}

/*
 * search_set_texts - 表示文字列を設定
 */
static void search_set_texts(const HWND hWnd)
{
	HWND hList;
	LV_COLUMN lvc;
	TCHAR buf[BUF_SIZE];
	int i;
	int ids[4];

	SetWindowText(hWnd, message_get_res(IDS_SEARCH_TITLE));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_LABEL), message_get_res(IDS_SEARCH_FIND));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_PASTE), message_get_res(IDS_SEARCH_PASTE_CHECK));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_COPY), message_get_res(IDS_SEARCH_COPY));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_PASTE_BTN), message_get_res(IDS_SEARCH_PASTE));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_SHOW), message_get_res(IDS_SEARCH_SHOW));
	SetWindowText(GetDlgItem(hWnd, IDC_SEARCH_CLOSE), message_get_res(IDS_SEARCH_CLOSE));
	lstrcpyn(search_cue, message_get_res(IDS_SEARCH_CUE), BUF_SIZE);
	SendMessage(GetDlgItem(hWnd, IDC_SEARCH_EDIT), EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)search_cue);

	hList = GetDlgItem(hWnd, IDC_SEARCH_LIST);
	ids[0] = IDS_SEARCH_COLUMN_DATA;
	ids[1] = IDS_SEARCH_COLUMN_SOURCE;
	ids[2] = IDS_SEARCH_COLUMN_WINDOW;
	ids[3] = IDS_SEARCH_COLUMN_DATE;
	for (i = 0; i < 4; i++) {
		ZeroMemory(&lvc, sizeof(LV_COLUMN));
		lvc.mask = LVCF_TEXT | LVCF_WIDTH;
		lvc.cx = Scale(120);
		lstrcpyn(buf, message_get_res(ids[i]), BUF_SIZE);
		lvc.pszText = buf;
		ListView_InsertColumn(hList, i, &lvc);
	}
}

/*
 * search_subclass - Esc と Enter を処理するため子ウィンドウをサブクラス化
 */
static void search_subclass(const HWND hCtrl)
{
	WNDPROC org;

	org = (WNDPROC)GetWindowLongPtr(hCtrl, GWLP_WNDPROC);
	SetProp(hCtrl, SEARCH_PROP_PROC, (HANDLE)org);
	SetWindowLongPtr(hCtrl, GWLP_WNDPROC, (LONG_PTR)search_child_proc);
}

/*
 * search_child_proc - 子ウィンドウのキー処理
 */
static LRESULT CALLBACK search_child_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC org;

	org = (WNDPROC)GetProp(hWnd, SEARCH_PROP_PROC);
	if (msg == WM_KEYDOWN) {
		if (wParam == VK_ESCAPE) {
			SendMessage(GetParent(hWnd), WM_CLOSE, 0, 0);
			return 0;
		}
		if (wParam == VK_RETURN && GetDlgCtrlID(hWnd) == IDC_SEARCH_EDIT) {
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_SEARCH_APPLY, BN_CLICKED), (LPARAM)hWnd);
			return 0;
		}
		if (wParam == VK_DOWN && GetDlgCtrlID(hWnd) == IDC_SEARCH_EDIT) {
			SetFocus(GetDlgItem(GetParent(hWnd), IDC_SEARCH_LIST));
			return 0;
		}
	} else if (msg == WM_NCDESTROY) {
		RemoveProp(hWnd, SEARCH_PROP_PROC);
		if (org != NULL) {
			SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)org);
			return CallWindowProc(org, hWnd, msg, wParam, lParam);
		}
	}
	if (org == NULL) {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return CallWindowProc(org, hWnd, msg, wParam, lParam);
}

/*
 * search_proc - 検索ウィンドウのプロシージャ
 */
static LRESULT CALLBACK search_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HBRUSH brush;
	RECT *new_rect;

	switch (msg) {
	case WM_CREATE:
		search_font = search_create_font();
		CreateWindowEx(0, TEXT("STATIC"), TEXT(""),
			WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_LABEL, hInst, NULL);
		CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);
		CreateWindowEx(0, TEXT("BUTTON"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_PASTE, hInst, NULL);
		CreateWindowEx(0, TEXT("STATIC"), TEXT(""),
			WS_CHILD | WS_VISIBLE | SS_RIGHT,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_COUNT, hInst, NULL);
		CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_LIST, hInst, NULL);
		ListView_SetExtendedListViewStyle(GetDlgItem(hWnd, IDC_SEARCH_LIST),
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		CreateWindowEx(0, TEXT("BUTTON"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_PASTE_BTN, hInst, NULL);
		CreateWindowEx(0, TEXT("BUTTON"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_COPY, hInst, NULL);
		CreateWindowEx(0, TEXT("BUTTON"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_SHOW, hInst, NULL);
		CreateWindowEx(0, TEXT("BUTTON"), TEXT(""),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			0, 0, 0, 0, hWnd, (HMENU)IDC_SEARCH_CLOSE, hInst, NULL);
		if (search_font != NULL) {
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_LABEL), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_EDIT), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_PASTE), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_COUNT), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_LIST), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_PASTE_BTN), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_COPY), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_SHOW), WM_SETFONT, (WPARAM)search_font, FALSE);
			SendMessage(GetDlgItem(hWnd, IDC_SEARCH_CLOSE), WM_SETFONT, (WPARAM)search_font, FALSE);
		}
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_EDIT));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_PASTE));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_LIST));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_PASTE_BTN));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_COPY));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_SHOW));
		search_subclass(GetDlgItem(hWnd, IDC_SEARCH_CLOSE));
		search_set_texts(hWnd);
		dark_mode_set_window(hWnd);
		dark_mode_set_children(hWnd);
		search_layout(hWnd);
		search_execute(hWnd);
		return 0;

	case WM_SIZE:
		search_layout(hWnd);
		return 0;

	case WM_DPICHANGED:
		SetDpi(HIWORD(wParam));
		if (lParam != 0) {
			new_rect = (RECT *)lParam;
			SetWindowPos(hWnd, NULL, new_rect->left, new_rect->top,
				new_rect->right - new_rect->left, new_rect->bottom - new_rect->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		search_layout(hWnd);
		return 0;

	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = Scale(520);
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = Scale(320);
		return 0;

	case WM_TIMER:
		if (wParam == SEARCH_TIMER) {
			KillTimer(hWnd, SEARCH_TIMER);
			search_execute(hWnd);
		}
		return 0;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_SEARCH_EDIT:
			if (HIWORD(wParam) == EN_CHANGE) {
				SetTimer(hWnd, SEARCH_TIMER, 200, NULL);
			}
			break;

		case IDC_SEARCH_APPLY:
			search_apply(hWnd, 0);
			break;

		case IDC_SEARCH_COPY:
			search_apply(hWnd, 1);
			break;

		case IDC_SEARCH_PASTE_BTN:
			search_apply(hWnd, 2);
			break;

		case IDC_SEARCH_SHOW:
			search_show_in_viewer(hWnd);
			break;

		case IDC_SEARCH_CLOSE:
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
		return 0;

	case WM_NOTIFY:
		if (((NMHDR *)lParam)->idFrom == IDC_SEARCH_LIST) {
			if (((NMHDR *)lParam)->code == NM_DBLCLK || ((NMHDR *)lParam)->code == NM_RETURN) {
				search_apply(hWnd, 0);
			} else if (((NMHDR *)lParam)->code == LVN_KEYDOWN) {
				if (((NMLVKEYDOWN *)lParam)->wVKey == VK_RETURN) {
					search_apply(hWnd, 0);
				} else if (((NMLVKEYDOWN *)lParam)->wVKey == VK_ESCAPE) {
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}
			}
		}
		return 0;

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLORLISTBOX:
		if (dark_mode_ctl_color(msg, wParam, lParam, &brush) == TRUE) {
			return (LRESULT)brush;
		}
		break;

	case WM_SETTINGCHANGE:
	case WM_THEMECHANGED:
		if (dark_mode_is_color_change(msg, lParam) == TRUE) {
			dark_mode_update();
			dark_mode_set_window(hWnd);
			dark_mode_set_children(hWnd);
			dark_mode_refresh_window(hWnd);
		}
		break;

	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		search_wnd = NULL;
		if (search_font != NULL && search_font != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
			DeleteObject(search_font);
		}
		search_font = NULL;
		if (search_results != NULL) {
			mem_free(&search_results);
		}
		search_count = 0;
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

/*
 * search_regist - ウィンドウクラスの登録
 */
BOOL search_regist(const HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)search_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WINDOW_CLASS;
	return RegisterClass(&wc);
}

/*
 * search_show - 検索ウィンドウを表示
 */
void search_show(const HWND main_wnd, const BOOL paste)
{
	HWND foreground;
	RECT work;
	POINT pt;
	int width;
	int height;

	foreground = GetForegroundWindow();
	search_main = main_wnd;
	if (search_is_own_window(foreground) == FALSE) {
		search_paste_target = foreground;
	}

	if (search_wnd != NULL && IsWindow(search_wnd) != 0) {
		if (IsIconic(search_wnd) != 0) {
			ShowWindow(search_wnd, SW_RESTORE);
		} else {
			ShowWindow(search_wnd, SW_SHOW);
		}
		CheckDlgButton(search_wnd, IDC_SEARCH_PASTE, (paste == TRUE) ? BST_CHECKED : BST_UNCHECKED);
		search_execute(search_wnd);
		_SetForegroundWindow(search_wnd);
		SetFocus(GetDlgItem(search_wnd, IDC_SEARCH_EDIT));
		SendMessage(GetDlgItem(search_wnd, IDC_SEARCH_EDIT), EM_SETSEL, 0, -1);
		return;
	}

	pt.x = 0;
	pt.y = 0;
	if (search_paste_target != NULL && IsWindow(search_paste_target) != 0) {
		GetWindowRect(search_paste_target, &work);
		pt.x = (work.left + work.right) / 2;
		pt.y = (work.top + work.bottom) / 2;
	} else {
		GetCursorPos(&pt);
	}
	SetDpiFromPoint(pt);
	width = Scale(860);
	height = Scale(520);
	{
		MONITORINFO mi;
		HMONITOR mon;
		int x, y;

		mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
		mi.cbSize = sizeof(MONITORINFO);
		x = pt.x - width / 2;
		y = pt.y - height / 2;
		if (GetMonitorInfo(mon, &mi) != FALSE) {
			if (x < mi.rcWork.left) {
				x = mi.rcWork.left;
			}
			if (y < mi.rcWork.top) {
				y = mi.rcWork.top;
			}
			if (x + width > mi.rcWork.right) {
				x = mi.rcWork.right - width;
			}
			if (y + height > mi.rcWork.bottom) {
				y = mi.rcWork.bottom - height;
			}
		}
		search_wnd = CreateWindowEx(0, WINDOW_CLASS, message_get_res(IDS_SEARCH_TITLE),
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
			x, y, width, height,
			NULL, NULL, hInst, NULL);
	}
	if (search_wnd == NULL) {
		return;
	}
	CheckDlgButton(search_wnd, IDC_SEARCH_PASTE, (paste == TRUE) ? BST_CHECKED : BST_UNCHECKED);
	_SetForegroundWindow(search_wnd);
	SetFocus(GetDlgItem(search_wnd, IDC_SEARCH_EDIT));
}
/* End of source */
