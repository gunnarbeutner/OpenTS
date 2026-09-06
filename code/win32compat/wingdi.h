/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The GDI records, constants, and calls of <wingdi.h>, as far as the software surface path reaches them.

#pragma once

#include "windef.h"

#define RGB(r, g, b)	((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c)	((BYTE)(c))
#define GetGValue(c)	((BYTE)(((WORD)(c)) >> 8))
#define GetBValue(c)	((BYTE)((c) >> 16))
// GDI, as far as the software surface path reaches it; the DIB section the
// engine blits from does not exist here.
typedef struct tagRGBQUAD {
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, * PBITMAPINFOHEADER, * LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[1];
} BITMAPINFO, * PBITMAPINFO, * LPBITMAPINFO;

typedef struct tagBITMAP {
	LONG bmType;
	LONG bmWidth;
	LONG bmHeight;
	LONG bmWidthBytes;
	WORD bmPlanes;
	WORD bmBitsPixel;
	LPVOID bmBits;
} BITMAP, * PBITMAP, * LPBITMAP;

typedef struct tagDIBSECTION {
	BITMAP dsBm;
	BITMAPINFOHEADER dsBmih;
	DWORD dsBitfields[3];
	HANDLE dshSection;
	DWORD dsOffset;
} DIBSECTION, * PDIBSECTION, * LPDIBSECTION;

#define BI_RGB				0L
#define BI_RLE8				1L
#define BI_RLE4				2L
#define BI_BITFIELDS		3L
#define DIB_RGB_COLORS		0
#define DIB_PAL_COLORS		1
#define BLACKONWHITE		1
#define WHITEONBLACK		2
#define COLORONCOLOR		3
#define HALFTONE			4
#define SRCCOPY				(DWORD)0x00CC0020
#define SRCPAINT			(DWORD)0x00EE0086
#define BLACKNESS			(DWORD)0x00000042

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateDIBSection(HDC dc, BITMAPINFO const * info, UINT usage, void ** bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
int GetObjectA(HGDIOBJ object, int count, LPVOID buffer);
BOOL GdiFlush(void);
int SetStretchBltMode(HDC dc, int mode);
BOOL StretchBlt(HDC destination, int x, int y, int width, int height, HDC source, int sx, int sy, int swidth, int sheight, DWORD rop);
BOOL BitBlt(HDC destination, int x, int y, int width, int height, HDC source, int sx, int sy, DWORD rop);
int StretchDIBits(HDC dc, int x, int y, int width, int height, int sx, int sy, int swidth, int sheight, void const * bits, BITMAPINFO const * info, UINT usage, DWORD rop);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
HGDIOBJ GetStockObject(int object);
#define GetObject	GetObjectA
#define FW_DONTCARE			0
#define FW_NORMAL			400
#define FW_BOLD				700
#define ANSI_CHARSET		0
#define DEFAULT_CHARSET		1
#define OUT_DEFAULT_PRECIS	0
#define CLIP_DEFAULT_PRECIS	0
#define DEFAULT_QUALITY		0
#define DEFAULT_PITCH		0
#define TRANSPARENT			1
#define OPAQUE				2

#define HORZRES				8
#define VERTRES				10
#define BITSPIXEL			12
#define PLANES				14
#define VREFRESH			116
#define LOGPIXELSX			88
#define LOGPIXELSY			90

#define GM_COMPATIBLE		1
#define GM_ADVANCED			2
typedef struct tagLOGFONTA {
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	CHAR lfFaceName[32];
} LOGFONTA, LOGFONT, * PLOGFONTA, * LPLOGFONTA;

typedef struct tagTEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	CHAR tmFirstChar;
	CHAR tmLastChar;
	CHAR tmDefaultChar;
	CHAR tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
} TEXTMETRICA, TEXTMETRIC, * PTEXTMETRICA, * LPTEXTMETRICA;
HBRUSH CreateSolidBrush(COLORREF color);
int SaveDC(HDC dc);
BOOL RestoreDC(HDC dc, int saved);
int SetGraphicsMode(HDC dc, int mode);
int GetDeviceCaps(HDC dc, int index);
HFONT CreateFontIndirectA(LOGFONTA const * font);
BOOL GetTextMetricsA(HDC dc, LPTEXTMETRICA metrics);
BOOL TextOutA(HDC dc, int x, int y, LPCSTR string, int count);
#define CreateFontIndirect		CreateFontIndirectA
#define GetTextMetrics			GetTextMetricsA
#define TextOut					TextOutA
#define SYSTEM_FONT			13
#define ANSI_VAR_FONT		12
#define DEFAULT_GUI_FONT	17
#define NULL_BRUSH			5
#define WHITE_BRUSH			0
#define BLACK_BRUSH			4

#define OUT_RASTER_PRECIS	6
#define OUT_TT_PRECIS		4
#define PROOF_QUALITY		2
#define DRAFT_QUALITY		1
#define FF_DONTCARE			(0 << 4)
#define FF_ROMAN			(1 << 4)
#define FF_SWISS			(2 << 4)
#define FF_MODERN			(3 << 4)
#define VARIABLE_PITCH		2
#define FIXED_PITCH			1

#define TA_LEFT				0
#define TA_RIGHT			2
#define TA_CENTER			6
#define TA_TOP				0
#define TA_BOTTOM			8
#define TA_BASELINE			24

#define MWT_IDENTITY		1
#define MWT_LEFTMULTIPLY	2
#define MWT_RIGHTMULTIPLY	3
/* BITMAPFILEHEADER is fourteen bytes: wingdi.h packs it to two. */
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, * LPBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct _devicemodeA {
	BYTE dmDeviceName[32];
	WORD dmSpecVersion;
	WORD dmDriverVersion;
	WORD dmSize;
	WORD dmDriverExtra;
	DWORD dmFields;
	short dmOrientation;
	short dmPaperSize;
	short dmPaperLength;
	short dmPaperWidth;
	short dmScale;
	short dmCopies;
	short dmDefaultSource;
	short dmPrintQuality;
	short dmColor;
	short dmDuplex;
	short dmYResolution;
	short dmTTOption;
	short dmCollate;
	BYTE dmFormName[32];
	WORD dmLogPixels;
	DWORD dmBitsPerPel;
	DWORD dmPelsWidth;
	DWORD dmPelsHeight;
	DWORD dmDisplayFlags;
	DWORD dmDisplayFrequency;
} DEVMODEA, DEVMODE, * LPDEVMODEA, * LPDEVMODE;
#define ENUM_CURRENT_SETTINGS	((DWORD)-1)
int GetBkMode(HDC dc);
COLORREF GetBkColor(HDC dc);
COLORREF GetTextColor(HDC dc);
UINT SetTextAlign(HDC dc, UINT align);
BOOL SetViewportOrgEx(HDC dc, int x, int y, LPPOINT previous);
BOOL SetWindowOrgEx(HDC dc, int x, int y, LPPOINT previous);
BOOL DPtoLP(HDC dc, LPPOINT points, int count);
BOOL LPtoDP(HDC dc, LPPOINT points, int count);
HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bitsperpixel, void const * bits);
BOOL GetTextExtentPoint32A(HDC dc, LPCSTR string, int count, LPSIZE size);
HFONT CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strikeout, DWORD charset, DWORD outprecision, DWORD clipprecision, DWORD quality, DWORD pitchandfamily, LPCSTR face);
BOOL ModifyWorldTransform(HDC dc, void const * transform, DWORD mode);
#define GetTextExtentPoint32	GetTextExtentPoint32A
#define CreateFont				CreateFontA
