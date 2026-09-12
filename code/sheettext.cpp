/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "always.h"

#include "sheettext.h"

#include "dbgprint.h"
#include "dsurface.h"
#include "hsv.h"
#include "rgb.h"
#include "srfcache.h"
#include "surface.h"
#include "utf8.h"

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>


static unsigned short _RedMask;
static unsigned short _GreenMask;
static unsigned short _BlueMask;


void Sheet_Text_Prepare(void)
{
	static bool _prepared = false;
	if (_prepared) {
		return;
	}
	_prepared = true;

	_RedMask = (unsigned short)(((unsigned short)255 >> DSurface::Get_Red_Left()) << DSurface::Get_Red_Right());
	_GreenMask = (unsigned short)(((unsigned short)255 >> DSurface::Get_Green_Left()) << DSurface::Get_Green_Right());
	_BlueMask = (unsigned short)(((unsigned short)255 >> DSurface::Get_Blue_Left()) << DSurface::Get_Blue_Right());

	Cache_Dialog_Artwork();
}


unsigned short Sheet_Blend_Pixel(unsigned short pixel, unsigned short color, unsigned char alpha)
{
	Sheet_Text_Prepare();

	unsigned const color_alpha = alpha;
	unsigned const pixel_alpha = 255 - alpha;

	unsigned short const r = ((((pixel & _RedMask) * pixel_alpha) + ((color & _RedMask) * color_alpha)) >> 8) & _RedMask;
	unsigned short const g = ((((pixel & _GreenMask) * pixel_alpha) + ((color & _GreenMask) * color_alpha)) >> 8) & _GreenMask;
	unsigned short const b = (((pixel & _BlueMask) * pixel_alpha) + ((color & _BlueMask) * color_alpha)) >> 8;
	return((unsigned short)(r | g | b));
}


// The sheets hold 256 cells in Windows-1252 order; a code point without a cell draws as '?'.
static unsigned char Glyph(char32_t code)
{
	if (code < ' ') {
		return((unsigned char)code);
	}
	int const index = UTF8::Windows_1252_Glyph(code);
	return((unsigned char)(index < 0 ? '?' : index));
}


float Sheet_Text_Remap_Factor(int hue)
{
	float val = 1.0f;

	int const primaries[3] = { 43, 128, 213 };

	for (int value : primaries) {
		if (hue > value - 16 && hue <= value) {
			val = float(value - hue);
			val *= (1.0f / 16);
			val *= (60.0f / 100);
			val += (40.0f / 100);
		} else if (hue > value && hue <= value + 16) {
			val = float(hue - value);
			val *= (1.0f / 16);
			val *= (60.0f / 100);
			val += (40.0f / 100);
		}
	}
	return(val);
}


/// <summary>
/// Measures a font's coverage sheet: the margins, the size of a character cell, and the inked
/// width of every character. The measurement is kept by font name.
/// </summary>
/// <returns>bool; Could the sheet be read?</returns>
bool Sheet_Font_Metrics(char const * font, SheetFontMetrics & metrics)
{
	static std::unordered_map<std::string, SheetFontMetrics> _measured;

	std::string name(font);
	for (char & c : name) {
		c = (char)std::tolower((unsigned char)c);
	}

	auto const found = _measured.find(name);
	if (found != _measured.end()) {
		metrics = found->second;
		return(true);
	}

	Sheet_Text_Prepare();

	char sheet[64];
	UTF8::Copy(sheet, font);
	UTF8::Append(sheet, "a.pcx");

	char palette[768];
	Surface * surf = SurfaceCache.GetSurface(sheet, palette);
	if (surf == nullptr) {
		return(false);
	}

	DebugString("TS: Computing font metrics....\n");

	SheetFontMetrics temp;
	std::memset(&temp, 0, sizeof(temp));

	char const * base = (char const *)surf->Lock();
	int const stride = surf->Stride();

	// The rows and columns of the size box in the first cell, probed at column 4 and along
	// its top row.
	while (temp.TopMargin < surf->Get_Height()) {
		if (base[stride * temp.TopMargin + 4] != 0) break;
		++temp.TopMargin;
	}
	for (int y = temp.TopMargin; y < surf->Get_Height() && base[stride * y + 4] != 0; ++y) {
		++temp.GlyphHeight;
	}

	while (temp.LeftMargin < surf->Get_Width()) {
		if (base[stride * temp.TopMargin + temp.LeftMargin] != 0) break;
		++temp.LeftMargin;
	}
	for (int x = temp.LeftMargin; x < surf->Get_Width() && base[stride * temp.TopMargin + x] != 0; ++x) {
		++temp.GlyphWidth;
	}

	int const per_row = surf->Get_Width() / (temp.LeftMargin + temp.GlyphWidth);

	for (int ch = 0; ch < 256; ++ch) {
		int const glyph_y = temp.TopMargin + (temp.GlyphHeight + temp.TopMargin) * ((ch + 1) / per_row);
		int const glyph_x = temp.LeftMargin + (temp.LeftMargin + temp.GlyphWidth) * ((ch + 1) % per_row);

		int first = -1;
		int last = 0;

		for (int x = glyph_x; x < glyph_x + temp.GlyphWidth; ++x) {
			bool inked = false;
			for (int y = glyph_y; y < glyph_y + temp.GlyphHeight; ++y) {
				if (base[stride * y + x] != 0) {
					inked = true;
				}
			}
			if (inked) {
				last = x;
				if (first == -1) first = x;
			}
		}

		temp.CharWidths[ch] = (first != -1) ? (last - first + 1) : (temp.GlyphWidth / 3 + 1);
	}

	surf->Unlock();

	_measured[name] = temp;
	metrics = temp;
	return(true);
}


/// <summary>
/// Draws one line of text, each palette entry of the colour sheet shifted toward the
/// requested colour and blended onto the surface by the coverage sheet. The whole cell is
/// drawn, one pixel left of the pen, with its size box's top row at the rectangle's top.
/// </summary>
void Sheet_Draw_Line(Surface & surface, char const * text, int max_chars, Rect const & rect, char const * font, std::uint32_t color, int flags, int spacing)
{
	Sheet_Text_Prepare();

	Rect draw_rect = rect;

	char name_i[64];
	UTF8::Copy(name_i, font);
	UTF8::Append(name_i, "i.pcx");

	char palette[768];
	Surface * sheet_i = SurfaceCache.GetSurface(name_i, palette);
	if (sheet_i == nullptr) {
		return;
	}

	char name_a[64];
	UTF8::Copy(name_a, font);
	UTF8::Append(name_a, "a.pcx");

	Surface * sheet_a = SurfaceCache.GetSurface(name_a, nullptr);
	if (sheet_a == nullptr) {
		return;
	}

	RGBClass remap_rgb((unsigned char)color, (unsigned char)(color >> 8), (unsigned char)(color >> 16));
	HSVClass remap_hsv = remap_rgb;
	RGBClass pal_rgb;
	HSVClass out_hsv;

	int const hue = remap_hsv.Get_Hue();

	float min_factor = 1.0f;
	for (int i = int(hue - 15.0); i <= int(hue + 15.0); ++i) {
		float const factor = Sheet_Text_Remap_Factor(i);
		if (factor < min_factor) {
			min_factor = factor;
		}
	}

	unsigned char const sat = (unsigned char)remap_hsv.Get_Saturation();
	unsigned char const val = (unsigned char)remap_hsv.Get_Value();

	unsigned short remap_table[256];
	float const hue_float = (float)hue;
	unsigned char const * pal = (unsigned char const *)palette;
	for (int i = 0; i < 256; ++i) {
		pal_rgb.Set_Red(pal[0]);
		pal_rgb.Set_Green(pal[1]);
		pal_rgb.Set_Blue(pal[2]);
		HSVClass pal_hsv = pal_rgb;

		out_hsv = pal_hsv;
		out_hsv.Set_Hue((unsigned char)(int)(hue_float - (int)(68.0f - pal_hsv.Get_Hue()) * min_factor));
		out_hsv.Set_Saturation((unsigned char)((sat * out_hsv.Get_Saturation()) >> 8));
		out_hsv.Set_Value((unsigned char)((val * out_hsv.Get_Value()) >> 8));

		RGBClass out_rgb = out_hsv;
		pal_rgb = out_rgb;

		remap_table[i] = (unsigned short)DSurface::Build_Hicolor_Pixel(out_rgb.Get_Red(), out_rgb.Get_Green(), out_rgb.Get_Blue());
		pal += 3;
	}

	SheetFontMetrics font_data;
	if (!Sheet_Font_Metrics(font, font_data)) {
		return;
	}

	if ((int)std::strlen(text) < max_chars) {
		max_chars = (int)std::strlen(text);
	}

	int total_width = 0;
	for (char const * cursor = text; cursor - text < max_chars; ) {
		total_width += font_data.CharWidths[Glyph(UTF8::Decode(cursor))] + spacing;
	}

	if ((flags & SHEET_TEXT_CENTER) != 0) {
		draw_rect.X += (draw_rect.Width - draw_rect.X - total_width) / 2;
	} else if ((flags & SHEET_TEXT_RIGHT) != 0) {
		draw_rect.X = draw_rect.Width - total_width - 1;
	}

	if ((flags & SHEET_TEXT_MIDDLE) != 0) {
		draw_rect.Y = draw_rect.Y + (draw_rect.Height - font_data.GlyphHeight - draw_rect.Y) / 2;
	}

	draw_rect.Y -= font_data.TopMargin;
	--draw_rect.X;

	unsigned char * src_i = (unsigned char *)sheet_i->Lock();
	unsigned char * src_a = (unsigned char *)sheet_a->Lock();
	unsigned char * dst = (unsigned char *)surface.Lock();

	if (src_i != nullptr && src_a != nullptr && dst != nullptr) {
		int const cell_w = font_data.GlyphWidth + font_data.LeftMargin;
		int const cell_h = font_data.GlyphHeight + font_data.TopMargin;
		int const chars_per_row = sheet_i->Get_Width() / cell_w;
		int const dst_stride = surface.Stride() / 2;
		int const src_stride = sheet_i->Stride();
		std::ptrdiff_t const src_delta = src_i - src_a;

		int x = draw_rect.X;
		for (char const * cursor = text; cursor - text < max_chars; ) {
			unsigned char const index = Glyph(UTF8::Decode(cursor));

			if (index > ' ') {
				int const glyph = index + 1;
				int const src_x = (glyph % chars_per_row) * cell_w;
				int const src_y = (glyph / chars_per_row) * cell_h;

				unsigned char const * alpha_col = src_a + (src_y * src_stride + src_x);
				unsigned char * dst_col = dst + 2 * (dst_stride * draw_rect.Y + x);

				for (int column = 0; column < cell_w; ++column) {
					unsigned short * dst_px = (unsigned short *)dst_col;
					unsigned char const * alpha_px = alpha_col;

					for (int row = 0; row < cell_h; ++row) {
						unsigned char const alpha = *alpha_px;
						if (alpha != 0) {
							*dst_px = Sheet_Blend_Pixel(*dst_px, remap_table[alpha_px[src_delta]], alpha);
						}

						dst_px += dst_stride;
						alpha_px += src_stride;
					}

					++alpha_col;
					dst_col += 2;
				}
			}

			x += font_data.CharWidths[index] + spacing;
		}
	}

	surface.Unlock();
	sheet_a->Unlock();
	sheet_i->Unlock();
}


/// <summary>
/// Draws text broken into lines that fit the rectangle, honouring the newlines already in it
/// and breaking at a space wherever one can be found. Vertical centring is dropped once a
/// line has to wrap.
/// </summary>
void Sheet_Draw_Text(Surface & surface, char const * text, Rect const & rect, char const * font, std::uint32_t color, int flags, int spacing)
{
	int line_len = (int)std::strlen(text);
	char const * line_ptr = text;
	Rect draw_rect = rect;

	SheetFontMetrics data;
	if (!Sheet_Font_Metrics(font, data)) {
		return;
	}

	while (line_len) {
		char const * nl_ptr = std::strchr(line_ptr, '\n');
		if (nl_ptr) {
			int const nl_len = (int)(nl_ptr - line_ptr) + 1;
			if (line_len >= nl_len) {
				line_len = nl_len;
			}
		}

		if ((unsigned char)*line_ptr <= ' ') {
			++line_ptr;
			if (--line_len == 0) {
				return;
			}
		}

		// Measured from the start of the whole text rather than the line, as the dialogs
		// measured it.
		int text_width = 0;
		for (char const * cursor = text; cursor - text < line_len; ) {
			text_width += spacing + data.CharWidths[Glyph(UTF8::Decode(cursor))];
		}

		if (text_width > draw_rect.Width - draw_rect.X) {
			int const fallback = (int)UTF8::Boundary_Before(line_ptr, line_len - 1);
			int cut = line_len - 1;

			flags &= ~SHEET_TEXT_MIDDLE;

			while (cut > 0) {
				if ((unsigned char)line_ptr[cut] <= ' ') {
					break;
				}
				--cut;
			}
			if (cut > 0) {
				line_len = cut;
				continue;
			}

			line_len = fallback;
		} else {
			Sheet_Draw_Line(surface, line_ptr, line_len, draw_rect, font, color, flags, spacing);
			line_ptr += line_len;
			draw_rect.Y += data.GlyphHeight;
			line_len = (int)std::strlen(line_ptr);
		}
	}
}
