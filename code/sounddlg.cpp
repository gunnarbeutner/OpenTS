/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/* $Header: /CounterStrike/SOUNDDLG.CPP 1     3/03/97 10:25a Joe_bostic $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : SOUNDDLG.CPP                                                 *
 *                                                                                             *
 *                   Programmer : Maria del Mar McCready-Legg, Joe L. Bostic                   *
 *                                                                                             *
 *                   Start Date : Jan 8, 1995                                                  *
 *                                                                                             *
 *                  Last Update : September 22, 1995 [JLB]                                     *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   MusicListClass::Draw_Entry -- Draw the score line in a list box.                          *
 *   SoundControlsClass::Process -- Handles all the options graphic interface.                 *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "sounddlg.h"

#include "dbgprint.h"
#include "globals.h"
#include "goptions.h"
#include "ui/uisound.h"


/// <summary>
/// Shows the sound and music options and returns once the player dismisses them. The music
/// controls that only apply to a game in progress are left off when there is none.
/// </summary>
void SoundControlsClass::Dialog(void)
{
	DebugString("SoundControls: GameSpeed = %d, ScrollRate = %d, Detail = %d\n", Options.GameSpeed, Options.ScrollRate, Options.DetailLevel);

	if (!UI_Sound_Screen()) {
		DebugString("SoundControls: the sound options screen could not be shown\n");
	}

	DebugString("SoundControls: GameSpeed = %d, ScrollRate = %d, Detail = %d\n", Options.GameSpeed, Options.ScrollRate, Options.DetailLevel);
}
