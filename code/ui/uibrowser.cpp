/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uibrowser.h"

#include "_ui.h"
#include "browser.h"
#include "keyboard.h"
#include "uishell.h"
#include "video.h"

#include <RmlUi/Core.h>


static void Game_To_Client(int gamex, int gamey, int & clientx, int & clienty)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	float const scalex = scale.ScaleX > 0.0f ? scale.ScaleX : 1.0f;
	float const scaley = scale.ScaleY > 0.0f ? scale.ScaleY : 1.0f;
	clientx = scale.DestX + (int)((float)gamex * scalex);
	clienty = scale.DestY + (int)((float)gamey * scaley);
}


static bool Handle_Event(unsigned short key, int x, int y, bool is_mouse, bool is_release)
{
	UIHostEvent event;
	if (is_mouse) {
		event.Type = is_release ? UI_HOST_BUTTON_UP : UI_HOST_BUTTON_DOWN;
		Game_To_Client(x, y, event.X, event.Y);
		switch (key & 0xFF) {
			case VK_RBUTTON: event.Button = 1; break;
			case VK_MBUTTON: event.Button = 2; break;
			case VK_XBUTTON1: event.Button = 3; break;
			case VK_XBUTTON2: event.Button = 4; break;
			default: event.Button = 0; break;
		}
		return(UIShell.Handle_Host_Event(event));
	}

	event.Type = is_release ? UI_HOST_KEY_UP : UI_HOST_KEY_DOWN;
	event.Key = key & 0xFF;
	bool taken = UIShell.Handle_Host_Event(event);

	unsigned short const held = Browser_Event_Modifiers();
	bool const command = (held & WWKEY_CTRL_BIT) != 0 && (held & WWKEY_ALT_BIT) == 0;
	if (!is_release && !command) {
		char32_t const character = Browser_Event_Character();
		if (character >= 32 && character != 127) {
			event.Type = UI_HOST_TEXT;
			event.Text = character;
			taken = UIShell.Handle_Host_Event(event) || taken;
		}
	}
	return(taken);
}


static int _LastX = -1;
static int _LastY = -1;


void UI_Browser_Service_Mouse(void)
{
	int const gamex = Browser_Mouse_X();
	int const gamey = Browser_Mouse_Y();
	if (gamex == _LastX && gamey == _LastY) {
		return;
	}
	_LastX = gamex;
	_LastY = gamey;

	UIHostEvent event;
	event.Type = UI_HOST_MOVE;
	Game_To_Client(gamex, gamey, event.X, event.Y);
	UIShell.Handle_Host_Event(event);
}


bool UI_Browser_Takes_Touch_Press(int gamex, int gamey)
{
	if (UIShell.Modal() == nullptr) {
		return(false);
	}
	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	return(gamex >= 0 && gamey >= 0 && gamex < scale.GameWidth && gamey < scale.GameHeight);
}


static bool _TextInputAsked = false;


void UI_Browser_Service_Text_Input(void)
{
	bool wanted = false;
	if (Rml::Context * context = UIShell.Rml_Context()) {
		if (Rml::Element * focus = context->GetFocusElement()) {
			Rml::String const & tag = focus->GetTagName();
			wanted = tag == "input" || tag == "textarea";
		}
	}

	if (wanted && !_TextInputAsked) {
		_TextInputAsked = true;
		Browser_Begin_Text_Input();
	} else if (!wanted && _TextInputAsked) {
		_TextInputAsked = false;
		Browser_End_Text_Input();
	}
}


void UI_Browser_Install_Hook(void)
{
	Browser_Set_Event_Hook(Handle_Event);
}


void UI_Browser_Remove_Hook(void)
{
	Browser_Set_Event_Hook(nullptr);
	if (_TextInputAsked) {
		_TextInputAsked = false;
		Browser_End_Text_Input();
	}
}
