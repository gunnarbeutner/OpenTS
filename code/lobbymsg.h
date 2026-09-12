/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The messages the network lobby's dialog procedures take and Lobby_Send answers. They
// keep the numbers of the Win32 control messages and notifications they stand for, so a
// procedure reads them as it read the dialog's.

#pragma once

#include "ui/uiwsstack.h"

#include <cstdint>


typedef std::uintptr_t LobbyWParam;
typedef std::intptr_t LobbyLParam;
typedef std::intptr_t LobbyResult;

typedef LobbyResult (*LobbyProc)(WSScreenHandle window, unsigned int message, LobbyWParam wparam, LobbyLParam lparam);


enum LobbyMessageType : unsigned int
{
	LOBBY_MSG_DESTROY = 0x0002,
	LOBBY_MSG_SET_TEXT = 0x000C,
	LOBBY_MSG_GET_TEXT = 0x000D,
	LOBBY_MSG_SET_TEXT_LIMIT = 0x00C5,
	LOBBY_MSG_GET_CHECK = 0x00F0,
	LOBBY_MSG_SET_CHECK = 0x00F1,
	LOBBY_MSG_INIT = 0x0110,

	// The low word of wparam is the control, the high word its notification.
	LOBBY_MSG_COMMAND = 0x0111,

	// lparam is the track bar's control identifier.
	LOBBY_MSG_HSCROLL = 0x0114,
	LOBBY_MSG_VSCROLL = 0x0115,

	LOBBY_MSG_COMBO_GET_COUNT = 0x0146,
	LOBBY_MSG_COMBO_GET_CUR_SEL = 0x0147,
	LOBBY_MSG_COMBO_INSERT = 0x014A,
	LOBBY_MSG_COMBO_RESET = 0x014B,
	LOBBY_MSG_COMBO_SET_CUR_SEL = 0x014E,
	LOBBY_MSG_COMBO_GET_ITEM_DATA = 0x0150,
	LOBBY_MSG_COMBO_SET_ITEM_DATA = 0x0151,
	LOBBY_MSG_COMBO_GET_DROPPED = 0x0157,

	LOBBY_MSG_LIST_INSERT = 0x0181,
	LOBBY_MSG_LIST_RESET = 0x0184,
	LOBBY_MSG_LIST_SET_SEL = 0x0185,
	LOBBY_MSG_LIST_SET_CUR_SEL = 0x0186,
	LOBBY_MSG_LIST_GET_SEL = 0x0187,
	LOBBY_MSG_LIST_GET_CUR_SEL = 0x0188,
	LOBBY_MSG_LIST_GET_COUNT = 0x018B,
	LOBBY_MSG_LIST_SELECT_RANGE = 0x019B,

	LOBBY_MSG_TRACK_GET_POS = 0x0400,
	LOBBY_MSG_TRACK_SET_POS = 0x0400 + 5,
	LOBBY_MSG_TRACK_SET_RANGE = 0x0400 + 6,

	// The screen's controls have been taken over; see UI_Lobby_Subclass.
	LOBBY_MSG_SUBCLASSED = 0x0400 + 151,

	// An item's colour: wparam is the item, lparam a colour from Lobby_Color.
	LOBBY_MSG_SET_COLOR = 0x0400 + 152,

	// A track bar's step: lparam is the step.
	LOBBY_MSG_TRACK_SET_STEP = 0x0400 + 171,
};


// The notification in the high word of a LOBBY_MSG_COMMAND's wparam.
enum LobbyNotifyType
{
	LOBBY_NOTIFY_CLICKED = 0,
	LOBBY_NOTIFY_SELCHANGE = 1,
	LOBBY_NOTIFY_DBLCLK = 2,
	LOBBY_NOTIFY_CHANGE = 0x0300,

	// Return in a chat field.
	LOBBY_NOTIFY_MAX_TEXT = 0x0501,
};

// The scroll code in the low word of a LOBBY_MSG_HSCROLL's wparam.
enum
{
	LOBBY_SCROLL_THUMB_TRACK = 5,
};

enum
{
	LOBBY_UNCHECKED = 0,
	LOBBY_CHECKED = 1,
};

// What a combo box answers for an item it does not have, or with nothing selected.
enum
{
	LOBBY_ERR = -1,
};


inline std::uint16_t Lobby_Low_Word(std::uintptr_t value)
{
	return((std::uint16_t)(value & 0xFFFF));
}


inline std::uint16_t Lobby_High_Word(std::uintptr_t value)
{
	return((std::uint16_t)((value >> 16) & 0xFFFF));
}


// Two 16-bit halves in one parameter, each truncated to its width.
inline std::uint32_t Lobby_Make_Param(unsigned int low, unsigned int high)
{
	return((std::uint32_t)(std::uint16_t)low | ((std::uint32_t)(std::uint16_t)high << 16));
}


// Red in the low byte, then green and blue.
constexpr std::uint32_t Lobby_Color(unsigned int red, unsigned int green, unsigned int blue)
{
	return((std::uint32_t)(red & 0xFF) | ((std::uint32_t)(green & 0xFF) << 8) | ((std::uint32_t)(blue & 0xFF) << 16));
}


LobbyResult Lobby_Send(WSScreenHandle window, int id, unsigned int message, LobbyWParam wparam, LobbyLParam lparam);
void Lobby_Enable_Item(WSScreenHandle window, int id, bool enable);
bool Lobby_Is_Item_Enabled(WSScreenHandle window, int id);
void Lobby_Invalidate(WSScreenHandle window);
void Lobby_Show(WSScreenHandle window, bool show);
void Lobby_Command(WSScreenHandle window, LobbyProc proc, int id, int notify);
