/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "imagebackend.h"

#include "bsurface.h"
#include "_mixfile.h"
#include "manifest.h"
#include "mixfile.h"

#include <emscripten/emscripten.h>

#include <string>


namespace {

// The browser owns the only WebP decoder either side has, so a picture is
// fetched and decoded there and the pixels are handed over in one piece. One
// decode is held at a time, between the call that reads its size and the call
// that takes its pixels.
EM_ASYNC_JS(int, Image_Element_Decode, (char const * url, int * size), {
	var address = size >>> 2;
	HEAP32[address] = 0;
	HEAP32[address + 1] = 0;

	try {
		var answer = await fetch(UTF8ToString(url));
		if (!answer.ok) return 0;

		var bitmap = await createImageBitmap(await answer.blob());
		var canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
		var context = canvas.getContext('2d', {willReadFrequently: true});

		context.drawImage(bitmap, 0, 0);
		bitmap.close();

		Module.OpenTSImage = context.getImageData(0, 0, canvas.width, canvas.height);

		HEAP32[address] = canvas.width;
		HEAP32[address + 1] = canvas.height;
		return 1;
	} catch (exception) {
		Module.OpenTSImage = null;
		return 0;
	}
});


// The shell's surfaces are 565 (Build_Convert_Table in bgfxbackend.cpp); a 4x4
// ordered dither keeps a gradient from banding on the truncation, as the movie
// path's own copy does.
EM_JS(void, Image_Element_Take, (void * destination, int stride), {
	var picture = Module.OpenTSImage;
	Module.OpenTSImage = null;
	if (!picture) return;

	var BAYER4 = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]];

	var pixels = picture.data;
	var width = picture.width;
	var height = picture.height;
	var target = (destination >>> 0) >>> 1;

	for (var y = 0; y < height; y++) {
		var sourceRow = y * width * 4;
		var targetRow = target + y * stride;
		var bayerRow = BAYER4[y & 3];

		for (var x = 0; x < width; x++) {
			var source = sourceRow + x * 4;
			var bayer = bayerRow[x & 3];
			var red = pixels[source] + (bayer >> 1);
			var green = pixels[source + 1] + (bayer >> 2);
			var blue = pixels[source + 2] + (bayer >> 1);
			if (red > 255) red = 255;
			if (green > 255) green = 255;
			if (blue > 255) blue = 255;
			HEAPU16[targetRow + x] = ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
		}
	}
});


// The multiple a copy was prepared at is part of its name, so one lookup
// answers both which copy exists and what it is. A copy at the artwork's own
// size carries no multiple.
std::string Browser_Name(char const * picture_filename, int scale)
{
	std::string name(picture_filename);
	std::size_t const dot = name.find_last_of('.');
	std::string const stem = (dot == std::string::npos) ? name : name.substr(0, dot);

	if (scale <= 1) {
		return(stem + ".WEBP");
	}

	return(stem + "." + std::to_string(scale) + "X.WEBP");
}


// Beyond this the artwork outweighs anything a display can show of it.
int const SCALE_MAX = 4;

}	// namespace


// The archive that answers the artwork answers its prepared copies too, since a
// name such as SCORE.PCX is a different picture in each side's archive.
static char const * Owning_Archive(char const * picture_filename)
{
	MixFileClass * mixfile = nullptr;

	if (!MFCD::Offset(picture_filename, nullptr, &mixfile, nullptr, nullptr)) return(nullptr);
	if (mixfile == nullptr) return(nullptr);

	return(mixfile->Filename);
}


bool Image_Browser_Available(char const * picture_filename)
{
	if (picture_filename == nullptr || *picture_filename == '\0') return(false);

	char const * const archive = Owning_Archive(picture_filename);

	for (int scale = SCALE_MAX; scale >= 1; scale--) {
		if (!Manifest_Find_File(Browser_Name(picture_filename, scale).c_str(), archive).empty()) {
			return(true);
		}
	}

	return(false);
}


// A picture resolves through the manifest's "files" section like a movie does,
// to a URL the browser fetches and caches itself.
Surface * Image_Browser_Load(char const * picture_filename, int wanted, int & scale)
{
	scale = 1;

	if (picture_filename == nullptr || *picture_filename == '\0') return(nullptr);

	if (wanted > SCALE_MAX) wanted = SCALE_MAX;
	if (wanted < 1) wanted = 1;

	char const * const archive = Owning_Archive(picture_filename);
	std::string url;

	for (int candidate = wanted; candidate >= 1; candidate--) {
		url = Manifest_Find_File(Browser_Name(picture_filename, candidate).c_str(), archive);

		if (!url.empty()) {
			scale = candidate;
			break;
		}
	}

	// A release that prepared its artwork at one size only is still worth
	// taking when the page is laid out below it: the picture is drawn down to
	// the page's size, which keeps more of the detail than magnifying the
	// artwork the copy was made from.
	for (int candidate = wanted + 1; url.empty() && candidate <= SCALE_MAX; candidate++) {
		url = Manifest_Find_File(Browser_Name(picture_filename, candidate).c_str(), archive);

		if (!url.empty()) {
			scale = candidate;
		}
	}

	if (url.empty()) return(nullptr);

	int size[2] = {0, 0};
	if (!Image_Element_Decode(url.c_str(), size) || size[0] <= 0 || size[1] <= 0) {
		scale = 1;
		return(nullptr);
	}

	BSurface * picture = new BSurface(size[0], size[1], 2);

	void * buffer = picture->Lock();
	if (buffer == nullptr) {
		delete picture;
		scale = 1;
		return(nullptr);
	}

	Image_Element_Take(buffer, picture->Stride() / 2);
	picture->Unlock();

	return(picture);
}

#endif
