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

/* $Header: /CounterStrike/MPLAYER.CPP 3     3/13/97 2:06p Steve_tall $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : MPLAYER.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Bill Randolph                                                *
 *                                                                                             *
 *                   Start Date : April 14, 1995                                               *
 *                                                                                             *
 *                  Last Update : November 30, 1995 [BRR]                                      *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Select_MPlayer_Game -- prompts user for NULL-Modem, Modem, or Network game                *
 *   Clear_Listbox -- clears the given list box                                                *
 *   Clear_Vector -- clears the given NodeNameType vector                                      *
 *   Computer_Message -- "sends" a message from the computer                                   *
 *   Garble_Message -- "garbles" a message                                                     *
 *   Surrender_Dialog -- Prompts user for surrendering                                         *
 *   Abort_Dialog -- Prompts user for confirmation on aborting the mission                     *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "mplayer.h"

#include "_surface.h"
#include "addon.h"
#include "dialogresult.h"
#include "init.h"
#include "msgbox.h"
#include "session.h"
#include "ui/uimenus.h"

class ListClass;


// Only the network and skirmish buttons lead anywhere; any other command backs out.
static GameType MPlayer_Game_For(int command)
{
	switch (command) {
		case IDC_NETWORK:
			return(GAME_IPX);

		case IDC_SKIRMISH:
			return(GAME_SKIRMISH);

		default:
			return(GAME_NORMAL);
	}
}


static bool Select_MPlayer_Game_Screen(bool firestorm, int & command)
{
	// Neither the online service the first two led to nor the tour it hosted can be reached.
	UIMenuRequest request;
	request.Document = "mpgame.rml";
	request.Variant = firestorm ? "fs" : nullptr;
	request.Choices = {
		{ "internet", IDC_INTERNET, false },
		{ "worlddom", IDC_WORLDDOM, false },
		{ "modem", IDC_MODEMSERIAL },
		{ "network", IDC_NETWORK },
		{ "skirmish", IDC_SKIRMISH },
		{ "back", DIALOG_CANCEL },
	};
	request.KeysAnswer = true;

	return(UI_Menu_Screen(request, command));
}


/// <summary>
/// Prompts the player for which kind of multiplayer game to start.
/// </summary>
/// <returns>Returns with the chosen game type, or GAME_NORMAL if the player backed out.</returns>
GameType Select_MPlayer_Game (void)
{
	GameType retval = GAME_NORMAL;
	AddonType addon;
	if (Addon_Installed(ADDON_ANY) == ADDON_FIRESTORM && !Select_Game_Type_Dialog(addon)) {
		return(retval);
	}

	// A menu that could not be shown backs out, as a dialog that could not be created did,
	// and so does a session that ended under it.
	int command = -1;
	if (Select_MPlayer_Game_Screen(Addon_Installed(ADDON_FIRESTORM) == ADDON_FIRESTORM, command)) {
		retval = MPlayer_Game_For(command);
		Session.Read_Scenario_Descriptions();
	}
	return(retval);
}	/* end of Select_MPlayer_Game */


/***************************************************************************
 * Surrender_Dialog -- Prompts user for surrendering                       *
 *                                                                         *
 * INPUT:                                                                  *
 *      none.                                                              *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      0 = user cancels, 1 = user wants to surrender.                     *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   07/05/1995 BRR : Created.                                             *
 *=========================================================================*/
int Surrender_Dialog(int text)
{
	return(WWMessageBox()._Process(text, 1, TXT_OK, TXT_CANCEL) == 0);
}


/***************************************************************************
 * Clear_Vector -- clears the given NodeNameType vector                    *
 *                                                                         *
 * INPUT:                                                                  *
 *      vector      ptr to vector to clear                                 *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      none.                                                              *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   11/29/1995 BRR : Created.                                             *
 *=========================================================================*/
void Clear_Vector(DynamicVectorClass <NodeNameType *> * vector)
{
	int i;

	//------------------------------------------------------------------------
	// Clear the 'Players' Vector
	//------------------------------------------------------------------------
	for (i = 0; i < vector->Count(); i++) {
		delete (*vector)[i];
	}
	vector->Clear();

}	// end of Clear_Vector


/************************** end of mplayer.cpp *****************************/
