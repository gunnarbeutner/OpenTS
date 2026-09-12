/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uicaption.h"

#include "ccfile.h"
#include "dbgprint.h"
#include "dsurface.h"
#include "surface.h"
#include "utf8.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <vector>


static char const CAPTION_FACE[] = "LiberationSans-Regular.ttf";

// What the caption asked GDI for: a cell 28 pixels tall whose average letter is 20 wide.
static int const CAPTION_CELL_HEIGHT = 28;
static int const CAPTION_AVERAGE_WIDTH = 20;


struct UICaptionFace
{
	bool Tried = false;
	FT_Library Library = nullptr;
	FT_Face Face = nullptr;
	std::vector<unsigned char> Data;

	// The cell's ascent in pixels, which puts the baseline below the requested top.
	int Ascent = 0;
};


static UICaptionFace _Caption;


/// <summary>
/// Opens the face once, sized as GDI sized a font from a cell height and an average width:
/// the cell is the Windows ascent and descent together, and the average width stretches the
/// face horizontally.
/// </summary>
static bool Prepare_Face(void)
{
	if (_Caption.Tried) {
		return(_Caption.Face != nullptr);
	}
	_Caption.Tried = true;

	CCFileClass file(CAPTION_FACE);
	int const size = file.Is_Available() ? file.Size() : 0;
	if (size <= 0 || !file.Open()) {
		DebugString("UI: the caption face %s could not be read\n", CAPTION_FACE);
		return(false);
	}

	_Caption.Data.resize((std::size_t)size);
	int const read = file.Read(_Caption.Data.data(), size);
	file.Close();
	if (read != size) {
		return(false);
	}

	if (FT_Init_FreeType(&_Caption.Library) != 0) {
		return(false);
	}

	if (FT_New_Memory_Face(_Caption.Library, _Caption.Data.data(), (FT_Long)_Caption.Data.size(), 0, &_Caption.Face) != 0) {
		_Caption.Face = nullptr;
		return(false);
	}

	FT_Face const face = _Caption.Face;
	TT_OS2 const * const os2 = (TT_OS2 const *)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);

	double const units = face->units_per_EM;
	double cell = face->ascender - face->descender;
	double ascent = face->ascender;
	double average = units / 2;

	if (os2 != nullptr) {
		cell = (double)os2->usWinAscent + os2->usWinDescent;
		ascent = os2->usWinAscent;
		average = os2->xAvgCharWidth;
	}

	double const em = CAPTION_CELL_HEIGHT * units / cell;
	double const stretch = CAPTION_AVERAGE_WIDTH / (average * em / units);

	FT_Set_Char_Size(face, 0, (FT_F26Dot6)(em * 64 + 0.5), 72, 72);

	FT_Matrix matrix;
	matrix.xx = (FT_Fixed)(stretch * 0x10000 + 0.5);
	matrix.xy = 0;
	matrix.yx = 0;
	matrix.yy = 0x10000;
	FT_Set_Transform(face, &matrix, nullptr);

	_Caption.Ascent = (int)(ascent * em / units + 0.5);
	return(true);
}


// Lightens a display pixel toward white by coverage in 255ths.
static unsigned short Lighten(unsigned short pixel, int coverage)
{
	int red = ((pixel >> DSurface::RedRight) << DSurface::RedLeft) & 0xFF;
	int green = ((pixel >> DSurface::GreenRight) << DSurface::GreenLeft) & 0xFF;
	int blue = ((pixel >> DSurface::BlueRight) << DSurface::BlueLeft) & 0xFF;

	red += (255 - red) * coverage / 255;
	green += (255 - green) * coverage / 255;
	blue += (255 - blue) * coverage / 255;

	return((unsigned short)DSurface::Build_Hicolor_Pixel(red, green, blue));
}


void UI_Draw_Caption(Surface & surface, Rect const & rect, char const * text)
{
	if (text == nullptr || text[0] == '\0' || surface.Bytes_Per_Pixel() != 2 || !Prepare_Face()) {
		return;
	}

	FT_Face const face = _Caption.Face;

	std::vector<char32_t> codes;
	for (char const * cursor = text; *cursor != '\0'; ) {
		codes.push_back(UTF8::Decode(cursor));
	}

	long width = 0;
	for (char32_t code : codes) {
		if (FT_Load_Char(face, code, FT_LOAD_DEFAULT) == 0) {
			width += face->glyph->advance.x;
		}
	}

	unsigned short * pixels = (unsigned short *)surface.Lock();
	if (pixels == nullptr) {
		return;
	}

	int const stride = surface.Stride() / 2;
	int const surface_width = surface.Get_Width();
	int const surface_height = surface.Get_Height();

	long pen = (long)(rect.X + rect.Width / 2) * 64 - width / 2;
	int const baseline = rect.Y + rect.Height / 2 + _Caption.Ascent;

	for (char32_t code : codes) {
		if (FT_Load_Char(face, code, FT_LOAD_RENDER) != 0) {
			continue;
		}

		FT_GlyphSlot const glyph = face->glyph;
		FT_Bitmap const & bitmap = glyph->bitmap;
		int const left = (int)(pen >> 6) + glyph->bitmap_left;
		int const top = baseline - glyph->bitmap_top;

		for (int row = 0; row < (int)bitmap.rows; row++) {
			int const y = top + row;
			if (y < 0 || y >= surface_height) {
				continue;
			}

			for (int column = 0; column < (int)bitmap.width; column++) {
				int const x = left + column;
				int const coverage = bitmap.buffer[row * bitmap.pitch + column];
				if (x < 0 || x >= surface_width || coverage == 0) {
					continue;
				}

				unsigned short & pixel = pixels[y * stride + x];
				pixel = Lighten(pixel, coverage);
			}
		}

		pen += glyph->advance.x;
	}

	surface.Unlock();
}
