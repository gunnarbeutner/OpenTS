/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Pins how the keyboard screen spells a binding. Windows names keys from the player's layout,
// so there the checks are on the shape of the answer; elsewhere the names are a US layout's.

#include "always.h"

#include "keyname.h"

#include <cstdio>
#include <cstring>

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-76s %s\n", what, condition ? "ok" : "FAILED");

	if (!condition) {
		Failures++;
	}
}


int Count(char const * text, char wanted)
{
	int count = 0;
	for (; *text != '\0'; text++) {
		count += (*text == wanted) ? 1 : 0;
	}
	return(count);
}

}


int main(void)
{
	char letter[64];
	Build_Hotkey_String((KeyNumType)VK_A, letter);
	Check(letter[0] != '\0' && Count(letter, '+') == 0, "A letter is named with no modifier");

	char function[64];
	Build_Hotkey_String((KeyNumType)VK_F1, function);
	Check(function[0] != '\0' && std::strcmp(function, letter) != 0, "A function key has a name of its own");

	char all[64];
	Build_Hotkey_String((KeyNumType)(VK_F1 | WWKEY_ALT_BIT | WWKEY_CTRL_BIT | WWKEY_SHIFT_BIT), all);
	Check(Count(all, '+') == 3, "Each modifier is joined by a plus");
	char const * tail = std::strrchr(all, '+');
	Check(tail != nullptr && std::strcmp(tail + 1, function) == 0, "The key comes after the modifiers");

	char unnamed[64];
	std::memset(unnamed, 'x', sizeof(unnamed));
	Build_Hotkey_String((KeyNumType)0, unnamed);
	Check(unnamed[0] == '\0', "A key with no name leaves an empty string");

#if !defined(_WIN32)
	char text[64];
	Build_Hotkey_String((KeyNumType)(VK_A | WWKEY_ALT_BIT | WWKEY_CTRL_BIT | WWKEY_SHIFT_BIT), text);
	Check(std::strcmp(text, "Alt+Ctrl+Shift+A") == 0, "The modifiers come in the order Alt, Ctrl, Shift");

	Build_Hotkey_String((KeyNumType)VK_HOME, text);
	Check(std::strcmp(text, "Num 7") == 0, "Home without the extended flag is named as the keypad key");

	Build_Hotkey_String((KeyNumType)(VK_HOME | WWKEY_RLS_BIT), text);
	Check(std::strcmp(text, "Home") == 0, "Home with the extended flag is named Home");

	Build_Hotkey_String((KeyNumType)VK_NONE_BD, text);
	Check(std::strcmp(text, "-") == 0, "A punctuation key is named by its character");
#endif

	std::printf("keyname: %d failures\n", Failures);
	return(Failures == 0 ? 0 : 1);
}
