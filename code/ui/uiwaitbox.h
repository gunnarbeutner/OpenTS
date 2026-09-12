/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The modeless "please wait" box. Unlike a modal screen it is not run by a loop of its own:
// a caller opens it, does blocking work or runs a wait of its own, and closes it. So it has
// to be on the screen before the caller blocks, and it is redrawn only when the caller
// changes it.

#pragma once


// Opens the box with a message and, when cancel is not null, a button that raises the flag
// and posts an escape key when it is pressed. False means the box could not be prepared,
// or one is already open.
bool UI_Wait_Box_Open(char const * message, char const * cancel, bool * cancelled);

// Puts the box on the screen now. A caller about to block calls this first, because nothing
// is drawn while it blocks.
void UI_Wait_Box_Show(void);

// Changes the message and redraws the box before returning, as the dialog did.
void UI_Wait_Box_Set_Text(char const * message);

void UI_Wait_Box_Close(void);

bool UI_Wait_Box_Is_Open(void);


// The box for the life of the object: on the screen once constructed and closed when
// destroyed, each clearing the keyboard queue as the dialog's showing and closing did. A box
// that could not be prepared shows nothing and its caller carries on without it.
class UIWaitBoxClass
{
	public:
		UIWaitBoxClass(char const * message, char const * cancel = nullptr, bool * cancelled = nullptr);
		~UIWaitBoxClass(void);

		UIWaitBoxClass(UIWaitBoxClass const &) = delete;
		UIWaitBoxClass & operator=(UIWaitBoxClass const &) = delete;

		void Set_Text(char const * message);

	private:
		bool IsOpen;
};
