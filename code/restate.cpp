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

#include "always.h"

#include "_keyboar.h"
#include "_surface.h"
#include "globals.h"
#include "gscreen.h"
#include "init.h"
#include "keyboard.h"
#include "movie.h"
#include "restate.h"
#include "scenario.h"
#include "surface.h"
#include "theme.h"
#include "ui/uibriefing.h"
#include "ui/uiscreens.h"


/***********************************************************************************************
 * Restate_Mission -- Handles restating the mission objective.                                 *
 *                                                                                             *
 *    This routine will display the mission objective (as text). It will also give the         *
 *    option to redisplay the mission briefing video.                                          *
 *                                                                                             *
 * INPUT:   scen  -- The scenario whose briefing is to be restated.                            *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/23/1995 JLB : Created.                                                                 *
 *   08/06/1995 JLB : Uses preloaded briefing text.                                            *
 *=============================================================================================*/
void Restate_Mission(ScenarioClass * scen)
{
	if (scen == nullptr || HiddenSurface == nullptr) {
		return;
	}

	bool const save_started = ScenarioActive;
	ScenarioActive = false;

	// The plate is filled out to the frame, so the screen over it is laid out against
	// wherever its own shape landed rather than against a design space of its own.
	Point2D const plate = Load_Title_Page(BRIEFING_PLATE, true);

	bool const video = UI_Briefing_Screen(scen, plate);

	HiddenSurface->Fill(0);
	Update_Visible_Surface(HiddenSurface);

	if (video) {
		ThemeType theme = Theme.What_Is_Playing();
		Theme.Stop();
		Play_Movie(scen->BriefMovie, THEME_NONE, 1, 1);
		Theme.Play_Song(theme);
	}

	ScenarioActive = save_started;
	Keyboard->Clear();
}


// Registered through the driver rather than through the screen, so a run exercises the path
// a caller takes. It is only meaningful over a scenario that has been read.
static bool Open_Briefing(void)
{
	if (Scen == nullptr) {
		return(false);
	}

	Restate_Mission(Scen);
	return(true);
}


static UIScreenRegistration _Register("briefing", Open_Briefing);
