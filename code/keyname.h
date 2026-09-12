/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "keyboard.h"


// Spells out a key and its modifiers as "Alt+Ctrl+Shift+Key" in the buffer, which must hold
// all four names; a key with no name leaves only the modifiers.
void Build_Hotkey_String(KeyNumType key, char * buffer);
