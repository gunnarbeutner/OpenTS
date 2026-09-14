/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "point.h"

class ScenarioClass;


// Prints the scenario's briefing over the plate the driver has already put on the frame.
// <paramref name="plate"/> is the artwork's own size, which the layout is placed against
// wherever the frame fitted it. True means the player asked for the briefing movie; a
// screen that could not be prepared answers false, as resuming does.
// The picture the screen is laid out against, and paints again whenever the frame changes.
char const * const BRIEFING_PLATE = "SCORE.PCX";

bool UI_Briefing_Screen(ScenarioClass * scen, Point2D const & plate);
