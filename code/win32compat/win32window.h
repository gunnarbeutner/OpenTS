/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The window, the pointer, and the code page, answered for a page. Everything
// but the cursor builder is a Win32 entry point win32compat.h declares.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "win.h"


// Builds a cursor from 0xAARRGGBB pixels in rows from the top. SetCursor and
// DestroyCursor accept the result like any other.
HCURSOR Win32_Window_Create_Cursor(unsigned long const * pixels, int width, int height, int hotx, int hoty);

// Past this size a browser shows no pointer at all, so a caller scales its
// image down to fit.
int Win32_Window_Max_Cursor_Size(void);

// The cursor that draws nothing. The null cursor falls back to the page's own
// pointer, so asking for no pointer at all needs a cursor of its own.
HCURSOR Win32_Window_Blank_Cursor(void);

#endif	// OPENTS_WIN32_SUBSTITUTE
