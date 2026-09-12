/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The game window as a host answering code/browser.h provides it, the page among them.

#if defined(__EMSCRIPTEN__)

#include "always.h"

#include "hostwindow.h"

#include "browser.h"
#include "gamewindow.h"
#include "globals.h"
#include "misc.h"
#include "nativewindow.hh"
#include "phase.h"
#include "video.h"
#include "vidscale.h"
#include "wincursor.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>


// Past this a browser shows no pointer at all rather than a clipped one.
static int const MAX_CURSOR_SIZE = 128;


struct HostCursor
{
	std::string Css;
};


// Starts at zero because a page always has a pointing device. Capture_Mouse raises it until
// it is not negative, so it has to count.
static int _DisplayCount = 0;

static bool _Captured = false;


// The page reports the pointer in frame pixels, so the position goes out through the
// scaling to come back in client pixels.
Point2D Host_Pointer_Position(void)
{
	Point2D point(Browser_Mouse_X(), Browser_Mouse_Y());
	Game_Point_To_Window(point);
	return(point);
}


// A page cannot move the pointer.
void Host_Move_Pointer(Point2D const &)
{
}


// A page raises no set-cursor request and has no class cursor, so the game's own image is
// put back here.
int Host_Show_Pointer(bool show)
{
	_DisplayCount += show ? 1 : -1;
	Win_Cursor_Apply();
	return(_DisplayCount);
}


// Every position the engine reads is already pulled onto the frame.
void Host_Confine_Pointer(bool)
{
}


// A page delivers every canvas mouse event to the canvas, so capture is bookkeeping the
// engine reads back rather than a routing change.
void Host_Capture_Pointer(void)
{
	_Captured = true;
}


void Host_Release_Pointer(void)
{
	_Captured = false;
}


bool Host_Pointer_Is_Captured(void)
{
	return(_Captured);
}


// Windows makes this configurable; a page has no preference to read, so this is its default.
Point2D Host_Drag_Threshold(void)
{
	return(Point2D(4, 4));
}


static void Append_Big_Endian(std::vector<unsigned char> & out, std::uint32_t value)
{
	out.push_back((unsigned char)((value >> 24) & 0xFF));
	out.push_back((unsigned char)((value >> 16) & 0xFF));
	out.push_back((unsigned char)((value >> 8) & 0xFF));
	out.push_back((unsigned char)(value & 0xFF));
}


static std::uint32_t Checksum_32(unsigned char const * data, std::size_t length)
{
	static std::uint32_t _table[256];
	static bool _built = false;

	if (!_built) {
		for (std::uint32_t index = 0; index < 256; index++) {
			std::uint32_t value = index;
			for (int bit = 0; bit < 8; bit++) {
				value = (value & 1) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
			}
			_table[index] = value;
		}
		_built = true;
	}

	std::uint32_t crc = 0xFFFFFFFFU;
	for (std::size_t index = 0; index < length; index++) {
		crc = _table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	}
	return(crc ^ 0xFFFFFFFFU);
}


static void Append_Chunk(std::vector<unsigned char> & out, char const * type, std::vector<unsigned char> const & body)
{
	Append_Big_Endian(out, (std::uint32_t)body.size());

	std::vector<unsigned char> checked;
	checked.insert(checked.end(), type, type + 4);
	checked.insert(checked.end(), body.begin(), body.end());

	out.insert(out.end(), checked.begin(), checked.end());
	Append_Big_Endian(out, Checksum_32(checked.data(), checked.size()));
}


// A stored block; a cursor is too small to be worth a compressor.
static std::vector<unsigned char> Store_Deflate(std::vector<unsigned char> const & raw)
{
	std::vector<unsigned char> out;

	out.push_back(0x78);
	out.push_back(0x01);

	std::size_t offset = 0;

	do {
		std::size_t remaining = raw.size() - offset;
		unsigned int length = (remaining > 0xFFFF) ? 0xFFFF : (unsigned int)remaining;
		bool last = ((offset + length) == raw.size());

		out.push_back(last ? 0x01 : 0x00);
		out.push_back((unsigned char)(length & 0xFF));
		out.push_back((unsigned char)((length >> 8) & 0xFF));
		out.push_back((unsigned char)(~length & 0xFF));
		out.push_back((unsigned char)((~length >> 8) & 0xFF));
		out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + length);

		offset += length;
	}
	while (offset < raw.size());

	std::uint32_t a = 1;
	std::uint32_t b = 0;
	for (std::size_t index = 0; index < raw.size(); index++) {
		a = (a + raw[index]) % 65521;
		b = (b + a) % 65521;
	}
	Append_Big_Endian(out, (b << 16) | a);

	return(out);
}


// A page takes a cursor image as a URL, and PNG is the format that carries alpha everywhere.
static std::vector<unsigned char> Encode_PNG(std::uint32_t const * pixels, int width, int height)
{
	std::vector<unsigned char> raw;
	raw.reserve((std::size_t)height * (1 + (std::size_t)width * 4));

	for (int y = 0; y < height; y++) {

		// Each row carries its filter byte, and these rows are not filtered.
		raw.push_back(0);

		for (int x = 0; x < width; x++) {
			std::uint32_t pixel = pixels[(std::size_t)y * width + x];
			raw.push_back((unsigned char)((pixel >> 16) & 0xFF));
			raw.push_back((unsigned char)((pixel >> 8) & 0xFF));
			raw.push_back((unsigned char)(pixel & 0xFF));
			raw.push_back((unsigned char)((pixel >> 24) & 0xFF));
		}
	}

	std::vector<unsigned char> out;
	unsigned char const signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	out.insert(out.end(), signature, signature + sizeof(signature));

	std::vector<unsigned char> header;
	Append_Big_Endian(header, (std::uint32_t)width);
	Append_Big_Endian(header, (std::uint32_t)height);
	header.push_back(8);		// Bits per channel.
	header.push_back(6);		// Red, green, blue, and alpha.
	header.push_back(0);		// The only compression the format has.
	header.push_back(0);		// The only filtering the format has.
	header.push_back(0);		// Not interlaced.
	Append_Chunk(out, "IHDR", header);

	Append_Chunk(out, "IDAT", Store_Deflate(raw));
	Append_Chunk(out, "IEND", std::vector<unsigned char>());

	return(out);
}


static std::string Base64(std::vector<unsigned char> const & data)
{
	static char const * const _alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve(((data.size() + 2) / 3) * 4);

	for (std::size_t index = 0; index < data.size(); index += 3) {
		std::size_t remaining = data.size() - index;

		std::uint32_t group = (std::uint32_t)data[index] << 16;
		if (remaining > 1) group |= (std::uint32_t)data[index + 1] << 8;
		if (remaining > 2) group |= (std::uint32_t)data[index + 2];

		out.push_back(_alphabet[(group >> 18) & 0x3F]);
		out.push_back(_alphabet[(group >> 12) & 0x3F]);
		out.push_back(remaining > 1 ? _alphabet[(group >> 6) & 0x3F] : '=');
		out.push_back(remaining > 2 ? _alphabet[group & 0x3F] : '=');
	}

	return(out);
}


HostCursor * Host_Create_Cursor(std::uint32_t const * pixels, int width, int height, int hotx, int hoty)
{
	if (pixels == nullptr || width <= 0 || height <= 0 || width > MAX_CURSOR_SIZE || height > MAX_CURSOR_SIZE) {
		return(nullptr);
	}

	// A page that cannot decode the image falls back to the keyword, so the pointer degrades
	// to an arrow rather than disappearing.
	HostCursor * cursor = new HostCursor;
	cursor->Css = "url(\"data:image/png;base64," + Base64(Encode_PNG(pixels, width, height)) + "\") "
		+ std::to_string(hotx) + " " + std::to_string(hoty) + ", auto";
	return(cursor);
}


static std::string _Shown;


static void Show_Css(std::string const & css)
{
	if (css != _Shown) {
		_Shown = css;
		Browser_Show_Cursor(css.c_str());
	}
}


void Host_Destroy_Cursor(HostCursor * cursor)
{
	if (cursor != nullptr && cursor->Css == _Shown) {
		Show_Css("default");
	}
	delete cursor;
}


void Host_Set_Cursor(HostCursor * cursor)
{
	Show_Css(cursor != nullptr ? cursor->Css : "default");
}


void Host_Hide_Cursor(void)
{
	Show_Css("none");
}


int Host_Max_Cursor_Size(void)
{
	return(MAX_CURSOR_SIZE);
}


bool Host_Has_Class_Cursor(void)
{
	return(false);
}

unsigned short Host_Key_Modifiers(void)
{
	return(Browser_Key_Modifiers());
}


// Buttons are never swapped here: the browser applies the desktop's setting before the event
// names a button.
bool Host_Key_Is_Down(unsigned short key)
{
	return(Browser_Key_Is_Down(key & 0xFF));
}


// Only the browser knows the layout, so this reads back the character it reported as the key
// went down.
int Host_Key_To_Character(unsigned short key)
{
	return((int)Browser_Key_To_Character(key));
}

static bool _WindowOpen = false;
static bool _PaintPending = false;


// The canvas is the window and is already there, so opening it is bookkeeping.
void Host_Create_Window(int, int)
{
	_WindowOpen = true;
	Game_Window_Created();
}


void Host_Close_Window(void)
{
	if (_WindowOpen) {
		_WindowOpen = false;
		Game_Window_Destroyed();
	}
}


bool Has_Main_Window(void)
{
	return(_WindowOpen);
}


NativeWindow Host_Native_Window(void)
{
	return(Browser_Native_Window());
}


bool Host_Window_Drawable_Size(int & width, int & height)
{
	width = Browser_Canvas_Width();
	height = Browser_Canvas_Height();
	return(width > 0 && height > 0);
}


// A page does not say how often its display refreshes.
int Host_Window_Refresh_Rate(void)
{
	return(0);
}


// The paint is made from the pump, where a window system would deliver it.
void Host_Invalidate_Window(void)
{
	_PaintPending = true;
}


bool Host_Take_Paint(void)
{
	bool const pending = _PaintPending;
	_PaintPending = false;
	return(pending && _WindowOpen);
}


void Host_Focus_Window(void)
{
}


// The canvas is sized by the page.
void Host_Fit_Window_To_Frame(int, int)
{
}


// confirm() stops the page and a browser may suppress it, so the box is laid out in the page
// and the wait is the engine's yield. Without the yield scaffold the question is logged and
// the box reported dismissed.
HostMessageBoxAnswer Host_Message_Box(char const * caption, char const * text, unsigned int style)
{
	char const * body = (text != nullptr) ? text : "";
	char const * title = (caption != nullptr) ? caption : "OpenTS";
	int const buttons = (int)(style & 0x0F);
	HostMessageBoxAnswer const dismissed = (buttons == HOST_BOX_YES_NO) ? HOST_ANSWER_NO : HOST_ANSWER_OK;

	fprintf(stderr, "OpenTS message box [%s]: %s\n", title, body);
	fflush(stderr);

	if (!Browser_Yield_Is_Available()) {
		return(dismissed);
	}

	PhaseScope phase("alert", body);

	int const answer = Browser_Message_Box(title, body, buttons);
	return(answer > 0 ? (HostMessageBoxAnswer)answer : dismissed);
}

// A page has no display mode list. What stands in is the set of frame sizes the renderer can
// produce here, in CSS pixels, none larger than the display.
struct DisplayModeEntry
{
	int Width;
	int Height;
};

static const DisplayModeEntry _DisplayLadder[] = {
	{ 640, 400 },	{ 640, 480 },	{ 800, 600 },	{ 1024, 768 },	{ 1152, 864 },
	{ 1280, 720 },	{ 1280, 800 },	{ 1280, 960 },	{ 1280, 1024 },	{ 1366, 768 },
	{ 1440, 900 },	{ 1600, 900 },	{ 1600, 1200 },	{ 1680, 1050 },	{ 1920, 1080 },
	{ 1920, 1200 },	{ 2048, 1152 },	{ 2560, 1440 },	{ 2560, 1600 },
};

static DisplayModeEntry _DisplayModes[32];
static int _DisplayModeCount = 0;


static void Add_Display_Mode(int width, int height)
{
	if (width < 640 || height < 400) return;
	if (_DisplayModeCount >= (int)(sizeof(_DisplayModes) / sizeof(_DisplayModes[0]))) return;

	for (int index = 0; index < _DisplayModeCount; index++) {
		if (_DisplayModes[index].Width == width && _DisplayModes[index].Height == height) return;
	}

	_DisplayModes[_DisplayModeCount].Width = width;
	_DisplayModes[_DisplayModeCount].Height = height;
	_DisplayModeCount++;
}


static void Build_Display_Modes(void)
{
	_DisplayModeCount = 0;

	int screenwidth = Browser_Screen_Width();
	int screenheight = Browser_Screen_Height();

	// A page that will not say how big the display is gets a laptop's sizes
	// rather than the whole ladder.
	if (screenwidth <= 0 || screenheight <= 0) {
		screenwidth = 1920;
		screenheight = 1080;
	}

	for (unsigned index = 0; index < sizeof(_DisplayLadder) / sizeof(_DisplayLadder[0]); index++) {
		if (_DisplayLadder[index].Width <= screenwidth && _DisplayLadder[index].Height <= screenheight) {
			Add_Display_Mode(_DisplayLadder[index].Width, _DisplayLadder[index].Height);
		}
	}

	// The window is named as the size it produces, not the size it was measured
	// at, since that is the frame a caller asking for it would get.
	int canvaswidth = Browser_Canvas_CSS_Width();
	int canvasheight = Browser_Canvas_CSS_Height();

	if (canvaswidth > 0 && canvasheight > 0) {
		Video_Clamp_Frame_Size(canvaswidth, canvasheight);
		Add_Display_Mode(canvaswidth, canvasheight);
	}

	// The current resolution belongs on the list the player is reading.
	Add_Display_Mode(VideoModeWidth, VideoModeHeight);

	std::sort(_DisplayModes, _DisplayModes + _DisplayModeCount,
		[](DisplayModeEntry const & lhs, DisplayModeEntry const & rhs) {
			return((lhs.Width != rhs.Width) ? (lhs.Width < rhs.Width) : (lhs.Height < rhs.Height));
		});
}


// The list is taken afresh at the start of an enumeration, as a driver's is, so a canvas that
// resizes part way through does not shorten it.
bool Host_Display_Mode(int index, int & width, int & height)
{
	if (index == 0) {
		Build_Display_Modes();
	}
	if (index < 0 || index >= _DisplayModeCount) {
		return(false);
	}

	width = _DisplayModes[index].Width;
	height = _DisplayModes[index].Height;
	return(true);
}

#endif	// !_WIN32
