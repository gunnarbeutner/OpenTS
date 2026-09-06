/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The GDI subset the front end asks for: string widths, line heights, and a
// context to hang a font and a color on. One typeface serves every face, so
// measuring and drawing agree; a window context only measures, and the mapping
// is the identity.

#include "always.h"
#include "substitute.h"

#include "win32gdi.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "ownrdraw.h"
#include "surface.h"

#include <cstring>
#include <string>
#include <vector>


using namespace OwnerDraw;

// Defined in ownrdraw.cpp and not exported; declared the way it declares them.
bool ODGetFontMetrics(char const * font_name, FontMetrics * metrics);
void ODDrawCharRemap(Surface & dst_surf, char const * text, int max_chars, Rect const & rect,
	char const * font_name, COLORREF color, char flags, int char_spacing);
int ODColorToHiColor(COLORREF color);


// ownrdraw.cpp names this sheet everywhere it paints dialog text.
static char const * const REMAP_FONT = "dlgsys";


enum GdiObjectType
{
	GDI_OBJECT_FONT,
	GDI_OBJECT_BRUSH
};


struct GdiObject
{
	GdiObjectType Type;
	bool Stock;

	// A font. The face and the height are what the caller reads back; the
	// metrics come from the remap sheet.
	std::string Face;
	LONG Height;
	LONG Weight;
	bool Italic;

	// A brush.
	COLORREF Color;
};


// Only the state callers set and read back.
struct DeviceContextState
{
	GdiObject * Font;
	GdiObject * Brush;
	COLORREF TextColor;
	COLORREF BackColor;
	int BackMode;
	UINT TextAlign;
};


struct DeviceContext
{
	// Where TextOut lands, or NULL for a context that only measures.
	Surface * Target;

	DeviceContextState State;
	std::vector<DeviceContextState> Saved;
};


static std::vector<GdiObject *> _Objects;
static std::vector<DeviceContext *> _Contexts;


static GdiObject * Object_Of(HGDIOBJ handle)
{
	if (handle == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < _Objects.size(); index++) {
		if (_Objects[index] == (GdiObject *)handle) {
			return(_Objects[index]);
		}
	}

	return(nullptr);
}


static DeviceContext * Context_Of(HDC handle)
{
	if (handle == nullptr) {
		return(nullptr);
	}

	for (unsigned int index = 0; index < _Contexts.size(); index++) {
		if (_Contexts[index] == (DeviceContext *)handle) {
			return(_Contexts[index]);
		}
	}

	return(nullptr);
}


static GdiObject * New_Object(GdiObjectType type)
{
	GdiObject * object = new GdiObject;
	object->Type = type;
	object->Stock = false;
	object->Height = 0;
	object->Weight = FW_NORMAL;
	object->Italic = false;
	object->Color = 0;

	_Objects.push_back(object);
	return(object);
}


static HDC New_Context(Surface * target)
{
	DeviceContext * context = new DeviceContext;
	context->Target = target;
	context->State.Font = nullptr;
	context->State.Brush = nullptr;
	context->State.TextColor = RGB(0, 0, 0);
	context->State.BackColor = RGB(255, 255, 255);
	context->State.BackMode = OPAQUE;
	context->State.TextAlign = TA_LEFT | TA_TOP;

	_Contexts.push_back(context);
	return((HDC)context);
}


static BOOL Destroy_Context(HDC handle)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return(FALSE);
	}

	for (unsigned int index = 0; index < _Contexts.size(); index++) {
		if (_Contexts[index] == context) {
			_Contexts.erase(_Contexts.begin() + index);
			break;
		}
	}

	delete context;
	return(TRUE);
}


//------------------------------------------------------------------------------
// Objects.
//------------------------------------------------------------------------------


HGDIOBJ GetStockObject(int object)
{
	// A stock object outlives every context that selects it; none is deleted.
	static GdiObject * _systemfont = nullptr;
	static GdiObject * _nullbrush = nullptr;
	static GdiObject * _blackbrush = nullptr;
	static GdiObject * _whitebrush = nullptr;

	switch (object) {
		case SYSTEM_FONT:
		case ANSI_VAR_FONT:
		case DEFAULT_GUI_FONT:
			if (_systemfont == nullptr) {
				_systemfont = New_Object(GDI_OBJECT_FONT);
				_systemfont->Stock = true;
				_systemfont->Face = REMAP_FONT;
			}
			return((HGDIOBJ)_systemfont);

		case NULL_BRUSH:
			if (_nullbrush == nullptr) {
				_nullbrush = New_Object(GDI_OBJECT_BRUSH);
				_nullbrush->Stock = true;
				_nullbrush->Color = (COLORREF)0xFFFFFFFF;
			}
			return((HGDIOBJ)_nullbrush);

		case BLACK_BRUSH:
			if (_blackbrush == nullptr) {
				_blackbrush = New_Object(GDI_OBJECT_BRUSH);
				_blackbrush->Stock = true;
				_blackbrush->Color = RGB(0, 0, 0);
			}
			return((HGDIOBJ)_blackbrush);

		case WHITE_BRUSH:
			if (_whitebrush == nullptr) {
				_whitebrush = New_Object(GDI_OBJECT_BRUSH);
				_whitebrush->Stock = true;
				_whitebrush->Color = RGB(255, 255, 255);
			}
			return((HGDIOBJ)_whitebrush);

		default:
			return(WIN32_UNSUPPORTED("GetStockObject: a stock object nothing here draws with",
				(HGDIOBJ)nullptr));
	}
}


HBRUSH CreateSolidBrush(COLORREF color)
{
	GdiObject * brush = New_Object(GDI_OBJECT_BRUSH);
	brush->Color = color;
	return((HBRUSH)brush);
}


HFONT CreateFontIndirectA(LOGFONTA const * description)
{
	if (description == nullptr) {
		return(nullptr);
	}

	GdiObject * font = New_Object(GDI_OBJECT_FONT);
	font->Face = description->lfFaceName;
	font->Height = (description->lfHeight < 0) ? -description->lfHeight : description->lfHeight;
	font->Weight = description->lfWeight;
	font->Italic = (description->lfItalic != 0);
	return((HFONT)font);
}


HFONT CreateFontA(int height, int, int, int, int weight, DWORD italic, DWORD, DWORD,
	DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR face)
{
	LOGFONTA description;
	memset(&description, 0, sizeof(description));

	description.lfHeight = height;
	description.lfWeight = weight;
	description.lfItalic = (BYTE)(italic != 0);

	if (face != nullptr) {
		strncpy(description.lfFaceName, face, sizeof(description.lfFaceName) - 1);
	}

	return(CreateFontIndirectA(&description));
}


BOOL DeleteObject(HGDIOBJ handle)
{
	GdiObject * object = Object_Of(handle);

	if (object == nullptr || object->Stock) {
		return(FALSE);
	}

	// A context still holding it would be left with a dangling selection.
	for (unsigned int index = 0; index < _Contexts.size(); index++) {
		if (_Contexts[index]->State.Font == object) return(FALSE);
		if (_Contexts[index]->State.Brush == object) return(FALSE);
	}

	for (unsigned int index = 0; index < _Objects.size(); index++) {
		if (_Objects[index] == object) {
			_Objects.erase(_Objects.begin() + index);
			break;
		}
	}

	delete object;
	return(TRUE);
}


HGDIOBJ SelectObject(HDC handle, HGDIOBJ object)
{
	DeviceContext * context = Context_Of(handle);
	GdiObject * entry = Object_Of(object);

	if (context == nullptr || entry == nullptr) {
		return(nullptr);
	}

	if (entry->Type == GDI_OBJECT_FONT) {
		GdiObject * previous = context->State.Font;
		context->State.Font = entry;
		return((HGDIOBJ)previous);
	}

	GdiObject * previous = context->State.Brush;
	context->State.Brush = entry;
	return((HGDIOBJ)previous);
}


//------------------------------------------------------------------------------
// Contexts.
//------------------------------------------------------------------------------


HDC Win32_GDI_Surface_DC(Surface & surface)
{
	return(New_Context(&surface));
}


// Every caller measures or builds a font against one of these; a draw through
// it reports itself rather than guessing at a surface.
HDC GetDC(HWND)
{
	return(New_Context(nullptr));
}


int ReleaseDC(HWND, HDC handle)
{
	return(Destroy_Context(handle) != FALSE ? 1 : 0);
}


HDC CreateCompatibleDC(HDC)
{
	return(New_Context(nullptr));
}


BOOL DeleteDC(HDC handle)
{
	return(Destroy_Context(handle));
}


int SaveDC(HDC handle)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return(0);
	}

	context->Saved.push_back(context->State);
	return((int)context->Saved.size());
}


BOOL RestoreDC(HDC handle, int saved)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr || context->Saved.empty()) {
		return(FALSE);
	}

	// A negative index counts back from the top, as every caller spells it.
	unsigned int depth = (saved < 0) ? (unsigned int)((int)context->Saved.size() + saved + 1)
									 : (unsigned int)saved;

	if (depth == 0 || depth > context->Saved.size()) {
		return(FALSE);
	}

	context->State = context->Saved[depth - 1];
	context->Saved.resize(depth - 1);
	return(TRUE);
}


COLORREF SetTextColor(HDC handle, COLORREF color)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return((COLORREF)0xFFFFFFFF);
	}

	COLORREF previous = context->State.TextColor;
	context->State.TextColor = color;
	return(previous);
}


COLORREF GetTextColor(HDC handle)
{
	DeviceContext * context = Context_Of(handle);
	return((context != nullptr) ? context->State.TextColor : (COLORREF)0xFFFFFFFF);
}


COLORREF SetBkColor(HDC handle, COLORREF color)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return((COLORREF)0xFFFFFFFF);
	}

	COLORREF previous = context->State.BackColor;
	context->State.BackColor = color;
	return(previous);
}


COLORREF GetBkColor(HDC handle)
{
	DeviceContext * context = Context_Of(handle);
	return((context != nullptr) ? context->State.BackColor : (COLORREF)0xFFFFFFFF);
}


int SetBkMode(HDC handle, int mode)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return(0);
	}

	int previous = context->State.BackMode;
	context->State.BackMode = mode;
	return(previous);
}


int GetBkMode(HDC handle)
{
	DeviceContext * context = Context_Of(handle);
	return((context != nullptr) ? context->State.BackMode : 0);
}


UINT SetTextAlign(HDC handle, UINT align)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return((UINT)0xFFFFFFFF);
	}

	UINT previous = context->State.TextAlign;
	context->State.TextAlign = align;
	return(previous);
}


BOOL GdiFlush(void)
{
	// Nothing here batches.
	return(TRUE);
}


//------------------------------------------------------------------------------
// The coordinate space, which is the identity.
//------------------------------------------------------------------------------


int SetGraphicsMode(HDC handle, int mode)
{
	if (Context_Of(handle) == nullptr) {
		return(0);
	}

	// With no world transform both modes describe the same identity mapping.
	if (mode != GM_COMPATIBLE && mode != GM_ADVANCED) {
		return(0);
	}

	return(GM_COMPATIBLE);
}


BOOL ModifyWorldTransform(HDC handle, void const *, DWORD mode)
{
	if (Context_Of(handle) == nullptr) {
		return(FALSE);
	}

	if (mode != MWT_IDENTITY) {
		return(WIN32_UNSUPPORTED("ModifyWorldTransform: a transform the surfaces have no room for", FALSE));
	}

	return(TRUE);
}


BOOL SetViewportOrgEx(HDC handle, int x, int y, LPPOINT previous)
{
	if (Context_Of(handle) == nullptr) {
		return(FALSE);
	}

	if (previous != nullptr) {
		previous->x = 0;
		previous->y = 0;
	}

	if (x != 0 || y != 0) {
		return(WIN32_UNSUPPORTED("SetViewportOrgEx: an origin away from the surface's own", FALSE));
	}

	return(TRUE);
}


BOOL SetWindowOrgEx(HDC handle, int x, int y, LPPOINT previous)
{
	if (Context_Of(handle) == nullptr) {
		return(FALSE);
	}

	if (previous != nullptr) {
		previous->x = 0;
		previous->y = 0;
	}

	if (x != 0 || y != 0) {
		return(WIN32_UNSUPPORTED("SetWindowOrgEx: an origin away from the surface's own", FALSE));
	}

	return(TRUE);
}


BOOL DPtoLP(HDC handle, LPPOINT, int)
{
	return(Context_Of(handle) != nullptr ? TRUE : FALSE);
}


BOOL LPtoDP(HDC handle, LPPOINT, int)
{
	return(Context_Of(handle) != nullptr ? TRUE : FALSE);
}


//------------------------------------------------------------------------------
// Text.
//------------------------------------------------------------------------------


static bool Fetch_Metrics(FontMetrics & metrics)
{
	return(ODGetFontMetrics(REMAP_FONT, &metrics));
}


static int Run_Width(FontMetrics const & metrics, char const * text, int count)
{
	int width = 0;

	for (int index = 0; index < count; index++) {
		width += metrics.charWidths[(unsigned char)text[index]];
	}

	return(width);
}


BOOL GetTextExtentPoint32A(HDC handle, LPCSTR text, int count, LPSIZE size)
{
	if (size != nullptr) {
		size->cx = 0;
		size->cy = 0;
	}

	if (Context_Of(handle) == nullptr || text == nullptr || size == nullptr) {
		return(FALSE);
	}

	FontMetrics metrics;
	if (!Fetch_Metrics(metrics)) {
		return(FALSE);
	}

	int length = (int)strlen(text);
	if (count < 0 || count > length) {
		count = length;
	}

	size->cx = Run_Width(metrics, text, count);
	size->cy = metrics.glyphHeight;
	return(TRUE);
}


BOOL GetTextMetricsA(HDC handle, LPTEXTMETRICA metrics)
{
	if (metrics == nullptr) {
		return(FALSE);
	}

	memset(metrics, 0, sizeof(*metrics));

	DeviceContext * context = Context_Of(handle);
	if (context == nullptr) {
		return(FALSE);
	}

	FontMetrics font;
	if (!Fetch_Metrics(font)) {
		return(FALSE);
	}

	// A sheet font has no baseline: every glyph fills the inked band, so the
	// ascent is the height.
	metrics->tmHeight = font.glyphHeight;
	metrics->tmAscent = font.glyphHeight;
	metrics->tmDescent = 0;
	metrics->tmInternalLeading = 0;
	metrics->tmExternalLeading = font.topMargin;

	int total = 0;
	int printable = 0;
	int widest = 0;

	for (int character = ' '; character <= '~'; character++) {
		total += font.charWidths[character];
		printable++;
	}

	for (int character = 0; character < 256; character++) {
		if (font.charWidths[character] > widest) {
			widest = font.charWidths[character];
		}
	}

	metrics->tmAveCharWidth = (printable > 0) ? (total / printable) : font.glyphWidth;
	metrics->tmMaxCharWidth = widest;
	metrics->tmWeight = (context->State.Font != nullptr) ? context->State.Font->Weight : FW_NORMAL;
	metrics->tmItalic = (BYTE)((context->State.Font != nullptr && context->State.Font->Italic) ? 1 : 0);
	metrics->tmFirstChar = ' ';
	metrics->tmLastChar = (CHAR)0xFF;
	metrics->tmDefaultChar = ' ';
	metrics->tmBreakChar = ' ';
	metrics->tmCharSet = ANSI_CHARSET;
	return(TRUE);
}


// The character blitter has no clipping, so a run that would pass the end of a
// row is cut here; the vertical extent is all or nothing.
static void Draw_Run(Surface & surface, FontMetrics const & metrics, char const * text, int count,
	int x, int y, COLORREF color)
{
	int cellwidth = metrics.glyphWidth + metrics.leftMargin;
	int cellheight = metrics.glyphHeight + metrics.topMargin;

	// Where ODDrawCharRemap draws the first glyph; clipping starts there.
	int left = x - 1;
	int top = y - metrics.topMargin;

	if (left < 0 || top < 0 || top + cellheight > surface.Get_Height()) {
		return;
	}

	int rightmost = surface.Get_Width() - cellwidth;

	int fitted = 0;
	int advance = left;
	while (fitted < count && advance <= rightmost) {
		advance += metrics.charWidths[(unsigned char)text[fitted]];
		fitted++;
	}

	if (fitted <= 0) {
		return;
	}

	ODDrawCharRemap(surface, text, fitted, Rect(x, y, x, y), REMAP_FONT, color, 0, 0);
}


BOOL TextOutA(HDC handle, int x, int y, LPCSTR text, int count)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr || text == nullptr) {
		return(FALSE);
	}

	if (context->Target == nullptr) {
		return(WIN32_UNSUPPORTED("TextOut: a context with no surface behind it", FALSE));
	}

	FontMetrics metrics;
	if (!Fetch_Metrics(metrics)) {
		return(FALSE);
	}

	int length = (int)strlen(text);
	if (count < 0 || count > length) {
		count = length;
	}

	if (count == 0) {
		return(TRUE);
	}

	int left = x;
	if ((context->State.TextAlign & TA_CENTER) == TA_CENTER) {
		left -= Run_Width(metrics, text, count) / 2;
	} else if ((context->State.TextAlign & TA_CENTER) == TA_RIGHT) {
		left -= Run_Width(metrics, text, count);
	}

	Draw_Run(*context->Target, metrics, text, count, left, y, context->State.TextColor);
	return(TRUE);
}


int DrawTextA(HDC handle, LPCSTR text, int count, LPRECT rect, UINT format)
{
	DeviceContext * context = Context_Of(handle);
	if (context == nullptr || text == nullptr || rect == nullptr) {
		return(0);
	}

	FontMetrics metrics;
	if (!Fetch_Metrics(metrics)) {
		return(0);
	}

	int length = (int)strlen(text);
	if (count < 0 || count > length) {
		count = length;
	}

	int width = Run_Width(metrics, text, count);

	int left = rect->left;
	if ((format & DT_CENTER) != 0) {
		left += (rect->right - rect->left - width) / 2;
	} else if ((format & DT_RIGHT) != 0) {
		left = rect->right - width;
	}

	int top = rect->top;
	if ((format & DT_VCENTER) != 0) {
		top += (rect->bottom - rect->top - metrics.glyphHeight) / 2;
	} else if ((format & DT_BOTTOM) != 0) {
		top = rect->bottom - metrics.glyphHeight;
	}

	if ((format & DT_CALCRECT) != 0) {
		rect->right = rect->left + width;
		rect->bottom = rect->top + metrics.glyphHeight;
		return(metrics.glyphHeight);
	}

	// Only a single line is laid out; word wrapped text goes through
	// OD_Draw_Text_Remap.
	if ((format & (DT_WORDBREAK | DT_SINGLELINE)) == DT_WORDBREAK) {
		return(WIN32_UNSUPPORTED("DrawText: wrapping text a single line cannot hold", 0));
	}

	if (context->Target == nullptr) {
		return(WIN32_UNSUPPORTED("DrawText: a context with no surface behind it", 0));
	}

	Draw_Run(*context->Target, metrics, text, count, left, top, context->State.TextColor);
	return(metrics.glyphHeight);
}


int FillRect(HDC handle, RECT const * rect, HBRUSH brush)
{
	DeviceContext * context = Context_Of(handle);
	GdiObject * entry = Object_Of((HGDIOBJ)brush);

	if (context == nullptr || rect == nullptr || entry == nullptr || entry->Type != GDI_OBJECT_BRUSH) {
		return(0);
	}

	if (context->Target == nullptr) {
		return(WIN32_UNSUPPORTED("FillRect: a context with no surface behind it", 0));
	}

	// The null brush paints nothing.
	if (entry->Stock && entry->Color == (COLORREF)0xFFFFFFFF) {
		return(1);
	}

	Rect fill(rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top);
	context->Target->Fill_Rect(fill, ODColorToHiColor(entry->Color));
	return(1);
}


//------------------------------------------------------------------------------
// The raster half, which is not written.
//------------------------------------------------------------------------------


HDC BeginPaint(HWND, LPPAINTSTRUCT) { return(WIN32_STUB((HDC)nullptr)); }
BOOL EndPaint(HWND, PAINTSTRUCT const *) { return(WIN32_STUB(FALSE)); }
HBITMAP CreateDIBSection(HDC, BITMAPINFO const *, UINT, void ** bits, HANDLE, DWORD) { if (bits != nullptr) *bits = nullptr; return(WIN32_STUB((HBITMAP)nullptr)); }
HBITMAP CreateBitmap(int, int, UINT, UINT, void const *) { return(WIN32_STUB((HBITMAP)nullptr)); }
HICON CreateIconIndirect(PICONINFO) { return(WIN32_STUB((HICON)nullptr)); }
int GetObjectA(HGDIOBJ, int, LPVOID) { return(WIN32_STUB(0)); }
int SetStretchBltMode(HDC, int) { return(WIN32_STUB(0)); }
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return(WIN32_STUB(FALSE)); }
int StretchDIBits(HDC, int, int, int, int, int, int, int, int, void const *, BITMAPINFO const *, UINT, DWORD) { return(WIN32_STUB(0)); }
int GetDeviceCaps(HDC, int) { return(WIN32_STUB(0)); }

#endif	// OPENTS_WIN32_SUBSTITUTE
