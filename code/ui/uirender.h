/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The shell's private view of its renderer. Only uirender.cpp includes bgfx, so this
// header names no bgfx type and uishell.cpp needs none of bgfx's build settings.

#pragma once

#include <RmlUi/Core/Types.h>

namespace Rml { class RenderInterface; }


bool UI_Render_Init(void);
void UI_Render_Shutdown(void);

// Hands RmlUi the interface it renders through. Valid only between init and shutdown.
Rml::RenderInterface * UI_Render_Interface(void);

// Frames the shell's submissions. The rectangle is the frame's destination in the window,
// in physical pixels, and becomes the overlay's viewport and the origin of its coordinate
// space. Rml::Context::Render is called between the two.
void UI_Render_Begin(int destx, int desty, int destwidth, int destheight);
void UI_Render_End(void);

// Drops every target-dependent resource so the next frame recreates it. Follows a window
// resize or a renderer reset.
void UI_Render_On_Reset(void);

// Holds everything the overlay draws inside a rectangle of the document's space, or stops.
void UI_Render_Set_Reveal(bool active, int left, int top, int right, int bottom);

// A picture the texture loader can name, in its own pixels, and a column of it drawn at a
// scale down from a point in the document's space, repeated until it is the height given.
// Only between UI_Render_Begin and UI_Render_End.
Rml::Vector2i UI_Render_Picture_Size(char const * source);
void UI_Render_Picture(char const * source, float x, float y, float scale, float height);
