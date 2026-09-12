/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Asks the player to keep a display mode that has just been put up. The answer is IDOK to
// keep it and IDCANCEL to go back; saying nothing for ten seconds answers IDCANCEL, because
// a bad mode can leave the screen unreadable. -1 means the session ended under the screen.
//
// False means the screen could not be prepared.
bool UI_Mode_Confirm_Screen(int & result);
