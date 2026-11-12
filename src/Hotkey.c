/*
 * CLCL
 *
 * Hotkey.c
 *
 * Hotkey registration and conflict detection.
 *
 * RegisterHotKey fails when the combination is already taken, but it does
 * not say who owns it. The same combination can also be assigned twice
 * inside CLCL. This file separates those cases and reports them.
 *
 * A hotkey that did register still is not delivered while a higher-integrity
 * window is in front. The low-level keyboard hook covers that case.
 */

/* Include Files */
#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <tchar.h>
#include <stdarg.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#include "General.h"
#include "Memory.h"
#include "Message.h"
#include "Data.h"
#include "File.h"
#include "Hotkey.h"

/* Define */
#define HOTKEY_MOD_MASK					(MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)
#define HOTKEY_PROBE_ID					0xBFF0

/* Global Variables */
extern TCHAR work_path[];

static CRITICAL_SECTION hotkey_cs;
static volatile LONG hotkey_ready;
static volatile LONG slot_count_pub;
static HOTKEY_SLOT slot_buf[HOTKEY_SLOT_MAX];
static int slot_count;
static DWORD our_integrity;

/* Local Function Prototypes */
static void hotkey_init(void);
static void hotkey_lock(void);
static DWORD current_integrity(HANDLE process);
static BOOL foreground_blocks_hotkey(void);
static BOOL hotkey_same(const UINT mod1, const UINT vk1, const UINT mod2, const UINT vk2);
static BOOL hotkey_is_reserved(const UINT modifiers, const UINT virtkey);
static void hotkey_format_name(const UINT modifiers, const UINT virtkey, TCHAR *buf, const int cch);
static void hotkey_res_copy(const UINT id, TCHAR *dst, const int cch, const TCHAR *fallback);
static void hotkey_append(TCHAR *dst, const int cch, const TCHAR *src);
static void hotkey_append_fmt(TCHAR *dst, const int cch, const TCHAR *fmt, ...);
static void owner_action(const ACTION_INFO *ai, TCHAR *buf, const int cch);
static void owner_tool(const TOOL_INFO *ti, TCHAR *buf, const int cch);
static void owner_item(const DATA_INFO *di, TCHAR *buf, const int cch);

/*
 * current_integrity - mandatory integrity level of a process
 */
static DWORD current_integrity(HANDLE process)
{
	HANDLE token;
	DWORD len = 0;
	BYTE buf[256];
	PTOKEN_MANDATORY_LABEL label;
	DWORD rid = 0;

	if (process == NULL) {
		return 0;
	}
	if (OpenProcessToken(process, TOKEN_QUERY, &token) == FALSE) {
		return 0;
	}
	if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len) != FALSE) {
		label = (PTOKEN_MANDATORY_LABEL)buf;
		if (label->Label.Sid != NULL) {
			rid = *GetSidSubAuthority(label->Label.Sid,
				(DWORD)(*GetSidSubAuthorityCount(label->Label.Sid) - 1));
		}
	}
	CloseHandle(token);
	return rid;
}

/*
 * hotkey_init - one-time setup for the slot table
 */
static void hotkey_init(void)
{
	LONG state;

	state = InterlockedCompareExchange(&hotkey_ready, 1, 0);
	if (state == 2) {
		return;
	}
	if (state == 0) {
		InitializeCriticalSection(&hotkey_cs);
		our_integrity = current_integrity(GetCurrentProcess());
		if (our_integrity == 0) {
			our_integrity = SECURITY_MANDATORY_MEDIUM_RID;
		}
		InterlockedExchange(&hotkey_ready, 2);
		return;
	}
	while (hotkey_ready != 2) {
		Sleep(0);
	}
}

/*
 * hotkey_lock - enter the slot table lock
 */
static void hotkey_lock(void)
{
	hotkey_init();
	EnterCriticalSection(&hotkey_cs);
}

/*
 * hotkey_res_copy - copy a resource string, or a fallback if it is missing
 */
static void hotkey_res_copy(const UINT id, TCHAR *dst, const int cch, const TCHAR *fallback)
{
	const TCHAR *src;

	if (dst == NULL || cch <= 0) {
		return;
	}
	src = message_get_res(id);
	if (src == NULL || src[0] == TEXT('\0')) {
		src = (fallback != NULL) ? fallback : TEXT("");
	}
	lstrcpyn(dst, src, cch);
}

/*
 * hotkey_append - append text, stopping at the buffer end
 */
static void hotkey_append(TCHAR *dst, const int cch, const TCHAR *src)
{
	int n;

	if (dst == NULL || cch <= 1 || src == NULL || src[0] == TEXT('\0')) {
		return;
	}
	n = lstrlen(dst);
	if (n >= cch - 1) {
		return;
	}
	lstrcpyn(dst + n, src, cch - n);
}

/*
 * hotkey_append_fmt - append a formatted line
 */
static void hotkey_append_fmt(TCHAR *dst, const int cch, const TCHAR *fmt, ...)
{
	TCHAR tmp[512];
	va_list ap;

	if (fmt == NULL || fmt[0] == TEXT('\0')) {
		return;
	}
	va_start(ap, fmt);
	tmp[0] = TEXT('\0');
	wvnsprintf(tmp, sizeof(tmp) / sizeof(TCHAR), fmt, ap);
	va_end(ap);
	hotkey_append(dst, cch, tmp);
}

/*
 * hotkey_same - modifiers and virtual key match
 */
static BOOL hotkey_same(const UINT mod1, const UINT vk1, const UINT mod2, const UINT vk2)
{
	if (vk1 == 0 || vk2 == 0 || vk1 != vk2) {
		return FALSE;
	}
	return ((mod1 & HOTKEY_MOD_MASK) == (mod2 & HOTKEY_MOD_MASK)) ? TRUE : FALSE;
}

/*
 * hotkey_is_reserved - combinations Windows will not give to RegisterHotKey
 */
static BOOL hotkey_is_reserved(const UINT modifiers, const UINT virtkey)
{
	UINT mod = modifiers & HOTKEY_MOD_MASK;

	if (virtkey == VK_TAB && (mod == MOD_ALT || mod == (MOD_ALT | MOD_SHIFT))) {
		return TRUE;
	}
	if (virtkey == VK_ESCAPE &&
		(mod == MOD_ALT || mod == MOD_CONTROL || mod == (MOD_CONTROL | MOD_SHIFT))) {
		return TRUE;
	}
	if (virtkey == VK_DELETE && (mod & MOD_WIN) == 0 &&
		(mod & (MOD_CONTROL | MOD_ALT)) == (MOD_CONTROL | MOD_ALT)) {
		return TRUE;
	}
	if (virtkey == (UINT)'L' && mod == MOD_WIN) {
		return TRUE;
	}
	if (virtkey == VK_LWIN || virtkey == VK_RWIN ||
		virtkey == VK_SHIFT || virtkey == VK_LSHIFT || virtkey == VK_RSHIFT ||
		virtkey == VK_CONTROL || virtkey == VK_LCONTROL || virtkey == VK_RCONTROL ||
		virtkey == VK_MENU || virtkey == VK_LMENU || virtkey == VK_RMENU) {
		return TRUE;
	}
	return FALSE;
}

/*
 * hotkey_format_name - "Ctrl+Alt+C"
 */
static void hotkey_format_name(const UINT modifiers, const UINT virtkey, TCHAR *buf, const int cch)
{
	TCHAR tmp[128];
	TCHAR none[64];
	UINT scan_code;
	int ext_flag = 0;
	int len;

	if (buf == NULL || cch <= 0) {
		return;
	}
	buf[0] = TEXT('\0');
	if (virtkey == 0) {
		hotkey_res_copy(IDS_HOTKEY_NOKEY, none, 64, TEXT("(none)"));
		lstrcpyn(buf, none, cch);
		return;
	}
	tmp[0] = TEXT('\0');
	if (modifiers & MOD_CONTROL) {
		lstrcat(tmp, TEXT("Ctrl+"));
	}
	if (modifiers & MOD_SHIFT) {
		lstrcat(tmp, TEXT("Shift+"));
	}
	if (modifiers & MOD_ALT) {
		lstrcat(tmp, TEXT("Alt+"));
	}
	if (modifiers & MOD_WIN) {
		lstrcat(tmp, TEXT("Win+"));
	}
	len = lstrlen(tmp);
	if ((scan_code = MapVirtualKey(virtkey, 0)) != 0 && len < (int)(sizeof(tmp) / sizeof(TCHAR)) - 1) {
		if (virtkey == VK_APPS || virtkey == VK_PRIOR || virtkey == VK_NEXT ||
			virtkey == VK_END || virtkey == VK_HOME || virtkey == VK_LEFT ||
			virtkey == VK_UP || virtkey == VK_RIGHT || virtkey == VK_DOWN ||
			virtkey == VK_INSERT || virtkey == VK_DELETE || virtkey == VK_NUMLOCK) {
			ext_flag = 1 << 24;
		}
		GetKeyNameText((scan_code << 16) | ext_flag, tmp + len, (int)(sizeof(tmp) / sizeof(TCHAR)) - len);
	}
	lstrcpyn(buf, tmp, cch);
}

/*
 * owner_action - display name of an action hotkey
 */
static void owner_action(const ACTION_INFO *ai, TCHAR *buf, const int cch)
{
	TCHAR fmt[BUF_SIZE];
	TCHAR name[BUF_SIZE];
	UINT name_id;

	if (buf == NULL || cch <= 0) {
		return;
	}
	buf[0] = TEXT('\0');
	if (ai == NULL) {
		return;
	}
	switch (ai->action) {
	case ACTION_VIEWER:
		name_id = IDS_HOTKEY_ACT_VIEWER;
		break;
	case ACTION_OPTION:
		name_id = IDS_HOTKEY_ACT_OPTION;
		break;
	case ACTION_CLIPBOARD_WATCH:
		name_id = IDS_HOTKEY_ACT_WATCH;
		break;
	case ACTION_EXIT:
		name_id = IDS_HOTKEY_ACT_EXIT;
		break;
	case ACTION_SEARCH:
		name_id = IDS_HOTKEY_ACT_SEARCH;
		break;
	case ACTION_POPUPMEMU:
	default:
		name_id = IDS_HOTKEY_ACT_MENU;
		break;
	}
	hotkey_res_copy(name_id, name, BUF_SIZE, TEXT("Action"));
	hotkey_res_copy(IDS_HOTKEY_OWNER_ACTION, fmt, BUF_SIZE, TEXT("Action: %s"));
	wnsprintf(buf, cch, fmt, name);
}

/*
 * owner_tool - display name of a tool hotkey
 */
static void owner_tool(const TOOL_INFO *ti, TCHAR *buf, const int cch)
{
	TCHAR fmt[BUF_SIZE];
	TCHAR untitled[64];
	const TCHAR *title;

	if (buf == NULL || cch <= 0) {
		return;
	}
	buf[0] = TEXT('\0');
	hotkey_res_copy(IDS_HOTKEY_UNTITLED, untitled, 64, TEXT("(untitled)"));
	title = (ti != NULL && ti->title != NULL && ti->title[0] != TEXT('\0')) ? ti->title : untitled;
	hotkey_res_copy(IDS_HOTKEY_OWNER_TOOL, fmt, BUF_SIZE, TEXT("Tool: %s"));
	wnsprintf(buf, cch, fmt, title);
}

/*
 * owner_item - display name of a template hotkey
 */
static void owner_item(const DATA_INFO *di, TCHAR *buf, const int cch)
{
	TCHAR fmt[BUF_SIZE];
	TCHAR untitled[64];
	const TCHAR *title;

	if (buf == NULL || cch <= 0) {
		return;
	}
	buf[0] = TEXT('\0');
	hotkey_res_copy(IDS_HOTKEY_UNTITLED, untitled, 64, TEXT("(untitled)"));
	title = (di != NULL && di->title != NULL && di->title[0] != TEXT('\0')) ? di->title : untitled;
	hotkey_res_copy(IDS_HOTKEY_OWNER_TEMPLATE, fmt, BUF_SIZE, TEXT("Template: %s"));
	wnsprintf(buf, cch, fmt, title);
}

/*
 * hotkey_os_available - TRUE when this process can register the combination
 *
 * Caller must not already hold the same combination. RegisterHotKey cannot
 * tell our own registration apart from another program.
 */
static BOOL hotkey_os_available(const HWND hWnd, const UINT modifiers, const UINT virtkey)
{
	BOOL ok;

	if (hWnd == NULL || virtkey == 0) {
		return FALSE;
	}
	ok = RegisterHotKey(hWnd, HOTKEY_PROBE_ID, modifiers & HOTKEY_MOD_MASK, virtkey);
	if (ok == FALSE) {
		return FALSE;
	}
	UnregisterHotKey(hWnd, HOTKEY_PROBE_ID);
	return TRUE;
}

/*
 * hotkey_push_action - add an enabled action hotkey
 */
int hotkey_push_action(HOTKEY_BIND *list, int count, int max, const ACTION_INFO *ai)
{
	if (list == NULL || ai == NULL || count >= max) {
		return count;
	}
	if (ai->type != ACTION_TYPE_HOTKEY || ai->enable == 0 || ai->virtkey == 0) {
		return count;
	}
	list[count].modifiers = ai->modifiers & HOTKEY_MOD_MASK;
	list[count].virtkey = ai->virtkey;
	list[count].identity = ai;
	owner_action(ai, list[count].owner, HOTKEY_OWNER_CCH);
	return count + 1;
}

/*
 * hotkey_push_tool - add a tool hotkey that is invoked from the menu
 */
int hotkey_push_tool(HOTKEY_BIND *list, int count, int max, const TOOL_INFO *ti)
{
	if (list == NULL || ti == NULL || count >= max) {
		return count;
	}
	if ((ti->call_type & CALLTYPE_MENU) == 0 || ti->virtkey == 0) {
		return count;
	}
	list[count].modifiers = ti->modifiers & HOTKEY_MOD_MASK;
	list[count].virtkey = ti->virtkey;
	list[count].identity = ti;
	owner_tool(ti, list[count].owner, HOTKEY_OWNER_CCH);
	return count + 1;
}

/*
 * hotkey_push_items - add template hotkeys, including items in folders
 */
int hotkey_push_items(HOTKEY_BIND *list, int count, int max, DATA_INFO *di)
{
	DATA_INFO *cdi;

	if (list == NULL) {
		return count;
	}
	for (cdi = di; cdi != NULL && count < max; cdi = cdi->next) {
		switch (cdi->type) {
		case TYPE_FOLDER:
			count = hotkey_push_items(list, count, max, cdi->child);
			break;

		case TYPE_ITEM:
			if (cdi->op_virtkey == 0 || count >= max) {
				break;
			}
			list[count].modifiers = cdi->op_modifiers & HOTKEY_MOD_MASK;
			list[count].virtkey = cdi->op_virtkey;
			list[count].identity = cdi;
			owner_item(cdi, list[count].owner, HOTKEY_OWNER_CCH);
			count++;
			break;

		default:
			break;
		}
	}
	return count;
}

/*
 * hotkey_load_regist - read templates so the settings process can compare hotkeys
 */
DATA_INFO *hotkey_load_regist(void)
{
	TCHAR path[MAX_PATH];
	TCHAR err_str[BUF_SIZE];
	DATA_INFO *root = NULL;

	wnsprintf(path, MAX_PATH, TEXT("%s\\%s"), work_path, REGIST_FILENAME);
	err_str[0] = TEXT('\0');
	if (file_read_data(path, &root, err_str) == FALSE) {
		data_free(root);
		return NULL;
	}
	return root;
}

/*
 * hotkey_confirm - warn when the key is duplicated, reserved, or taken
 */
BOOL hotkey_confirm(const HWND hWnd, const UINT modifiers, const UINT virtkey,
	const HOTKEY_BIND *list, const int count, const void *self)
{
	TCHAR key[80];
	TCHAR fmt[BUF_SIZE];
	TCHAR msg[BUF_SIZE * 2];
	int i;
	UINT mod;

	if (virtkey == 0) {
		return TRUE;
	}
	mod = modifiers & HOTKEY_MOD_MASK;
	hotkey_format_name(mod, virtkey, key, 80);

	for (i = 0; i < count; i++) {
		if (self != NULL && list[i].identity == self) {
			continue;
		}
		if (hotkey_same(list[i].modifiers, list[i].virtkey, mod, virtkey) == FALSE) {
			continue;
		}
		hotkey_res_copy(IDS_HOTKEY_ASK_INTERNAL, fmt, BUF_SIZE,
			TEXT("%s conflicts with %s.\nThe later one will not work.\n\nUse it anyway?"));
		wnsprintf(msg, sizeof(msg) / sizeof(TCHAR), fmt, key, list[i].owner);
		if (MessageBox(hWnd, msg, TEXT("CLCL"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDNO) {
			return FALSE;
		}
		return TRUE;
	}

	if (hotkey_is_reserved(mod, virtkey) == TRUE) {
		hotkey_res_copy(IDS_HOTKEY_ASK_RESERVED, fmt, BUF_SIZE,
			TEXT("%s is reserved by Windows and cannot be registered.\n\nSave it anyway?"));
		wnsprintf(msg, sizeof(msg) / sizeof(TCHAR), fmt, key);
		if (MessageBox(hWnd, msg, TEXT("CLCL"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDNO) {
			return FALSE;
		}
		return TRUE;
	}

	if (hWnd != NULL && hotkey_os_available(hWnd, mod, virtkey) == FALSE) {
		hotkey_res_copy(IDS_HOTKEY_ASK_EXTERNAL, fmt, BUF_SIZE,
			TEXT("%s is already used by another program or by Windows, so it will not work.\n\nSave it anyway?"));
		wnsprintf(msg, sizeof(msg) / sizeof(TCHAR), fmt, key);
		if (MessageBox(hWnd, msg, TEXT("CLCL"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDNO) {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * foreground_blocks_hotkey - RegisterHotKey is not delivered to a lower integrity process
 */
static BOOL foreground_blocks_hotkey(void)
{
	HWND fg;
	DWORD pid = 0;
	HANDLE process;
	DWORD il;

	fg = GetForegroundWindow();
	if (fg == NULL) {
		return FALSE;
	}
	GetWindowThreadProcessId(fg, &pid);
	if (pid == 0 || pid == GetCurrentProcessId()) {
		return FALSE;
	}
	process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == NULL) {
		return (GetLastError() == ERROR_ACCESS_DENIED) ? TRUE : FALSE;
	}
	il = current_integrity(process);
	CloseHandle(process);
	if (il == 0 || our_integrity == 0) {
		return FALSE;
	}
	return (il > our_integrity) ? TRUE : FALSE;
}

/*
 * hotkey_has_slots - a hotkey was registered and the hook should stay installed
 */
BOOL hotkey_has_slots(void)
{
	return (slot_count_pub > 0) ? TRUE : FALSE;
}

/*
 * hotkey_clear_slots - drop the hook table before unregistering
 */
void hotkey_clear_slots(void)
{
	hotkey_lock();
	slot_count = 0;
	slot_count_pub = 0;
	LeaveCriticalSection(&hotkey_cs);
}

/*
 * hotkey_publish_slots - publish the combinations the hook may synthesize
 */
static void hotkey_publish_slots(const HOTKEY_SLOT *slots, int count)
{
	int i;

	hotkey_lock();
	if (slots == NULL || count <= 0) {
		slot_count = 0;
		slot_count_pub = 0;
		LeaveCriticalSection(&hotkey_cs);
		return;
	}
	if (count > HOTKEY_SLOT_MAX) {
		count = HOTKEY_SLOT_MAX;
	}
	for (i = 0; i < count; i++) {
		slot_buf[i] = slots[i];
	}
	slot_count = count;
	slot_count_pub = count;
	LeaveCriticalSection(&hotkey_cs);
}

/*
 * hotkey_hook_match - TRUE when the hook should post WM_HOTKEY and eat the key
 *
 * Only higher-integrity foreground windows need this. Otherwise RegisterHotKey
 * already posts WM_HOTKEY, and eating the key here would run the action twice.
 */
BOOL hotkey_hook_match(const KBDLLHOOKSTRUCT *kb, int *id)
{
	HOTKEY_SLOT local[HOTKEY_SLOT_MAX];
	int count;
	int i;
	UINT mod;
	UINT vk;

	if (id == NULL || kb == NULL) {
		return FALSE;
	}
	if ((kb->flags & (LLKHF_UP | LLKHF_INJECTED)) != 0) {
		return FALSE;
	}
	if (slot_count_pub <= 0) {
		return FALSE;
	}
	vk = (UINT)kb->vkCode;
	if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
		vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
		vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
		vk == VK_LWIN || vk == VK_RWIN) {
		return FALSE;
	}

	hotkey_lock();
	count = slot_count;
	if (count > HOTKEY_SLOT_MAX) {
		count = HOTKEY_SLOT_MAX;
	}
	for (i = 0; i < count; i++) {
		local[i] = slot_buf[i];
	}
	LeaveCriticalSection(&hotkey_cs);

	mod = 0;
	if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0) {
		mod |= MOD_SHIFT;
	}
	if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0) {
		mod |= MOD_CONTROL;
	}
	if ((kb->flags & LLKHF_ALTDOWN) != 0 ||
		(GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_RMENU) & 0x8000) != 0) {
		mod |= MOD_ALT;
	}
	if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) {
		mod |= MOD_WIN;
	}

	for (i = 0; i < count; i++) {
		if ((local[i].modifiers & HOTKEY_MOD_MASK) == mod && local[i].virtkey == vk) {
			if (foreground_blocks_hotkey() == FALSE) {
				return FALSE;
			}
			*id = local[i].id;
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * consider_hotkey - register one combination or record why it cannot be registered
 */
static BOOL consider_hotkey(const HWND hWnd, const UINT modifiers, const UINT virtkey, const int id,
	const TCHAR *owner, HOTKEY_BIND *seen, int *seen_n, HOTKEY_SLOT *slots, int *slot_n,
	TCHAR *body, const int body_cch, int *fails)
{
	TCHAR key[80];
	TCHAR fmt[BUF_SIZE];
	UINT mod;
	DWORD err;
	int i;

	mod = modifiers & HOTKEY_MOD_MASK;
	hotkey_format_name(mod, virtkey, key, 80);
	if (virtkey == 0) {
		hotkey_res_copy(IDS_HOTKEY_INVALID, fmt, BUF_SIZE, TEXT("%s\n    No key is assigned.\n"));
		hotkey_append_fmt(body, body_cch, fmt, owner);
		(*fails)++;
		return FALSE;
	}
	for (i = 0; i < *seen_n; i++) {
		if (hotkey_same(seen[i].modifiers, seen[i].virtkey, mod, virtkey) == TRUE) {
			hotkey_res_copy(IDS_HOTKEY_INTERNAL, fmt, BUF_SIZE, TEXT("%s  %s\n    Conflicts with %s.\n"));
			hotkey_append_fmt(body, body_cch, fmt, key, owner, seen[i].owner);
			(*fails)++;
			return FALSE;
		}
	}
	if (hotkey_is_reserved(mod, virtkey) == TRUE) {
		hotkey_res_copy(IDS_HOTKEY_RESERVED, fmt, BUF_SIZE,
			TEXT("%s  %s\n    This combination is reserved by Windows.\n"));
		hotkey_append_fmt(body, body_cch, fmt, key, owner);
		(*fails)++;
		return FALSE;
	}
	if (RegisterHotKey(hWnd, id, mod, virtkey) == FALSE) {
		err = GetLastError();
		if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
			hotkey_res_copy(IDS_HOTKEY_EXTERNAL, fmt, BUF_SIZE,
				TEXT("%s  %s\n    Already used by another program or by Windows.\n"));
			hotkey_append_fmt(body, body_cch, fmt, key, owner);
		} else {
			hotkey_res_copy(IDS_HOTKEY_FAIL, fmt, BUF_SIZE,
				TEXT("%s  %s\n    Registration failed (error %u).\n"));
			hotkey_append_fmt(body, body_cch, fmt, key, owner, (unsigned)err);
		}
		(*fails)++;
		return FALSE;
	}
	if (*seen_n < HOTKEY_BIND_MAX) {
		seen[*seen_n].modifiers = mod;
		seen[*seen_n].virtkey = virtkey;
		seen[*seen_n].identity = NULL;
		lstrcpyn(seen[*seen_n].owner, (owner != NULL) ? owner : TEXT(""), HOTKEY_OWNER_CCH);
		(*seen_n)++;
	}
	if (*slot_n < HOTKEY_SLOT_MAX) {
		slots[*slot_n].modifiers = mod;
		slots[*slot_n].virtkey = virtkey;
		slots[*slot_n].id = id;
		(*slot_n)++;
	}
	return TRUE;
}

/*
 * register_items - register template hotkeys
 */
static void register_items(const HWND hWnd, DATA_INFO *di, int *id,
	HOTKEY_BIND *seen, int *seen_n, HOTKEY_SLOT *slots, int *slot_n,
	TCHAR *body, const int body_cch, int *fails)
{
	DATA_INFO *cdi;
	TCHAR owner[HOTKEY_OWNER_CCH];

	for (cdi = di; cdi != NULL; cdi = cdi->next) {
		switch (cdi->type) {
		case TYPE_FOLDER:
			register_items(hWnd, cdi->child, id, seen, seen_n, slots, slot_n, body, body_cch, fails);
			break;

		case TYPE_ITEM:
			cdi->hkey_id = 0;
			if (cdi->op_virtkey == 0) {
				break;
			}
			owner_item(cdi, owner, HOTKEY_OWNER_CCH);
			if (consider_hotkey(hWnd, cdi->op_modifiers, cdi->op_virtkey, *id, owner,
				seen, seen_n, slots, slot_n, body, body_cch, fails) == TRUE) {
				cdi->hkey_id = *id;
			}
			(*id)++;
			break;

		default:
			break;
		}
	}
}

/*
 * hotkey_register_all - register actions, tools and templates
 */
int hotkey_register_all(const HWND hWnd, OPTION_INFO *opt, DATA_INFO *regist,
	TCHAR *report, int report_cch)
{
	HOTKEY_BIND seen[HOTKEY_BIND_MAX];
	HOTKEY_SLOT slots[HOTKEY_SLOT_MAX];
	TCHAR body[3072];
	TCHAR owner[HOTKEY_OWNER_CCH];
	TCHAR head[BUF_SIZE];
	TCHAR note[BUF_SIZE];
	int seen_n = 0;
	int slot_n = 0;
	int fails = 0;
	int id;
	int i;

	body[0] = TEXT('\0');
	if (report != NULL && report_cch > 0) {
		report[0] = TEXT('\0');
	}
	if (opt == NULL) {
		hotkey_publish_slots(NULL, 0);
		return 0;
	}

	if (opt->action_info != NULL) {
		for (i = 0; i < opt->action_cnt; i++) {
			ACTION_INFO *ai = opt->action_info + i;

			if (ai->type != ACTION_TYPE_HOTKEY || ai->enable == 0) {
				continue;
			}
			if (ai->id == 0) {
				ai->id = HKEY_ID + i;
			}
			owner_action(ai, owner, HOTKEY_OWNER_CCH);
			consider_hotkey(hWnd, ai->modifiers, ai->virtkey, ai->id, owner,
				seen, &seen_n, slots, &slot_n, body, sizeof(body) / sizeof(TCHAR), &fails);
		}
	}
	if (opt->tool_info != NULL) {
		for (i = 0; i < opt->tool_cnt; i++) {
			TOOL_INFO *ti = opt->tool_info + i;

			if ((ti->call_type & CALLTYPE_MENU) == 0 || ti->virtkey == 0) {
				continue;
			}
			if (ti->id == 0) {
				ti->id = HKEY_ID + opt->action_cnt + i;
			}
			owner_tool(ti, owner, HOTKEY_OWNER_CCH);
			consider_hotkey(hWnd, ti->modifiers, ti->virtkey, ti->id, owner,
				seen, &seen_n, slots, &slot_n, body, sizeof(body) / sizeof(TCHAR), &fails);
		}
	}
	id = HKEY_ID + opt->action_cnt + opt->tool_cnt;
	register_items(hWnd, regist, &id, seen, &seen_n, slots, &slot_n,
		body, sizeof(body) / sizeof(TCHAR), &fails);

	hotkey_publish_slots(slots, slot_n);

	if (fails > 0 && report != NULL && report_cch > 0) {
		hotkey_res_copy(IDS_HOTKEY_HEAD, head, BUF_SIZE, TEXT("These hotkeys could not be registered:\n"));
		hotkey_append(report, report_cch, head);
		hotkey_append(report, report_cch, body);
		if (our_integrity < SECURITY_MANDATORY_HIGH_RID) {
			hotkey_res_copy(IDS_HOTKEY_ELEVATION, note, BUF_SIZE, TEXT(""));
			hotkey_append(report, report_cch, note);
		}
	}
	return fails;
}
/* End of source */
