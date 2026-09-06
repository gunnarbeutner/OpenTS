/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Geometry, the pointer, and the code page for a page. The window manager
// answers geometry for every handle its registry knows and the canvas for the
// rest, null included. Reversed, every control covers the whole frame.

#include "always.h"
#include "substitute.h"

#include "win32window.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "browser.h"
#include "misc.h"
#include "vidscale.h"
#include "video.h"
#include "win.h"
#include "win32user.h"
#include "wincursor.h"


#include <algorithm>
#include <cstring>
#include <string>
#include <vector>


// Windows-1252 is Latin-1 apart from 0x80 to 0x9F. The five undefined positions
// carry the C1 control of the same value, as Windows's table does, so every
// byte round trips.
static unsigned short const _HighRange[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

// Past this a browser shows no pointer at all rather than a clipped one.
static int const MAX_CURSOR_SIZE = 128;

// Windows reserves this block for the cursors, icons and bitmaps it supplies
// itself; nothing else is a system cursor.
static ULONG_PTR const FIRST_SYSTEM_CURSOR = 32512;
static ULONG_PTR const LAST_SYSTEM_CURSOR = 32767;


// The CSS value is the page's cursor and the pixels are every other host's; encoding
// the CSS is the expensive part of changing pointers, so both are built once.
struct Win32CursorClass
{
	std::string Css;
	std::vector<unsigned long> Pixels;
	int Width = 0;
	int Height = 0;
	int HotX = 0;
	int HotY = 0;
	bool IsShared;
};

static std::vector<Win32CursorClass *> _Cursors;
static Win32CursorClass * _Current = nullptr;

// Starts at zero because a page always has a pointing device. Capture_Mouse
// raises it until it is not negative, so it has to count.
static int _DisplayCount = 0;

static HWND _Capture = nullptr;


//------------------------------------------------------------------------------
// Window geometry.
//------------------------------------------------------------------------------


// False until the page has laid the canvas out.
static bool Canvas_Rect(LPRECT rect)
{
	int width = Browser_Canvas_Width();
	int height = Browser_Canvas_Height();

	if (width <= 0 || height <= 0) {
		SetRectEmpty(rect);
		return(false);
	}

	rect->left = 0;
	rect->top = 0;
	rect->right = width;
	rect->bottom = height;
	return(true);
}


// The registry is the only thing that knows where a control is; the canvas
// answers for a handle it has never heard of.
BOOL GetClientRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	if (Win32_User_Client_Rect(window, rect)) {
		return(TRUE);
	}

	return(Canvas_Rect(rect) ? TRUE : FALSE);
}


BOOL GetWindowRect(HWND window, LPRECT rect)
{
	if (rect == nullptr) {
		return(FALSE);
	}

	if (Win32_User_Window_Rect(window, rect)) {
		return(TRUE);
	}

	return(Canvas_Rect(rect) ? TRUE : FALSE);
}


BOOL ClientToScreen(HWND window, LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	POINT origin;

	if (Win32_User_Client_Origin(window, &origin)) {
		point->x += origin.x;
		point->y += origin.y;
	}

	return(TRUE);
}


BOOL ScreenToClient(HWND window, LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	POINT origin;

	if (Win32_User_Client_Origin(window, &origin)) {
		point->x -= origin.x;
		point->y -= origin.y;
	}

	return(TRUE);
}


// A canvas has no frame, and a menu is refused rather than sized.
BOOL AdjustWindowRect(LPRECT rect, DWORD, BOOL menu)
{
	if (rect == nullptr || menu != FALSE) {
		return(FALSE);
	}

	return(TRUE);
}


BOOL AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD)
{
	return(AdjustWindowRect(rect, style, menu));
}


// Window furniture is absent rather than unknown, so zero is its measurement.
int GetSystemMetrics(int index)
{
	switch (index) {
		case SM_CXSCREEN:
		case SM_CXFULLSCREEN:
			return(Browser_Canvas_Width());

		case SM_CYSCREEN:
		case SM_CYFULLSCREEN:
			return(Browser_Canvas_Height());

		case SM_CXVSCROLL:
		case SM_CYHSCROLL:
		case SM_CYCAPTION:
		case SM_CXFIXEDFRAME:
		case SM_CYFIXEDFRAME:
		case SM_CXSIZEFRAME:
		case SM_CYSIZEFRAME:
			return(0);

		case SM_CXBORDER:
		case SM_CYBORDER:
			return(1);

		// Windows makes both configurable; a page has no preference to read, so
		// these are its defaults.
		case SM_CXDRAG:
		case SM_CYDRAG:
			return(4);

		case SM_CXDOUBLECLK:
		case SM_CYDOUBLECLK:
			return(4);

		// A swapped pair reaches the page already swapped.
		case SM_SWAPBUTTON:
			return(0);

		default:
			return(WIN32_UNSUPPORTED("GetSystemMetrics: a measurement with no counterpart on a page", 0));
	}
}


//------------------------------------------------------------------------------
// The pointer.
//------------------------------------------------------------------------------


int Win32_Window_Max_Cursor_Size(void)
{
	return(MAX_CURSOR_SIZE);
}


// The null cursor falls back to the page's pointer: no WM_SETCURSOR and no
// window class would ever put one back, so drawing nothing would leave nothing
// to point with. The blank cursor is how a caller asks for no pointer.
static void Apply_Cursor(void)
{
	static std::string _applied;

	char const * css = (_Current != nullptr) ? _Current->Css.c_str() : "default";

	if (_applied == css) {
		return;
	}

	_applied = css;

	if (_Current != nullptr && !_Current->Pixels.empty()) {
		Browser_Show_Cursor(_Current, css, _Current->Pixels.data(), _Current->Width, _Current->Height, _Current->HotX, _Current->HotY);
	} else {
		Browser_Show_Cursor(_Current, css, nullptr, 0, 0, 0, 0);
	}
}


static Win32CursorClass * Find_Cursor(HCURSOR cursor)
{
	if (cursor == nullptr) {
		return(nullptr);
	}

	for (unsigned index = 0; index < _Cursors.size(); index++) {
		if ((HCURSOR)_Cursors[index] == cursor) {
			return(_Cursors[index]);
		}
	}

	return(nullptr);
}


static void Append_Big_Endian(std::vector<unsigned char> & out, unsigned long value)
{
	out.push_back((unsigned char)((value >> 24) & 0xFF));
	out.push_back((unsigned char)((value >> 16) & 0xFF));
	out.push_back((unsigned char)((value >> 8) & 0xFF));
	out.push_back((unsigned char)(value & 0xFF));
}


static unsigned long Checksum_32(unsigned char const * data, std::size_t length)
{
	static unsigned long _table[256];
	static bool _built = false;

	if (!_built) {
		for (unsigned long index = 0; index < 256; index++) {
			unsigned long value = index;
			for (int bit = 0; bit < 8; bit++) {
				value = (value & 1) ? (0xEDB88320UL ^ (value >> 1)) : (value >> 1);
			}
			_table[index] = value;
		}
		_built = true;
	}

	unsigned long crc = 0xFFFFFFFFUL;
	for (std::size_t index = 0; index < length; index++) {
		crc = _table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	}
	return(crc ^ 0xFFFFFFFFUL);
}


static void Append_Chunk(std::vector<unsigned char> & out, char const * type, std::vector<unsigned char> const & body)
{
	Append_Big_Endian(out, (unsigned long)body.size());

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

	unsigned long a = 1;
	unsigned long b = 0;
	for (std::size_t index = 0; index < raw.size(); index++) {
		a = (a + raw[index]) % 65521;
		b = (b + a) % 65521;
	}
	Append_Big_Endian(out, (b << 16) | a);

	return(out);
}


// A page takes a cursor image as a URL, and PNG is the format that carries
// alpha everywhere.
static std::vector<unsigned char> Encode_PNG(unsigned long const * pixels, int width, int height)
{
	std::vector<unsigned char> raw;
	raw.reserve((std::size_t)height * (1 + (std::size_t)width * 4));

	for (int y = 0; y < height; y++) {

		// Each row carries its filter byte, and these rows are not filtered.
		raw.push_back(0);

		for (int x = 0; x < width; x++) {
			unsigned long pixel = pixels[(std::size_t)y * width + x];
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
	Append_Big_Endian(header, (unsigned long)width);
	Append_Big_Endian(header, (unsigned long)height);
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

		unsigned long group = (unsigned long)data[index] << 16;
		if (remaining > 1) group |= (unsigned long)data[index + 1] << 8;
		if (remaining > 2) group |= (unsigned long)data[index + 2];

		out.push_back(_alphabet[(group >> 18) & 0x3F]);
		out.push_back(_alphabet[(group >> 12) & 0x3F]);
		out.push_back(remaining > 1 ? _alphabet[(group >> 6) & 0x3F] : '=');
		out.push_back(remaining > 2 ? _alphabet[group & 0x3F] : '=');
	}

	return(out);
}


HCURSOR Win32_Window_Create_Cursor(unsigned long const * pixels, int width, int height, int hotx, int hoty)
{
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return(nullptr);
	}

	if (width > MAX_CURSOR_SIZE || height > MAX_CURSOR_SIZE) {
		return(WIN32_UNSUPPORTED("Win32_Window_Create_Cursor: an image past the size a browser will draw", (HCURSOR)nullptr));
	}

	if (hotx < 0) hotx = 0;
	if (hoty < 0) hoty = 0;
	if (hotx >= width) hotx = width - 1;
	if (hoty >= height) hoty = height - 1;

	Win32CursorClass * cursor = new Win32CursorClass;

	// A page that cannot decode the image falls back to the keyword, so the
	// pointer degrades to an arrow rather than disappearing.
	cursor->Css = "url(\"data:image/png;base64," + Base64(Encode_PNG(pixels, width, height)) + "\") "
		+ std::to_string(hotx) + " " + std::to_string(hoty) + ", auto";
	cursor->Pixels.assign(pixels, pixels + (size_t)width * (size_t)height);
	cursor->Width = width;
	cursor->Height = height;
	cursor->HotX = hotx;
	cursor->HotY = hoty;
	cursor->IsShared = false;

	_Cursors.push_back(cursor);
	return((HCURSOR)cursor);
}


HCURSOR LoadCursorA(HINSTANCE instance, LPCSTR name)
{
	static Win32CursorClass * _shared[4];

	struct {
		LPCSTR Name;
		char const * Css;
	} const _standard[4] = {
		{ IDC_ARROW, "default" },
		{ IDC_WAIT, "wait" },
		{ IDC_NO, "not-allowed" },
		{ IDC_HAND, "pointer" }
	};

	if (instance != nullptr) {
		return(WIN32_UNSUPPORTED("LoadCursorA: a cursor resource held in a module", (HCURSOR)nullptr));
	}

	for (int index = 0; index < 4; index++) {
		if (_standard[index].Name != name) {
			continue;
		}

		if (_shared[index] == nullptr) {
			_shared[index] = new Win32CursorClass;
			_shared[index]->Css = _standard[index].Css;
			_shared[index]->IsShared = true;
			_Cursors.push_back(_shared[index]);
		}

		return((HCURSOR)_shared[index]);
	}

	// A null instance reaches only the block Windows reserves for its own
	// resources; a name outside it asks a module, and NULL is what Windows
	// answers with none. The engine's own cursor arrives this way and is drawn
	// onto the surface rather than through the pointer.
	if ((ULONG_PTR)name < FIRST_SYSTEM_CURSOR || (ULONG_PTR)name > LAST_SYSTEM_CURSOR) {
		return(nullptr);
	}

	return(WIN32_UNSUPPORTED("LoadCursorA: a system cursor with no counterpart on a page", (HCURSOR)nullptr));
}


// Windows has no such cursor because the null one means this and the class
// cursor ends it; neither holds on a page.
HCURSOR Win32_Window_Blank_Cursor(void)
{
	static Win32CursorClass * _blank = nullptr;

	if (_blank == nullptr) {
		_blank = new Win32CursorClass;
		_blank->Css = "none";
		_blank->IsShared = true;
		_Cursors.push_back(_blank);
	}

	return((HCURSOR)_blank);
}


HCURSOR SetCursor(HCURSOR cursor)
{
	Win32CursorClass * previous = _Current;

	if (cursor != nullptr) {
		_Current = Find_Cursor(cursor);
		if (_Current == nullptr) {
			Win32_Unsupported_Reached("SetCursor: a cursor this port did not build");
		}
	} else {
		_Current = nullptr;
	}

	Apply_Cursor();
	return((HCURSOR)previous);
}


// A page raises no WM_SETCURSOR and has no class cursor, so the game's own
// cursor is put back here.
int ShowCursor(BOOL show)
{
	_DisplayCount += (show != FALSE) ? 1 : -1;

	Win_Cursor_Apply();
	return(_DisplayCount);
}


BOOL DestroyCursor(HCURSOR cursor)
{
	for (unsigned index = 0; index < _Cursors.size(); index++) {
		if ((HCURSOR)_Cursors[index] != cursor) {
			continue;
		}

		// The shared ones outlive every caller, as on Windows.
		if (_Cursors[index]->IsShared) {
			return(FALSE);
		}

		if (_Current == _Cursors[index]) {
			_Current = nullptr;
			Apply_Cursor();
		}

		delete _Cursors[index];
		_Cursors.erase(_Cursors.begin() + index);
		return(TRUE);
	}

	return(FALSE);
}


// The page reports the pointer in frame pixels; the screen is the canvas, so
// the position goes out through the scaling.
BOOL GetCursorPos(LPPOINT point)
{
	if (point == nullptr) {
		return(FALSE);
	}

	point->x = Browser_Mouse_X();
	point->y = Browser_Mouse_Y();
	Game_Point_To_Window(*point);
	return(TRUE);
}


BOOL SetCursorPos(int, int) { return(WIN32_STUB(FALSE)); }


// A page delivers every canvas mouse event to the canvas, so capture is
// bookkeeping the engine reads back rather than a routing change.
HWND SetCapture(HWND window)
{
	HWND previous = _Capture;
	_Capture = window;
	return(previous);
}


BOOL ReleaseCapture(void)
{
	_Capture = nullptr;
	return(TRUE);
}


HWND GetCapture(void)
{
	return(_Capture);
}


// The page cannot confine a pointer, but every position the engine reads is
// already pulled onto the frame, so only a smaller rectangle is a confinement
// nothing provides.
BOOL ClipCursor(RECT const * rect)
{
	if (rect == nullptr) {
		return(TRUE);
	}

	RECT canvas;
	if (!Canvas_Rect(&canvas)) {
		return(FALSE);
	}

	if (rect->left > canvas.left || rect->top > canvas.top
	||	rect->right < canvas.right || rect->bottom < canvas.bottom) {
		return(WIN32_UNSUPPORTED("ClipCursor: a rectangle smaller than the canvas", FALSE));
	}

	return(TRUE);
}


//------------------------------------------------------------------------------
// The display.
//------------------------------------------------------------------------------


HMONITOR MonitorFromWindow(HWND, DWORD) { return(WIN32_STUB((HMONITOR)nullptr)); }
BOOL GetMonitorInfoA(HMONITOR, LPMONITORINFO) { return(WIN32_STUB(FALSE)); }


// A page has no display mode list. What stands in is the set of frame sizes the
// renderer can produce here, in CSS pixels, none larger than the display.
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


BOOL EnumDisplaySettingsA(LPCSTR, DWORD mode, LPDEVMODEA devmode)
{
	if (devmode == nullptr) return(FALSE);

	int width = 0;
	int height = 0;

	if (mode == ENUM_CURRENT_SETTINGS) {
		width = Browser_Canvas_CSS_Width();
		height = Browser_Canvas_CSS_Height();
		if (width <= 0 || height <= 0) return(FALSE);
	} else {
		// Taken afresh at the start of an enumeration, as a driver's is, so a
		// canvas that resizes part way through does not shorten it.
		if (mode == 0) {
			Build_Display_Modes();
		}
		if ((int)mode >= _DisplayModeCount) return(FALSE);
		width = _DisplayModes[mode].Width;
		height = _DisplayModes[mode].Height;
	}

	memset(devmode, 0, sizeof(*devmode));
	devmode->dmSize = sizeof(*devmode);
	devmode->dmBitsPerPel = 16;
	devmode->dmPelsWidth = (DWORD)width;
	devmode->dmPelsHeight = (DWORD)height;
	devmode->dmDisplayFrequency = 60;
	return(TRUE);
}

//------------------------------------------------------------------------------
// The code page.
//------------------------------------------------------------------------------


// CP_ACP is Windows-1252 on the systems the engine was written for.
static bool Is_Windows_1252(UINT codepage)
{
	return(codepage == CP_ACP || codepage == 1252);
}


static unsigned short Widen_Character(unsigned char byte)
{
	if (byte < 0x80 || byte >= 0xA0) {
		return(byte);
	}

	return(_HighRange[byte - 0x80]);
}


// Windows also has a best fit table that answers a near miss with a
// resemblance; this has none, so anything outside the code page is replaced.
static int Narrow_Character(unsigned short code)
{
	if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
		return((int)code);
	}

	for (int index = 0; index < 32; index++) {
		if (_HighRange[index] == code) {
			return(0x80 + index);
		}
	}

	return(-1);
}


int MultiByteToWideChar(UINT codepage, DWORD flags, LPCSTR multibyte, int multibytecount, LPWSTR wide, int widecount)
{
	if (multibyte == nullptr || multibytecount == 0 || widecount < 0 || (widecount > 0 && wide == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a code page other than Windows-1252", 0));
	}

	// MB_PRECOMPOSED is the default and asks for what this produces anyway.
	if ((flags & ~(DWORD)MB_PRECOMPOSED) != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("MultiByteToWideChar: a conversion flag with no implementation", 0));
	}

	int length = multibytecount;
	if (length < 0) {
		length = (int)strlen(multibyte) + 1;
	}

	if (widecount == 0) {
		return(length);
	}

	if (widecount < length) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	for (int index = 0; index < length; index++) {
		wide[index] = (WCHAR)Widen_Character((unsigned char)multibyte[index]);
	}

	return(length);
}


int WideCharToMultiByte(UINT codepage, DWORD flags, LPCWSTR wide, int widecount, LPSTR multibyte, int multibytecount, LPCSTR defaultchar, LPBOOL useddefaultchar)
{
	if (wide == nullptr || widecount == 0 || multibytecount < 0 || (multibytecount > 0 && multibyte == nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	if (!Is_Windows_1252(codepage)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a code page other than Windows-1252", 0));
	}

	if (flags != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WideCharToMultiByte: a conversion flag with no implementation", 0));
	}

	int length = widecount;
	if (length < 0) {
		length = 0;
		while (wide[length] != 0) length++;
		length++;
	}

	char replacement = (defaultchar != nullptr) ? defaultchar[0] : '?';
	bool replaced = false;

	std::string out;
	out.reserve((std::size_t)length);

	for (int index = 0; index < length; index++) {
		unsigned short code = (unsigned short)wide[index];

		// A surrogate pair is one character, so it is consumed together and
		// replaced once.
		if (code >= 0xD800 && code <= 0xDBFF && (index + 1) < length) {
			unsigned short low = (unsigned short)wide[index + 1];
			if (low >= 0xDC00 && low <= 0xDFFF) {
				index++;
				out.push_back(replacement);
				replaced = true;
				continue;
			}
		}

		int byte = Narrow_Character(code);
		if (byte < 0) {
			out.push_back(replacement);
			replaced = true;
		} else {
			out.push_back((char)byte);
		}
	}

	if (useddefaultchar != nullptr) {
		*useddefaultchar = replaced ? TRUE : FALSE;
	}

	if (multibytecount == 0) {
		return((int)out.size());
	}

	if ((std::size_t)multibytecount < out.size()) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	memcpy(multibyte, out.data(), out.size());
	return((int)out.size());
}

#endif	// OPENTS_WIN32_SUBSTITUTE
