/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "mstimer.h"

#include "win.h"

#include <chrono>


#ifdef _WIN32
// Windows 10 SDKs such as 10.0.19041 lack this Windows 11 flag.
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif


// One request for the life of the process gives every timer millisecond resolution. Without
// the opt-out, Windows 11 drops it while the window is minimized and each sleep lasts about 16 ms.
static struct MillisecondResolutionClass
{
	MillisecondResolutionClass(void)
	{
		PROCESS_POWER_THROTTLING_STATE state = {};
		state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		state.StateMask = 0;
		SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
		timeBeginPeriod(1);
	}
	~MillisecondResolutionClass(void) { timeEndPeriod(1); }
} MillisecondResolution;
#endif


/// <summary>
/// Returns the milliseconds elapsed since the first call. The origin is this process, not
/// the machine, so readings are only meaningful against one another.
/// </summary>
unsigned int System_Milliseconds(void)
{
	using namespace std::chrono;

	static steady_clock::time_point const started = steady_clock::now();
	return((unsigned int)duration_cast<milliseconds>(steady_clock::now() - started).count());
}


/// <summary>
/// Fetches the current millisecond reading. This is the sampling routine that the timer
/// templates call whenever they need to know how much time has passed.
/// </summary>
int MillisecondSystemTimerClass::operator () (void) const
{
	return((int)System_Milliseconds());
}


/// <summary>
/// Converts the timer into its current millisecond reading, so that it can be used wherever
/// a plain time value is expected.
/// </summary>
MillisecondSystemTimerClass::operator int (void) const
{
	return((int)System_Milliseconds());
}
