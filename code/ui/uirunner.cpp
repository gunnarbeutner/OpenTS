/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The modal runner. It is the RmlUi twin of OwnerDraw::Dialog_Message_Handler's loop and
// makes the same choice between stepping the game and merely servicing it, so a screen
// opened during a network session keeps the session running underneath it.

#include "always.h"

#include "uirunner.h"

#include "conquer.h"
#include "mainloop.h"
#include "msgloop.h"
#include "session.h"
#include "uirmlview.h"
#include "uishell.h"
#include "video.h"
#include "win.h"
#include "winstub.h"


// The same test the dialog driver made, and for the same reason: a session that is neither
// a single-player game nor a skirmish has to keep stepping while a screen is up, and
// anything else is only serviced. The guard against re-entering Main_Loop is the driver's
// too.
bool UI_Service_Game(void)
{
	static bool inmainloop = false;

	Windows_Message_Handler();

	if (Session.Type != GAME_NORMAL && Session.Type != GAME_SKIRMISH && !Session.NetOpen && !Session.Suspended) {
		if (!inmainloop) {
			inmainloop = true;
			bool const ended = Main_Loop();
			inmainloop = false;

			if (ended) {
				return(true);
			}
		}
	} else {
		Call_Back();

	}

	return(false);
}


UIResult UI_Run_Modal(UIPresenterClass & presenter, UIRmlViewClass & view)
{
	UIResult result;

	if (!UI_Is_Initialized()) {
		result.Type = UI_RESULT_FAILED;
		return(result);
	}

	// Preparation completes before the screen becomes interactive, so a missing document
	// never shows as an empty dialog that has to be dismissed.
	if (!view.Prepare()) {
		result.Type = UI_RESULT_FAILED;
		return(result);
	}

	presenter.Refresh();
	view.Sync();
	view.Show(true);

	// The keyboard queue is cleared around a modal screen the way every legacy driver does,
	// so a key pressed before it opened is not read by it.
	UI_Begin_Modal();

	while (!presenter.Has_Result()) {
		if (UI_Service_Game()) {
			// The session ended under the screen. The result says so rather than reporting
			// a dismissal the player never made.
			presenter.Discard();
			result.Type = UI_RESULT_SESSION_ENDED;
			break;
		}

		presenter.Service();
		UI_Tick();

		presenter.Drain();

		if (presenter.Has_Result()) {
			break;
		}

		view.Sync();

		UI_Mark_Overlay_Dirty();
		Video_Present_If_Dirty();
	}

	if (result.Type == UI_RESULT_NONE) {
		result = presenter.Get_Result();
	}

	// Teardown order: the screen is finished, so its pending intents go, then the document
	// and its listeners, and only then is the keyboard queue cleared and focus returned.
	presenter.Discard();
	view.Close();
	UI_End_Modal();

	return(result);
}
