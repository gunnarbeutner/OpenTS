/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The page's side of the shell's input. The page hands each of its events to one hook before
// the keyboard buffer sees it; the shell is that hook, and what no document claims goes on to
// the game, so a claimed event never reaches the KN_ queue, as on Windows.

#include "always.h"

#include "uibrowser.h"

#include "browser.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "uikeymap.h"
#include "uishell.h"
#include "utf8.h"
#include "video.h"


// The hook reports a position in the game's frame, and the shell works in the window's
// client pixels, so the mapping the presenter uses is applied in reverse.
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
	if (!UI_Is_Initialized()) {
		return(false);
	}

	unsigned int const modifiers = UI_Modifiers_From_Bits(Browser_Event_Modifiers());

	if (is_mouse) {
		int clientx = 0;
		int clienty = 0;
		Game_To_Client(x, y, clientx, clienty);

		UIMouseButtonType button = UI_MOUSE_LEFT;

		switch (key & 0xFF) {
			case VK_RBUTTON:
				button = UI_MOUSE_RIGHT;
				break;

			case VK_MBUTTON:
				button = UI_MOUSE_MIDDLE;
				break;

			default:
				button = UI_MOUSE_LEFT;
				break;
		}

		if (UI_Handle_Mouse_Button(button, !is_release, clientx, clienty, modifiers)) {
			return(true);
		}

		return(false);
	}

	UIKeyType const identity = UI_Key_From_Virtual(key & 0xFF);
	bool taken = UI_Handle_Key(identity, !is_release, modifiers);

	// The character a key stands for is typed whether or not the key itself was taken, as a
	// window receives its character message after its key message either way: a text field
	// takes the space key and still has to be handed the space.
	// Control with a letter is a command; control with alt is AltGr, which types.
	unsigned short const held = Browser_Event_Modifiers();
	bool const command = (held & WWKEY_CTRL_BIT) != 0 && (held & WWKEY_ALT_BIT) == 0;

	if (!is_release && !command) {
		char32_t const character = Browser_Event_Character();

		if (UTF8::Is_Printable(character)) {
			char text[UTF8::MAX_SEQUENCE + 1] = {};
			UTF8::Encode(character, text);

			if (UI_Handle_Text(text)) {
				taken = true;
			}
		}
	}

	return(taken);
}


// The last position a move was delivered for, in the game's frame. The page reports the
// pointer's position whether or not it changed, and RmlUi does work on every move.
static int _LastX = -1;
static int _LastY = -1;


void UI_Browser_Service_Mouse(void)
{
	if (!UI_Is_Initialized()) {
		return;
	}

	int const gamex = Browser_Mouse_X();
	int const gamey = Browser_Mouse_Y();

	if (gamex == _LastX && gamey == _LastY) {
		return;
	}

	_LastX = gamex;
	_LastY = gamey;

	int clientx = 0;
	int clienty = 0;
	Game_To_Client(gamex, gamey, clientx, clienty);

	UI_Handle_Mouse_Move(clientx, clienty, UI_Modifiers_From_Bits(Browser_Event_Modifiers()));
}


bool UI_Browser_Takes_Touch_Press(int gamex, int gamey)
{
	if (!UI_Is_Initialized()) {
		return(false);
	}

	int clientx = 0;
	int clienty = 0;
	Game_To_Client(gamex, gamey, clientx, clienty);
	return(UI_Point_Is_Over_Document(clientx, clienty));
}


// Whether the keyboard up is the one a field asked for, so one the score screen raised is
// never put away from here.
static bool _TextInputAsked = false;


void UI_Browser_Service_Text_Input(void)
{
	bool const wanted = UI_Text_Field_Has_Focus();

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
