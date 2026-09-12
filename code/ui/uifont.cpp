/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uifont.h"

#include "uitexture.h"

#include "dbgprint.h"
#include "hsv.h"
#include "rgb.h"
#include "sheettext.h"
#include "utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>



static char const * const DIALOG_ALPHA_SHEET = "dlgsysa.pcx";
static char const * const DIALOG_COLOUR_SHEET = "dlgsysi.pcx";

// Transparent texels around each atlas cell, so a scaled glyph never samples its neighbour.
static int const ATLAS_PADDING = 1;
static int const ATLAS_COLUMNS = 16;

// The frame the dialogs draw into is 565, and the presenter widens it to eight bits a channel.
static int const RED_MASK = 0xF800;
static int const GREEN_MASK = 0x07E0;
static int const BLUE_MASK = 0x001F;


static int Pack_565(RGBClass const & rgb)
{
	return(((rgb.Get_Red() >> 3) << 11) | ((rgb.Get_Green() >> 2) << 5) | (rgb.Get_Blue() >> 3));
}


// OD_Blend_Color over a black pixel, which is the colour's share of what it leaves.
static int Blend_Over_Black(int colour, int alpha)
{
	int const red = (((colour & RED_MASK) * alpha) >> 8) & RED_MASK;
	int const green = (((colour & GREEN_MASK) * alpha) >> 8) & GREEN_MASK;
	int const blue = ((colour & BLUE_MASK) * alpha) >> 8;
	return(red | green | blue);
}


// The 565 frame texture is sampled as unsigned normalized values, which round to nearest.
static unsigned char Widen(int value, int maximum)
{
	return((unsigned char)((value * 255 + maximum / 2) / maximum));
}


/// <summary>
/// Gives the pixel ODDrawCharRemap leaves on the presented frame for one colour sheet pixel
/// drawn in the requested colour. At a full alpha weight it is exact whatever lies under the
/// text; at a partial one it is the colour's premultiplied share, which blended over the scene
/// comes within a 565 step of what the frame would hold.
/// </summary>
/// <param name="sheet">The colour sheet pixel, red, green and blue.</param>
/// <param name="premultiplied">Receives red, green, blue and alpha.</param>
void UIDialogFontClass::Remap(unsigned char red, unsigned char green, unsigned char blue,
	unsigned char const * sheet, unsigned char alpha, unsigned char * premultiplied)
{
	HSVClass const remap_hsv = RGBClass(red, green, blue);

	int const hue = remap_hsv.Get_Hue();

	int const end = int(hue + 15.0);
	float min_factor = 1.0f;
	for (int i = int(hue - 15.0); i <= end; ++i) {
		float const factor = Sheet_Text_Remap_Factor(i);
		if (factor < min_factor) {
			min_factor = factor;
		}
	}

	unsigned char const sat = (unsigned char)remap_hsv.Get_Saturation();
	unsigned char const val = (unsigned char)remap_hsv.Get_Value();
	float const hue_float = (float)hue;

	HSVClass const pal_hsv = RGBClass(sheet[0], sheet[1], sheet[2]);
	HSVClass out_hsv = pal_hsv;
	out_hsv.Set_Hue((unsigned char)(int)(hue_float - (int)(68.0f - pal_hsv.Get_Hue()) * min_factor));
	out_hsv.Set_Saturation((unsigned char)((sat * out_hsv.Get_Saturation()) >> 8));
	out_hsv.Set_Value((unsigned char)((val * out_hsv.Get_Value()) >> 8));

	int const pixel = Blend_Over_Black(Pack_565(out_hsv), alpha);

	premultiplied[0] = Widen((pixel & RED_MASK) >> 11, 31);
	premultiplied[1] = Widen((pixel & GREEN_MASK) >> 5, 63);
	premultiplied[2] = Widen(pixel & BLUE_MASK, 31);
	premultiplied[3] = alpha;
}


int UIDialogFontClass::Glyph(char32_t code)
{
	if (code < ' ') {
		return((int)code);
	}
	int const index = UTF8::Windows_1252_Glyph(code);
	return((index < 0) ? '?' : index);
}


UIDialogFontClass::CellPixel const & UIDialogFontClass::Pixel(int x, int y) const
{
	static CellPixel const blank = {};

	if (x < 0 || y < 0 || x >= SheetWidth || y >= SheetHeight) {
		return(blank);
	}
	return(Pixels[(std::size_t)y * SheetWidth + x]);
}


// ODDrawCharRemap draws character N from cell N + 1, the size box being cell 0.
void UIDialogFontClass::Cell_Origin(int glyph, int & x, int & y) const
{
	int const cell = glyph + 1;
	x = (cell % CellsPerRow) * Cell_Width();
	y = (cell / CellsPerRow) * Cell_Height();
}


/// <summary>
/// Measures the alpha sheet as ODGetFontMetrics does.
/// </summary>
/// <returns>bool; Does the first cell hold a size box of a usable size?</returns>
bool UIDialogFontClass::Measure(void)
{
	while (TopMargin < SheetHeight && Pixel(4, TopMargin).Alpha == 0) {
		TopMargin++;
	}

	for (int y = TopMargin; y < SheetHeight && Pixel(4, y).Alpha != 0; y++) {
		GlyphHeight++;
	}

	while (LeftMargin < SheetWidth && Pixel(LeftMargin, TopMargin).Alpha == 0) {
		LeftMargin++;
	}

	for (int x = LeftMargin; x < SheetWidth && Pixel(x, TopMargin).Alpha != 0; x++) {
		GlyphWidth++;
	}

	if (GlyphHeight == 0 || GlyphWidth == 0) {
		return(false);
	}

	int const cellsperrow = SheetWidth / Cell_Width();
	int const rows = SheetHeight / Cell_Height();
	if (cellsperrow == 0 || cellsperrow * rows < CELL_COUNT + 1) {
		return(false);
	}
	CellsPerRow = cellsperrow;

	for (int character = 0; character < CELL_COUNT; character++) {
		int cellx;
		int celly;
		Cell_Origin(character, cellx, celly);

		int const glyphx = cellx + LeftMargin;
		int const glyphy = celly + TopMargin;

		int first = -1;
		int last = 0;

		for (int x = glyphx; x < glyphx + GlyphWidth; x++) {
			for (int y = glyphy; y < glyphy + GlyphHeight; y++) {
				if (Pixel(x, y).Alpha != 0) {
					last = x;
					if (first == -1) {
						first = x;
					}
					break;
				}
			}
		}

		CharWidths[character] = (first != -1) ? (last - first + 1) : (GlyphWidth / 3 + 1);
	}

	return(true);
}


/// <summary>
/// Fetches the rows of a character's bright pixels, counted from the top of the size box. The
/// sheet draws each letter over a dark copy of itself, which is not part of its shape.
/// </summary>
/// <returns>bool; Does the character have any bright pixels?</returns>
bool UIDialogFontClass::Letter_Rows(int character, int & first, int & last) const
{
	int cellx;
	int celly;
	Cell_Origin(character, cellx, celly);

	first = -1;
	last = -1;

	for (int y = 0; y < Cell_Height(); y++) {
		for (int x = 0; x < Cell_Width(); x++) {
			CellPixel const & pixel = Pixel(cellx + x, celly + y);
			if (pixel.Alpha != 0 && std::max({ pixel.Red, pixel.Green, pixel.Blue }) >= 0x80) {
				if (first == -1) {
					first = y - TopMargin;
				}
				last = y - TopMargin;
				break;
			}
		}
	}

	return(first != -1);
}


/// <summary>
/// Reads the dlgsys sheets through the texture loader and measures them.
/// </summary>
/// <returns>bool; False, leaving the font empty, when a sheet is missing or unusable.</returns>
bool UIDialogFontClass::Load(void)
{
	*this = UIDialogFontClass();

	std::vector<unsigned char> alpha;
	std::vector<unsigned char> colour;
	int alphawidth = 0;
	int alphaheight = 0;
	int colourwidth = 0;
	int colourheight = 0;

	if (!UI_Texture_Load(DIALOG_ALPHA_SHEET, alpha, alphawidth, alphaheight)) {
		DebugString("UI: dialog font sheet '%s' could not be read\n", DIALOG_ALPHA_SHEET);
		return(false);
	}

	if (!UI_Texture_Load(DIALOG_COLOUR_SHEET, colour, colourwidth, colourheight)) {
		DebugString("UI: dialog font sheet '%s' could not be read\n", DIALOG_COLOUR_SHEET);
		return(false);
	}

	// ODDrawCharRemap reads both sheets at one offset, so they share a layout.
	if (alphawidth != colourwidth || alphaheight != colourheight || alphawidth <= 0 || alphaheight <= 0) {
		DebugString("UI: dialog font sheets '%s' and '%s' differ in size\n", DIALOG_ALPHA_SHEET, DIALOG_COLOUR_SHEET);
		return(false);
	}

	SheetWidth = alphawidth;
	SheetHeight = alphaheight;
	Pixels.resize((std::size_t)SheetWidth * SheetHeight);

	// The surface cache reduces the alpha sheet to its palette's red component.
	for (std::size_t index = 0; index < Pixels.size(); index++) {
		Pixels[index].Alpha = alpha[index * 4];
		Pixels[index].Red = colour[index * 4];
		Pixels[index].Green = colour[index * 4 + 1];
		Pixels[index].Blue = colour[index * 4 + 2];
	}

	if (!Measure()) {
		DebugString("UI: dialog font sheet '%s' holds no usable size box\n", DIALOG_ALPHA_SHEET);
		*this = UIDialogFontClass();
		return(false);
	}

	// The sheet marks no baseline, so it goes under the capital H.
	AscentRows = GlyphHeight;
	int top = 0;
	int bottom = 0;
	if (Letter_Rows('H', top, bottom)) {
		AscentRows = std::clamp(bottom + 1, 1, GlyphHeight);
	}

	if (Letter_Rows('x', top, bottom)) {
		XHeightRows = std::max(0, AscentRows - top);
	}

	return(true);
}


// Where a glyph's cell sits in an atlas, excluding its padding.
void UIDialogFontClass::Atlas_Cell(int glyph, int magnify, int & x, int & y) const
{
	int const slotwidth = Cell_Width() * magnify + ATLAS_PADDING * 2;
	int const slotheight = Cell_Height() * magnify + ATLAS_PADDING * 2;
	int const slot = glyph & 0xFF;

	x = (slot % ATLAS_COLUMNS) * slotwidth + ATLAS_PADDING;
	y = (slot / ATLAS_COLUMNS) * slotheight + ATLAS_PADDING;
}


/// <summary>
/// Draws every cell ODDrawCharRemap draws, in the requested colour, into one premultiplied
/// RGBA picture. Each sheet pixel becomes a square of magnify texels, and a cell that is never
/// drawn stays transparent.
/// </summary>
/// <param name="magnify">Texels to a sheet pixel, one or more.</param>
void UIDialogFontClass::Build_Atlas(unsigned char red, unsigned char green, unsigned char blue, int magnify,
	std::vector<unsigned char> & rgba, int & width, int & height) const
{
	magnify = std::max(1, magnify);

	width = ATLAS_COLUMNS * (Cell_Width() * magnify + ATLAS_PADDING * 2);
	height = (CELL_COUNT / ATLAS_COLUMNS) * (Cell_Height() * magnify + ATLAS_PADDING * 2);
	rgba.assign((std::size_t)width * height * 4, 0);

	if (!Is_Loaded()) {
		return;
	}

	// The sheet has few colours, and each is remapped once.
	std::unordered_map<std::uint32_t, std::uint32_t> remapped;

	for (int glyph = 0; glyph < CELL_COUNT; glyph++) {
		if (!Is_Drawn(glyph)) {
			continue;
		}

		int cellx;
		int celly;
		Cell_Origin(glyph, cellx, celly);

		int atlasx;
		int atlasy;
		Atlas_Cell(glyph, magnify, atlasx, atlasy);

		for (int y = 0; y < Cell_Height(); y++) {
			for (int x = 0; x < Cell_Width(); x++) {
				CellPixel const & pixel = Pixel(cellx + x, celly + y);
				if (pixel.Alpha == 0) {
					continue;
				}

				std::uint32_t const key = ((std::uint32_t)pixel.Alpha << 24) | ((std::uint32_t)pixel.Red << 16)
					| ((std::uint32_t)pixel.Green << 8) | pixel.Blue;

				auto found = remapped.find(key);
				if (found == remapped.end()) {
					unsigned char const sheet[3] = { pixel.Red, pixel.Green, pixel.Blue };
					unsigned char out[4];
					Remap(red, green, blue, sheet, pixel.Alpha, out);
					found = remapped.emplace(key, ((std::uint32_t)out[3] << 24) | ((std::uint32_t)out[2] << 16)
						| ((std::uint32_t)out[1] << 8) | out[0]).first;
				}

				for (int row = 0; row < magnify; row++) {
					unsigned char * texel = &rgba[(((std::size_t)atlasy + y * magnify + row) * width + atlasx + x * magnify) * 4];
					for (int column = 0; column < magnify; column++) {
						texel[0] = (unsigned char)found->second;
						texel[1] = (unsigned char)(found->second >> 8);
						texel[2] = (unsigned char)(found->second >> 16);
						texel[3] = (unsigned char)(found->second >> 24);
						texel += 4;
					}
				}
			}
		}
	}
}
