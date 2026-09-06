/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "point.h"

#include <SDL.h>


// Offers an SDL event to the UI shell, returning true when a document consumed it. This is
// the whole of the shell's dependence on SDL, as uiwin32.cpp is its dependence on the
// Windows message loop.
//
// The host supplies what only it can: the position in the window's client pixels, for a
// mouse event, and the virtual key code its scan-code table gives, for a key event. Both are
// ignored for the events that do not carry them.
//
// It belongs in the host's pump before the game's own input handling, which keeps consumed
// input out of the KN_ queue.
bool UI_Handle_SDL_Event(SDL_Event const & event, Point2D const & client, unsigned short key);
