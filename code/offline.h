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

// Banks every archive a release names into the block store, a chunk per call, so a later
// visit plays with no network. Films are left alone: they are streamed by the page's own
// video element and never enter the store.
//
// The page asks for it and reads the two totals for progress; the engine does the work.
// Called once a frame from Browser_Service, which is inside the engine's own suspending
// context; the fetch each step waits for has nothing to suspend into anywhere else.
void Offline_Service(void);

double Offline_Total_Bytes(void);

// The size of the offline set, and how much of it the store already holds.
double Offline_Set_Bytes(void);
double Offline_Stored_Bytes(void);
double Offline_Done_Bytes(void);

#endif
