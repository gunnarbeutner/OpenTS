/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The code pages, locale formatting, and string conversion of <winnls.h> and <stringapiset.h>.

#pragma once

#include "winbase.h"

#define CP_ACP			0
#define CP_UTF8			65001
UINT GetACP(void);
UINT GetOEMCP(void);
int MultiByteToWideChar(UINT codepage, DWORD flags, LPCSTR multibyte, int multibytecount, LPWSTR wide, int widecount);
int WideCharToMultiByte(UINT codepage, DWORD flags, LPCWSTR wide, int widecount, LPSTR multibyte, int multibytecount, LPCSTR defaultchar, LPBOOL useddefaultchar);
#define MB_PRECOMPOSED	0x00000001
#define LOCALE_USER_DEFAULT		0x0400
#define LANG_USER_DEFAULT		0x0400
#define TIME_NOMINUTESORSECONDS	0x00000001
#define TIME_NOSECONDS			0x00000002
#define TIME_NOTIMEMARKER		0x00000004
#define TIME_FORCE24HOURFORMAT	0x00000008
#define DATE_SHORTDATE			0x00000001
#define DATE_LONGDATE			0x00000002
int GetTimeFormatA(LCID locale, DWORD flags, SYSTEMTIME const * time, LPCSTR format, LPSTR text, int count);
int GetDateFormatA(LCID locale, DWORD flags, SYSTEMTIME const * date, LPCSTR format, LPSTR text, int count);
#define GetTimeFormat			GetTimeFormatA
#define GetDateFormat			GetDateFormatA
