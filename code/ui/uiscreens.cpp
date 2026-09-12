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

#include "uishell.h"

#if defined(__EMSCRIPTEN__)
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
	if (_Requested < 0 || UI_Modal_Is_Open() || UI_Is_Closing()) {
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
