/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Screens drawn by the UI shell in place of a dialog of the WS_ family. Such a screen takes
// a slot on the family's dialog stack, under the template identifier of the dialog it
// replaces, so every query the family answers (the top window, a dialog found by its
// template, a wait on a dialog) answers as it did while the dialog was a window. The stack
// is kept in code/windlg.cpp.

#pragma once


// Names a screen on the stack. It is never defined, and a handle is never dereferenced.
struct WSScreen;
typedef WSScreen * WSScreenHandle;


enum WSScreenEventType
{
	// Once each pass of WS_Wait_Dialog that waits on the screen, after the message pump.
	WS_SCREEN_SERVICE,

	// The screen has been taken off the stack, alone or with the dialogs under it. Its
	// owner closes whatever draws it.
	WS_SCREEN_DESTROYED,
};

typedef void (*WSScreenProc)(WSScreenHandle screen, WSScreenEventType event);


// A handle for a screen that is not yet on the stack. It is never a window, and it is not
// handed out again for a long while, so a handle kept after its screen has gone does not
// name the next one.
WSScreenHandle WS_Screen_Handle(void);

// Puts a screen on top of the stack.
void WS_Push_Screen(WSScreenHandle screen, int id, WSScreenProc proc);

// Whether the handle names a screen on the stack.
bool WS_Is_Screen(WSScreenHandle window);
