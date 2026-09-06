/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The GDI subset the dialog code needs: a device context records the font, text
// color and background mode a caller selected, and text goes through the
// engine's remapped bitmap font. Only the surface context lacks a Win32 name.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "windows.h"

class Surface;


// Fetches a context that draws onto an engine surface, in the surface's own
// pixels. DSurface::GetDC answers with nothing on this target; release the
// result with DeleteDC.
HDC Win32_GDI_Surface_DC(Surface & surface);

#endif	// OPENTS_WIN32_SUBSTITUTE
