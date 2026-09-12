/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows the random map generator and does not return until the player accepts or cancels.
// Answers as Do_Random_Map_Dialog does: 1 when a map was accepted, 2 when the screen was
// cancelled, and 0 when it could not be prepared, which is the caller's cue to open the
// dialog instead. The callback is run on every pass, as the dialog's loop ran it.
int UI_Map_Generator_Screen(bool (*callback)());

// Redraws the preview while a map is being generated, if the screen is up. The generator
// asked its dialog to repaint at each stage; this is that request for the screen.
void UI_Map_Generator_Repaint(void);
