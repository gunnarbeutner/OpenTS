/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The host the native macOS build runs under. An SDL window answers code/hostwindow.h, and
// the event pump makes the same calls into the game that the Windows window procedure makes
// from its messages. The pump runs in engine context, from Windows_Message_Handler, so an
// event never reaches the keyboard buffer part way through a read of it.

#include "always.h"

#if defined(OPENTS_SDL_HOST)

#include "hostwindow.h"

#include "_keyboar.h"
#include "dbgprint.h"
#include "gamewindow.h"
#include "globals.h"
#include "goptions.h"
#include "keyboard.h"
#include "mainwindow.h"
#include "misc.h"
#include "nativewindow.hh"
#include "queue.h"
#include "sdlhost.h"
#include "session.h"
#include "ui/uisdl.h"
#include "video.h"
#include "vidscale.h"
#include "win.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <cctype>
#include <cstring>


#define WINDOW_NAME		"Tiberian Sun"

// WHEEL_DELTA, which is what the sidebar's wheel handler is written against.
static int const WHEEL_NOTCH_DELTA = 120;

static SDL_Window * _Window = nullptr;

// ShowCursor's display count and the image last handed to Host_Set_Cursor. The pointer shows
// only while the count is not negative and an image is wanted, which is how the two Windows
// calls behave together.
static int _ShowCount = 0;
static SDL_Cursor * _CurrentCursor = nullptr;
static bool _CursorHidden = false;

static bool _Captured = false;
static bool _NeedsPaint = false;

// Wheel travel not yet reported, in notches; a trackpad reports fractions of one.
static float _WheelPending = 0.0f;

// The character each key typed when it was last pressed, which is the only account of the
// player's layout this host has.
static char _Ascii[256];
static char _ShiftedAscii[256];
static unsigned short _LastKey = VK_NONE;

static void Apply_Cursor(void);


//----------------------------------------------------------------------------------------
// Keys
//----------------------------------------------------------------------------------------

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


// The table above read backwards. A key code that two scan codes give, such as a modifier
// with a left and a right half, is answered for by the lower of the two.
static SDL_Scancode Scancode_For_Virtual(unsigned short key)
{
	static SDL_Scancode _codes[256];
	static bool _built = false;

	if (!_built) {
		for (int code = SDL_NUM_SCANCODES - 1; code >= 0; code--) {
			unsigned short const mapped = Virtual_Key_For_Scancode((SDL_Scancode)code);
			if (mapped != VK_NONE) {
				_codes[mapped & 0xFF] = (SDL_Scancode)code;
			}
		}
		_built = true;
	}

	return(_codes[key & 0xFF]);
}


unsigned short Host_Key_Modifiers(void)
{
	SDL_Keymod const state = SDL_GetModState();
	unsigned short modifiers = 0;

	if ((state & KMOD_SHIFT) != 0) {
		modifiers |= WWKEY_SHIFT_BIT;
	}
	if ((state & KMOD_CTRL) != 0) {
		modifiers |= WWKEY_CTRL_BIT;
	}
	if ((state & KMOD_ALT) != 0) {
		modifiers |= WWKEY_ALT_BIT;
	}

	return(modifiers);
}


bool Host_Key_Is_Down(unsigned short key)
{
	key &= 0xFF;

	switch (key) {
		case VK_LBUTTON:	return((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);
		case VK_MBUTTON:	return((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0);
		case VK_RBUTTON:	return((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0);
		case VK_SHIFT:		return((SDL_GetModState() & KMOD_SHIFT) != 0);
		case VK_CONTROL:	return((SDL_GetModState() & KMOD_CTRL) != 0);
		case VK_MENU:		return((SDL_GetModState() & KMOD_ALT) != 0);
		default:			break;
	}

	SDL_Scancode const code = Scancode_For_Virtual(key);
	if (code == SDL_SCANCODE_UNKNOWN) {
		return(false);
	}

	int count = 0;
	Uint8 const * state = SDL_GetKeyboardState(&count);
	return(state != nullptr && (int)code < count && state[code] != 0);
}


// Remembers what a key typed, so that a later Host_Key_To_Character can answer for it. SDL
// names the key by the layout's unshifted character; the text event that follows a press
// corrects it for the shift state.
static void Note_Character(unsigned short key, SDL_Keycode symbol)
{
	char character = '\0';

	if (symbol >= 32 && symbol < 127) {
		character = (char)symbol;
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
		if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
			_ShiftedAscii[key & 0xFF] = character;
		} else {
			_Ascii[key & 0xFF] = character;
		}
	}

	_LastKey = key;
}


static void Note_Text(char const * text)
{
	if (_LastKey == VK_NONE || text == nullptr || text[0] == '\0' || (text[0] & 0x80) != 0) {
		return;
	}

	if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
		_ShiftedAscii[_LastKey & 0xFF] = text[0];
	} else {
		_Ascii[_LastKey & 0xFF] = text[0];
	}
}


// Answers with the modifiers the key code carries rather than those held now, as Windows
// does. A key the player has not pressed yet is answered from the layout instead.
int Host_Key_To_Character(unsigned short key)
{
	unsigned short const code = key & 0xFF;
	bool const shifted = (key & WWKEY_SHIFT_BIT) != 0;

	char const remembered = shifted ? _ShiftedAscii[code] : _Ascii[code];
	if (remembered != '\0') {
		return((unsigned char)remembered);
	}

	SDL_Scancode const scancode = Scancode_For_Virtual(code);
	SDL_Keycode const symbol = (scancode != SDL_SCANCODE_UNKNOWN) ? SDL_GetKeyFromScancode(scancode) : SDLK_UNKNOWN;

	if (symbol >= 32 && symbol < 127) {
		return(shifted ? std::toupper((int)symbol) : (int)symbol);
	}

	switch (code) {
		case VK_RETURN:	return('\r');
		case VK_BACK:	return('\b');
		case VK_TAB:	return('\t');
		case VK_ESCAPE:	return(27);
		default:		return(0);
	}
}


//----------------------------------------------------------------------------------------
// The window
//----------------------------------------------------------------------------------------

static bool Ensure_Video(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
		return(true);
	}

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		DebugString("Host: SDL video failed to start: %s\n", SDL_GetError());
		return(false);
	}

	return(true);
}


// The window is measured in points and drawn in pixels, and the engine works in pixels
// throughout, so every position and size crossing this file is scaled by their ratio.
static double Pixel_Ratio(void)
{
	if (_Window == nullptr) {
		return(1.0);
	}

	int points = 0;
	int pixels = 0;
	SDL_GetWindowSize(_Window, &points, nullptr);
	SDL_GetWindowSizeInPixels(_Window, &pixels, nullptr);

	return(points > 0 ? (double)pixels / (double)points : 1.0);
}


static Point2D Client_Point(int windowx, int windowy)
{
	double const ratio = Pixel_Ratio();
	return(Point2D((int)(windowx * ratio + 0.5), (int)(windowy * ratio + 0.5)));
}


bool Has_Main_Window(void)
{
	return(_Window != nullptr);
}


void Host_Create_Window(int width, int height)
{
	if (_Window != nullptr || !Ensure_Video()) {
		return;
	}

	Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
	int clientwidth = width;
	int clientheight = height;

	if (WindowedMode) {
		flags |= SDL_WINDOW_RESIZABLE;
		if (Options.WindowWidth > 0) clientwidth = Options.WindowWidth;
		if (Options.WindowHeight > 0) clientheight = Options.WindowHeight;
	} else {
		// The desktop keeps its own resolution and the window simply covers it. The frame is
		// scaled to fit at presentation time.
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	_Window = SDL_CreateWindow(WINDOW_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		clientwidth, clientheight, flags);
	if (_Window == nullptr) {
		DebugString("Host: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return;
	}

	memset(_Ascii, '\0', sizeof(_Ascii));
	memset(_ShiftedAscii, '\0', sizeof(_ShiftedAscii));

	// Text input stays on for the life of the window, so that every press reports the
	// character the layout gives it.
	SDL_StartTextInput();
	SDL_RaiseWindow(_Window);

	GameInFocus = true;
	Apply_Cursor();
	Game_Window_Created();

	int drawablewidth = 0;
	int drawableheight = 0;
	SDL_GetWindowSizeInPixels(_Window, &drawablewidth, &drawableheight);
	DebugString("Host: the SDL window is %dx%d points and %dx%d pixels.\n",
		clientwidth, clientheight, drawablewidth, drawableheight);
}


void Host_Close_Window(void)
{
	if (_Window == nullptr) {
		return;
	}

	SDL_StopTextInput();
	SDL_DestroyWindow(_Window);
	_Window = nullptr;

	Game_Window_Destroyed();
}


NativeWindow Host_Native_Window(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if (_Window == nullptr || SDL_GetWindowWMInfo(_Window, &info) != SDL_TRUE) {
		return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, nullptr });
	}

#if defined(SDL_VIDEO_DRIVER_COCOA)
	// bgfx takes the NSWindow and attaches a Metal layer to its content view itself.
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, info.info.cocoa.window });
#else
	return(NativeWindow{ NATIVE_WINDOW_DEFAULT, nullptr, nullptr });
#endif
}


bool Host_Window_Drawable_Size(int & width, int & height)
{
	if (_Window == nullptr) {
		return(false);
	}

	width = 0;
	height = 0;
	SDL_GetWindowSizeInPixels(_Window, &width, &height);
	return(width > 0 && height > 0);
}


int Host_Window_Refresh_Rate(void)
{
	SDL_DisplayMode mode;
	int const display = (_Window != nullptr) ? SDL_GetWindowDisplayIndex(_Window) : 0;

	if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0) {
		return(mode.refresh_rate);
	}

	return(0);
}


// SDL repaints from the pump rather than from a call, so the request is answered on the way
// out of the next one.
void Host_Invalidate_Window(void)
{
	_NeedsPaint = true;
}


void Host_Focus_Window(void)
{
	if (_Window != nullptr) {
		SDL_RaiseWindow(_Window);
	}
}


void Host_Fit_Window_To_Frame(int width, int height)
{
	if (_Window == nullptr) {
		return;
	}

	double const ratio = Pixel_Ratio();
	int const newwidth = (int)(width / ratio + 0.5);
	int const newheight = (int)(height / ratio + 0.5);

	int x = 0;
	int y = 0;
	int currentwidth = 0;
	int currentheight = 0;
	SDL_GetWindowPosition(_Window, &x, &y);
	SDL_GetWindowSize(_Window, &currentwidth, &currentheight);

	// The window grows about its middle rather than its corner, so the picture stays where
	// the player was looking.
	x += (currentwidth - newwidth) / 2;
	y += (currentheight - newheight) / 2;

	// Growing about the middle can push the window past the edges of the screen, and a title
	// bar above the top of it cannot be grabbed to bring the window back.
	SDL_Rect usable;
	if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(_Window), &usable) == 0) {
		if (x + newwidth > usable.x + usable.w) x = usable.x + usable.w - newwidth;
		if (y + newheight > usable.y + usable.h) y = usable.y + usable.h - newheight;
		if (x < usable.x) x = usable.x;
		if (y < usable.y) y = usable.y;
	}

	SDL_SetWindowSize(_Window, newwidth, newheight);
	SDL_SetWindowPosition(_Window, x, y);
}


bool Host_Display_Mode(int index, int & width, int & height)
{
	if (!Ensure_Video()) {
		return(false);
	}

	SDL_DisplayMode mode;
	if (index < 0 || SDL_GetDisplayMode(0, index, &mode) != 0) {
		return(false);
	}

	width = mode.w;
	height = mode.h;
	return(true);
}


HostMessageBoxAnswer Host_Message_Box(char const * caption, char const * text, unsigned int style)
{
	bool const question = (style & HOST_BOX_YES_NO) != 0;

	SDL_MessageBoxButtonData buttons[2];
	memset(buttons, 0, sizeof(buttons));

	if (question) {
		buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
		buttons[0].buttonid = HOST_ANSWER_YES;
		buttons[0].text = "Yes";
		buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
		buttons[1].buttonid = HOST_ANSWER_NO;
		buttons[1].text = "No";
	} else {
		buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
		buttons[0].buttonid = HOST_ANSWER_OK;
		buttons[0].text = "OK";
	}

	Uint32 icon = SDL_MESSAGEBOX_INFORMATION;
	if ((style & HOST_BOX_ERROR) == HOST_BOX_ERROR) icon = SDL_MESSAGEBOX_ERROR;
	if ((style & HOST_BOX_WARNING) == HOST_BOX_WARNING) icon = SDL_MESSAGEBOX_WARNING;

	SDL_MessageBoxData box;
	memset(&box, 0, sizeof(box));
	box.flags = icon;
	box.window = _Window;
	box.title = caption;
	box.message = text;
	box.numbuttons = question ? 2 : 1;
	box.buttons = buttons;

	int pressed = -1;
	if (!Ensure_Video() || SDL_ShowMessageBox(&box, &pressed) != 0 || pressed < 0) {
		return(question ? HOST_ANSWER_NO : HOST_ANSWER_OK);
	}

	return((HostMessageBoxAnswer)pressed);
}


//----------------------------------------------------------------------------------------
// The pointer and its image
//----------------------------------------------------------------------------------------

static void Apply_Cursor(void)
{
	SDL_SetCursor(_CurrentCursor != nullptr ? _CurrentCursor : SDL_GetDefaultCursor());
	SDL_ShowCursor((_ShowCount >= 0 && !_CursorHidden) ? SDL_ENABLE : SDL_DISABLE);
}


Point2D Host_Pointer_Position(void)
{
	int pointerx = 0;
	int pointery = 0;
	SDL_GetGlobalMouseState(&pointerx, &pointery);

	int windowx = 0;
	int windowy = 0;
	if (_Window != nullptr) {
		SDL_GetWindowPosition(_Window, &windowx, &windowy);
	}

	return(Client_Point(pointerx - windowx, pointery - windowy));
}


void Host_Move_Pointer(Point2D const & position)
{
	if (_Window == nullptr) {
		return;
	}

	double const ratio = Pixel_Ratio();
	SDL_WarpMouseInWindow(_Window, (int)(position.X / ratio + 0.5), (int)(position.Y / ratio + 0.5));
}


int Host_Show_Pointer(bool show)
{
	_ShowCount += show ? 1 : -1;
	Apply_Cursor();
	return(_ShowCount);
}


void Host_Confine_Pointer(bool confine)
{
	if (_Window != nullptr) {
		SDL_SetWindowMouseGrab(_Window, confine ? SDL_TRUE : SDL_FALSE);
	}
}


void Host_Capture_Pointer(void)
{
	if (SDL_CaptureMouse(SDL_TRUE) == 0) {
		_Captured = true;
	}
}


void Host_Release_Pointer(void)
{
	if (_Captured) {
		SDL_CaptureMouse(SDL_FALSE);
		_Captured = false;
	}
}


bool Host_Pointer_Is_Captured(void)
{
	return(_Captured);
}


// SDL reports no system drag threshold, so Windows's own default stands in for it.
Point2D Host_Drag_Threshold(void)
{
	return(Point2D(4, 4));
}


HostCursor * Host_Create_Cursor(std::uint32_t const * pixels, int width, int height, int hotx, int hoty)
{
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return(nullptr);
	}

	// SDL_CreateColorCursor takes its own copy of the pixels, so the surface only describes
	// the caller's buffer and neither outlives this call.
	SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint32_t *>(pixels),
		width, height, 32, width * 4, SDL_PIXELFORMAT_ARGB8888);
	if (surface == nullptr) {
		return(nullptr);
	}

	SDL_Cursor * cursor = SDL_CreateColorCursor(surface, hotx, hoty);
	SDL_FreeSurface(surface);

	return((HostCursor *)cursor);
}


void Host_Destroy_Cursor(HostCursor * cursor)
{
	if (cursor == nullptr) {
		return;
	}

	if ((SDL_Cursor *)cursor == _CurrentCursor) {
		_CurrentCursor = nullptr;
		Apply_Cursor();
	}

	SDL_FreeCursor((SDL_Cursor *)cursor);
}


void Host_Set_Cursor(HostCursor * cursor)
{
	_CurrentCursor = (SDL_Cursor *)cursor;
	_CursorHidden = false;
	Apply_Cursor();
}


void Host_Hide_Cursor(void)
{
	_CursorHidden = true;
	Apply_Cursor();
}


//----------------------------------------------------------------------------------------
// The event pump
//----------------------------------------------------------------------------------------

static void Handle_Key(SDL_Event const & event)
{
	unsigned short const key = Virtual_Key_For_Scancode(event.key.keysym.scancode);
	if (key == VK_NONE) {
		return;
	}

	bool const release = (event.type == SDL_KEYUP);
	if (!release) {
		Note_Character(key, event.key.keysym.sym);
	}

	// Before the game's own handling, so input a document took never enters the KN_ queue.
	if (UI_Handle_SDL_Event(event, Point2D(0, 0), key)) {
		return;
	}

	// Scroll Lock was a debugger's breakpoint key and types nothing. A key SDL repeats while
	// held is taken only once.
	if (!release && (event.key.repeat != 0 || key == VK_SCROLL)) {
		return;
	}

	if (Keyboard != nullptr) {
		Keyboard->Post_Key_Event(key, release);
	}
}


static void Handle_Mouse_Button(SDL_Event const & event)
{
	Point2D const client = Client_Point(event.button.x, event.button.y);

	unsigned short key;
	switch (event.button.button) {
		case SDL_BUTTON_MIDDLE:	key = VK_MBUTTON;	break;
		case SDL_BUTTON_RIGHT:	key = VK_RBUTTON;	break;
		default:				key = VK_LBUTTON;	break;
	}

	if (UI_Handle_SDL_Event(event, client, VK_NONE)) {
		return;
	}

	Point2D point = client;
	Window_Point_To_Game(point);

	// SDL counts the clicks of a double click instead of raising an event of its own, so the
	// second press is what WM_LBUTTONDBLCLK is on Windows.
	if (event.type == SDL_MOUSEBUTTONDOWN && (event.button.clicks % 2) == 0) {
		Game_Window_Mouse_Double_Click(key, point);
		return;
	}

	Game_Window_Mouse_Button(key, point, event.type == SDL_MOUSEBUTTONUP);
}


static void Handle_Wheel(SDL_Event const & event)
{
	int pointerx = 0;
	int pointery = 0;
	SDL_GetMouseState(&pointerx, &pointery);

	if (UI_Handle_SDL_Event(event, Client_Point(pointerx, pointery), VK_NONE)) {
		return;
	}

	float travel = event.wheel.preciseY;
	if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
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
		Game_Window_On_Mouse_Wheel(forward ? WHEEL_NOTCH_DELTA : -WHEEL_NOTCH_DELTA);
	}
}


static void Handle_Close(void)
{
	// A running game resigns rather than closing, and keeps its window: the exit is played
	// through the queue, and the game ends itself once it arrives.
	if (GameActive && PlayerPtr != nullptr && !Session.Play) {
		Queue_Exit();
		return;
	}

	Host_Close_Window();
}


static void Handle_Window(SDL_Event const & event)
{
	UI_Handle_SDL_Event(event, Point2D(0, 0), VK_NONE);

	switch (event.window.event) {
		case SDL_WINDOWEVENT_CLOSE:
			Handle_Close();
			break;

		case SDL_WINDOWEVENT_EXPOSED:
			Game_Window_On_Paint(GameInFocus == true || WindowedMode == true);
			break;

		case SDL_WINDOWEVENT_SIZE_CHANGED: {
			int width = 0;
			int height = 0;
			if (Host_Window_Drawable_Size(width, height)) {
				Video_On_Resize(width, height);
				Video_Set_Refresh_Rate(Host_Window_Refresh_Rate());
			}
			break;
		}

		case SDL_WINDOWEVENT_FOCUS_GAINED:
			if (!GameInFocus) {
				GameInFocus = true;
				Focus_Restore();
				DebugString("Focus gained\n");
			}
			break;

		case SDL_WINDOWEVENT_FOCUS_LOST:
			if (_Captured) {
				Host_Release_Pointer();
				Game_Window_Pointer_Capture_Lost();
			}
			if (GameInFocus) {
				GameInFocus = false;
				Focus_Loss();
				DebugString("Focus lost\n");
			}
			break;

		default:
			break;
	}
}


void Host_Pump_Events(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				Handle_Close();
				break;

			case SDL_KEYDOWN:
			case SDL_KEYUP:
				Handle_Key(event);
				break;

			case SDL_TEXTINPUT:
				Note_Text(event.text.text);
				UI_Handle_SDL_Event(event, Point2D(0, 0), VK_NONE);
				break;

			case SDL_MOUSEMOTION:
				// A move is never consumed: the game goes on tracking the cursor whatever a
				// document is doing with it.
				UI_Handle_SDL_Event(event, Client_Point(event.motion.x, event.motion.y), VK_NONE);
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				Handle_Mouse_Button(event);
				break;

			case SDL_MOUSEWHEEL:
				Handle_Wheel(event);
				break;

			case SDL_WINDOWEVENT:
				Handle_Window(event);
				break;

			default:
				break;
		}

		// An event can take the window away, and the rest of the queue belongs to a window
		// that no longer exists.
		if (_Window == nullptr) {
			break;
		}
	}

	if (_NeedsPaint && _Window != nullptr) {
		_NeedsPaint = false;
		Game_Window_On_Paint(GameInFocus == true || WindowedMode == true);
	}
}

#endif	// OPENTS_SDL_HOST
