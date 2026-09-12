/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The box a network game puts up when it has gone out of sync: the decision the master makes
// and the wait everyone else sits through. It is modeless: DesyncDialogClass opens it, fills
// it in as the session changes, and closes it, and what the player asks for is handed back
// to that class's own loop.

#pragma once

#include "uiscreen.h"

#include <string>
#include <vector>


enum
{
	UI_DESYNC_LOAD = UI_ACTION_SCREEN,
	UI_DESYNC_CONTINUE,
	UI_DESYNC_QUIT,

	// Identity is 1 when the chat field has taken the focus and 0 when it has lost it.
	UI_DESYNC_CHAT_FOCUS,
};


// A row of the player list.
struct UIDesyncPlayer
{
	std::string Name;
	bool Host = false;
	std::string Status;
	int Red = 0;
	int Green = 0;
	int Blue = 0;
};


// Opens the decision box when host is true and the wait box otherwise. False means the box
// could not be prepared, or one is already open, which leaves the caller to use the dialog.
bool UI_Desync_Open(bool host);

void UI_Desync_Close(void);

bool UI_Desync_Is_Open(void);

void UI_Desync_Set_Players(std::vector<UIDesyncPlayer> const & players);

// Replaces the chat list and shows its newest line.
void UI_Desync_Set_Chat(std::vector<std::string> const & lines);

// Enables or disables the button for UI_DESYNC_LOAD, UI_DESYNC_CONTINUE or UI_DESYNC_QUIT.
void UI_Desync_Enable(int action, bool enabled);

// Shows the countdown line and its bar, which start hidden.
void UI_Desync_Show_Countdown(void);
void UI_Desync_Set_Countdown_Text(char const * text);

// The bar is cut at the share of the countdown that is left, and never drawn narrower than
// six pixels.
void UI_Desync_Set_Countdown_Bar(int remaining, int total, int red, int green, int blue);

void UI_Desync_Set_Chat_Text(char const * text);
std::string UI_Desync_Chat_Text(void);

void UI_Desync_Focus_Chat(void);

// Takes the focus away from the chat field.
void UI_Desync_Focus_Box(void);

// A suspended box drops every action but a change of focus, as a disabled dialog took no
// input. A caller services it once more before lifting the suspension, so that what arrived
// in between is dropped rather than acted on late.
void UI_Desync_Suspend(bool suspended);

// Hands each thing the player asked for since the last call to the handler, in order. A
// button's action is dropped while the button is disabled; Enter arrives as
// UI_ACTION_ACCEPT.
void UI_Desync_Service(void (*handler)(UIIntent const & intent));
