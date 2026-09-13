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

enum PgoProfileKind {
	PGO_PROFILE_MENU,
	PGO_PROFILE_CAMPAIGN,
	PGO_PROFILE_FIRST_MISSION
};

// Prefetches every archive range that the release's pointer names for this kind
// and returns only once they are banked. A missing, malformed, or mismatched
// profile does nothing; the per-archive prefetch heuristic still covers every
// archive.
void PGO_Profile_Apply(PgoProfileKind kind);

// Banks the campaign profile and then the first-mission profile behind a menu
// that is already up, a bounded chunk per call and for a bounded share of the
// time, and leaves anything it has not reached to an ordinary read. Called once
// a frame from Browser_Service, which is inside the engine's own suspending
// context.
void PGO_Profile_Service(void);

#endif
