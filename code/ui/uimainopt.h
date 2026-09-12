/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows the frontend options hub and waits for the player to choose. The choice is the
// control identifier the dialog answered with, IDC_OPTMAIN_* or IDOK and IDCANCEL for Enter
// and Escape, and -1 when the session ended under the screen.
//
// False means the screen could not be prepared.
bool UI_Main_Options_Screen(int & choice);
