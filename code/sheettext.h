/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Text drawn from a remappable font's colour and coverage sheets ("dlgsys" is dlgsysi.pcx and
// dlgsysa.pcx) into a 16-bit surface, as the owner-draw dialogs drew their lettering.

#pragma once

#include "_rect.h"

#include <cstdint>

class Surface;


struct SheetFontMetrics
{
	int CharWidths[256];		// inked width of each character, indexed by character code
	int GlyphWidth;				// width of the inked part of a glyph cell
	int GlyphHeight;			// height of the inked part of a glyph cell
	int TopMargin;				// blank rows above each row of glyphs
	int LeftMargin;				// blank columns before each glyph
};


// How Sheet_Draw_Line and Sheet_Draw_Text place text within their rectangle. The rectangle's
// Width and Height are its right and bottom edges, as the owner-draw callers passed them.
enum
{
	SHEET_TEXT_CENTER = 1,
	SHEET_TEXT_RIGHT = 2,
	SHEET_TEXT_MIDDLE = 4,
};


// The dialogs' text green, RGB(112,255,0), packed with red in the low byte as RGB() packs it.
std::uint32_t const SHEET_TEXT_GREEN = 0x0000FF70;


// Caches the dlgsys sheets in the forms the drawing reads them in. Needs the mix files
// registered and the display's pixel format known; later calls do nothing.
void Sheet_Text_Prepare(void);

bool Sheet_Font_Metrics(char const * font, SheetFontMetrics & metrics);

// Blends a display pixel toward a colour by alpha in 255ths, as the lettering does. Even at
// 255 every non-zero channel drops by one step.
unsigned short Sheet_Blend_Pixel(unsigned short pixel, unsigned short color, unsigned char alpha);

// How far the remap pulls a hue back around the primaries; the dialog face's own colours
// are built from it too.
float Sheet_Text_Remap_Factor(int hue);

// Draws up to max_chars characters of one line.
void Sheet_Draw_Line(Surface & surface, char const * text, int max_chars, Rect const & rect, char const * font, std::uint32_t color, int flags, int spacing = 0);

// Draws text word wrapped to the rectangle, honouring the newlines already in it.
void Sheet_Draw_Text(Surface & surface, char const * text, Rect const & rect, char const * font, std::uint32_t color, int flags, int spacing = 0);
