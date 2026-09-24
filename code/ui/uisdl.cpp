/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uisdl.h"

#include "_ui.h"
#include "ui/uishell.h"
#include "utf8.h"


static int Button_From_SDL(Uint8 button)
{
	switch (button) {
		case SDL_BUTTON_RIGHT: return(1);
		case SDL_BUTTON_MIDDLE: return(2);
		case SDL_BUTTON_X1: return(3);
		case SDL_BUTTON_X2: return(4);
		default: return(0);
	}
}


bool UI_Handle_SDL_Event(SDL_Event const & event, Point2D const & client, unsigned short key)
{
	UIHostEvent host;
	host.X = client.X;
	host.Y = client.Y;

	switch (event.type) {
		case SDL_MOUSEMOTION:
			host.Type = UI_HOST_MOVE;
			UIShell.Handle_Host_Event(host);
			return(false);

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			host.Type = event.type == SDL_MOUSEBUTTONDOWN ? UI_HOST_BUTTON_DOWN : UI_HOST_BUTTON_UP;
			host.Button = Button_From_SDL(event.button.button);
			return(UIShell.Handle_Host_Event(host));

		case SDL_MOUSEWHEEL:
			host.Type = UI_HOST_WHEEL;
			host.Wheel = -event.wheel.preciseY;
			if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
				host.Wheel = -host.Wheel;
			}
			if (host.Wheel > -1.0f && host.Wheel < 0.0f) {
				host.Wheel = -1.0f;
			} else if (host.Wheel > 0.0f && host.Wheel < 1.0f) {
				host.Wheel = 1.0f;
			}
			return(UIShell.Handle_Host_Event(host));

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			host.Type = event.type == SDL_KEYDOWN ? UI_HOST_KEY_DOWN : UI_HOST_KEY_UP;
			host.Key = key;
			host.Repeat = event.key.repeat != 0;
			return(UIShell.Handle_Host_Event(host));

		case SDL_TEXTINPUT: {
			host.Type = UI_HOST_TEXT;
			bool consumed = false;
			char const * cursor = event.text.text;
			while (*cursor != '\0') {
				host.Text = UTF8::Decode(cursor);
				consumed = UIShell.Handle_Host_Event(host) || consumed;
			}
			return(consumed);
		}

		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				host.Type = UI_HOST_FOCUS;
				host.Focused = event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
				UIShell.Handle_Host_Event(host);
			}
			return(false);

		default:
			return(false);
	}
}
