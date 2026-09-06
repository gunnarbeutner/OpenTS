/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// SDL's side of the UI shell's input, the counterpart of uiwin32.cpp for the hosts that pump
// SDL events. Nothing else turns an SDL event into a shell event.

#include "always.h"

#include "uisdl.h"

#include "uikeymap.h"
#include "uishell.h"


static unsigned int Current_Modifiers(void)
{
	SDL_Keymod const state = SDL_GetModState();
	unsigned int modifiers = UI_MODIFIER_NONE;

	if ((state & KMOD_SHIFT) != 0) {
		modifiers |= UI_MODIFIER_SHIFT;
	}
	if ((state & KMOD_CTRL) != 0) {
		modifiers |= UI_MODIFIER_CONTROL;
	}
	if ((state & KMOD_ALT) != 0) {
		modifiers |= UI_MODIFIER_ALT;
	}
	if ((state & KMOD_GUI) != 0) {
		modifiers |= UI_MODIFIER_META;
	}

	return(modifiers);
}


// A press that a document took owns its release, so the pointer is held until the button
// comes back up even if the cursor leaves the frame in between.
static bool _Captured = false;


static bool Handle_Button(UIMouseButtonType button, bool down, Point2D const & client)
{
	if (!down && _Captured) {
		_Captured = false;
		SDL_CaptureMouse(SDL_FALSE);

		// The owner of the press owns the release whatever the document now reports, so the
		// release is delivered and consumed either way.
		UI_Handle_Mouse_Button(button, false, client.X, client.Y, Current_Modifiers());
		return(true);
	}

	if (!UI_Handle_Mouse_Button(button, down, client.X, client.Y, Current_Modifiers())) {
		return(false);
	}

	if (down && SDL_CaptureMouse(SDL_TRUE) == 0) {
		_Captured = true;
	}

	return(true);
}


static UIMouseButtonType Button_From_SDL(Uint8 button)
{
	switch (button) {
		case SDL_BUTTON_MIDDLE:	return(UI_MOUSE_MIDDLE);
		case SDL_BUTTON_RIGHT:	return(UI_MOUSE_RIGHT);
		default:				return(UI_MOUSE_LEFT);
	}
}


bool UI_Handle_SDL_Event(SDL_Event const & event, Point2D const & client, unsigned short key)
{
	if (!UI_Is_Initialized()) {
		return(false);
	}

	switch (event.type) {
		case SDL_MOUSEMOTION:
			// A move is never consumed: the game goes on tracking the cursor whatever a
			// document is doing with it.
			UI_Handle_Mouse_Move(client.X, client.Y, Current_Modifiers());
			return(false);

		case SDL_MOUSEBUTTONDOWN:
			return(Handle_Button(Button_From_SDL(event.button.button), true, client));

		case SDL_MOUSEBUTTONUP:
			return(Handle_Button(Button_From_SDL(event.button.button), false, client));

		case SDL_MOUSEWHEEL: {
			// RmlUi scrolls in lines and reads a positive delta as downward, the opposite of
			// the wheel's sign, and travel that rounds to no lines at all still scrolls the
			// way it points.
			float lines = -event.wheel.preciseY;
			if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
				lines = -lines;
			}

			if (lines > -1.0f && lines < 0.0f) {
				lines = -1.0f;
			} else if (lines > 0.0f && lines < 1.0f) {
				lines = 1.0f;
			}

			return(UI_Handle_Mouse_Wheel(lines, Current_Modifiers()));
		}

		case SDL_KEYDOWN:
			return(UI_Handle_Key(UI_Key_From_Virtual(key), true, Current_Modifiers()));

		case SDL_KEYUP:
			return(UI_Handle_Key(UI_Key_From_Virtual(key), false, Current_Modifiers()));

		case SDL_TEXTINPUT:
			// SDL reports only what the layout actually typed, control characters excluded,
			// and reports it as whole UTF-8.
			return(UI_Handle_Text(event.text.text));

		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				if (_Captured) {
					_Captured = false;
					SDL_CaptureMouse(SDL_FALSE);
				}
				UI_On_Focus_Lost();
			}
			return(false);

		default:
			break;
	}

	return(false);
}
