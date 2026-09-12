/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window system's side of the UI shell's input. Nothing else translates a message into
// a shell event, and the shell itself names no message, so this file is what a port to
// another window system replaces.

#include "always.h"

#include "uiwin32.h"

// The engine declares the virtual key codes itself rather than taking them from the window
// system's headers, so the translation reads them from there.
#include "keyboard.h"
#include "uikeymap.h"
#include "uishell.h"
#include "utf8.h"


static unsigned int Current_Modifiers(void)
{
	unsigned int modifiers = UI_MODIFIER_NONE;

	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
		modifiers |= UI_MODIFIER_SHIFT;
	}
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
		modifiers |= UI_MODIFIER_CONTROL;
	}
	if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
		modifiers |= UI_MODIFIER_ALT;
	}

	return(modifiers);
}


// A press that a document took owns its release, so the window keeps the mouse until the
// button comes back up even if the cursor leaves the frame in between.
static bool _Captured = false;


static bool Handle_Button(HWND window, UIMouseButtonType button, bool down, LPARAM lparam)
{
	int x = (int)(short)LOWORD(lparam);
	int y = (int)(short)HIWORD(lparam);

	if (!down && _Captured) {
		_Captured = false;
		ReleaseCapture();

		// The owner of the press owns the release whatever the document now reports, so
		// the release is delivered and consumed either way.
		UI_Handle_Mouse_Button(button, false, x, y, Current_Modifiers());
		return(true);
	}

	if (!UI_Handle_Mouse_Button(button, down, x, y, Current_Modifiers())) {
		return(false);
	}

	if (down) {
		_Captured = true;
		SetCapture(window);
	}

	return(true);
}


bool UI_Handle_Window_Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (!UI_Is_Initialized()) {
		return(false);
	}

	switch (message) {
		case WM_MOUSEMOVE:
			// A move is never consumed: the game goes on tracking the cursor whatever a
			// document is doing with it.
			UI_Handle_Mouse_Move((int)(short)LOWORD(lparam), (int)(short)HIWORD(lparam), Current_Modifiers());
			return(false);

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			return(Handle_Button(window, UI_MOUSE_LEFT, true, lparam));

		case WM_LBUTTONUP:
			return(Handle_Button(window, UI_MOUSE_LEFT, false, lparam));

		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
			return(Handle_Button(window, UI_MOUSE_RIGHT, true, lparam));

		case WM_RBUTTONUP:
			return(Handle_Button(window, UI_MOUSE_RIGHT, false, lparam));

		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
			return(Handle_Button(window, UI_MOUSE_MIDDLE, true, lparam));

		case WM_MBUTTONUP:
			return(Handle_Button(window, UI_MOUSE_MIDDLE, false, lparam));

		case WM_MOUSEWHEEL: {
			// One notch is 120 units. RmlUi scrolls in lines and reads a positive delta
			// as downward, the opposite of the wheel's sign, and a delta that rounds to
			// no lines at all still scrolls the way it points.
			const float notch = 120.0f;
			float lines = -(float)(short)HIWORD(wparam) / notch;

			if (lines > -1.0f && lines < 0.0f) {
				lines = -1.0f;
			} else if (lines > 0.0f && lines < 1.0f) {
				lines = 1.0f;
			}

			return(UI_Handle_Mouse_Wheel(lines, Current_Modifiers()));
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			return(UI_Handle_Key(UI_Key_From_Virtual((unsigned int)wparam), true, Current_Modifiers()));

		case WM_KEYUP:
		case WM_SYSKEYUP:
			return(UI_Handle_Key(UI_Key_From_Virtual((unsigned int)wparam), false, Current_Modifiers()));

		case WM_CHAR: {
			// Control characters reach a document as key events instead. The manifest makes
			// the process code page UTF-8, so a narrow window can be sent a character as one
			// message per byte; the bytes are gathered until the sequence is whole.
			static char _pending[UTF8::MAX_SEQUENCE + 1];
			static int _have = 0;
			static int _want = 0;

			if (wparam < ' ') {
				_have = 0;
				return(false);
			}

			unsigned char const byte = (unsigned char)wparam;

			if (wparam > 0xFF || byte < 0x80) {
				_have = 0;
				if (!UTF8::Is_Printable((char32_t)wparam) || (wparam & 0xF800) == 0xD800) {
					return(false);
				}
				char text[UTF8::MAX_SEQUENCE + 1] = {};
				UTF8::Encode((char32_t)wparam, text);
				return(UI_Handle_Text(text));
			}

			if (!UTF8::Is_Continuation(byte)) {
				_have = 0;
				_want = UTF8::Sequence_Length(byte);
			}
			if (_want == 0 || (_have == 0 && UTF8::Is_Continuation(byte))) {
				return(true);
			}

			_pending[_have++] = (char)byte;

			if (_have < _want) {
				return(true);
			}

			_pending[_have] = '\0';
			_have = 0;
			return(UI_Handle_Text(_pending));
		}

		case WM_KILLFOCUS:
			if (_Captured) {
				_Captured = false;
				ReleaseCapture();
			}
			UI_On_Focus_Lost();
			return(false);

		default:
			break;
	}

	return(false);
}
