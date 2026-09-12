/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "dialogresult.h"
#include "utf8.h"
#include "always.h"

#include "skirmish.h"

#include "_rules.h"
#include "data.h"
#include "dbgprint.h"
#include "globals.h"
#include "goptions.h"
#include "houstype.h"
#include "netdlg2.h"
#include "init.h"
#include "language/language.h"
#include "mapgen.h"
#include "mplayer.h"
#include "msgbox.h"
#include "netshare.h"
#include "newmenu.h"
#include "rules.h"
#include "ui/uiskirmish.h"
#include "win.h"


/// <summary>
/// Handles the skirmish game setup dialog.
/// This routine is used by the main menu when the player picks a skirmish game. The house
/// and side rules are re-read first so that the dialog offers the current playable sides,
/// and the chosen settings are recorded as the player's multiplayer preferences on the way
/// out.
/// </summary>
/// <returns>bool; Did the player accept the settings and ask for the game to start?</returns>
bool Skirmish_Mode_Dialog(void)
{
	int rc = -1;

	Prepare_Side_Roster();

	Hide_Mouse();
	Draw_Menu_Background();
	Show_Mouse();

	// A screen that could not be shown leaves rc unanswered, as a dialog that could not be
	// created did.
	if (!UI_Skirmish_Screen(rc)) {
		DebugString("The skirmish setup screen could not be shown\n");
	}

	if (MultiplayerMapPreview != NULL) {
		delete MultiplayerMapPreview;
		MultiplayerMapPreview = NULL;
	}

	Session.Write_MultiPlayer_Settings();

	if (rc == DIALOG_CANCEL) {
		Hide_Mouse();
		Draw_Menu_Background();
		Show_Mouse();
	}

	if (rc == DIALOG_OK) {
		return(true);
	}

	return(false);
}
