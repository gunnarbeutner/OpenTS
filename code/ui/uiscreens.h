/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The register of migrated screens, for opening one without reproducing the menu route
// that reaches it. A screen registers itself from its own translation unit, so adding one
// touches no file but its own.

#pragma once


// What a screen's entry point looks like: it runs the screen and reports false only when
// the screen could not be prepared.
typedef bool (*UIScreenOpener)(void);


/// <summary>
/// Adds a screen to the register. Declare one of these at file scope in a screen's own
/// translation unit; the name is what a run asks for and is not shown to a player.
/// </summary>
class UIScreenRegistration
{
	public:
		UIScreenRegistration(char const * name, UIScreenOpener open);
};


// Asks for a screen to be opened at the shell's next service point with no modal screen
// already up, so a request made from outside the engine's thread of control never opens a
// modal screen from inside another one. An unknown index is ignored.
void UI_Request_Screen(int index);

// Opens a requested screen if one is pending. The shell's tick calls this.
void UI_Service_Screen_Request(void);

int UI_Screen_Count(void);

// The registered name at an index, or an empty string when there is none. A run resolves
// the index it wants by name rather than by counting.
char const * UI_Screen_Name(int index);
