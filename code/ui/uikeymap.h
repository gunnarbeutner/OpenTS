/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "uievent.hh"


// Translates a virtual key code, which the engine declares itself in keyboard.h, into the
// shell's own key identity.
UIKeyType UI_Key_From_Virtual(unsigned int key);

