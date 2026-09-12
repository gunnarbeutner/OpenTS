/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

class GameOptionsClass;


// Shows the display options and waits for OK or Cancel. What the player picks is staged in
// the options the caller passes, as the dialog staged it in TempOptions, and the caller
// applies it; the movie stretching preference is written straight into Options on OK, as
// the dialog wrote it. Result is IDOK, IDCANCEL, or -1 when the session ended.
//
// False means the screen could not be prepared.
bool UI_Display_Options_Screen(GameOptionsClass & staged, int & result);
