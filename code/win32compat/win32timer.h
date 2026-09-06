/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The multimedia timer on a target with one thread. A callback runs only inside
// Win32_Timer_Service, on the engine's stack, so a period is a lower bound on
// the gap between calls and missed periods coalesce into one late call.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "windows.h"


// Runs every armed callback whose deadline has passed and rearms the periodic
// ones. A reentrant call returns without doing anything, so a callback may
// service, sleep, or kill a timer.
void Win32_Timer_Service(void);

#endif	// OPENTS_WIN32_SUBSTITUTE
