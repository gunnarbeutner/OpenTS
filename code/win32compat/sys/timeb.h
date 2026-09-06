/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// MSVC's _timeb and _ftime, in place of the host's own <sys/timeb.h>.

#pragma once

#include <time.h>

struct _timeb {
	long time;
	unsigned short millitm;
	short timezone;
	short dstflag;
};


inline void _ftime(struct _timeb * result)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	result->time = (long)now.tv_sec;
	result->millitm = (unsigned short)(now.tv_nsec / 1000000);
	result->timezone = 0;
	result->dstflag = 0;
}
