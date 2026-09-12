/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "win.h"


// Offers a window message to the UI shell, returning true when a document consumed it.
// This is the whole of the shell's dependence on the window system: it translates messages
// into the shell's own input events, so retiring the Win32 message loop costs this file and
// nothing behind it.
//
// It belongs in Windows_Procedure after the position is taken into the frame, and before
// the game's own input handling, which keeps consumed input out of the KN_ queue.
bool UI_Handle_Window_Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
