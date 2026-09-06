/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The console records and calls of <wincon.h>; there is no console here.

#pragma once

#include "windef.h"

BOOL AllocConsole(void);
BOOL FreeConsole(void);
BOOL SetConsoleTitleA(LPCSTR title);
BOOL SetConsoleCP(UINT codepage);
BOOL SetConsoleOutputCP(UINT codepage);
HANDLE GetStdHandle(DWORD handle);
BOOL SetConsoleMode(HANDLE console, DWORD mode);
BOOL GetConsoleMode(HANDLE console, LPDWORD mode);
typedef struct _COORD {
	SHORT X;
	SHORT Y;
} COORD, * PCOORD;

typedef struct _SMALL_RECT {
	SHORT Left;
	SHORT Top;
	SHORT Right;
	SHORT Bottom;
} SMALL_RECT, * PSMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
	COORD dwSize;
	COORD dwCursorPosition;
	WORD wAttributes;
	SMALL_RECT srWindow;
	COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, * PCONSOLE_SCREEN_BUFFER_INFO;
BOOL GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info);
HWND GetConsoleWindow(void);
BOOL WriteConsoleA(HANDLE console, void const * buffer, DWORD towrite, LPDWORD written, LPVOID reserved);
#define WriteConsole			WriteConsoleA
BOOL SetConsoleScreenBufferSize(HANDLE console, COORD size);
#define SetConsoleTitle		SetConsoleTitleA
