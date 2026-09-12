/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows the keyboard configuration and does not return until the player leaves it. A key
// is assigned as the player works; OK writes the assignments to KEYBOARD.INI and Cancel
// reloads them from the files, as the dialog did. A session that ends under the screen
// leaves the assignments as they stand, unsaved.
//
// False means the screen could not be prepared.
bool UI_Hotkey_Screen(void);
