/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The network lobby's screens: the game list, the host's and the guest's setup, the message
// box the lobby raises, and the map picker. The lobby's drivers in code/netdlg2.cpp and
// code/netshare.cpp keep their loops, their packets and their handlers; these screens stand
// where the owner-draw dialogs stood. A screen is addressed by the handle it holds on the
// WS_ dialog stack, and each of its controls by the identifier the dialog template gave that
// control, so a driver reads and writes a screen the way it read and wrote the dialog.
//
// The controls behave as the owner-draw controls did where a handler can see it: a list
// reports every change to its selection, including the ones its dialog makes, and a track
// bar reports a change to its position, including one its dialog makes. Text is UTF-8.

#pragma once

#include <string>
#include <vector>


enum UILobbyKind
{
	UI_LOBBY_GAME_LIST,
	UI_LOBBY_HOST,
	UI_LOBBY_GUEST,
};


// What a control reported, in the terms its dialog procedure read.
enum UILobbyNotifyType
{
	// A button was pressed, or a check box clicked; Value is a check box's new state.
	UI_LOBBY_CLICKED,

	// A list's selection changed, or was set again, or a combo box's item was picked.
	UI_LOBBY_SELCHANGE,

	// A list row was double-clicked.
	UI_LOBBY_DBLCLK,

	// A text field's text changed; Text is the new text.
	UI_LOBBY_TEXT_CHANGED,

	// Return in a chat field. Text is what the field held, with the line break the
	// owner-draw edit added.
	UI_LOBBY_TEXT_ENTERED,

	// A track bar's position changed; Value is its position.
	UI_LOBBY_SCROLLED,
};


struct UILobbyCommand
{
	int Control = 0;
	UILobbyNotifyType Notify = UI_LOBBY_CLICKED;
	int Value = 0;
	std::string Text;
};


// A row of a list or an item of a combo box. A player row of the host's and the guest's
// list also names the pictures its cells held.
struct UILobbyItem
{
	std::string Text;

	// A COLORREF, or -1 for the dialogs' own text colour.
	int Color = -1;

	// A combo box item's data.
	int Data = 0;

	std::string Emblem;
	std::string Mark;
};


// Receives what a control reported. It is called while the driver services the screen, and
// synchronously from a call that changed a list's selection or a track bar's position.
typedef void (*UILobbyHandler)(void const * screen, UILobbyCommand const & command);


// Opens a screen for the handle, hidden. False means the screen could not be prepared.
bool UI_Lobby_Open(void const * screen, UILobbyKind kind, UILobbyHandler handler);

void UI_Lobby_Close(void const * screen);
bool UI_Lobby_Is_Open(void const * screen);
void UI_Lobby_Show(void const * screen, bool show);

// From here on the controls behave as the owner-draw controls did once subclassed: a list
// forgets its selection, and a track bar starts reporting changes.
void UI_Lobby_Subclass(void const * screen);

// Acts on what the player did since the last call, then redraws the screen. The drivers
// call this at the points where the dialog's messages were dispatched.
void UI_Lobby_Service(void const * screen);

// Services every open lobby screen.
void UI_Lobby_Service_All(void);

bool UI_Lobby_Has_Control(void const * screen, int control);

void UI_Lobby_Enable(void const * screen, int control, bool enable);
bool UI_Lobby_Is_Enabled(void const * screen, int control);

void UI_Lobby_Set_Text(void const * screen, int control, char const * text);
std::string UI_Lobby_Get_Text(void const * screen, int control);
void UI_Lobby_Set_Limit(void const * screen, int control, int limit);

void UI_Lobby_Set_Check(void const * screen, int control, bool checked);
bool UI_Lobby_Get_Check(void const * screen, int control);

void UI_Lobby_Set_Range(void const * screen, int control, int minimum, int maximum);
void UI_Lobby_Set_Step(void const * screen, int control, int step);
void UI_Lobby_Set_Position(void const * screen, int control, int position);
int UI_Lobby_Get_Position(void const * screen, int control);

// Lists and combo boxes, as LB_RESETCONTENT, LB_INSERTSTRING and CB_INSERTSTRING.
void UI_Lobby_Reset(void const * screen, int control);
void UI_Lobby_Add(void const * screen, int control, UILobbyItem const & item);
int UI_Lobby_Get_Count(void const * screen, int control);
std::string UI_Lobby_Get_Item_Text(void const * screen, int control, int index);
void UI_Lobby_Set_Item_Data(void const * screen, int control, int index, int data);
int UI_Lobby_Get_Item_Data(void const * screen, int control, int index);
void UI_Lobby_Set_Item_Color(void const * screen, int control, int index, int color);

// The selection, as LB_SETCURSEL, LB_SETSEL and LB_SELITEMRANGE set it and CB_SETCURSEL
// sets a combo box's.
void UI_Lobby_Set_Cur_Sel(void const * screen, int control, int index);
int UI_Lobby_Get_Cur_Sel(void const * screen, int control);
void UI_Lobby_Set_Sel(void const * screen, int control, bool selected, int index);
bool UI_Lobby_Get_Sel(void const * screen, int control, int index);
void UI_Lobby_Select_Range(void const * screen, int control, bool selected, int first, int last);
bool UI_Lobby_Is_Multiple(void const * screen, int control);
bool UI_Lobby_Is_Dropped(void const * screen, int control);

// Adds a line to a message list in a colour, keeping the list to its last 500 lines.
void UI_Lobby_Add_Message(void const * screen, int control, char const * text, int color);

// The map preview was replaced; the screen shows MultiplayerMapPreview afresh.
void UI_Lobby_Preview_Changed(void);


// The message box ODMessageBox raises, for one of the templates IDD_MSGBOX_2,
// IDD_MSGBOX_3_SMALL and IDD_MSGBOX_3_LARGE, with the buttons the caller asked for.
bool UI_Net_Message_Box_Open(void const * screen, int id, char const * text, bool ok, bool cancel, bool yes, bool no);
void UI_Net_Message_Box_Close(void const * screen);

// The button pressed since the last call, as IDOK, IDCANCEL, IDYES or IDNO, or zero. Enter
// answers IDOK and Escape IDCANCEL, as the dialog manager answered for the dialog.
int UI_Net_Message_Box_Service(void const * screen);


// The map picker, IDD_MPLAYER_SELECT_MAP. Its list holds the scenario descriptions.
bool UI_Map_Select_Open(void const * screen, std::vector<std::string> const & maps, int selected);
void UI_Map_Select_Close(void const * screen);
void UI_Map_Select_Show(void const * screen, bool show);
void UI_Map_Select_Set_Maps(void const * screen, std::vector<std::string> const & maps, int selected);
int UI_Map_Select_Get_Selection(void const * screen);

// The button pressed since the last call, as IDOK, IDCANCEL or IDC_CREATE_RANDOM_MAP, or
// zero.
int UI_Map_Select_Service(void const * screen);
