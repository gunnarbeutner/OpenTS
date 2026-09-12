/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(_WIN32)

#include "platform/wait.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


// The scheduler tick decides how long this takes, which is why startup asks for a one
// millisecond period with timeBeginPeriod.
void Platform_Sleep(unsigned int milliseconds)
{
	::Sleep(milliseconds);
}

#endif	// _WIN32
