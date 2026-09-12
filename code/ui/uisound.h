/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows the sound and music options and does not return until the player closes them.
// False means the screen could not be prepared.
//
// The screen applies a volume as it is dragged, the way the dialog did, so there is nothing
// to cancel and the only way out is the accept button.
bool UI_Sound_Screen(void);
