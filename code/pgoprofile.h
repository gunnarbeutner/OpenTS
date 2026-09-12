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
	PGO_PROFILE_FIRST_MISSION
};

// Prefetches every archive range that "profiles/<kind>.json" beside the
// manifest names. A missing, malformed, or mismatched profile does nothing;
// the per-archive prefetch heuristic still covers every archive.
void PGO_Profile_Apply(PgoProfileKind kind);

#endif
