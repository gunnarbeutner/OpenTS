/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "_rect.h"

class Surface;


// Draws white UTF-8 text centred across the rectangle, its top at the middle, in the shipped
// face at the GDI Swiss cell the tactical caption asked for; nothing if the face is missing.
void UI_Draw_Caption(Surface & surface, Rect const & rect, char const * text);
