/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The shapes here are built by hand rather than read from a game file, so the harness
// covers both frame encodings without any game data.

#include "shapemagnify.h"

#include "shapeset.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>


namespace {

struct Frame
{
	int X;
	int Y;
	int Width;
	int Height;
	bool RLE;
	std::vector<unsigned char> Pixels;
};


// One shape file's worth of bytes, laid out as the format puts them: the header, then a
// record for each frame, then the frame data.
std::vector<char> Build_Shape(int width, int height, std::vector<Frame> const & frames)
{
	std::size_t const records = 24 * frames.size();
	std::vector<char> blob(8 + records);

	short * header = (short *)blob.data();
	header[0] = 0;
	header[1] = (short)width;
	header[2] = (short)height;
	header[3] = (short)frames.size();

	for (std::size_t index = 0; index < frames.size(); index++) {
		Frame const & frame = frames[index];
		std::vector<unsigned char> encoded;

		if (!frame.RLE) {
			encoded = frame.Pixels;
		} else {
			for (int y = 0; y < frame.Height; y++) {
				std::vector<unsigned char> row;
				int x = 0;

				while (x < frame.Width) {
					unsigned char const value = frame.Pixels[(std::size_t)y * frame.Width + x];

					if (value != 0) {
						row.push_back(value);
						x++;
						continue;
					}

					int run = 0;
					while (x + run < frame.Width && run < 255
							&& frame.Pixels[(std::size_t)y * frame.Width + x + run] == 0) {
						run++;
					}

					row.push_back(0);
					row.push_back((unsigned char)run);
					x += run;
				}

				int const length = (int)row.size() + 2;
				encoded.push_back((unsigned char)(length & 0xff));
				encoded.push_back((unsigned char)(length >> 8));
				encoded.insert(encoded.end(), row.begin(), row.end());
			}
		}

		short * record = (short *)(blob.data() + 8 + 24 * index);
		record[0] = (short)frame.X;
		record[1] = (short)frame.Y;
		record[2] = (short)frame.Width;
		record[3] = (short)frame.Height;
		record[4] = (short)(0x01 | (frame.RLE ? 0x02 : 0));
		record[5] = (short)encoded.size();

		int const at = (int)blob.size();
		std::memcpy(blob.data() + 8 + 24 * index + 20, &at, sizeof(at));
		blob.insert(blob.end(), (char const *)encoded.data(),
			(char const *)encoded.data() + encoded.size());
	}

	return(blob);
}


unsigned char Pixel(ShapeSet const * shape, int frame, int x, int y)
{
	Rect const rect = shape->Get_Rect(frame);
	unsigned char const * pixels = (unsigned char const *)shape->Get_Data(frame);

	return(pixels[(std::size_t)y * rect.Width + x]);
}


// Every source pixel becomes a scale by scale block, and the frame's own box grows with it.
void Check_Magnified(std::vector<Frame> const & frames, bool rle)
{
	std::vector<char> const blob = Build_Shape(8, 6, frames);
	ShapeSet const * source = (ShapeSet const *)blob.data();

	int const scale = 3;
	ShapeSet * magnified = Magnify_Shape(source, (int)blob.size(), scale, 1);
	assert(magnified != nullptr);

	assert(magnified->Get_Count() == source->Get_Count());
	assert(magnified->Get_Width() == source->Get_Width() * scale);
	assert(magnified->Get_Height() == source->Get_Height() * scale);
	assert(!magnified->Is_RLE_Compressed(0));

	for (int index = 0; index < source->Get_Count(); index++) {
		Rect const before = source->Get_Rect(index);
		Rect const after = magnified->Get_Rect(index);

		assert(after.X == before.X * scale);
		assert(after.Y == before.Y * scale);
		assert(after.Width == before.Width * scale);
		assert(after.Height == before.Height * scale);

		for (int y = 0; y < after.Height; y++) {
			for (int x = 0; x < after.Width; x++) {
				unsigned char const want = frames[(std::size_t)index]
					.Pixels[(std::size_t)(y / scale) * before.Width + (x / scale)];

				assert(Pixel(magnified, index, x, y) == want);
			}
		}
	}

	delete [] (char *)magnified;

	std::printf("%-64s %s\n", rle ? "an RLE frame magnifies pixel for pixel"
		: "a raw frame magnifies pixel for pixel", "ok");
}

}	// namespace


int main(void)
{
	// A run of transparent pixels, a run of ink, and a lone pixel at the end, so the row
	// decoder meets every case it has.
	std::vector<unsigned char> const pattern = {
		0, 0, 0, 7, 7, 0, 0, 9,
		0, 1, 2, 3, 4, 5, 6, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		8, 0, 8, 0, 8, 0, 8, 0,
	};

	Frame raw = { 1, 2, 8, 4, false, pattern };
	Frame packed = { 1, 2, 8, 4, true, pattern };

	Check_Magnified({ raw }, false);
	Check_Magnified({ packed }, true);

	// Two frames, so the second one's data offset has to be right as well.
	Check_Magnified({ packed, raw }, true);

	std::vector<char> const blob = Build_Shape(8, 6, { raw });
	ShapeSet const * source = (ShapeSet const *)blob.data();

	assert(Magnify_Shape(source, (int)blob.size(), 1, 1) == nullptr);
	assert(Magnify_Shape(source, (int)blob.size(), 1, 2) == nullptr);
	assert(Magnify_Shape(source, (int)blob.size(), 2, 0) == nullptr);
	assert(Magnify_Shape(nullptr, 16, 2, 1) == nullptr);
	assert(Magnify_Shape(source, 0, 2, 1) == nullptr);
	std::printf("%-64s %s\n", "a ratio that would not enlarge and a missing shape are refused", "ok");

	// A ratio between two whole numbers, which is what a window that is not a multiple of
	// the artwork asks for.
	{
		std::vector<char> const blob3 = Build_Shape(8, 6, { raw });
		ShapeSet const * source3 = (ShapeSet const *)blob3.data();
		ShapeSet * half = Magnify_Shape(source3, (int)blob3.size(), 3, 2);

		assert(half != nullptr);
		assert(half->Get_Width() == 12 && half->Get_Height() == 9);

		Rect const box = half->Get_Rect(0);
		assert(box.Width == 12 && box.Height == 6);
		assert(box.X == 1 && box.Y == 3);

		// The corners still carry the corner pixels they were taken from.
		assert(Pixel(half, 0, 0, 0) == pattern[0]);
		assert(Pixel(half, 0, box.Width - 1, 0) == pattern[7]);

		delete [] (char *)half;
		std::printf("%-64s %s\n", "a three halves ratio keeps the frame's shape", "ok");
	}

	return 0;
}
