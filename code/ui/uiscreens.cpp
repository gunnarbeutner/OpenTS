/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uiscreens.h"

#include "_ui.h"
#include "uishell.h"

#if defined(__EMSCRIPTEN__)
#include "globals.h"
#include "loaddlg.h"
#include "scenario.h"
#include "ui/screens/abort/uiabort.h"
#include "ui/screens/gamectrl/uigamectrl.h"
#include "ui/screens/gameopt/uigameopt.h"
#include "ui/screens/keyboard/uikeyboard.h"
#include "ui/screens/mainopt/uimainopt.h"
#include "ui/screens/mapgen/uimapgen.h"
#include "ui/screens/skirmish/uiskirmish.h"
#include "ui/screens/sound/uisound.h"
#include "ui/screens/version/uiversion.h"
#include "utf8.h"
#include <emscripten/emscripten.h>
#endif

#include <cstring>
#include <vector>


struct UIScreenEntry
{
	char const * Name;
	UIScreenOpener Open;
};


// Reached through a function so that a registration made during static initialization
// cannot run before the container exists.
static std::vector<UIScreenEntry> & Registry(void)
{
	static std::vector<UIScreenEntry> registry;
	return(registry);
}


UIScreenRegistration::UIScreenRegistration(char const * name, UIScreenOpener open)
{
	if (name == nullptr || open == nullptr) {
		return;
	}

	UIScreenEntry entry;
	entry.Name = name;
	entry.Open = open;
	Registry().push_back(entry);
}


static int _Requested = -1;


void UI_Request_Screen(int index)
{
	_Requested = index;
}


void UI_Service_Screen_Request(void)
{
	if (_Requested < 0 || UIShell.Screen_Shown()) {
		return;
	}

	int const index = _Requested;

	// Taken before the screen runs, so a screen that asks for itself again cannot loop.
	_Requested = -1;

	if (index < (int)Registry().size()) {
		Registry()[index].Open();
	}
}


int UI_Screen_Count(void)
{
	return((int)Registry().size());
}


char const * UI_Screen_Name(int index)
{
	if (index < 0 || index >= (int)Registry().size()) {
		return("");
	}

	return(Registry()[index].Name);
}


#if defined(__EMSCRIPTEN__)

static bool Open_Abort(void) { UI_Abort_Dialog(); return(true); }
static bool Open_Game_Controls(void) { UI_Game_Controls_Dialog(); return(true); }
static bool Open_Game_Options(void) { UI_Game_Options_Dialog(); return(true); }
static bool Open_Keyboard(void) { UI_Keyboard_Dialog(); return(true); }
static bool Open_Options(void) { UI_Main_Options_Dialog(); return(true); }
static bool Open_Map_Generator(void) { UI_Map_Generator_Dialog(); return(true); }
static bool Open_Skirmish(void) { UI_Skirmish_Dialog(); return(true); }
static bool Open_Sound(void) { UI_Sound_Dialog(); return(true); }
static bool Open_Version(void) { UI_Version_Dialog(); return(true); }
static bool Open_Load(void) { LoadOptionsClass().Load(); return(true); }
static bool Open_Delete(void) { LoadOptionsClass().Delete(); return(true); }
static bool Open_Save(void)
{
	char description[512] = {};
	if (Scen != nullptr) {
		UTF8::Copy(description, Scen->Description);
	}
	LoadOptionsClass().Save(description, sizeof(description));
	return(true);
}

static UIScreenRegistration _RegisterAbort("abort-mission", Open_Abort);
static UIScreenRegistration _RegisterControls("game-controls", Open_Game_Controls);
static UIScreenRegistration _RegisterGameOptions("game-options", Open_Game_Options);
static UIScreenRegistration _RegisterKeyboard("keyboard", Open_Keyboard);
static UIScreenRegistration _RegisterOptions("options", Open_Options);
static UIScreenRegistration _RegisterMapGen("mapgen", Open_Map_Generator);
static UIScreenRegistration _RegisterSkirmish("skirmish", Open_Skirmish);
static UIScreenRegistration _RegisterSound("sound", Open_Sound);
static UIScreenRegistration _RegisterVersion("version", Open_Version);
static UIScreenRegistration _RegisterLoad("mission-load", Open_Load);
static UIScreenRegistration _RegisterSave("mission-save", Open_Save);
static UIScreenRegistration _RegisterDelete("mission-delete", Open_Delete);

// The browser harness drives a screen through these rather than through a key, so a screen
// can be opened in any phase, the title screen included.
extern "C" {

EMSCRIPTEN_KEEPALIVE void OpenTS_UI_Request_Screen(int index)
{
	UI_Request_Screen(index);
}


EMSCRIPTEN_KEEPALIVE int OpenTS_UI_Screen_Count(void)
{
	return(UI_Screen_Count());
}


EMSCRIPTEN_KEEPALIVE char const * OpenTS_UI_Screen_Name(int index)
{
	return(UI_Screen_Name(index));
}

}

#endif
