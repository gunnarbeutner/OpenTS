/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "platform/wait.h"

#include <chrono>
#include <cstdio>


// browser.h's; the header reaches the window layer, which this library is built without.
void Browser_Yield(void);
bool Browser_Yield_Is_Available(void);


/// <summary>
/// Hands the thread back to the page until the time has passed, which rounds a request
/// up to the gap between two frames; a hidden tab waits as long as the browser's
/// throttling makes it. Without the yield scaffold nothing can carry a wait, so the call
/// reports itself once and returns at once rather than spin on the page's thread.
/// </summary>
void Platform_Sleep(unsigned int milliseconds)
{
	if (!Browser_Yield_Is_Available()) {
		static bool _reported = false;
		if (!_reported) {
			_reported = true;
			std::fprintf(stderr, "OpenTS: Platform_Sleep has no yield to wait on and returns at once.\n");
			std::fflush(stderr);
		}
		return;
	}

	// A page has no timeslice to give back, so zero takes a whole frame; yielding only
	// once one is due leaves an idle loop such as MSEngine::Wait_Delay spinning.
	if (milliseconds == 0) {
		Browser_Yield();
		return;
	}

	std::chrono::steady_clock::time_point const start = std::chrono::steady_clock::now();
	do {
		Browser_Yield();
	} while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(milliseconds));
}

#endif	// __EMSCRIPTEN__
