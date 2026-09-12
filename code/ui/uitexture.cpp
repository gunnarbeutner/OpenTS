/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The game's dialog artwork as textures. The backdrop is composed here rather than
// expressed in a stylesheet because the original is not a border and a fill: it is a
// wallpaper sampled at the dialog's own position, two tiled bars, four corner pieces, and
// sixteen fading glow passes. Composing it reproduces the dialog exactly and leaves the
// stylesheet to say only where it goes.

#include "always.h"

#include "uitexture.h"

#include "ccfile.h"
#include "dbgprint.h"
#include "palette.h"
#include "pcx.h"
#include "rgb.h"
#include "shapeset.h"
#include "surface.h"
#include "uishell.h"

#include <RmlUi/Core/Core.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>


// A decoded picture: straight RGBA8, opaque, because the dialogs blit their artwork
// without a colour key.
struct UIPicture
{
	std::vector<unsigned char> Pixels;
	int Width = 0;
	int Height = 0;

	bool Is_Valid(void) const { return(Width > 0 && Height > 0 && !Pixels.empty()); }
};


static std::map<std::string, UIPicture> _Pictures;
static std::map<std::string, UISurfaceSource> _Surfaces;


// The pieces the backdrop is made of, in the order they are laid down.
static char const * const FRAME_WALLPAPER = "dbak6440.pcx";
static char const * const FRAME_LEFT_BAR = "leftbar.pcx";
static char const * const FRAME_RIGHT_BAR = "rightbar.pcx";
static char const * const FRAME_CORNER_UL = "bar_ul.pcx";
static char const * const FRAME_CORNER_UR = "bar_ur.pcx";
static char const * const FRAME_CORNER_LL = "bar_ll.pcx";
static char const * const FRAME_CORNER_LR = "bar_lr.pcx";


static UIPicture const & Picture(char const * name)
{
	static UIPicture const missing;

	auto found = _Pictures.find(name);
	if (found != _Pictures.end()) {
		return(found->second);
	}

	UIPicture picture;

	CCFileClass file(name);
	PaletteClass palette;
	Surface * surface = Read_PCX_File(file, &palette);

	if (surface == nullptr) {
		DebugString("UI: artwork '%s' could not be read\n", name);
		_Pictures[name] = picture;
		return(_Pictures[name]);
	}

	picture.Width = surface->Get_Width();
	picture.Height = surface->Get_Height();

	if (surface->Bytes_Per_Pixel() == 1 && picture.Width > 0 && picture.Height > 0) {
		picture.Pixels.resize((std::size_t)picture.Width * picture.Height * 4);

		unsigned char const * source = (unsigned char const *)surface->Lock();
		int const stride = surface->Stride();

		if (source != nullptr) {
			for (int y = 0; y < picture.Height; y++) {
				unsigned char const * row = source + (std::size_t)y * stride;
				unsigned char * out = picture.Pixels.data() + (std::size_t)y * picture.Width * 4;

				for (int x = 0; x < picture.Width; x++) {
					RGBClass const & colour = palette[row[x]];
					out[x * 4 + 0] = (unsigned char)colour.Get_Red();
					out[x * 4 + 1] = (unsigned char)colour.Get_Green();
					out[x * 4 + 2] = (unsigned char)colour.Get_Blue();
					out[x * 4 + 3] = 0xFF;
				}
			}

			surface->Unlock();
		} else {
			picture.Pixels.clear();
		}
	} else {
		DebugString("UI: artwork '%s' is %d bytes per pixel, which is not a paletted picture\n",
			name, surface->Bytes_Per_Pixel());
	}

	delete surface;

	_Pictures[name] = std::move(picture);
	return(_Pictures[name]);
}


// Copies a rectangle of a picture into the target. A source position outside the picture
// is left as it was, which is what keeps the black margin the original shows when a dialog
// reaches past the 640 by 400 wallpaper.
static void Blit(std::vector<unsigned char> & target, int width, int height,
	UIPicture const & source, int sourcex, int sourcey)
{
	if (!source.Is_Valid()) {
		return;
	}

	for (int y = 0; y < height; y++) {
		int const sy = sourcey + y;
		if (sy < 0 || sy >= source.Height) {
			continue;
		}

		for (int x = 0; x < width; x++) {
			int const sx = sourcex + x;
			if (sx < 0 || sx >= source.Width) {
				continue;
			}

			std::memcpy(&target[((std::size_t)y * width + x) * 4],
				&source.Pixels[((std::size_t)sy * source.Width + sx) * 4], 4);
		}
	}
}


// Copies a whole picture to a position in the target, clipped to it.
static void Blit_At(std::vector<unsigned char> & target, int width, int height,
	UIPicture const & source, int destx, int desty)
{
	if (!source.Is_Valid()) {
		return;
	}

	for (int y = 0; y < source.Height; y++) {
		int const ty = desty + y;
		if (ty < 0 || ty >= height) {
			continue;
		}

		for (int x = 0; x < source.Width; x++) {
			int const tx = destx + x;
			if (tx < 0 || tx >= width) {
				continue;
			}

			std::memcpy(&target[((std::size_t)ty * width + tx) * 4],
				&source.Pixels[((std::size_t)y * source.Width + x) * 4], 4);
		}
	}
}


// Tiles a picture into a rectangle, sampling from the middle of a picture larger than the
// rectangle, which is what the surface cache's own draw does.
static void Tile(std::vector<unsigned char> & target, int width, int height,
	UIPicture const & source, int destx, int desty, int destwidth, int destheight)
{
	if (!source.Is_Valid()) {
		return;
	}

	int const offsetx = source.Width > destwidth ? (source.Width - destwidth) / 2 : 0;
	int const offsety = source.Height > destheight ? (source.Height - destheight) / 2 : 0;

	for (int y = 0; y < destheight; y++) {
		int const ty = desty + y;
		if (ty < 0 || ty >= height) {
			continue;
		}

		for (int x = 0; x < destwidth; x++) {
			int const tx = destx + x;
			if (tx < 0 || tx >= width) {
				continue;
			}

			int const sx = (offsetx + x) % source.Width;
			int const sy = (offsety + y) % source.Height;

			std::memcpy(&target[((std::size_t)ty * width + tx) * 4],
				&source.Pixels[((std::size_t)sy * source.Width + sx) * 4], 4);
		}
	}
}


// One glow pass: white blended over a run of pixels at the given strength.
static void Glow_Run(std::vector<unsigned char> & target, int width, int height,
	int x0, int y0, int x1, int y1, unsigned char strength)
{
	int const stepx = (x1 > x0) ? 1 : ((x1 < x0) ? -1 : 0);
	int const stepy = (y1 > y0) ? 1 : ((y1 < y0) ? -1 : 0);
	int const count = 1 + ((stepx != 0) ? std::abs(x1 - x0) : std::abs(y1 - y0));

	int x = x0;
	int y = y0;

	for (int index = 0; index < count; index++, x += stepx, y += stepy) {
		if (x < 0 || x >= width || y < 0 || y >= height) {
			continue;
		}

		unsigned char * pixel = &target[((std::size_t)y * width + x) * 4];

		for (int channel = 0; channel < 3; channel++) {
			int const blended = pixel[channel] + ((0xFF - pixel[channel]) * strength) / 0xFF;
			pixel[channel] = (unsigned char)(blended > 0xFF ? 0xFF : blended);
		}
	}
}


/// <summary>
/// Composes the dialog backdrop at a given size, as OwnerDraw::Draw_Dialog_Back does, with
/// the wallpaper sampled at the dialog's position within it, or centred when placed is false.
/// </summary>
static bool Compose_Frame(int width, int height, bool placed, int x, int y, std::vector<unsigned char> & rgba)
{
	if (width <= 0 || height <= 0) {
		return(false);
	}

	UIPicture const & wallpaper = Picture(FRAME_WALLPAPER);
	if (!wallpaper.Is_Valid()) {
		return(false);
	}

	rgba.assign((std::size_t)width * height * 4, 0);

	for (std::size_t index = 3; index < rgba.size(); index += 4) {
		rgba[index] = 0xFF;
	}

	// The wallpaper and an unplaced dialog are both centred on the frame, so the part that
	// shows through is decided by their sizes alone, whatever the resolution. A dialog wider
	// or taller than the artwork keeps black where it runs out.
	if (!placed) {
		x = (wallpaper.Width - width) / 2;
		y = (wallpaper.Height - height) / 2;
	}

	Blit(rgba, width, height, wallpaper, x, y);

	UIPicture const & leftbar = Picture(FRAME_LEFT_BAR);
	UIPicture const & rightbar = Picture(FRAME_RIGHT_BAR);

	if (leftbar.Is_Valid()) {
		Tile(rgba, width, height, leftbar, 0, 0, leftbar.Width, height);
	}

	if (rightbar.Is_Valid()) {
		Tile(rgba, width, height, rightbar, width - rightbar.Width, 0, rightbar.Width, height);
	}

	UIPicture const & upperleft = Picture(FRAME_CORNER_UL);

	if (upperleft.Is_Valid()) {
		int const cornerwidth = upperleft.Width;
		int const cornerheight = upperleft.Height;

		Blit_At(rgba, width, height, upperleft, 0, 0);
		Blit_At(rgba, width, height, Picture(FRAME_CORNER_LL), 0, height - cornerheight);
		Blit_At(rgba, width, height, Picture(FRAME_CORNER_UR), width - cornerwidth, 0);
		Blit_At(rgba, width, height, Picture(FRAME_CORNER_LR), width - cornerwidth, height - cornerheight);
	}

	// Sixteen passes of white, fading from 96 to 6 as they move inward, between the bars.
	int const leftwidth = leftbar.Is_Valid() ? leftbar.Width : 0;
	int const rightwidth = rightbar.Is_Valid() ? rightbar.Width : 0;

	for (int layer = 0; layer < 16; layer++) {
		unsigned char const strength = (unsigned char)(96 - 6 * layer);

		int const left = layer + leftwidth;
		int const right = width - layer - rightwidth - 1;

		Glow_Run(rgba, width, height, left, layer, right, layer, strength);
		Glow_Run(rgba, width, height, left, height - layer - 1, right, height - layer - 1, strength);
		Glow_Run(rgba, width, height, left, layer + 1, left, height - layer - 2, strength);
		Glow_Run(rgba, width, height, right, layer + 1, right, height - layer - 2, strength);
	}

	return(true);
}


/// <summary>
/// Composes a button skin at a given size, as the owner-draw button painter does: a left
/// cap, the middle strip tiled from its own centre, and a right cap.
/// </summary>
/// <param name="pressed">Whether to use the pressed set rather than the raised one.</param>
static bool Compose_Button(bool pressed, int width, int height, std::vector<unsigned char> & rgba)
{
	if (width <= 0 || height <= 0) {
		return(false);
	}

	// The artwork comes in two heights and the painter took the taller set only for a
	// button tall enough for it.
	int const skin = (height >= 30) ? 30 : 24;
	char const updown = pressed ? 'd' : 'u';

	char leftname[32];
	char middlename[32];
	char rightname[32];

	std::snprintf(leftname, sizeof(leftname), "b%ce_li%d.pcx", updown, skin);
	std::snprintf(middlename, sizeof(middlename), "b%ce_mi%d.pcx", updown, skin);
	std::snprintf(rightname, sizeof(rightname), "b%ce_ri%d.pcx", updown, skin);

	UIPicture const & left = Picture(leftname);
	UIPicture const & middle = Picture(middlename);
	UIPicture const & right = Picture(rightname);

	if (!left.Is_Valid() || !middle.Is_Valid() || !right.Is_Valid()) {
		return(false);
	}

	rgba.assign((std::size_t)width * height * 4, 0);

	int const capleft = left.Width;
	int const capright = right.Width;
	int const middlewidth = width - capleft - capright;

	// The painter centred the artwork in a button taller than it, with the dialog showing
	// through above and below, and drew a pressed button two pixels lower, cut off at the
	// button's bottom.
	int const top = (height - left.Height) / 2 + (pressed ? 2 : 0);

	if (middlewidth > 0) {
		// Sampled from the middle of the strip when the strip is the wider of the two,
		// which is what the surface cache's own draw does.
		Tile(rgba, width, height, middle, capleft, top, middlewidth, middle.Height);
	}

	Blit_At(rgba, width, height, left, 0, top);
	Blit_At(rgba, width, height, right, width - capright, top);

	return(true);
}


// Reads a whole file from the game's file system.
static bool Read_File(char const * name, std::vector<unsigned char> & bytes)
{
	CCFileClass file(name);
	if (!file.Is_Available() || file.Open(CCFileClass::READ) == 0) {
		return(false);
	}

	int const size = file.Size();
	if (size <= 0) {
		file.Close();
		return(false);
	}

	bytes.resize((std::size_t)size);
	int const read = file.Read(bytes.data(), size);
	file.Close();

	return(read == size);
}


// A palette file is 256 entries of three six-bit components, the VGA form the game's
// palettes are stored in, so each is widened to eight bits here.
static bool Read_Palette(char const * name, unsigned char (&rgb)[256][3])
{
	std::vector<unsigned char> bytes;
	if (!Read_File(name, bytes) || bytes.size() < 768) {
		DebugString("UI: palette '%s' could not be read\n", name);
		return(false);
	}

	for (int index = 0; index < 256; index++) {
		for (int channel = 0; channel < 3; channel++) {
			unsigned int const value = bytes[index * 3 + channel] & 0x3F;
			rgb[index][channel] = (unsigned char)((value << 2) | (value >> 4));
		}
	}

	return(true);
}


/// <summary>
/// Decodes one frame of a shape file into RGBA8 on a canvas of the set's logical size.
/// Index zero is transparent, as it is to every blitter that draws a shape.
/// </summary>
static bool Decode_Shape(char const * shapename, int frame, char const * palettename,
	std::vector<unsigned char> & rgba, int & width, int & height)
{
	unsigned char palette[256][3];
	if (!Read_Palette(palettename, palette)) {
		return(false);
	}

	std::vector<unsigned char> bytes;
	if (!Read_File(shapename, bytes)) {
		DebugString("UI: shape '%s' could not be read\n", shapename);
		return(false);
	}

	// A shape file is an image of the ShapeSet it describes, so it is read in place, the way
	// the engine reads one it has loaded.
	ShapeSet const * set = (ShapeSet const *)bytes.data();

	if (frame < 0 || frame >= set->Get_Count()) {
		DebugString("UI: shape '%s' has no frame %d\n", shapename, frame);
		return(false);
	}

	width = set->Get_Width();
	height = set->Get_Height();
	if (width <= 0 || height <= 0) {
		return(false);
	}

	rgba.assign((std::size_t)width * height * 4, 0);

	unsigned char const * data = (unsigned char const *)set->Get_Data(frame);
	if (data == nullptr) {
		// An empty frame is a transparent canvas rather than a failure.
		return(true);
	}

	Rect const area = set->Get_Rect(frame);
	bool const rle = set->Is_RLE_Compressed(frame);

	auto put = [&](int x, int y, unsigned char index) {
		int const tx = area.X + x;
		int const ty = area.Y + y;
		if (index == 0 || tx < 0 || ty < 0 || tx >= width || ty >= height) {
			return;
		}

		unsigned char * pixel = &rgba[((std::size_t)ty * width + tx) * 4];
		pixel[0] = palette[index][0];
		pixel[1] = palette[index][1];
		pixel[2] = palette[index][2];
		pixel[3] = 0xFF;
	};

	for (int y = 0; y < area.Height; y++) {
		if (rle) {
			// Each row is prefixed by its own length in bytes, that length included, and a
			// zero is followed by a count of transparent pixels to skip.
			unsigned short const rowlength = (unsigned short)(data[0] | (data[1] << 8));
			unsigned char const * cursor = data + 2;
			int x = 0;

			while (x < area.Width && cursor < data + rowlength) {
				unsigned char const value = *cursor++;
				if (value == 0) {
					x += *cursor++;
				} else {
					put(x, y, value);
					x++;
				}
			}

			data += rowlength;
		} else {
			for (int x = 0; x < area.Width; x++) {
				put(x, y, data[x]);
			}

			data += area.Width;
		}
	}

	return(true);
}


// Reads a 16 bit surface into RGBA8. The engine's surfaces are RGB565, the layout
// dsurface.cpp declares, and they are opaque.
static bool Read_Surface(Surface & surface, std::vector<unsigned char> & rgba, int & width, int & height)
{
	if (surface.Bytes_Per_Pixel() != 2) {
		return(false);
	}

	width = surface.Get_Width();
	height = surface.Get_Height();
	if (width <= 0 || height <= 0) {
		return(false);
	}

	unsigned char const * pixels = (unsigned char const *)surface.Lock();
	if (pixels == nullptr) {
		return(false);
	}

	int const stride = surface.Stride();
	rgba.resize((std::size_t)width * height * 4);

	for (int y = 0; y < height; y++) {
		unsigned short const * row = (unsigned short const *)(pixels + (std::size_t)y * stride);
		unsigned char * out = rgba.data() + (std::size_t)y * width * 4;

		for (int x = 0; x < width; x++) {
			unsigned int const pixel = row[x];
			unsigned int const red = (pixel >> 11) & 0x1F;
			unsigned int const green = (pixel >> 5) & 0x3F;
			unsigned int const blue = pixel & 0x1F;

			// Widened by repeating the top bits, so white stays white rather than 248.
			out[x * 4 + 0] = (unsigned char)((red << 3) | (red >> 2));
			out[x * 4 + 1] = (unsigned char)((green << 2) | (green >> 4));
			out[x * 4 + 2] = (unsigned char)((blue << 3) | (blue >> 2));
			out[x * 4 + 3] = 0xFF;
		}
	}

	surface.Unlock();
	return(true);
}


bool UI_Texture_Load(char const * source, std::vector<unsigned char> & rgba, int & width, int & height)
{
	if (source == nullptr) {
		return(false);
	}

	// name.shp#frame@palette.pal
	char const * hash = std::strchr(source, '#');
	char const * at = std::strchr(source, '@');

	if (hash != nullptr && at != nullptr && at > hash) {
		std::string const shapename(source, (std::size_t)(hash - source));
		std::string const palettename(at + 1);
		int const frame = std::atoi(hash + 1);

		return(Decode_Shape(shapename.c_str(), frame, palettename.c_str(), rgba, width, height));
	}

	if (std::strncmp(source, "surface:", 8) == 0) {
		auto found = _Surfaces.find(source + 8);
		if (found == _Surfaces.end() || found->second == nullptr) {
			return(false);
		}

		Surface * surface = found->second();
		if (surface == nullptr) {
			return(false);
		}

		return(Read_Surface(*surface, rgba, width, height));
	}

	if (std::strncmp(source, "frame:", 6) == 0) {
		int framewidth = 0;
		int frameheight = 0;
		int framex = 0;
		int framey = 0;

		int const fields = std::sscanf(source + 6, "%dx%d@%d,%d", &framewidth, &frameheight, &framex, &framey);
		if (fields != 2 && fields != 4) {
			DebugString("UI: '%s' does not name a frame size\n", source);
			return(false);
		}

		if (!Compose_Frame(framewidth, frameheight, fields == 4, framex, framey, rgba)) {
			return(false);
		}

		width = framewidth;
		height = frameheight;
		return(true);
	}

	if (std::strncmp(source, "button:", 7) == 0) {
		char state = 'u';
		int buttonwidth = 0;
		int buttonheight = 0;

		if (std::sscanf(source + 7, "%c:%dx%d", &state, &buttonwidth, &buttonheight) != 3) {
			DebugString("UI: '%s' does not name a button skin\n", source);
			return(false);
		}

		if (!Compose_Button(state == 'd', buttonwidth, buttonheight, rgba)) {
			return(false);
		}

		width = buttonwidth;
		height = buttonheight;
		return(true);
	}

	// key:name.pcx, the picture with the magenta the dialogs drew it keyed on left out. The
	// key was compared in the display's 16-bit form, so nearby colours drop out too.
	if (std::strncmp(source, "key:", 4) == 0) {
		UIPicture const & picture = Picture(source + 4);
		if (!picture.Is_Valid()) {
			return(false);
		}

		rgba = picture.Pixels;
		for (std::size_t index = 0; index + 3 < rgba.size(); index += 4) {
			if ((rgba[index] >> 3) == 31 && (rgba[index + 1] >> 2) == 0 && (rgba[index + 2] >> 3) == 31) {
				rgba[index] = rgba[index + 1] = rgba[index + 2] = rgba[index + 3] = 0;
			}
		}

		width = picture.Width;
		height = picture.Height;
		return(true);
	}

	std::size_t const length = std::strlen(source);

	if (length > 4 && std::strcmp(source + length - 4, ".pcx") == 0) {
		UIPicture const & picture = Picture(source);
		if (!picture.Is_Valid()) {
			return(false);
		}

		rgba = picture.Pixels;
		width = picture.Width;
		height = picture.Height;
		return(true);
	}

	return(false);
}


void UI_Texture_Shutdown(void)
{
	_Pictures.clear();
}


void UI_Surface_Register(char const * name, UISurfaceSource source)
{
	if (name == nullptr) {
		return;
	}

	_Surfaces[name] = source;
}


void UI_Surface_Invalidate(char const * name)
{
	if (name == nullptr) {
		return;
	}

	Rml::ReleaseTexture(std::string("surface:") + name);
	UI_Mark_Overlay_Dirty();
}
