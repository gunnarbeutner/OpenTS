/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Shows a message with up to three buttons and does not return until one is answered.
//
// The answer is the button's index, 0 for the first, matching what WWMessageBox returns. A
// button whose text is null or empty is not shown. Enter answers with defresponse, and
// Escape answers as the second button does, which is what the dialog it replaces did
// whether or not that button is present. A box with no button at all answers 0 without
// showing anything.
//
// UI_MESSAGE_BOX_UNAVAILABLE means the screen could not be prepared. UI_MESSAGE_BOX_INTERRUPTED
// means the session ended under the box, which the dialog's driver reported by leaving its
// answer at -1.
enum
{
	UI_MESSAGE_BOX_UNAVAILABLE = -1,
	UI_MESSAGE_BOX_INTERRUPTED = -2,
};


int UI_Message_Box(char const * message, int defresponse,
	char const * button1, char const * button2, char const * button3);
