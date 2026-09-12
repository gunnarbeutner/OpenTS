/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Runs the skirmish setup screen until the player accepts or cancels it. False means the
// screen could not be prepared, and nothing has been read or written.
//
// Otherwise rc receives IDOK or IDCANCEL, or keeps the -1 the caller gave it when the session
// ended under the screen, which is what the dialog driver's loop left in it.
bool UI_Skirmish_Screen(int & rc);
