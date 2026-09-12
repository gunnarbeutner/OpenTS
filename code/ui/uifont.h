/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The dialogs' lettering, read from the dlgsys glyph sheets and coloured as ODDrawCharRemap
// colours it. Nothing here knows RmlUi; uifontengine.cpp serves it to RmlUi.

#pragma once

#include <vector>


class UIDialogFontClass
{
	public:
		static int const CELL_COUNT = 256;

		// Reads and measures the sheets. False, leaving the font empty, when they are unusable.
		bool Load(void);
		bool Is_Loaded(void) const { return(CellsPerRow > 0); }

		// The cell ODDrawCharRemap draws for a code point.
		static int Glyph(char32_t code);

		// Whether ODDrawCharRemap draws the cell or only advances past it.
		static bool Is_Drawn(int glyph) { return(glyph > ' ' && glyph < CELL_COUNT); }

		int Advance(int glyph) const { return(CharWidths[glyph & 0xFF]); }

		// A line is one size box tall, and the box's top row is the top of the line.
		int Line_Height(void) const { return(GlyphHeight); }
		int Ascent(void) const { return(AscentRows); }
		int X_Height(void) const { return(XHeightRows); }

		// A glyph draws its whole cell, margins included, with the cell's first column one
		// pixel left of the pen and its first row TopMargin rows above the top of the line.
		int Cell_Width(void) const { return(LeftMargin + GlyphWidth); }
		int Cell_Height(void) const { return(TopMargin + GlyphHeight); }
		int Top_Margin(void) const { return(TopMargin); }

		void Build_Atlas(unsigned char red, unsigned char green, unsigned char blue, int magnify,
			std::vector<unsigned char> & rgba, int & width, int & height) const;
		void Atlas_Cell(int glyph, int magnify, int & x, int & y) const;

		static void Remap(unsigned char red, unsigned char green, unsigned char blue,
			unsigned char const * sheet, unsigned char alpha, unsigned char * premultiplied);

	private:
		struct CellPixel
		{
			unsigned char Alpha;
			unsigned char Red;
			unsigned char Green;
			unsigned char Blue;
		};

		std::vector<CellPixel> Pixels;
		int SheetWidth = 0;
		int SheetHeight = 0;

		int TopMargin = 0;
		int GlyphHeight = 0;
		int LeftMargin = 0;
		int GlyphWidth = 0;
		int CellsPerRow = 0;
		int CharWidths[CELL_COUNT] = {};

		int AscentRows = 0;
		int XHeightRows = 0;

		CellPixel const & Pixel(int x, int y) const;
		bool Measure(void);
		bool Letter_Rows(int character, int & first, int & last) const;
		void Cell_Origin(int glyph, int & x, int & y) const;
};
