/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows the game settings and does not return until the player leaves them. Accepting, or
// leaving for the sound or keyboard settings in a game, applies every control and writes the
// settings file, as the dialog did; cancelling applies nothing.
//
// False means the screen could not be prepared.
bool UI_Game_Controls_Screen(void);
