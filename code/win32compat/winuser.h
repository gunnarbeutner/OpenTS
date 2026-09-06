/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window manager, dialogs, messages, and input of <winuser.h>.

#pragma once

#include "windef.h"
#include "wingdi.h"

// Callback signatures.
typedef LRESULT (CALLBACK * WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (CALLBACK * DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (CALLBACK * TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef BOOL (CALLBACK * WNDENUMPROC)(HWND, LPARAM);
typedef struct tagMSG {
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
} MSG, * PMSG, * LPMSG;
typedef struct tagPAINTSTRUCT {
	HDC hdc;
	BOOL fErase;
	RECT rcPaint;
	BOOL fRestore;
	BOOL fIncUpdate;
	BYTE rgbReserved[32];
} PAINTSTRUCT, * LPPAINTSTRUCT;

typedef struct tagWNDCLASSA {
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
} WNDCLASSA, WNDCLASS, * LPWNDCLASSA, * LPWNDCLASS;

typedef struct tagWNDCLASSEXA {
	UINT cbSize;
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
	HICON hIconSm;
} WNDCLASSEXA, WNDCLASSEX, * LPWNDCLASSEXA, * LPWNDCLASSEX;

// ownrdraw.cpp reads the creation parameter as *(HWND *)lParam, which only
// matches lpCreateParams while that member stays first.
typedef struct tagCREATESTRUCTA {
	LPVOID lpCreateParams;
	HINSTANCE hInstance;
	HMENU hMenu;
	HWND hwndParent;
	int cy;
	int cx;
	int y;
	int x;
	LONG style;
	LPCSTR lpszName;
	LPCSTR lpszClass;
	DWORD dwExStyle;
} CREATESTRUCTA, CREATESTRUCT, * LPCREATESTRUCTA, * LPCREATESTRUCT;

typedef struct tagDRAWITEMSTRUCT {
	UINT CtlType;
	UINT CtlID;
	UINT itemID;
	UINT itemAction;
	UINT itemState;
	HWND hwndItem;
	HDC hDC;
	RECT rcItem;
	ULONG_PTR itemData;
} DRAWITEMSTRUCT, * LPDRAWITEMSTRUCT;

typedef struct tagMEASUREITEMSTRUCT {
	UINT CtlType;
	UINT CtlID;
	UINT itemID;
	UINT itemWidth;
	UINT itemHeight;
	ULONG_PTR itemData;
} MEASUREITEMSTRUCT, * LPMEASUREITEMSTRUCT;
#define MB_OK					0x00000000L
#define MB_OKCANCEL				0x00000001L
#define MB_ABORTRETRYIGNORE		0x00000002L
#define MB_YESNOCANCEL			0x00000003L
#define MB_YESNO				0x00000004L
#define MB_RETRYCANCEL			0x00000005L
#define MB_ICONHAND				0x00000010L
#define MB_ICONQUESTION			0x00000020L
#define MB_ICONEXCLAMATION		0x00000030L
#define MB_ICONASTERISK			0x00000040L
#define MB_ICONERROR			MB_ICONHAND
#define MB_ICONSTOP				MB_ICONHAND
#define MB_ICONWARNING			MB_ICONEXCLAMATION
#define MB_ICONINFORMATION		MB_ICONASTERISK
#define MB_DEFBUTTON1			0x00000000L
#define MB_DEFBUTTON2			0x00000100L
#define MB_SYSTEMMODAL			0x00001000L
#define MB_TASKMODAL			0x00002000L
#define MB_TOPMOST				0x00040000L
#define MB_SETFOREGROUND		0x00010000L

#define IDOK		1
#define IDCANCEL	2
#define IDABORT		3
#define IDRETRY		4
#define IDIGNORE	5
#define IDYES		6
#define IDNO		7

#define SW_HIDE				0
#define SW_SHOWNORMAL		1
#define SW_NORMAL			1
#define SW_SHOWMINIMIZED	2
#define SW_SHOWMAXIMIZED	3
#define SW_MAXIMIZE			3
#define SW_SHOWNOACTIVATE	4
#define SW_SHOW				5
#define SW_MINIMIZE			6
#define SW_SHOWMINNOACTIVE	7
#define SW_SHOWNA			8
#define SW_RESTORE			9
#define SW_SHOWDEFAULT		10

#define GWL_WNDPROC		(-4)
#define GWL_HINSTANCE	(-6)
#define GWL_HWNDPARENT	(-8)
#define GWL_STYLE		(-16)
#define GWL_EXSTYLE		(-20)
#define GWL_USERDATA	(-21)
#define GWL_ID			(-12)
#define GWLP_WNDPROC	(-4)
#define GWLP_USERDATA	(-21)
#define GWLP_HINSTANCE	(-6)
#define GWLP_HWNDPARENT	(-8)
#define GWLP_ID			(-12)
#define DWL_MSGRESULT	0
#define DWL_DLGPROC		4
#define DWL_USER		8
#define DWLP_MSGRESULT	0
#define DWLP_DLGPROC	(DWLP_MSGRESULT + (int)sizeof(LRESULT))
#define DWLP_USER		(DWLP_DLGPROC + (int)sizeof(DLGPROC))

#define SM_CXSCREEN			0
#define SM_CYSCREEN			1
#define SM_CXVSCROLL		2
#define SM_CYHSCROLL		3
#define SM_CYCAPTION		4
#define SM_CXBORDER			5
#define SM_CYBORDER			6
#define SM_CXFIXEDFRAME		7
#define SM_CYFIXEDFRAME		8
#define SM_CXFULLSCREEN		16
#define SM_CYFULLSCREEN		17
#define SM_CXSIZEFRAME		32
#define SM_CYSIZEFRAME		33
#define SM_CMOUSEBUTTONS	43

#define SWP_NOSIZE			0x0001
#define SWP_NOMOVE			0x0002
#define SWP_NOZORDER		0x0004
#define SWP_NOREDRAW		0x0008
#define SWP_NOACTIVATE		0x0010
#define SWP_SHOWWINDOW		0x0040
#define SWP_HIDEWINDOW		0x0080
#define SWP_FRAMECHANGED	0x0020

#define HWND_TOP		((HWND)0)
#define HWND_BOTTOM		((HWND)1)
#define HWND_TOPMOST	((HWND)-1)
#define HWND_NOTOPMOST	((HWND)-2)
#define HWND_DESKTOP	((HWND)0)
#define HWND_BROADCAST	((HWND)0xffff)

#define PM_NOREMOVE		0x0000
#define PM_REMOVE		0x0001
#define PM_NOYIELD		0x0002

#define CS_VREDRAW		0x0001
#define CS_HREDRAW		0x0002
#define CS_DBLCLKS		0x0008
#define CS_OWNDC		0x0020
#define CS_CLASSDC		0x0040
#define CS_SAVEBITS		0x0800

#define IDC_ARROW		((LPCSTR)32512)
#define IDC_IBEAM		((LPCSTR)32513)
#define IDC_WAIT		((LPCSTR)32514)
#define IDC_CROSS		((LPCSTR)32515)
#define IDI_APPLICATION	((LPCSTR)32512)
#define IDI_INFORMATION	((LPCSTR)32516)

#define MK_LBUTTON		0x0001
#define MK_RBUTTON		0x0002
#define MK_SHIFT		0x0004
#define MK_CONTROL		0x0008
#define MK_MBUTTON		0x0010

#define WM_NULL			0x0000
#define WM_CREATE		0x0001
#define WM_DESTROY		0x0002
#define WM_MOVE			0x0003
#define WM_SIZE			0x0005
#define WM_ACTIVATE		0x0006
#define WM_SETFOCUS		0x0007
#define WM_KILLFOCUS	0x0008
#define WM_ENABLE		0x000A
#define WM_SETREDRAW	0x000B
#define WM_SETTEXT		0x000C
#define WM_GETTEXT		0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT		0x000F
#define WM_CLOSE		0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT			0x0012
#define WM_ERASEBKGND	0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW	0x0018
#define WM_ACTIVATEAPP	0x001C
#define WM_SETCURSOR	0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT		0x0030
#define WM_GETFONT		0x0031
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE		0x0081
#define WM_NCDESTROY	0x0082
#define WM_NCHITTEST	0x0084
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_KEYFIRST		0x0100
#define WM_KEYDOWN		0x0100
#define WM_KEYUP		0x0101
#define WM_CHAR			0x0102
#define WM_DEADCHAR		0x0103
#define WM_SYSKEYDOWN	0x0104
#define WM_SYSKEYUP		0x0105
#define WM_SYSCHAR		0x0106
#define WM_KEYLAST		0x0108
#define WM_INITDIALOG	0x0110
#define WM_COMMAND		0x0111
#define WM_SYSCOMMAND	0x0112
#define WM_TIMER		0x0113
#define WM_HSCROLL		0x0114
#define WM_VSCROLL		0x0115
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT	0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN	0x0135
#define WM_CTLCOLORDLG	0x0136
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST	0x0200
#define WM_MOUSEMOVE	0x0200
#define WM_LBUTTONDOWN	0x0201
#define WM_LBUTTONUP	0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN	0x0204
#define WM_RBUTTONUP	0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN	0x0207
#define WM_MBUTTONUP	0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL	0x020A
#define WM_MOUSELAST	0x020A
#define WM_MOVING		0x0216
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE	0x0232
#define WM_DRAWITEM		0x002B
#define WM_MEASUREITEM	0x002C
#define WM_DELETEITEM	0x002D
#define WM_VKEYTOITEM	0x002E
#define WM_CHARTOITEM	0x002F
#define WM_DISPLAYCHANGE 0x007E
#define WM_USER			0x0400
#define WM_APP			0x8000

#define WS_OVERLAPPED	0x00000000L
#define WS_POPUP		0x80000000L
#define WS_CHILD		0x40000000L
#define WS_MINIMIZE		0x20000000L
#define WS_VISIBLE		0x10000000L
#define WS_DISABLED		0x08000000L
#define WS_CLIPSIBLINGS	0x04000000L
#define WS_CLIPCHILDREN	0x02000000L
#define WS_MAXIMIZE		0x01000000L
#define WS_CAPTION		0x00C00000L
#define WS_BORDER		0x00800000L
#define WS_DLGFRAME		0x00400000L
#define WS_VSCROLL		0x00200000L
#define WS_HSCROLL		0x00100000L
#define WS_SYSMENU		0x00080000L
#define WS_THICKFRAME	0x00040000L
#define WS_GROUP		0x00020000L
#define WS_TABSTOP		0x00010000L
#define WS_MINIMIZEBOX	0x00020000L
#define WS_MAXIMIZEBOX	0x00010000L
#define WS_OVERLAPPEDWINDOW \
	(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW	(WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST	0x00000008L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_CLIENTEDGE 0x00000200L

#define CW_USEDEFAULT	((int)0x80000000)
#define BM_GETCHECK		0x00F0
#define BM_SETCHECK		0x00F1
#define BM_GETSTATE		0x00F2
#define BM_SETSTATE		0x00F3
#define BST_UNCHECKED	0x0000
#define BST_CHECKED		0x0001

#define LB_ADDSTRING		0x0180
#define LB_INSERTSTRING		0x0181
#define LB_DELETESTRING		0x0182
#define LB_RESETCONTENT		0x0184
#define LB_SETSEL			0x0185
#define LB_SETCURSEL		0x0186
#define LB_GETSEL			0x0187
#define LB_GETCURSEL		0x0188
#define LB_GETTEXT			0x0189
#define LB_GETTEXTLEN		0x018A
#define LB_GETCOUNT			0x018B
#define LB_SELECTSTRING		0x018C
#define LB_GETTOPINDEX		0x018E
#define LB_FINDSTRING		0x018F
#define LB_SETITEMDATA		0x019A
#define LB_GETITEMDATA		0x0199
#define LB_SETTOPINDEX		0x0197
#define LB_ERR				(-1)
#define LB_ERRSPACE			(-2)

#define CB_ADDSTRING		0x0143
#define CB_RESETCONTENT		0x014B
#define CB_SETCURSEL		0x014E
#define CB_GETCURSEL		0x0147
#define CB_ERR				(-1)

#define SB_LINEUP		0
#define SB_LINEDOWN		1
#define SB_PAGEUP		2
#define SB_PAGEDOWN		3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK	5
#define SB_TOP			6
#define SB_BOTTOM		7
#define SB_ENDSCROLL	8
// Windows keeps one caret, owned by the focused window and blinked by the
// system; ShowCaret and HideCaret count rather than toggle.
BOOL CreateCaret(HWND window, HBITMAP bitmap, int width, int height);
BOOL DestroyCaret(void);
BOOL SetCaretPos(int x, int y);
BOOL ShowCaret(HWND window);
BOOL HideCaret(HWND window);
UINT GetCaretBlinkTime(void);
int wsprintfA(LPSTR output, LPCSTR format, ...) __attribute__((format(printf, 2, 3)));
#define wsprintf	wsprintfA
/* Rectangles are pure arithmetic on the structure, so they are implemented. */
inline BOOL SetRect(LPRECT rect, int left, int top, int right, int bottom)
{
	if (rect == nullptr) return(FALSE);
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
	return(TRUE);
}


inline BOOL SetRectEmpty(LPRECT rect) { return(SetRect(rect, 0, 0, 0, 0)); }


inline BOOL OffsetRect(LPRECT rect, int dx, int dy)
{
	if (rect == nullptr) return(FALSE);
	rect->left += dx;
	rect->right += dx;
	rect->top += dy;
	rect->bottom += dy;
	return(TRUE);
}


inline BOOL InflateRect(LPRECT rect, int dx, int dy)
{
	if (rect == nullptr) return(FALSE);
	rect->left -= dx;
	rect->right += dx;
	rect->top -= dy;
	rect->bottom += dy;
	return(TRUE);
}


inline BOOL IsRectEmpty(LPCRECT rect)
{
	if (rect == nullptr) return(TRUE);
	return(rect->left >= rect->right || rect->top >= rect->bottom);
}


inline BOOL PtInRect(LPCRECT rect, POINT point)
{
	if (rect == nullptr) return(FALSE);
	return(point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom);
}


inline BOOL EqualRect(LPCRECT left, LPCRECT right)
{
	return(left->left == right->left && left->top == right->top
		&& left->right == right->right && left->bottom == right->bottom);
}


inline BOOL IntersectRect(LPRECT destination, LPCRECT left, LPCRECT right)
{
	destination->left = left->left > right->left ? left->left : right->left;
	destination->top = left->top > right->top ? left->top : right->top;
	destination->right = left->right < right->right ? left->right : right->right;
	destination->bottom = left->bottom < right->bottom ? left->bottom : right->bottom;

	if (IsRectEmpty(destination)) {
		SetRectEmpty(destination);
		return(FALSE);
	}
	return(TRUE);
}


inline BOOL UnionRect(LPRECT destination, LPCRECT left, LPCRECT right)
{
	if (IsRectEmpty(left)) { *destination = *right; return(!IsRectEmpty(destination)); }
	if (IsRectEmpty(right)) { *destination = *left; return(!IsRectEmpty(destination)); }

	destination->left = left->left < right->left ? left->left : right->left;
	destination->top = left->top < right->top ? left->top : right->top;
	destination->right = left->right > right->right ? left->right : right->right;
	destination->bottom = left->bottom > right->bottom ? left->bottom : right->bottom;
	return(TRUE);
}


inline BOOL CopyRect(LPRECT destination, LPCRECT source) { *destination = *source; return(TRUE); }
/* Windows, messages, and input. */
LRESULT SendMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PostMessageA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PeekMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax, UINT remove);
BOOL GetMessageA(LPMSG message, HWND window, UINT filtermin, UINT filtermax);
BOOL TranslateMessage(MSG const * message);
LRESULT DispatchMessageA(MSG const * message);
void PostQuitMessage(int exitcode);
ATOM RegisterClassA(WNDCLASSA const * windowclass);
ATOM RegisterClassExA(WNDCLASSEXA const * windowclass);
BOOL UnregisterClassA(LPCSTR classname, HINSTANCE instance);
HWND CreateWindowExA(DWORD exstyle, LPCSTR classname, LPCSTR windowname, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param);
BOOL DestroyWindow(HWND window);
BOOL ShowWindow(HWND window, int command);
BOOL UpdateWindow(HWND window);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL SetWindowPos(HWND window, HWND insertafter, int x, int y, int cx, int cy, UINT flags);
BOOL GetWindowRect(HWND window, LPRECT rect);
BOOL GetClientRect(HWND window, LPRECT rect);
BOOL ClientToScreen(HWND window, LPPOINT point);
BOOL ScreenToClient(HWND window, LPPOINT point);
BOOL AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu);
BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exstyle);
BOOL InvalidateRect(HWND window, RECT const * rect, BOOL erase);
BOOL ValidateRect(HWND window, RECT const * rect);
HDC BeginPaint(HWND window, LPPAINTSTRUCT paint);
BOOL EndPaint(HWND window, PAINTSTRUCT const * paint);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
int FillRect(HDC dc, RECT const * rect, HBRUSH brush);
LONG GetWindowLongA(HWND window, int index);
LONG SetWindowLongA(HWND window, int index, LONG value);
LONG_PTR GetWindowLongPtrA(HWND window, int index);
LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value);
BOOL SetWindowTextA(HWND window, LPCSTR text);
int GetWindowTextA(HWND window, LPSTR text, int count);
int GetWindowTextLengthA(HWND window);
BOOL EnableWindow(HWND window, BOOL enable);
BOOL IsWindow(HWND window);
BOOL IsWindowVisible(HWND window);
BOOL IsIconic(HWND window);
HWND SetFocus(HWND window);
HWND GetFocus(void);
HWND SetCapture(HWND window);
BOOL ReleaseCapture(void);
HWND GetActiveWindow(void);
HWND SetActiveWindow(HWND window);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND window);
BOOL BringWindowToTop(HWND window);
HWND GetDesktopWindow(void);
HWND FindWindowA(LPCSTR classname, LPCSTR windowname);
HWND GetParent(HWND window);
HWND GetDlgItem(HWND dialog, int id);
LRESULT SendDlgItemMessageA(HWND dialog, int id, UINT message, WPARAM wparam, LPARAM lparam);
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC callback);
BOOL KillTimer(HWND window, UINT_PTR id);
int MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type);
int GetSystemMetrics(int index);
HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name);
HICON LoadIconA(HINSTANCE instance, LPCSTR name);
HCURSOR SetCursor(HCURSOR cursor);
int ShowCursor(BOOL show);
BOOL GetCursorPos(LPPOINT point);
BOOL SetCursorPos(int x, int y);
BOOL ClipCursor(RECT const * rect);
SHORT GetKeyState(int key);
SHORT GetAsyncKeyState(int key);
BOOL GetKeyboardState(PBYTE state);
UINT MapVirtualKeyA(UINT code, UINT maptype);
#define SendMessage			SendMessageA
#define PostMessage			PostMessageA
#define DefWindowProc		DefWindowProcA
#define PeekMessage			PeekMessageA
#define GetMessage			GetMessageA
#define DispatchMessage		DispatchMessageA
#define RegisterClass		RegisterClassA
#define RegisterClassEx		RegisterClassExA
#define UnregisterClass		UnregisterClassA
#define CreateWindowEx		CreateWindowExA
#define GetWindowLong		GetWindowLongA
#define SetWindowLong		SetWindowLongA
#define GetWindowLongPtr	GetWindowLongPtrA
#define SetWindowLongPtr	SetWindowLongPtrA
#define SetWindowText		SetWindowTextA
#define GetWindowText		GetWindowTextA
#define GetWindowTextLength	GetWindowTextLengthA
#define FindWindow			FindWindowA
#define SendDlgItemMessage	SendDlgItemMessageA
#define MessageBox			MessageBoxA
#define LoadCursor			LoadCursorA
#define LoadIcon			LoadIconA
#define MapVirtualKey		MapVirtualKeyA
// Window manager.
#define GW_HWNDFIRST	0
#define GW_HWNDLAST		1
#define GW_HWNDNEXT		2
#define GW_HWNDPREV		3
#define GW_OWNER		4
#define GW_CHILD		5

#define RDW_INVALIDATE		0x0001
#define RDW_INTERNALPAINT	0x0002
#define RDW_ERASE			0x0004
#define RDW_VALIDATE		0x0008
#define RDW_UPDATENOW		0x0100
#define RDW_ERASENOW		0x0200
#define RDW_FRAME			0x0400
#define RDW_ALLCHILDREN		0x0080

#define WM_XBUTTONDOWN		0x020B
#define WM_XBUTTONUP		0x020C
#define WM_XBUTTONDBLCLK	0x020D
#define WM_NCMOUSEMOVE		0x00A0
#define WM_CANCELMODE		0x001F
#define WM_ENTERMENULOOP	0x0211
#define WM_EXITMENULOOP		0x0212

#define SM_SWAPBUTTON		23
#define SM_CXDOUBLECLK		36
#define SM_CYDOUBLECLK		37

#define MONITOR_DEFAULTTONULL		0x00000000
#define MONITOR_DEFAULTTOPRIMARY	0x00000001
#define MONITOR_DEFAULTTONEAREST	0x00000002

typedef struct tagMONITORINFO {
	DWORD cbSize;
	RECT rcMonitor;
	RECT rcWork;
	DWORD dwFlags;
} MONITORINFO, * LPMONITORINFO;

HWND GetTopWindow(HWND window);
HWND GetWindow(HWND window, UINT command);
BOOL IsWindowEnabled(HWND window);
BOOL CloseWindow(HWND window);
int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count);
BOOL RedrawWindow(HWND window, RECT const * update, HRGN region, UINT flags);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info);
BOOL SetDlgItemTextA(HWND dialog, int id, LPCSTR text);
UINT GetDlgItemTextA(HWND dialog, int id, LPSTR text, int count);
BOOL EndDialog(HWND dialog, INT_PTR result);
BOOL IsDialogMessageA(HWND dialog, LPMSG message);
int TranslateAcceleratorA(HWND window, HACCEL table, LPMSG message);
int ToAscii(UINT virtualkey, UINT scancode, BYTE const * keystate, LPWORD character, UINT flags);
BOOL CharToOemBuffA(LPCSTR source, LPSTR destination, DWORD length);
#define GetMonitorInfo		GetMonitorInfoA
#define SetDlgItemText		SetDlgItemTextA
#define GetDlgItemText		GetDlgItemTextA
#define IsDialogMessage		IsDialogMessageA
#define TranslateAccelerator TranslateAcceleratorA
#define CharToOemBuff		CharToOemBuffA
// Module resources.
#define MAKEINTRESOURCEA(id)	((LPSTR)(ULONG_PTR)(WORD)(ULONG_PTR)(id))
#define MAKEINTRESOURCE			MAKEINTRESOURCEA
#define RT_STRING				MAKEINTRESOURCEA(6)
#define RT_RCDATA				MAKEINTRESOURCEA(10)
int LoadStringA(HINSTANCE instance, UINT id, LPSTR buffer, int size);
#define LoadString				LoadStringA
// windowsx.h's control wrappers, macros over SendMessage as under Windows.
#define BN_CLICKED			0
#define BN_DOUBLECLICKED	5
#define LBN_SELCHANGE		1
#define LBN_DBLCLK			2
#define CBN_SELCHANGE		1
#define EN_CHANGE			0x0300

#define EM_SETSEL			0x00B1
#define EM_GETSEL			0x00B0
#define EM_REPLACESEL		0x00C2
#define EM_SETLIMITTEXT		0x00C5
#define EM_LIMITTEXT		0x00C5
#define EM_SETPASSWORDCHAR	0x00CC
#define WM_NOTIFY			0x004E
#define WM_HELP				0x0053
#define WM_CONTEXTMENU		0x007B
#define WM_SETTINGCHANGE	0x001A
#define WM_SIZING			0x0214
#define WM_CAPTURECHANGED	0x0215
#define WM_NEXTDLGCTL		0x0028
#define WM_PARENTNOTIFY		0x0210

#define HTERROR				(-2)
#define HTTRANSPARENT		(-1)
#define HTNOWHERE			0
#define HTCLIENT			1
#define HTCAPTION			2

#define SIZE_RESTORED		0
#define SIZE_MINIMIZED		1
#define SIZE_MAXIMIZED		2

#define SC_SIZE				0xF000
#define SC_MOVE				0xF010
#define SC_MINIMIZE			0xF020
#define SC_MAXIMIZE			0xF030
#define SC_CLOSE			0xF060
#define SC_KEYMENU			0xF100
#define SC_SCREENSAVE		0xF140
#define SC_MONITORPOWER		0xF170

#define MF_BYCOMMAND		0x00000000L
#define MF_BYPOSITION		0x00000400L
#define MF_ENABLED			0x00000000L
#define MF_GRAYED			0x00000001L
#define MF_DISABLED			0x00000002L

#define MOD_ALT				0x0001
#define MOD_CONTROL			0x0002
#define MOD_SHIFT			0x0004
#define MOD_WIN				0x0008
#define CB_GETEDITSEL				0x0140
#define CB_LIMITTEXT				0x0141
#define CB_SETEDITSEL				0x0142
#define CB_DELETESTRING				0x0144
#define CB_GETCOUNT					0x0146
#define CB_GETLBTEXT				0x0148
#define CB_GETLBTEXTLEN				0x0149
#define CB_INSERTSTRING				0x014A
#define CB_FINDSTRING				0x014C
#define CB_SELECTSTRING				0x014D
#define CB_SHOWDROPDOWN				0x014F
#define CB_GETITEMDATA				0x0150
#define CB_SETITEMDATA				0x0151
#define CB_GETDROPPEDCONTROLRECT	0x0152
#define CB_SETITEMHEIGHT			0x0153
#define CB_GETITEMHEIGHT			0x0154
#define CB_GETDROPPEDSTATE			0x0157
#define CB_GETTOPINDEX				0x015B
#define CB_SETTOPINDEX				0x015C
#define CB_SETDROPPEDWIDTH			0x0160

#define EN_MAXTEXT			0x0501
#define EN_UPDATE			0x0400
#define EN_SETFOCUS			0x0100
#define EN_KILLFOCUS		0x0200

#define SBM_SETPOS			0x00E0
#define SBM_GETPOS			0x00E1
#define SBM_SETRANGE		0x00E2
#define SBM_GETRANGE		0x00E3
#define SBM_SETSCROLLINFO	0x00E9
#define SBM_GETSCROLLINFO	0x00EA

#define SB_HORZ				0
#define SB_VERT				1
#define SB_CTL				2
#define SB_BOTH				3

#define SIF_RANGE			0x0001
#define SIF_PAGE			0x0002
#define SIF_POS				0x0004
#define SIF_DISABLENOSCROLL	0x0008
#define SIF_TRACKPOS		0x0010
#define SIF_ALL				(SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)

#define IDC_SIZEALL			((LPCSTR)32646)
#define IDC_NO				((LPCSTR)32648)
#define IDC_HAND			((LPCSTR)32649)
#define IDC_APPSTARTING		((LPCSTR)32650)
#define MAKEWPARAM(l, h)	((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h)	((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h)	((LRESULT)(DWORD)MAKELONG(l, h))
typedef struct tagSCROLLINFO {
	UINT cbSize;
	UINT fMask;
	int nMin;
	int nMax;
	UINT nPage;
	int nPos;
	int nTrackPos;
} SCROLLINFO, * LPSCROLLINFO;
typedef SCROLLINFO const * LPCSCROLLINFO;
typedef struct tagNMHDR {
	HWND hwndFrom;
	UINT_PTR idFrom;
	UINT code;
} NMHDR, * LPNMHDR;
// A dialog template and its item templates are two-byte packed fixed heads
// followed by variable-length name, class, title, and font fields; the extended
// form, whose first DWORD is 0xFFFF0001, is read as bytes.
#pragma pack(push, 2)
typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	WORD cdit;
	short x;
	short y;
	short cx;
	short cy;
} DLGTEMPLATE, * LPDLGTEMPLATE;

typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	short x;
	short y;
	short cx;
	short cy;
	WORD id;
} DLGITEMTEMPLATE, * LPDLGITEMTEMPLATE;
#pragma pack(pop)

typedef DLGTEMPLATE const * LPCDLGTEMPLATE;
typedef DLGITEMTEMPLATE const * LPCDLGITEMTEMPLATE;

#define DS_SETFONT			0x0040L

#define DLGWINDOWEXTRA		30
HWND CreateDialogParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc, LPARAM initparam);
HWND CreateDialogIndirectParamA(HINSTANCE instance, LPCDLGTEMPLATE dialogtemplate, HWND parent, DLGPROC dialogproc, LPARAM initparam);
INT_PTR DialogBoxParamA(HINSTANCE instance, LPCSTR templatename, HWND parent, DLGPROC dialogproc, LPARAM initparam);
DWORD GetDialogBaseUnits(void);
BOOL EnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM parameter);
int GetClassNameA(HWND window, LPSTR classname, int count);
HWND ChildWindowFromPoint(HWND parent, POINT point);
HWND GetCapture(void);
BOOL IsChild(HWND parent, HWND window);
HMENU GetMenu(HWND window);
HMENU GetSystemMenu(HWND window, BOOL revert);
BOOL DeleteMenu(HMENU menu, UINT position, UINT flags);
BOOL EnableMenuItem(HMENU menu, UINT item, UINT enable);
BOOL DestroyCursor(HCURSOR cursor);
BOOL GetScrollInfo(HWND window, int bar, LPSCROLLINFO info);
int SetScrollInfo(HWND window, int bar, LPCSCROLLINFO info, BOOL redraw);
#define CreateDialogParam		CreateDialogParamA
#define CreateDialogIndirectParam CreateDialogIndirectParamA
#define DialogBoxParam			DialogBoxParamA
#define GetClassName			GetClassNameA
#define LB_SELITEMRANGE		0x019B
#define LB_SELITEMRANGEEX	0x0183
#define LB_SETITEMHEIGHT	0x01A0
#define LB_GETITEMHEIGHT	0x01A1
#define LB_GETSELCOUNT		0x0190
#define LB_GETSELITEMS		0x0191
#define BS_PUSHBUTTON		0x00000000L
#define BS_DEFPUSHBUTTON	0x00000001L
#define BS_CHECKBOX			0x00000002L
#define BS_AUTOCHECKBOX		0x00000003L
#define BS_RADIOBUTTON		0x00000004L
#define BS_3STATE			0x00000005L
#define BS_GROUPBOX			0x00000007L
#define BS_AUTORADIOBUTTON	0x00000009L
#define BS_OWNERDRAW		0x0000000BL

#define ES_LEFT				0x0000L
#define ES_CENTER			0x0001L
#define ES_RIGHT			0x0002L
#define ES_MULTILINE		0x0004L
#define ES_PASSWORD			0x0020L
#define ES_AUTOHSCROLL		0x0080L
#define ES_READONLY			0x0800L
#define ES_NUMBER			0x2000L
#define SWP_NOOWNERZORDER	0x0200
#define SWP_NOSENDCHANGING	0x0400
#define SWP_DRAWFRAME		SWP_FRAMECHANGED

#define SM_CXDRAG			68
#define SM_CYDRAG			69
#define RT_CURSOR			MAKEINTRESOURCEA(1)
#define RT_BITMAP			MAKEINTRESOURCEA(2)
#define RT_ICON				MAKEINTRESOURCEA(3)
#define RT_MENU				MAKEINTRESOURCEA(4)
#define RT_DIALOG			MAKEINTRESOURCEA(5)
#define RT_VERSION			MAKEINTRESOURCEA(16)
#define HELP_CONTEXT		0x0001L
#define HELP_CONTEXTPOPUP	0x0008L
typedef struct tagWINDOWPOS {
	HWND hwnd;
	HWND hwndInsertAfter;
	int x;
	int y;
	int cx;
	int cy;
	UINT flags;
} WINDOWPOS, * LPWINDOWPOS, * PWINDOWPOS;

typedef struct tagHELPINFO {
	UINT cbSize;
	int iContextType;
	int iCtrlId;
	HANDLE hItemHandle;
	DWORD_PTR dwContextId;
	POINT MousePos;
} HELPINFO, * LPHELPINFO;

typedef struct tagICONINFO {
	BOOL fIcon;
	DWORD xHotspot;
	DWORD yHotspot;
	HBITMAP hbmMask;
	HBITMAP hbmColor;
} ICONINFO, * PICONINFO;
BOOL CheckDlgButton(HWND dialog, int id, UINT check);
UINT IsDlgButtonChecked(HWND dialog, int id);
int GetDlgCtrlID(HWND window);
BOOL GetUpdateRect(HWND window, LPRECT rect, BOOL erase);
LRESULT CallWindowProcA(WNDPROC previous, HWND window, UINT message, WPARAM wparam, LPARAM lparam);
HWND WindowFromPoint(POINT point);
BOOL RegisterHotKey(HWND window, int id, UINT modifiers, UINT key);
BOOL UnregisterHotKey(HWND window, int id);
DWORD GetWindowContextHelpId(HWND window);
BOOL WinHelpA(HWND window, LPCSTR help, UINT command, ULONG_PTR data);
HICON CreateIconIndirect(PICONINFO info);
BOOL EnumDisplaySettingsA(LPCSTR devicename, DWORD mode, LPDEVMODEA devmode);
#define CallWindowProc		CallWindowProcA
#define WinHelp				WinHelpA
#define EnumDisplaySettings	EnumDisplaySettingsA
#define WM_NCPAINT			0x0085
#define WM_NCCALCSIZE		0x0083
#define WM_NCACTIVATE		0x0086
#define HELP_CONTEXTMENU	0x000AL
#define WM_CTLCOLORSCROLLBAR	0x0137
#define LBS_NOTIFY				0x0001L
#define LBS_SORT				0x0002L
#define LBS_MULTIPLESEL			0x0008L
#define LBS_OWNERDRAWFIXED		0x0010L
#define LBS_HASSTRINGS			0x0040L
#define LBS_EXTENDEDSEL			0x0800L

#define CBS_SIMPLE				0x0001L
#define CBS_DROPDOWN			0x0002L
#define CBS_DROPDOWNLIST		0x0003L
typedef struct tagMSGBOXPARAMSA {
	UINT cbSize;
	HWND hwndOwner;
	HINSTANCE hInstance;
	LPCSTR lpszText;
	LPCSTR lpszCaption;
	DWORD dwStyle;
	LPCSTR lpszIcon;
	DWORD_PTR dwContextHelpId;
	void * lpfnMsgBoxCallback;
	DWORD dwLanguageId;
} MSGBOXPARAMSA, MSGBOXPARAMS, * LPMSGBOXPARAMSA;

#define MB_USERICON		0x00000080L
#define MB_HELP			0x00004000L

int MessageBoxIndirectA(MSGBOXPARAMSA const * parameters);
#define MessageBoxIndirect	MessageBoxIndirectA
#define LBS_NOSEL			0x4000L
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define LB_FINDSTRINGEXACT	0x01A2
#define EM_POSFROMCHAR		0x00D6
#define EM_CHARFROMPOS		0x00D7
#define EM_LINEFROMCHAR		0x00C9
#define EM_LINEINDEX		0x00BB
#define WM_SYSDEADCHAR		0x0107
#define WM_UNICHAR			0x0109

HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous);
HWND GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous);
#define SS_LEFT				0x00000000L
#define SS_CENTER			0x00000001L
#define SS_RIGHT			0x00000002L
#define SS_ICON				0x00000003L
#define SS_BLACKRECT		0x00000004L
#define SS_OWNERDRAW		0x0000000DL
#define SS_BITMAP			0x0000000EL
#define SS_NOPREFIX			0x00000080L
#define SS_NOTIFY			0x00000100L
#define SS_CENTERIMAGE		0x00000200L
#define SS_TYPEMASK			0x0000001FL
#define LB_GETITEMRECT		0x0198
#define WM_GETDLGCODE		0x0087
#define DLGC_WANTARROWS		0x0001
#define DLGC_WANTTAB		0x0002
#define DLGC_WANTALLKEYS	0x0004
#define DLGC_WANTMESSAGE	0x0004
#define DLGC_HASSETSEL		0x0008
#define DLGC_DEFPUSHBUTTON	0x0010
#define DLGC_BUTTON			0x2000
#define DLGC_WANTCHARS		0x0080
#define DT_LEFT				0x00000000
#define DT_CENTER			0x00000001
#define DT_RIGHT			0x00000002
#define DT_TOP				0x00000000
#define DT_VCENTER			0x00000004
#define DT_BOTTOM			0x00000008
#define DT_WORDBREAK		0x00000010
#define DT_SINGLELINE		0x00000020
#define DT_CALCRECT			0x00000400
#define DT_NOPREFIX			0x00000800
#define DT_END_ELLIPSIS		0x00008000

int GetKeyNameTextA(LONG param, LPSTR buffer, int size);
int DrawTextA(HDC dc, LPCSTR text, int count, LPRECT rect, UINT format);
#define GetKeyNameText	GetKeyNameTextA
#define DrawText		DrawTextA
#define ODT_MENU		1
#define ODT_LISTBOX		2
#define ODT_COMBOBOX	3
#define ODT_BUTTON		4
#define ODT_STATIC		5
#define ODT_TAB			101
#define ODT_LISTVIEW	102

#define ODA_DRAWENTIRE	0x0001
#define ODA_SELECT		0x0002
#define ODA_FOCUS		0x0004

#define ODS_SELECTED	0x0001
#define ODS_GRAYED		0x0002
#define ODS_DISABLED	0x0004
#define ODS_CHECKED		0x0008
#define ODS_FOCUS		0x0010
#define ODS_DEFAULT		0x0020
