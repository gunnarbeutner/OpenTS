/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "shapemagnify.h"

#include "shapeset.h"

#include <algorithm>
#include <cstring>
#include <vector>


namespace {

// The on-disk shape layout, written here because ShapeSet keeps its own fields protected
// and a magnified copy has to be built rather than read.
#pragma pack(push, 4)
struct ShapeSetLayout
{
	short Flags;
	short Width;
	short Height;
	short Count;
};

struct ShapeRecordLayout
{
	short X;
	short Y;
	short Width;
	short Height;
	short Flags;
	short Size;
	unsigned char Color[3];
	unsigned char Unused[5];
	int Data;
};
#pragma pack(pop)

static_assert(sizeof(ShapeSetLayout) == 8, "the SHP header is 8 bytes on disk");
static_assert(sizeof(ShapeRecordLayout) == 24, "a SHP frame record is 24 bytes on disk");


// An RLE row carries its own byte length and collapses transparent runs to a zero byte
// followed by a count.
bool Shape_Row(unsigned char const * data, int size, int & at, int width, unsigned char * row)
{
	std::memset(row, 0, (std::size_t)width);

	if (at + 2 > size) return(false);

	int const length = data[at] | (data[at + 1] << 8);
	if (length < 2 || at + length > size) return(false);

	int position = at + 2;
	int filled = 0;

	while (filled < width && position < at + length) {
		unsigned char const value = data[position++];

		if (value != 0) {
			row[filled++] = value;
			continue;
		}

		if (position >= at + length) return(false);

		int run = data[position++];
		while (run-- > 0 && filled < width) {
			row[filled++] = 0;
		}
	}

	at += length;
	return(true);
}

}	// namespace


/// <summary>
/// Builds a shape set whose pixels are <paramref name="scale"/> times larger in each
/// direction, so a screen laid out at a multiple of the artwork's own size can draw the
/// artwork through the ordinary blitters. Every frame comes back uncompressed. The caller
/// owns the result and releases it with delete[] on the returned pointer cast to char *.
/// Null when the shape cannot be read or the scale is not worth applying.
/// </summary>
ShapeSet * Magnify_Shape(ShapeSet const * shapefile, int size, int numerator, int denominator)
{
	if (shapefile == nullptr || size <= 0 || denominator <= 0) return(nullptr);
	if (numerator <= denominator) return(nullptr);

	int const count = shapefile->Get_Count();
	if (count <= 0) return(nullptr);

	// Each destination pixel takes the source pixel its own centre falls on, so a frame
	// keeps its shape at any ratio and no pixel is invented between two.
	auto const scaled = [numerator, denominator](int value) {
		return((int)(((long long)value * numerator) / denominator));
	};

	std::size_t total = sizeof(ShapeSet) + sizeof(ShapeRecordLayout) * (std::size_t)count;

	for (int index = 0; index < count; index++) {
		Rect const rect = shapefile->Get_Rect(index);
		total += (std::size_t)scaled(rect.Width) * (std::size_t)scaled(rect.Height);
	}

	char * buffer = new char[total];
	std::memset(buffer, 0, total);

	ShapeSetLayout * header = (ShapeSetLayout *)buffer;
	header->Flags = 0;
	header->Width = (short)scaled(shapefile->Get_Width());
	header->Height = (short)scaled(shapefile->Get_Height());
	header->Count = (short)count;

	ShapeRecordLayout * records = (ShapeRecordLayout *)(buffer + sizeof(ShapeSet));
	std::size_t at = sizeof(ShapeSet) + sizeof(ShapeRecordLayout) * (std::size_t)count;
	std::vector<unsigned char> frame;

	for (int index = 0; index < count; index++) {
		Rect const rect = shapefile->Get_Rect(index);
		int const width = scaled(rect.Width);
		int const height = scaled(rect.Height);

		records[index].X = (short)scaled(rect.X);
		records[index].Y = (short)scaled(rect.Y);
		records[index].Width = (short)width;
		records[index].Height = (short)height;
		records[index].Flags = shapefile->Is_Transparent(index) ? 0x01 : 0;

		// Nothing in the draw path reads the size, and a magnified frame does not fit the
		// sixteen bits the field holds, so it is left at zero rather than made up.
		records[index].Size = 0;
		records[index].Data = (int)at;

		unsigned char * destination = (unsigned char *)(buffer + at);
		unsigned char const * source = (unsigned char const *)shapefile->Get_Data(index);

		if (source != nullptr && rect.Width > 0 && rect.Height > 0 && width > 0 && height > 0) {
			// The whole frame is read once, because a destination row may take any source
			// row and a compressed frame only reads forwards.
			frame.assign((std::size_t)rect.Width * (std::size_t)rect.Height, 0);

			int const remaining = size - (int)((char const *)source - (char const *)shapefile);
			int position = 0;

			for (int y = 0; y < rect.Height; y++) {
				unsigned char * line = frame.data() + (std::size_t)y * rect.Width;

				if (shapefile->Is_RLE_Compressed(index)) {
					if (!Shape_Row(source, remaining, position, rect.Width, line)) break;
				} else {
					std::memcpy(line, source + (std::size_t)y * rect.Width, (std::size_t)rect.Width);
				}
			}

			// The destination pixel's own centre decides which source pixel it takes.
			// Sampling its leading edge instead would never reach the last source row or
			// column at a ratio that is not whole, which drops the foot of a letter.
			for (int y = 0; y < height; y++) {
				long long const row = ((2LL * y + 1) * denominator) / (2LL * numerator);
				int const from = (int)std::min(row, (long long)(rect.Height - 1));
				unsigned char const * line = frame.data() + (std::size_t)from * rect.Width;
				unsigned char * out = destination + (std::size_t)y * width;

				for (int x = 0; x < width; x++) {
					long long const column = ((2LL * x + 1) * denominator) / (2LL * numerator);
					out[x] = line[(int)std::min(column, (long long)(rect.Width - 1))];
				}
			}
		}

		at += (std::size_t)width * (std::size_t)height;
	}

	return((ShapeSet *)buffer);
}


