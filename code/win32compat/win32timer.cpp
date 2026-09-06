/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Armed callbacks with a deadline apiece on timeGetTime's clock, run from the
// engine's waits. Sleep hands the thread back to the page and services the
// timer on the way through, so a sleeping caller cannot starve its callbacks.

#include "always.h"
#include "substitute.h"
#include "mmsystem.h"

#include "win32timer.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include "browser.h"


// The engine arms two at most, so the table is small; a request it cannot hold
// fails as Windows fails one.
struct Win32TimerEventType
{
	UINT Id;					// Zero when the slot is free. Never zero for an armed timer.
	LPTIMECALLBACK Callback;
	DWORD_PTR User;
	UINT Period;				// Milliseconds between calls.
	DWORD Due;					// The timeGetTime reading the next call is owed at.
	bool IsPeriodic;
};

static const int WIN32_TIMER_EVENTS = 8;

static Win32TimerEventType _TimerEvents[WIN32_TIMER_EVENTS];

// Sequential identifiers, so a stale handle names nothing rather than whatever
// took its slot.
static UINT _NextTimerID = 1;

// A callback may sleep, and a sleep services the timer, so this stops a
// periodic callback re-entering itself.
static bool _Servicing = false;

// A period finer than timeGetTime's millisecond is not expressible; the upper
// bound is Windows's own.
static const UINT WIN32_TIMER_PERIOD_MIN = 1;
static const UINT WIN32_TIMER_PERIOD_MAX = 1000000;


MMRESULT timeGetDevCaps(LPTIMECAPS caps, UINT size)
{
	if (caps == nullptr || size < sizeof(TIMECAPS)) {
		return(TIMERR_NOCANDO);
	}

	caps->wPeriodMin = WIN32_TIMER_PERIOD_MIN;
	caps->wPeriodMax = WIN32_TIMER_PERIOD_MAX;
	return(TIMERR_NOERROR);
}


MMRESULT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags)
{
	(void)resolution;

	if (callback == nullptr || delay < WIN32_TIMER_PERIOD_MIN || delay > WIN32_TIMER_PERIOD_MAX) {
		return(0);
	}

	// Signalling or pulsing an event has no meaning with one thread, so it
	// fails rather than becoming a callback. TIME_KILL_SYNCHRONOUS asks for
	// what timeKillEvent already does.
	if ((flags & ~(UINT)(TIME_PERIODIC | TIME_KILL_SYNCHRONOUS)) != 0) {
		return(WIN32_UNSUPPORTED("timeSetEvent with anything but a function callback", 0));
	}

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		Win32TimerEventType * event = &_TimerEvents[index];

		if (event->Id != 0) continue;

		if (_NextTimerID == 0) _NextTimerID = 1;

		event->Id = _NextTimerID++;
		event->Callback = callback;
		event->User = user;
		event->Period = delay;
		event->Due = timeGetTime() + delay;
		event->IsPeriodic = ((flags & TIME_PERIODIC) != 0);
		return(event->Id);
	}

	return(0);
}


// A callback may kill its own timer from inside itself.
MMRESULT timeKillEvent(UINT id)
{
	if (id == 0) {
		return(TIMERR_NOCANDO);
	}

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		if (_TimerEvents[index].Id == id) {
			_TimerEvents[index].Id = 0;
			_TimerEvents[index].Callback = nullptr;
			return(TIMERR_NOERROR);
		}
	}

	return(TIMERR_NOCANDO);
}


void Win32_Timer_Service(void)
{
	if (_Servicing) {
		return;
	}

	_Servicing = true;

	DWORD now = timeGetTime();

	for (int index = 0; index < WIN32_TIMER_EVENTS; index++) {
		Win32TimerEventType * event = &_TimerEvents[index];

		if (event->Id == 0) continue;

		// The clock wraps, so the comparison is on the signed difference.
		if ((long)(now - event->Due) < 0) continue;

		UINT id = event->Id;
		LPTIMECALLBACK callback = event->Callback;
		DWORD_PTR user = event->User;

		// Rearmed from the present, so an engine that was away for many periods
		// gets one late call rather than a burst.
		if (event->IsPeriodic) {
			event->Due = now + event->Period;
		} else {
			event->Id = 0;
			event->Callback = nullptr;
		}

		callback(id, 0, user, 0, 0);
	}

	_Servicing = false;
}


// The wait rounds up to the gap between two of the page's frames, so a request
// shorter than a frame costs a frame; a hidden tab waits as long as the
// browser's throttling makes it.
void Sleep(DWORD milliseconds)
{
	// Without the yield scaffold nothing carries a wait, and spinning would
	// keep the thread the page needs.
	if (!Browser_Yield_Is_Available()) {
		WIN32_STUB_VOID();
		return;
	}

	Win32_Timer_Service();

	// Sleep(0) gives the rest of a timeslice back, and the page's nearest equivalent is a
	// frame. Yielding only when one is already due leaves the caller spinning through the
	// whole interval, which is what an idle engine loop does with its thread.
	if (milliseconds == 0) {
		Browser_Yield();
		return;
	}

	DWORD start = timeGetTime();

	do {
		Browser_Yield();
		Win32_Timer_Service();
	} while ((DWORD)(timeGetTime() - start) < milliseconds);
}

#endif	// OPENTS_WIN32_SUBSTITUTE
