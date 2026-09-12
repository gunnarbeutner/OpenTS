/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// The renderer and the UI shell submit into the same bgfx frame from separate translation
// units, and bgfx renders views in ascending order, so the draw order is fixed here rather
// than in either one. The magnify pass must carry the lower id for the present pass to
// sample its output from this frame rather than the last one, and the UI draws over the
// frame the present pass put down.
enum ViewIdType
{
	VIEW_PRESCALE = 0,
	VIEW_PRESENT = 1,
	VIEW_UI = 2,
};
