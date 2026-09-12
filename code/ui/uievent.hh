/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// The UI shell's input vocabulary. It is deliberately not Win32's: the window procedure
// hook translates messages into these, so retiring the Win32 message loop costs the
// translator and nothing inside the shell.

enum UIMouseButtonType
{
	UI_MOUSE_LEFT,
	UI_MOUSE_RIGHT,
	UI_MOUSE_MIDDLE,
};


// Held modifiers at the moment the event was raised, as a bit set.
enum UIModifierType
{
	UI_MODIFIER_NONE = 0,
	UI_MODIFIER_SHIFT = 1 << 0,
	UI_MODIFIER_CONTROL = 1 << 1,
	UI_MODIFIER_ALT = 1 << 2,
	UI_MODIFIER_META = 1 << 3,
};


// A key identity independent of the keyboard layout and of the platform's own codes. Only
// the keys the shell acts on are named; a key outside this set reaches a document as text
// or not at all.
enum UIKeyType
{
	UI_KEY_UNKNOWN,
	UI_KEY_TAB,
	UI_KEY_RETURN,
	UI_KEY_ESCAPE,
	UI_KEY_SPACE,
	UI_KEY_BACKSPACE,
	UI_KEY_DELETE,
	UI_KEY_INSERT,
	UI_KEY_HOME,
	UI_KEY_END,
	UI_KEY_PAGE_UP,
	UI_KEY_PAGE_DOWN,
	UI_KEY_LEFT,
	UI_KEY_RIGHT,
	UI_KEY_UP,
	UI_KEY_DOWN,
	UI_KEY_F1,
	UI_KEY_F2,
	UI_KEY_F3,
	UI_KEY_F4,
	UI_KEY_F5,
	UI_KEY_F6,
	UI_KEY_F7,
	UI_KEY_F8,
	UI_KEY_F9,
	UI_KEY_F10,
	UI_KEY_F11,
	UI_KEY_F12,
};
