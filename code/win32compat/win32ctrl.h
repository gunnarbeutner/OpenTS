/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The stock control classes the WebAssembly target's dialogs are built from.
// They are models, not painters: ownrdraw.cpp subclasses each one and paints
// it, asking the class for its state. win32user.cpp owns the windows and the
// dispatch they run on.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "windows.h"


// Registers every stock control class once. The dialog manager calls this
// before it builds a dialog.
void Win32_Register_Stock_Controls(void);

// The class name a template's ordinal names, or NULL. A template writes the six
// original control classes as ordinals rather than as strings.
char const * Win32_Stock_Control_Class(unsigned int ordinal);

#endif	// OPENTS_WIN32_SUBSTITUTE
