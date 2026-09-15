/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#if defined(__EMSCRIPTEN__)

class Surface;

// A picture is named by its legacy filename, such as "TSTBACK.PCX". A copy
// prepared at a multiple of the artwork's own size states the multiple in its
// name -- "TSTBACK.2X.WEBP" -- so a page can lay itself out at the size it is
// about to draw without opening the artwork it replaces.
bool Image_Browser_Available(char const * picture_filename);

/// <summary>
/// The multiple a prepared copy of this picture would be taken at, without fetching or
/// decoding it, so a screen can claim its design space before it draws. One when the
/// release prepared none.
/// </summary>
int Image_Browser_Scale(char const * picture_filename, int wanted);

/// <summary>
/// Decodes the release's WebP copy of a picture into a new 16 bit surface the
/// caller owns, or returns null when the release carries none the page can
/// use. The largest copy prepared at no more than <paramref name="wanted"/>
/// times the artwork's own size is taken, and the multiple it was prepared at
/// is reported through <paramref name="scale"/>.
/// </summary>
Surface * Image_Browser_Load(char const * picture_filename, int wanted, int & scale);

#endif
