/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#if defined(_WIN32)

#include "always.h"

#include "hostwindow.h"

#include "_keyboar.h"
#include "dbgprint.h"
#include "except.h"
#include "gamewindow.h"
#include "globals.h"
#include "goptions.h"
#include "keyboard.h"
#include "mainwindow.h"
#include "misc.h"
#include "msgloop.h"
#include "nativewindow.hh"
#include "queue.h"
#include "resource.h"
#include "session.h"
#include "ui/uiwin32.h"
#include "video.h"
#include "vidscale.h"
#include "win.h"
#include "wincursor.h"

#include <algorithm>
#include <climits>
#include <commctrl.h>
#include <cstring>
#include <windowsx.h>


Point2D Host_Pointer_Position(void)
{
	POINT point;
	GetCursorPos(&point);
	ScreenToClient(MainWindow, &point);
	return(Point2D(point.x, point.y));
}


void Host_Move_Pointer(Point2D const & position)
{
	POINT point;
	point.x = position.X;
	point.y = position.Y;
	ClientToScreen(MainWindow, &point);
	SetCursorPos(point.x, point.y);
}


int Host_Show_Pointer(bool show)
{
	return(ShowCursor(show ? TRUE : FALSE));
}


void Host_Confine_Pointer(bool confine)
{
	if (!confine) {
		ClipCursor(NULL);
		return;
	}

	RECT clip_rect;
	GetClientRect(MainWindow, &clip_rect);
	ClientToScreen(MainWindow, (LPPOINT)&clip_rect.left);
	ClientToScreen(MainWindow, (LPPOINT)&clip_rect.right);
	ClipCursor(&clip_rect);
}


void Host_Capture_Pointer(void)
{
	SetCapture(MainWindow);
}


void Host_Release_Pointer(void)
{
	if (MainWindow != NULL && GetCapture() == MainWindow) {
		ReleaseCapture();
	}
}


bool Host_Pointer_Is_Captured(void)
{
	return(MainWindow != NULL && GetCapture() == MainWindow);
}


Point2D Host_Drag_Threshold(void)
{
	return(Point2D(GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG)));
}


// A color cursor carries its transparency in the alpha channel, but Windows still wants a
// mask bitmap alongside it.
HostCursor * Host_Create_Cursor(std::uint32_t const * pixels, int width, int height, int hotx, int hoty)
{
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return(nullptr);
	}

	BITMAPINFO info;
	memset(&info, '\0', sizeof(info));
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	void * surface = nullptr;
	HBITMAP color = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &surface, NULL, 0);
	if (color == NULL) {
		return(nullptr);
	}

	memcpy(surface, pixels, (size_t)width * (size_t)height * 4);

	int mask_pitch = ((width + 15) / 16) * 2;
	char * mask_bits = new char[mask_pitch * height];
	memset(mask_bits, '\0', mask_pitch * height);
	HBITMAP mask = CreateBitmap(width, height, 1, 1, mask_bits);
	delete [] mask_bits;

	ICONINFO icon;
	icon.fIcon = FALSE;
	icon.xHotspot = hotx;
	icon.yHotspot = hoty;
	icon.hbmMask = mask;
	icon.hbmColor = color;
	HCURSOR cursor = (HCURSOR)CreateIconIndirect(&icon);

	DeleteObject(mask);
	DeleteObject(color);
	return((HostCursor *)cursor);
}


void Host_Destroy_Cursor(HostCursor * cursor)
{
	if (cursor != nullptr) {
		DestroyCursor((HCURSOR)cursor);
	}
}


void Host_Set_Cursor(HostCursor * cursor)
{
	SetCursor((HCURSOR)cursor);
}


void Host_Hide_Cursor(void)
{
	SetCursor(NULL);
}


int Host_Max_Cursor_Size(void)
{
	return(INT_MAX);
}


bool Host_Has_Class_Cursor(void)
{
	return(true);
}

unsigned short Host_Key_Modifiers(void)
{
	unsigned short modifiers = 0;

	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
		modifiers |= WWKEY_SHIFT_BIT;
	}
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
		modifiers |= WWKEY_CTRL_BIT;
	}
	if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
		modifiers |= WWKEY_ALT_BIT;
	}

	return(modifiers);
}


bool Host_Key_Is_Down(unsigned short key)
{
	key &= 0xFF;

	if ((key == VK_LBUTTON || key == VK_RBUTTON) && GetSystemMetrics(SM_SWAPBUTTON) == TRUE) {
		key = (key != VK_LBUTTON) ? VK_LBUTTON : VK_RBUTTON;
	}

	return(GetAsyncKeyState(key) != 0);
}


// Windows translates with the modifiers the key code carries rather than those held now.
int Host_Key_To_Character(unsigned short key)
{
	static BYTE _keystate[256];

	if (key & WWKEY_SHIFT_BIT) {
		_keystate[VK_SHIFT] = 0x80;
	}
	if (key & WWKEY_CTRL_BIT) {
		_keystate[VK_CONTROL] = 0x80;
	}
	if (key & WWKEY_ALT_BIT) {
		_keystate[VK_MENU] = 0x80;
	}

	wchar_t buffer[4];
	int const scancode = MapVirtualKey(key & 0xFF, 0);
	int const result = ToUnicode((UINT)(key & 0xFF), (UINT)scancode, _keystate, buffer, ARRAY_SIZE(buffer), 0);

	_keystate[VK_SHIFT] = 0;
	_keystate[VK_CONTROL] = 0;
	_keystate[VK_MENU] = 0;

	if (result == 2 && IS_SURROGATE_PAIR(buffer[0], buffer[1])) {
		return(0x10000 + ((buffer[0] - 0xD800) << 10) + (buffer[1] - 0xDC00));
	}

	return(result == 1 ? buffer[0] : 0);
}


int ShowCommand = SW_SHOWNORMAL;
HWND MainWindow;
HINSTANCE ProgramInstance;


bool Has_Main_Window(void)
{
	return(MainWindow != nullptr);
}


/*
 * Taken from later Windows SDK after what is shipped in VS6
 */

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  /// message that will be supported
#endif

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wParam)  ((short)HIWORD(wParam))
#endif
///////////////////////////////////////////////////////////


/// <summary>
/// Keeps the game window from being dragged off the edge of the display by correcting the
/// proposed rectangle in place, which Windows then moves the window to.
/// </summary>
/// <returns>bool; Was the rectangle pulled back onto the screen?</returns>
static bool On_WM_MOVING(HWND window, WPARAM wparam, LPARAM lparam)
{
	RECT *rcl = (RECT *)lparam;

	bool res = false;

	if (rcl->left < 0) {
		rcl->right -= rcl->left;
		rcl->left = 0;
		res = true;
	}

	if (rcl->top < 0) {
		rcl->bottom -= rcl->top;
		rcl->top = 0;
		res = true;
	}

	if (rcl->right > GetSystemMetrics(SM_CXFULLSCREEN))
	{
		rcl->left += GetSystemMetrics(SM_CXFULLSCREEN) - rcl->right;
		rcl->right = GetSystemMetrics(SM_CXFULLSCREEN);
		res = true;
	}
	if (rcl->bottom > GetSystemMetrics(SM_CYFULLSCREEN))
	{
		rcl->top += GetSystemMetrics(SM_CYFULLSCREEN) - rcl->bottom;
		rcl->bottom = GetSystemMetrics(SM_CYFULLSCREEN);
		res = true;
	}

	return(res);
}


static bool Is_Mouse_Position_Message(UINT message)
{
	switch (message) {
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
			return(true);

		default:
			return(false);
	}
}


/// <summary>
/// Hands a key or mouse button message, its position already in the frame, to the game.
/// </summary>
/// <returns>bool; Was it one? The window procedure lets Windows see it too and returns.</returns>
static bool Handle_Input_Message(UINT message, WPARAM wparam, LPARAM lparam)
{
	Point2D const point((short)LOWORD(lparam), (short)HIWORD(lparam));

	switch (message) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			// Scroll Lock was a debugger's breakpoint key and types nothing. A key Windows
			// repeats while held is taken only once.
			if (wparam != VK_SCROLL && !(lparam & (1 << 30)) && Keyboard != nullptr) {
				Keyboard->Post_Key_Event((unsigned short)wparam, false);
			}
			return(true);

		case WM_SYSKEYUP:
		case WM_KEYUP:
			if (Keyboard != nullptr) {
				Keyboard->Post_Key_Event((unsigned short)wparam, true);
			}
			return(true);

		case WM_LBUTTONDOWN:	Game_Window_Mouse_Button(VK_LBUTTON, point, false);	return(true);
		case WM_LBUTTONUP:		Game_Window_Mouse_Button(VK_LBUTTON, point, true);	return(true);
		case WM_MBUTTONDOWN:	Game_Window_Mouse_Button(VK_MBUTTON, point, false);	return(true);
		case WM_MBUTTONUP:		Game_Window_Mouse_Button(VK_MBUTTON, point, true);	return(true);
		case WM_RBUTTONDOWN:	Game_Window_Mouse_Button(VK_RBUTTON, point, false);	return(true);
		case WM_RBUTTONUP:		Game_Window_Mouse_Button(VK_RBUTTON, point, true);	return(true);

		case WM_LBUTTONDBLCLK:	Game_Window_Mouse_Double_Click(VK_LBUTTON, point);	return(true);
		case WM_MBUTTONDBLCLK:	Game_Window_Mouse_Double_Click(VK_MBUTTON, point);	return(true);
		case WM_RBUTTONDBLCLK:	Game_Window_Mouse_Double_Click(VK_RBUTTON, point);	return(true);

		case WM_CAPTURECHANGED:
			if ((HWND)lparam != MainWindow) {
				Game_Window_Pointer_Capture_Lost();
			}
			return(false);

		default:
			return(false);
	}
}


/// <summary>
/// Handles the Windows messages sent to the main game window.
/// This is the window procedure registered for the main window. It offers each
/// message to the network transport, the map and the keyboard handlers, deals with
/// the messages the game must react to itself -- focus changes, painting, tray
/// locking and shutdown -- and passes everything else back to Windows.
/// </summary>
/// <returns>Returns with the result Windows expects for the message handled.</returns>
LRESULT CALLBACK /*_export*/ Windows_Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{

	// The frame may be drawn scaled, so a position is taken into the frame before anything
	// reads it. A wheel message carries a screen position, which nothing reads.
	if (Is_Mouse_Position_Message(message)) {
		Point2D point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		Window_Point_To_Game(point);
		lParam = MAKELPARAM((short)point.X, (short)point.Y);
	}

	// Before the game's own handling, so input a document took never enters the KN_ queue.
	if (UI_Handle_Window_Message(hwnd, message, wParam, lParam)) {
		return(0);
	}

	if (Handle_Input_Message(message, wParam, lParam)) {
		DefWindowProc(hwnd, message, wParam, lParam);
		return(0);
	}

	if (MainWindow) {
		GetMenu(MainWindow);
	}

	switch ( message ) {
		// Raised on request so that crash reporting can be exercised from inside window
		// procedure dispatch, which the operating system unwinds differently from a call.
		case WM_EXCEPTION_TEST:
			Exception_Wndproc_Test_Fault();
			return(0);

//		case WM_SYSKEYDOWN:
//			Mono_Printf("wparam=%08X lparam=%08X\n", (long)wParam, (long)lParam);
			// fall through

//		case WM_MOUSEMOVE:
//		case WM_KEYDOWN:
//		case WM_SYSKEYUP:
//		case WM_KEYUP:
//		case WM_LBUTTONDOWN:
//		case WM_LBUTTONUP:
//		case WM_LBUTTONDBLCLK:
//		case WM_MBUTTONDOWN:
//		case WM_MBUTTONUP:
//		case WM_MBUTTONDBLCLK:
//		case WM_RBUTTONDOWN:
//		case WM_RBUTTONUP:
//		case WM_RBUTTONDBLCLK:
//	 		Keyboard->Message_Handler(hwnd, message, wParam, lParam);
//			return(0);

		case WM_SHOWWINDOW:
			return(0);

		case WM_PAINT:
			Game_Window_On_Paint(GameInFocus == true || WindowedMode == true);
			ValidateRect(hwnd, NULL);
			break;

		case WM_ERASEBKGND:
			return(1);

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT && Win_Cursor_Handle_Set_Cursor()) {
				return(TRUE);
			}
			break;

		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED) {
				Video_On_Resize(LOWORD(lParam), HIWORD(lParam));
				Video_Set_Refresh_Rate(Host_Window_Refresh_Rate());
			}
			break;

		case WM_DISPLAYCHANGE:
			Video_Set_Refresh_Rate(Host_Window_Refresh_Rate());
			break;

		case WM_CLOSE:
			break;

		case WM_CREATE:
			Game_Window_Created();
			break;

			/*
			**	Windoze message says we have to shut down. Try and do it cleanly.
			*/
		case WM_DESTROY:
			MainWindow = 0;
			Game_Window_Destroyed();
			return(0);

		case WM_ACTIVATEAPP:
			if (hwnd == MainWindow && GameInFocus != (wParam != 0)) {
				GameInFocus = (wParam != 0);
				if (!GameInFocus) {
					Focus_Loss();
					DebugString("Focus lost\n");
				} else {
					Focus_Restore();
					DebugString("Focus gained\n");
				}
			}
			return(0);

		case WM_MOVING:
			return(On_WM_MOVING(hwnd, wParam, lParam));

		case WM_MOUSEWHEEL:
			Game_Window_On_Mouse_Wheel(GET_WHEEL_DELTA_WPARAM(wParam));
			break;

		case WM_SYSCOMMAND:
			switch ( wParam ) {

				case SC_CLOSE:
					// A running game resigns rather than closing, and keeps its window: the exit
					// is played through the queue, and the game ends itself once it arrives.
					if (GameActive && PlayerPtr != NULL && !Session.Play) {
						Queue_Exit();
					}
					return(0);

				case SC_SCREENSAVE:
					/*
					**	Windoze is about to start the screen saver. If we just return without passing
					**	this message to DefWindowProc then the screen saver will not be allowed to start.
					*/
					return(0);
			}
			break;

	}

	return(DefWindowProc (hwnd, message, wParam, lParam));
}


NativeWindow Host_Native_Window(void)
{
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, MainWindow });
}


// Client dimensions are physical pixels because the process is per-monitor DPI aware.
bool Host_Window_Drawable_Size(int & width, int & height)
{
	RECT client;
	if (MainWindow == NULL || !GetClientRect(MainWindow, &client)) {
		return(false);
	}

	width = client.right - client.left;
	height = client.bottom - client.top;
	return(width > 0 && height > 0);
}


int Host_Window_Refresh_Rate(void)
{
	int refreshrate = 0;
	HDC dc = GetDC(MainWindow);

	if (dc != NULL) {
		refreshrate = GetDeviceCaps(dc, VREFRESH);
		ReleaseDC(MainWindow, dc);
	}

	return(refreshrate);
}


#define CC_ICON		IDI_SUN
#define CC_CURSOR	IDC_CURSOR1

#define WINDOW_NAME		"Tiberian Sun"


// The window class and the window, full screen or a window sized for the frame.
void Host_Create_Window(int width, int height)
{
	HINSTANCE const instance = ProgramInstance;

	InitCommonControls();

	WNDCLASS    	wndclass ;
	//
	// Register the window class
	//

	/*
	 * The dialog controls are hit tested through the main window, so its class has to
	 * report the double clicks they expect.
	 */
	wndclass.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS ;
	wndclass.lpfnWndProc   = Windows_Procedure ;
	wndclass.cbClsExtra    = 0 ;
	wndclass.cbWndExtra    = 0 ;
	wndclass.hInstance     = instance ;
	wndclass.hIcon         = LoadIcon (instance, MAKEINTRESOURCE(CC_ICON)) ;
	wndclass.hCursor       = LoadCursor(ProgramInstance, MAKEINTRESOURCE(CC_CURSOR));
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName  = NULL;	///WINDOW_NAME
	wndclass.lpszClassName = WINDOW_NAME;

	RegisterClass (&wndclass) ;


	//
	// Create our main window
	//
	/*
	 * The dialogs paint themselves onto the game's surfaces rather than into their own
	 * windows, so clipping their regions out of the main window would leave holes where
	 * they sit.
	 */
	if (WindowedMode) {
		int clientwidth = (Options.WindowWidth > 0) ? Options.WindowWidth : width;
		int clientheight = (Options.WindowHeight > 0) ? Options.WindowHeight : height;

		MainWindow = CreateWindowEx (
								0,
								WINDOW_NAME,
								WINDOW_NAME,
								WS_OVERLAPPEDWINDOW,
								0,
								0,
								0,
								0,
								NULL,
								NULL,
								instance,
								NULL );

		RECT rect;
		SetRect(&rect, 0, 0, clientwidth, clientheight);
		AdjustWindowRectEx(&rect, GetWindowLong(MainWindow, GWL_STYLE), FALSE, GetWindowLong(MainWindow, GWL_EXSTYLE));

		int windowwidth = rect.right - rect.left;
		int windowheight = rect.bottom - rect.top;
		int x = (GetSystemMetrics(SM_CXSCREEN) - windowwidth) / 2;
		int y = (GetSystemMetrics(SM_CYSCREEN) - windowheight) / 2;

		MoveWindow(MainWindow, std::max(x, 0), std::max(y, 0), windowwidth, windowheight, 1);

	} else {
		/*
		 * The desktop keeps its own resolution and the window simply covers it. The
		 * frame is scaled to fit at presentation time.
		 */
		MainWindow = CreateWindowEx (
								0,
								WINDOW_NAME,
								WINDOW_NAME,
								WS_POPUP,
								0,
								0,
								GetSystemMetrics(SM_CXSCREEN),
								GetSystemMetrics(SM_CYSCREEN),
								NULL,
								NULL,
								instance,
								NULL );
	}

	ShowWindow (MainWindow, SW_NORMAL);
	UpdateWindow (MainWindow);
	SetFocus (MainWindow);

	RegisterHotKey(MainWindow, 1, MOD_ALT|MOD_CONTROL|MOD_SHIFT, VK_M);

	SetCursor(LoadCursor(ProgramInstance, MAKEINTRESOURCE(CC_CURSOR)));

	//Misc_Focus_Loss_Function = &Focus_Loss;
	//Misc_Focus_Restore_Function = &Focus_Restore;
	//Gbuffer_Focus_Loss_Function = &Focus_Loss;
}


// The window procedure answers WM_DESTROY, so the pump runs until it has.
void Host_Close_Window(void)
{
	if (MainWindow == NULL) {
		return;
	}

	PostMessage(MainWindow, WM_DESTROY, 0, 0);

	do {
		Windows_Message_Handler();
	} while (MainWindow != NULL && ReadyToQuit == 1);
}


void Host_Invalidate_Window(void)
{
	InvalidateRect(MainWindow, NULL, FALSE);
}


void Host_Focus_Window(void)
{
	SetFocus(MainWindow);
}


void Host_Fit_Window_To_Frame(int width, int height)
{
	RECT windowrect;
	SetRect(&windowrect, 0, 0, width, height);
	AdjustWindowRectEx(&windowrect, GetWindowLong(MainWindow, GWL_STYLE), FALSE, GetWindowLong(MainWindow, GWL_EXSTYLE));

	int newwidth = windowrect.right - windowrect.left;
	int newheight = windowrect.bottom - windowrect.top;

	/*
	 * The window grows about its middle rather than its corner, so the picture stays
	 * where the player was looking.
	 */
	RECT current;
	GetWindowRect(MainWindow, &current);
	int x = current.left + (((current.right - current.left) - newwidth) / 2);
	int y = current.top + (((current.bottom - current.top) - newheight) / 2);

	/*
	 * Growing about the middle can push the window past the edges of the screen, and a
	 * title bar above the top of it cannot be grabbed to bring the window back.
	 */
	MONITORINFO monitor;
	monitor.cbSize = sizeof(monitor);
	if (GetMonitorInfo(MonitorFromWindow(MainWindow, MONITOR_DEFAULTTONEAREST), &monitor)) {
		if (x + newwidth > monitor.rcWork.right) x = monitor.rcWork.right - newwidth;
		if (y + newheight > monitor.rcWork.bottom) y = monitor.rcWork.bottom - newheight;
		if (x < monitor.rcWork.left) x = monitor.rcWork.left;
		if (y < monitor.rcWork.top) y = monitor.rcWork.top;
	}

	SetWindowPos(MainWindow, NULL, x, y, newwidth, newheight, SWP_NOZORDER);
}


bool Host_Display_Mode(int index, int & width, int & height)
{
	DEVMODE devmode;
	memset(&devmode, 0, sizeof(devmode));
	devmode.dmSize = sizeof(devmode);

	if (!EnumDisplaySettings(NULL, index, &devmode)) {
		return(false);
	}

	width = (int)devmode.dmPelsWidth;
	height = (int)devmode.dmPelsHeight;
	return(true);
}


HostMessageBoxAnswer Host_Message_Box(char const * caption, char const * text, unsigned int style)
{
	return((HostMessageBoxAnswer)MessageBox(MainWindow, text, caption, style));
}

#endif	// _WIN32
