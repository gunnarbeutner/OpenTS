/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

class ShapeSet;

/// <summary>
/// Builds a shape set enlarged by <paramref name="numerator"/> over
/// <paramref name="denominator"/>, so a screen laid out at the window's own size can draw
/// the artwork through the ordinary blitters whatever that size is.
/// <paramref name="size"/> is how many bytes the source occupies, which bounds the frame
/// decode. Every frame comes back uncompressed and the caller owns the result, releasing it
/// with delete[] on the returned pointer cast to char *. Null when the shape cannot be read
/// or the ratio would not enlarge it.
/// </summary>
ShapeSet * Magnify_Shape(ShapeSet const * shapefile, int size, int numerator, int denominator);
