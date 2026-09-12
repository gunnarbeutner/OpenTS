/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

/*
 * How a blit that changes size resamples its source; a same-size blit never
 * resamples. A name says what the picture is, and the kernel behind it is
 * XSurface's own business.
 */
enum SurfaceFilterType {
	/*
	 * Nearest neighbor, which keeps pixel artwork such as the sidebar and the
	 * cameos unblended.
	 */
	SURFACE_FILTER_POINT,

	/*
	 * A dithered photographic source such as a movie frame; the filter is
	 * widened so the ordered dither of a 16 bit picture is low passed away.
	 * Only 16 bit surfaces and opaque blits use it; anything else falls back
	 * to the point filter.
	 */
	SURFACE_FILTER_SMOOTH,

	/*
	 * Pre-rendered artwork with clean edges such as a menu page; the filter
	 * reconstructs at the sampling rate, keeping edges crisp at the price of
	 * a little overshoot. It has the same fallbacks as the smooth filter.
	 */
	SURFACE_FILTER_SHARP,
};
