/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <functional>
#include <vector>


// One button of a menu screen: the id its document gives it, what pressing it answers, and
// whether it takes a press at all.
struct UIMenuChoice
{
	char const * Name = nullptr;
	int Answer = 0;
	bool Enabled = true;
};


// A screen that is a column of buttons, each of which ends it.
struct UIMenuRequest
{
	char const * Document = nullptr;

	// A class set on the document's #menu element, for a document serving two templates.
	char const * Variant = nullptr;

	std::vector<UIMenuChoice> Choices;

	// Enter and Escape reached these dialogs as IDOK and IDCANCEL, and a dialog that ended on
	// any command ended on those too.
	bool KeysAnswer = false;

	// The driver loop's own work each pass; an answer other than NoAnswer ends the menu.
	std::function<int(std::function<void(bool)> const & show)> Each_Pass;
	int NoAnswer = 0;
};


// False means the screen could not be prepared; a session ending under it leaves answer alone.
bool UI_Menu_Screen(UIMenuRequest const & request, int & answer);
