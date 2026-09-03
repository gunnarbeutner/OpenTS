/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

bool Change_Display_Mode(int width, int height);
void Main_Options_Dialog(void);

// Asks the player to keep a display mode just set, and puts the old one back unless they do.
bool Test_Display_Mode_Dialog(int width, int height);

#if !defined(_WIN32)
// The frame follows the window on this target, so the display options offer interface
// scales in place of display modes.
struct InterfaceSizeStruct
{
	char const * Name;
	int Scale;
};

enum { INTERFACE_SIZE_COUNT = 5 };
extern InterfaceSizeStruct const InterfaceSizes[INTERFACE_SIZE_COUNT];
#endif
