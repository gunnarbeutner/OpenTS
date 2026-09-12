/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The box a network game puts up while it waits on a player who has stopped answering. It
// is modeless: the lockstep's wait opens it, updates it on each pass, and closes it, and
// what the player asks for is handed back to that wait at its own point in the pass.

#pragma once

#include "uiscreen.h"


enum
{
	// Identity is the index into Session.Players of the player the button names.
	UI_DISCONNECT_KICK = UI_ACTION_SCREEN,
};


// Opens the box with a button and a bar for each player in the session, as the dialog made
// them when it was created. False means the box could not be prepared, or one is already
// open, which leaves the caller to use the dialog instead.
bool UI_Disconnect_Open(void);

void UI_Disconnect_Close(void);

bool UI_Disconnect_Is_Open(void);

void UI_Disconnect_Set_Time(char const * text);

// Appends a line to the message list without trimming or scrolling it.
void UI_Disconnect_Add_Message(char const * text);

// Drops the oldest line once the list holds more than fifty and scrolls to the newest.
void UI_Disconnect_Trim_Messages(void);

// Sets how much of a player's bar is left, from 0 to 100, and its colour. A negative share
// leaves the bar empty.
void UI_Disconnect_Set_Bar(int slot, int share, int red, int green, int blue);

// Hands each thing the player asked for since the last call to the handler, in order: a
// kick, or UI_ACTION_CANCEL for the Cancel button and for Escape.
void UI_Disconnect_Service(void (*handler)(UIIntent const & intent));
