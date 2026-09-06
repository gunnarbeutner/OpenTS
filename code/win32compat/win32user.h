/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window manager the WebAssembly target runs on: real windows with real
// message dispatch, drawn by whoever owns them onto the engine's surfaces.
// win32compat.h declares the Win32 entry points; only what other platform
// sources need is declared here.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "windows.h"

#include <string>


// Turns what the page has reported since the last pass into queued window
// messages. Windows_Message_Handler calls this before it pumps the queue.
void Win32_User_Service(void);

// Queues a character the page reported with no key press behind it to the
// focus, and only when that window wants characters.
void Win32_User_Post_Character(char character);

// Whether the window's caret is on screen this instant, and its rectangle in
// client coordinates. Nothing here draws; whoever paints the window draws it.
bool Win32_Caret_Visible(HWND window, RECT * where);

// Where the registry has a window, in window pixels with no non-client frame; a
// window with no parent is at its own screen position. win32window.cpp's
// GetWindowRect and relatives must answer out of these for any handle the
// registry knows, or every control covers the whole frame.
bool Win32_User_Window_Rect(HWND window, RECT * rect);
bool Win32_User_Client_Rect(HWND window, RECT * rect);
bool Win32_User_Client_Origin(HWND window, POINT * origin);

// Appends every visible window as a JSON array, front to back and parents
// before children, with rectangles in the game's frame.
void Win32_User_Describe(std::string & out);

// Whether posted messages are still waiting to be pumped.
bool Win32_User_Has_Pending_Messages(void);

#endif	// OPENTS_WIN32_SUBSTITUTE
