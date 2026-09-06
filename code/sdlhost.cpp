/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The host the native build runs under: an SDL window standing where the page's canvas
// stands, answering the same calls browser.cpp answers for the page. Events are queued
// from the poll and drained in engine context, as the page's are, so a callback never
// writes the keyboard buffer part way through a read of it.

#include "always.h"

#if defined(OPENTS_SDL_HOST)

#include "browser.h"

#include "_keyboar.h"
#include "dbgprint.h"
#include "globals.h"
#include "keyboard.h"
#include "video.h"
#include "vidscale.h"
#include <windows.h>
#include "win32gdi.h"
#include "wwmouse.h"

// The engine defines WIN32 on every target, and SDL's platform header reads it as a
// Windows build.
#undef WIN32
#undef _WINDOWS
#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>


// The shortest gap between two returns to the host; the engine's own timers decide the
// frame rate.
static Uint64 const YIELD_INTERVAL = 1000 / 60;

static int const EVENT_QUEUE_SIZE = 128;

static int const INITIAL_WIDTH = 1024;
static int const INITIAL_HEIGHT = 768;

struct HostEvent
{
	unsigned short Key;
	short X;
	short Y;
	bool IsMouse;
	bool IsRelease;
	unsigned short Modifiers;
};

static HostEvent _Events[EVENT_QUEUE_SIZE];
static int _EventHead = 0;
static int _EventTail = 0;
static BrowserEventHook _EventHook = nullptr;

static SDL_Window * _Window = nullptr;
static bool _Initialized = false;

static int _CanvasWidth = 0;
static int _CanvasHeight = 0;
static int _CanvasCSSWidth = 0;
static int _CanvasCSSHeight = 0;
static int _ScreenWidth = 0;
static int _ScreenHeight = 0;
static bool _Hidden = false;
static int _MouseX = 0;
static int _MouseY = 0;
static bool _MouseHovering = false;
static unsigned short _Modifiers = 0;
static unsigned short _EventModifiers = 0;
static unsigned char _KeyDown[256];
static char _Ascii[256];
static char _ShiftedAscii[256];
// The key most recently pressed, which the text event that follows it describes.
static unsigned short _LastKey = VK_NONE;
static Uint64 _LastYield = 0;

// Wheel travel not yet reported, in notches; a trackpad reports fractions of one.
static float _WheelPending = 0.0f;

// WHEEL_DELTA; only the sign is read.
static int const WHEEL_MESSAGE_DELTA = 120;

// The cursors built so far, by the substitute's key; a system cursor by its SDL id.
static std::map<void const *, SDL_Cursor *> _ImageCursors;
static SDL_Cursor * _SystemCursors[SDL_NUM_SYSTEM_CURSORS];
static unsigned int _FrameSerial = 0;
static unsigned int _BlockingWaits = 0;


static void Queue_Event(HostEvent const & event)
{
	int next = (_EventTail + 1) % EVENT_QUEUE_SIZE;
	if (next == _EventHead) {
		return;
	}
	_Events[_EventTail] = event;
	_Events[_EventTail].Modifiers = _Modifiers;
	_EventTail = next;
}


// The scan code names the physical key, which is what the hotkey tables want.
static unsigned short Virtual_Key_For_Scancode(SDL_Scancode code)
{
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
		return((unsigned short)(VK_A + (code - SDL_SCANCODE_A)));
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
		return((unsigned short)(VK_1 + (code - SDL_SCANCODE_1)));
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
		return((unsigned short)(VK_F1 + (code - SDL_SCANCODE_F1)));
	}
	if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
		return((unsigned short)(VK_NUMPAD1 + (code - SDL_SCANCODE_KP_1)));
	}

	switch (code) {
		case SDL_SCANCODE_0:			return(VK_0);
		case SDL_SCANCODE_KP_0:			return(VK_NUMPAD0);
		case SDL_SCANCODE_ESCAPE:		return(VK_ESCAPE);
		case SDL_SCANCODE_BACKSPACE:	return(VK_BACK);
		case SDL_SCANCODE_TAB:			return(VK_TAB);
		case SDL_SCANCODE_RETURN:		return(VK_RETURN);
		case SDL_SCANCODE_KP_ENTER:		return(VK_RETURN);
		case SDL_SCANCODE_SPACE:		return(VK_SPACE);
		case SDL_SCANCODE_LSHIFT:		return(VK_SHIFT);
		case SDL_SCANCODE_RSHIFT:		return(VK_SHIFT);
		case SDL_SCANCODE_LCTRL:		return(VK_CONTROL);
		case SDL_SCANCODE_RCTRL:		return(VK_CONTROL);
		case SDL_SCANCODE_LALT:			return(VK_MENU);
		case SDL_SCANCODE_RALT:			return(VK_MENU);
		case SDL_SCANCODE_CAPSLOCK:		return(VK_CAPITAL);
		case SDL_SCANCODE_NUMLOCKCLEAR:	return(VK_NUMLOCK);
		case SDL_SCANCODE_SCROLLLOCK:	return(VK_SCROLL);
		case SDL_SCANCODE_PAUSE:		return(VK_PAUSE);
		case SDL_SCANCODE_PRINTSCREEN:	return(VK_SNAPSHOT);
		case SDL_SCANCODE_INSERT:		return(VK_INSERT);
		case SDL_SCANCODE_DELETE:		return(VK_DELETE);
		case SDL_SCANCODE_HOME:			return(VK_HOME);
		case SDL_SCANCODE_END:			return(VK_END);
		case SDL_SCANCODE_PAGEUP:		return(VK_PRIOR);
		case SDL_SCANCODE_PAGEDOWN:		return(VK_NEXT);
		case SDL_SCANCODE_LEFT:			return(VK_LEFT);
		case SDL_SCANCODE_UP:			return(VK_UP);
		case SDL_SCANCODE_RIGHT:		return(VK_RIGHT);
		case SDL_SCANCODE_DOWN:			return(VK_DOWN);
		case SDL_SCANCODE_KP_MULTIPLY:	return(VK_MULTIPLY);
		case SDL_SCANCODE_KP_PLUS:		return(VK_ADD);
		case SDL_SCANCODE_KP_MINUS:		return(VK_SUBTRACT);
		case SDL_SCANCODE_KP_PERIOD:	return(VK_DECIMAL);
		case SDL_SCANCODE_KP_DIVIDE:	return(VK_DIVIDE);
		case SDL_SCANCODE_MINUS:		return(VK_NONE_BD);
		case SDL_SCANCODE_EQUALS:		return(VK_NONE_BB);
		case SDL_SCANCODE_LEFTBRACKET:	return(VK_NONE_DB);
		case SDL_SCANCODE_RIGHTBRACKET:	return(VK_NONE_DD);
		case SDL_SCANCODE_BACKSLASH:	return(VK_NONE_DC);
		case SDL_SCANCODE_SEMICOLON:	return(VK_NONE_BA);
		case SDL_SCANCODE_APOSTROPHE:	return(VK_NONE_DE);
		case SDL_SCANCODE_GRAVE:		return(VK_NONE_C0);
		case SDL_SCANCODE_COMMA:		return(VK_NONE_BC);
		case SDL_SCANCODE_PERIOD:		return(VK_NONE_BE);
		case SDL_SCANCODE_SLASH:		return(VK_NONE_BF);
		case SDL_SCANCODE_APPLICATION:	return(VK_NONE_5D);
		case SDL_SCANCODE_LGUI:			return(VK_NONE_5B);
		case SDL_SCANCODE_RGUI:			return(VK_NONE_5C);
		default:						return(VK_NONE);
	}
}


static void Note_Modifiers(Uint16 mod)
{
	_Modifiers = 0;
	if ((mod & KMOD_SHIFT) != 0) _Modifiers |= WWKEY_SHIFT_BIT;
	if ((mod & KMOD_CTRL) != 0) _Modifiers |= WWKEY_CTRL_BIT;
	if ((mod & KMOD_ALT) != 0) _Modifiers |= WWKEY_ALT_BIT;
	Browser_Apply_Modifiers(_Modifiers);
}


// Measures the window; true when its size changed.
static bool Measure_Window(void)
{
	int width = 0;
	int height = 0;
	int csswidth = 0;
	int cssheight = 0;

	SDL_GetWindowSizeInPixels(_Window, &width, &height);
	SDL_GetWindowSize(_Window, &csswidth, &cssheight);

	if (width == _CanvasWidth && height == _CanvasHeight && csswidth == _CanvasCSSWidth && cssheight == _CanvasCSSHeight) {
		return(false);
	}

	_CanvasWidth = width;
	_CanvasHeight = height;
	_CanvasCSSWidth = csswidth;
	_CanvasCSSHeight = cssheight;
	return(true);
}


// SDL reports window points and the drawing buffer is in device pixels; the engine's own
// scaling conversion then finishes the job.
static void Window_Point_To_Game_Point(int pointx, int pointy, int & x, int & y)
{
	double ratio = (_CanvasCSSWidth > 0) ? (double)_CanvasWidth / (double)_CanvasCSSWidth : 1.0;
	POINT point;
	point.x = (LONG)(pointx * ratio + 0.5);
	point.y = (LONG)(pointy * ratio + 0.5);
	Window_Point_To_Game(point);
	Clamp_To_Game(point);
	x = (int)point.x;
	y = (int)point.y;
}


static void Poll_Key(SDL_KeyboardEvent const & event)
{
	Note_Modifiers(event.keysym.mod);

	unsigned short key = Virtual_Key_For_Scancode(event.keysym.scancode);
	if (key == VK_NONE) {
		return;
	}

	bool release = (event.type == SDL_KEYUP);

	// The key's own character is the layout's unshifted answer; the text event that
	// follows a press corrects it for the shift state.
	if (!release) {
		char character = '\0';
		if (event.keysym.sym >= 32 && event.keysym.sym < 127) {
			character = (char)event.keysym.sym;
		} else {
			switch (key & 0xFF) {
				case VK_RETURN:	character = '\r';	break;
				case VK_BACK:	character = '\b';	break;
				case VK_TAB:	character = '\t';	break;
				case VK_ESCAPE:	character = 27;		break;
				default:							break;
			}
		}
		if (character != '\0') {
			if ((_Modifiers & WWKEY_SHIFT_BIT) != 0) {
				_ShiftedAscii[key & 0xFF] = character;
			} else {
				_Ascii[key & 0xFF] = character;
			}
		}
		_LastKey = key;
	}

	// The engine does not want key repeats.
	if (!release && event.repeat != 0) {
		return;
	}

	_KeyDown[key & 0xFF] = release ? 0 : 1;

	HostEvent queued;
	queued.Key = key;
	queued.X = 0;
	queued.Y = 0;
	queued.IsMouse = false;
	queued.IsRelease = release;
	Queue_Event(queued);
}


static void Poll_Text(SDL_TextInputEvent const & event)
{
	if (_LastKey == VK_NONE || event.text[0] == '\0' || (event.text[0] & 0x80) != 0) {
		return;
	}
	if ((_Modifiers & WWKEY_SHIFT_BIT) != 0) {
		_ShiftedAscii[_LastKey & 0xFF] = event.text[0];
	} else {
		_Ascii[_LastKey & 0xFF] = event.text[0];
	}
}


static void Poll_Mouse_Button(SDL_MouseButtonEvent const & event)
{
	Note_Modifiers(SDL_GetModState());
	_MouseHovering = true;
	Window_Point_To_Game_Point(event.x, event.y, _MouseX, _MouseY);

	unsigned short key;
	switch (event.button) {
		case SDL_BUTTON_MIDDLE:	key = VK_MBUTTON;	break;
		case SDL_BUTTON_RIGHT:	key = VK_RBUTTON;	break;
		default:				key = VK_LBUTTON;	break;
	}

	bool release = (event.type == SDL_MOUSEBUTTONUP);
	_KeyDown[key] = release ? 0 : 1;

	HostEvent queued;
	queued.Key = key;
	queued.X = (short)_MouseX;
	queued.Y = (short)_MouseY;
	queued.IsMouse = true;
	queued.IsRelease = release;
	Queue_Event(queued);

	// WM_LBUTTONDBLCLK reaches the engine as a second press and release.
	if (release && event.clicks == 2) {
		queued.IsRelease = false;
		Queue_Event(queued);
		queued.IsRelease = true;
		Queue_Event(queued);
	}
}


// The build list takes the wheel a notch at a time as WM_MOUSEWHEEL, which msgroute.cpp
// expects in screen coordinates and posted to the main window so that routing picks the
// window under it.
static void Poll_Wheel(SDL_MouseWheelEvent const & event)
{
	if (MainWindow == NULL) {
		_WheelPending = 0.0f;
		return;
	}

	float travel = event.preciseY;
	if (event.direction == SDL_MOUSEWHEEL_FLIPPED) {
		travel = -travel;
	}

	// A reversal discards a remainder too small to have moved anything.
	if ((travel > 0.0f && _WheelPending < 0.0f) || (travel < 0.0f && _WheelPending > 0.0f)) {
		_WheelPending = 0.0f;
	}
	_WheelPending += travel;

	while (_WheelPending >= 1.0f || _WheelPending <= -1.0f) {
		bool const forward = (_WheelPending > 0.0f);
		_WheelPending += forward ? -1.0f : 1.0f;

		short const delta = (short)(forward ? WHEEL_MESSAGE_DELTA : -WHEEL_MESSAGE_DELTA);

		POINT screen;
		screen.x = _MouseX;
		screen.y = _MouseY;
		Game_Point_To_Window(screen);

		PostMessage(MainWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, (unsigned short)delta),
			MAKELPARAM((short)screen.x, (short)screen.y));
	}
}


static void Poll_Window(SDL_WindowEvent const & event)
{
	switch (event.event) {
		case SDL_WINDOWEVENT_ENTER:
			_MouseHovering = true;
			break;
		case SDL_WINDOWEVENT_LEAVE:
			_MouseHovering = false;
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
		case SDL_WINDOWEVENT_HIDDEN:
			_Hidden = true;
			break;
		case SDL_WINDOWEVENT_RESTORED:
		case SDL_WINDOWEVENT_SHOWN:
			_Hidden = false;
			break;
		default:
			break;
	}
}


static void Poll_Events(void)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				// Closing the window is the close message the engine already answers; before
				// there is a window to answer it, the process simply ends.
				if (MainWindow != NULL) {
					PostMessage(MainWindow, WM_CLOSE, 0, 0);
				} else {
					exit(EXIT_SUCCESS);
				}
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				Poll_Key(event.key);
				break;
			case SDL_TEXTINPUT:
				Poll_Text(event.text);
				break;
			case SDL_MOUSEMOTION:
				Note_Modifiers(SDL_GetModState());
				_MouseHovering = true;
				Window_Point_To_Game_Point(event.motion.x, event.motion.y, _MouseX, _MouseY);
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				Poll_Mouse_Button(event.button);
				break;
			case SDL_MOUSEWHEEL:
				Poll_Wheel(event.wheel);
				break;
			case SDL_WINDOWEVENT:
				Poll_Window(event.window);
				break;
			default:
				break;
		}
	}
}


void Browser_Service(void)
{
	if (!_Initialized) {
		return;
	}

	Poll_Events();

	if (Measure_Window()) {
		if (MainWindow != NULL) {
			MoveWindow(MainWindow, 0, 0, _CanvasWidth, _CanvasHeight, FALSE);
		}
		Video_On_Resize(_CanvasWidth, _CanvasHeight);
		Video_Request_Frame_Size(_CanvasCSSWidth, _CanvasCSSHeight);
	}

	// Visibility stands in for window focus; a lockstep game paused on focus loss would
	// stall its peers.
	GameInFocus = !_Hidden;

	while (_EventHead != _EventTail) {
		HostEvent const event = _Events[_EventHead];
		_EventHead = (_EventHead + 1) % EVENT_QUEUE_SIZE;

		_EventModifiers = event.Modifiers;
		if (_EventHook != nullptr && _EventHook(event.Key, event.X, event.Y, event.IsMouse, event.IsRelease)) {
			continue;
		}
		if (Keyboard != nullptr) {
			if (event.IsMouse) {
				Keyboard->Post_Mouse_Event(event.Key, event.X, event.Y, event.IsRelease);
			} else {
				Keyboard->Post_Key_Event(event.Key, event.IsRelease);
			}
		}
	}
	_EventModifiers = _Modifiers;
}


void Browser_Set_Event_Hook(BrowserEventHook hook)
{
	_EventHook = hook;
}


// A wait hands the thread to the host for the rest of the frame interval, so an idle
// engine loop costs a frame rather than a core.
void Browser_Yield(void)
{
	if (!_Initialized) {
		return;
	}

	_BlockingWaits++;

	Uint64 const now = SDL_GetTicks64();
	if (now - _LastYield < YIELD_INTERVAL) {
		SDL_Delay((Uint32)(YIELD_INTERVAL - (now - _LastYield)));
	}
	_FrameSerial++;
	_LastYield = SDL_GetTicks64();

	Browser_Service();
}


bool Browser_Yield_If_Due(void)
{
	if (SDL_GetTicks64() - _LastYield < YIELD_INTERVAL) {
		return(false);
	}
	Browser_Yield();
	return(true);
}


unsigned int Browser_Blocking_Wait_Count(void)
{
	return(_BlockingWaits);
}


bool Browser_Yield_Is_Available(void)
{
	return(_Initialized);
}


unsigned int Browser_Frame_Serial(void)
{
	return(_FrameSerial);
}


int Browser_Pending_Events(void)
{
	return((_EventTail - _EventHead + EVENT_QUEUE_SIZE) % EVENT_QUEUE_SIZE);
}


char const * Browser_Canvas_Selector(void)
{
	return("");
}


NativeWindow Browser_Native_Window(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (_Window == nullptr || SDL_GetWindowWMInfo(_Window, &info) != SDL_TRUE) {
		return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, nullptr });
	}
#if defined(SDL_VIDEO_DRIVER_COCOA)
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, info.info.cocoa.window });
#else
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, nullptr });
#endif
}


int Browser_Canvas_Width(void) { return(_CanvasWidth); }
int Browser_Canvas_Height(void) { return(_CanvasHeight); }
int Browser_Canvas_CSS_Width(void) { return(_CanvasCSSWidth); }
int Browser_Canvas_CSS_Height(void) { return(_CanvasCSSHeight); }
int Browser_Screen_Width(void) { return(_ScreenWidth); }
int Browser_Screen_Height(void) { return(_ScreenHeight); }
BrowserDisplayPolicy Browser_Display_Policy(void) { return(BROWSER_DISPLAY_NATIVE); }
int Browser_Display_Width(void) { return(_CanvasWidth); }
int Browser_Display_Height(void) { return(_CanvasHeight); }
bool Browser_Is_Hidden(void) { return(_Hidden); }
unsigned short Browser_Key_Modifiers(void) { return(_Modifiers); }
bool Browser_Key_Is_Down(unsigned short vk_key) { return(_KeyDown[vk_key & 0xFF] != 0); }
unsigned short Browser_Event_Modifiers(void) { return(_EventModifiers); }
int Browser_Mouse_X(void) { return(_MouseX); }
int Browser_Mouse_Y(void) { return(_MouseY); }
bool Browser_Mouse_Is_Hovering(void) { return(_MouseHovering); }


// The engine acts on a queued click several frames after the host reported it, so the
// modifiers it was made under are presented while its message is dispatched.
void Browser_Apply_Modifiers(unsigned short modifiers)
{
	_KeyDown[VK_SHIFT] = ((modifiers & WWKEY_SHIFT_BIT) != 0) ? 1 : 0;
	_KeyDown[VK_CONTROL] = ((modifiers & WWKEY_CTRL_BIT) != 0) ? 1 : 0;
	_KeyDown[VK_MENU] = ((modifiers & WWKEY_ALT_BIT) != 0) ? 1 : 0;
}


int Browser_Key_To_ASCII(unsigned short key)
{
	unsigned short vk_key = key & 0xFF;
	if ((key & WWKEY_SHIFT_BIT) != 0 && _ShiftedAscii[vk_key] != '\0') {
		return(_ShiftedAscii[vk_key]);
	}
	return(_Ascii[vk_key]);
}


// Text input stays on for the life of the window, so that every press reports the
// character the layout gives it.
void Browser_Begin_Text_Input(void) {}
void Browser_End_Text_Input(void) {}


// The host has no cursor to query, so the position is that of the last event over the
// window.
class HostMouseClass : public WWMouseClass
{
	public:
		HostMouseClass(HWND window) : WWMouseClass(window) {}
		virtual int Get_Mouse_X(void) const override {return(Browser_Mouse_X());}
		virtual int Get_Mouse_Y(void) const override {return(Browser_Mouse_Y());}
		virtual Point2D Get_Mouse_Point(void) const override {return(Point2D(Browser_Mouse_X(), Browser_Mouse_Y()));}
		virtual bool Is_Hovering(void) const override {return(Browser_Mouse_Is_Hovering());}
};


Mouse * Browser_Create_Mouse(HWND window)
{
	return(new HostMouseClass(window));
}


static SDL_Cursor * System_Cursor(SDL_SystemCursor id)
{
	if (_SystemCursors[id] == nullptr) {
		_SystemCursors[id] = SDL_CreateSystemCursor(id);
	}
	return(_SystemCursors[id]);
}


// The substitute's pixels are one unsigned long each, which is wider than a pixel on
// this host, so they are packed before SDL sees them.
static SDL_Cursor * Image_Cursor(void const * key, unsigned long const * pixels, int width, int height, int hotx, int hoty)
{
	auto found = _ImageCursors.find(key);
	if (found != _ImageCursors.end()) {
		return(found->second);
	}

	std::vector<Uint32> packed((size_t)width * (size_t)height);
	for (size_t index = 0; index < packed.size(); index++) {
		packed[index] = (Uint32)pixels[index];
	}

	SDL_Cursor * cursor = nullptr;
	SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormatFrom(packed.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ARGB8888);
	if (surface != nullptr) {
		cursor = SDL_CreateColorCursor(surface, hotx, hoty);
		SDL_FreeSurface(surface);
	}
	_ImageCursors[key] = cursor;
	return(cursor);
}


void Browser_Show_Cursor(void const * key, char const * css, unsigned long const * pixels, int width, int height, int hotx, int hoty)
{
	if (css != nullptr && strcmp(css, "none") == 0) {
		SDL_ShowCursor(SDL_DISABLE);
		return;
	}

	SDL_Cursor * cursor = nullptr;
	if (pixels != nullptr && width > 0 && height > 0) {
		cursor = Image_Cursor(key, pixels, width, height, hotx, hoty);
	} else if (css != nullptr && strcmp(css, "wait") == 0) {
		cursor = System_Cursor(SDL_SYSTEM_CURSOR_WAIT);
	} else if (css != nullptr && strcmp(css, "not-allowed") == 0) {
		cursor = System_Cursor(SDL_SYSTEM_CURSOR_NO);
	} else if (css != nullptr && strcmp(css, "pointer") == 0) {
		cursor = System_Cursor(SDL_SYSTEM_CURSOR_HAND);
	} else {
		cursor = System_Cursor(SDL_SYSTEM_CURSOR_ARROW);
	}

	if (cursor != nullptr) {
		SDL_SetCursor(cursor);
	}
	SDL_ShowCursor(SDL_ENABLE);
}


int Browser_Message_Box(char const * caption, char const * text, int buttons)
{
	SDL_MessageBoxButtonData data[3];
	int count = 0;
	auto add = [&](char const * label, int id) {
		data[count].flags = 0;
		data[count].buttonid = id;
		data[count].text = label;
		count++;
	};

	switch (buttons) {
		case 1:		add("OK", IDOK); add("Cancel", IDCANCEL); break;
		case 2:		add("Abort", IDABORT); add("Retry", IDRETRY); add("Ignore", IDIGNORE); break;
		case 3:		add("Yes", IDYES); add("No", IDNO); add("Cancel", IDCANCEL); break;
		case 4:		add("Yes", IDYES); add("No", IDNO); break;
		case 5:		add("Retry", IDRETRY); add("Cancel", IDCANCEL); break;
		default:	add("OK", IDOK); break;
	}
	data[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	data[count - 1].flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

	SDL_MessageBoxData box;
	memset(&box, 0, sizeof(box));
	box.flags = SDL_MESSAGEBOX_INFORMATION;
	box.window = _Window;
	box.title = caption;
	box.message = text;
	box.numbuttons = count;
	box.buttons = data;

	int pressed = 0;
	if (SDL_ShowMessageBox(&box, &pressed) != 0) {
		return(0);
	}
	return(pressed > 0 ? pressed : -1);
}


// The GDI residue draws with the engine's own dialog face, which is the one face this
// host has; the call is the one tactical.cpp makes on a Windows device context.
void Browser_Draw_Caption(Surface & surface, Rect const & rect, char const * text)
{
	HDC hdc = Win32_GDI_Surface_DC(surface);
	if (hdc == NULL) {
		return;
	}

	HFONT font = CreateFontA(28, 20, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
		OUT_RASTER_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FF_SWISS | DEFAULT_PITCH, NULL);
	HGDIOBJ previous = SelectObject(hdc, font);
	SetBkMode(hdc, TRANSPARENT);
	SetTextAlign(hdc, TA_CENTER);
	SetTextColor(hdc, RGB(255, 255, 255));
	TextOutA(hdc, rect.X + rect.Width / 2, rect.Y + rect.Height / 2, text, (int)strlen(text));
	SelectObject(hdc, previous);
	DeleteObject(font);
	DeleteDC(hdc);
}


bool Browser_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		DebugString("Host: SDL_Init failed: %s\n", SDL_GetError());
		return(false);
	}

	_Window = SDL_CreateWindow("OpenTS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		INITIAL_WIDTH, INITIAL_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (_Window == nullptr) {
		DebugString("Host: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return(false);
	}

	SDL_DisplayMode mode;
	if (SDL_GetDesktopDisplayMode(0, &mode) == 0) {
		_ScreenWidth = mode.w;
		_ScreenHeight = mode.h;
	}

	memset(_KeyDown, 0, sizeof(_KeyDown));
	memset(_Ascii, 0, sizeof(_Ascii));
	memset(_ShiftedAscii, 0, sizeof(_ShiftedAscii));
	_Modifiers = 0;
	_EventModifiers = 0;
	Measure_Window();
	SDL_StartTextInput();

	_LastYield = SDL_GetTicks64();
	_Initialized = true;
	GameInFocus = true;

	DebugString("Host: SDL window is %dx%d points and %dx%d pixels on a %dx%d screen.\n",
		_CanvasCSSWidth, _CanvasCSSHeight, _CanvasWidth, _CanvasHeight, _ScreenWidth, _ScreenHeight);
	return(true);
}

#endif	// OPENTS_SDL_HOST
