/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Installs the shell's hook on the page's event queue.
void UI_Browser_Install_Hook(void);
void UI_Browser_Remove_Hook(void);

// Delivers a mouse move when the pointer has moved. The page queues a press and a release
// but tracks the position on its own, so a move has to be polled rather than awaited. The
// browser service calls this.
void UI_Browser_Service_Mouse(void);

// Raises the page's keyboard while a text field holds the focus and puts it away after, as
// the page has no other way to learn that a field wants typing. Browser service calls this.
void UI_Browser_Service_Text_Input(void);

// Whether a finger landing at this point of the game's frame is on a screen, which takes the
// press at once rather than waiting to see what the gesture becomes.
bool UI_Browser_Takes_Touch_Press(int gamex, int gamey);
