/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Runs the in-game options hub the way Game_Options_Dialog runs its dialog, from locking
// out game input to restating the briefing or settling the mouse on the way out. A save,
// load or delete in a single player game closes the hub while the save list is up and opens
// it again afterwards, as the dialog hid itself.
//
// False means the screen could not be prepared the first time.
bool UI_Game_Options_Screen(void);
