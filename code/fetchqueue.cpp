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

#include "fetchqueue.h"

#include "platform/filehint.h"

#include <emscripten/emscripten.h>


void FetchQueueClass::Clear(void)
{
	Runs.clear();
	Cursor = 0;
	TotalBytes = 0;
	DoneBytes = 0;
	Resume = 0.0;
}


void FetchQueueClass::Add(char const * name, std::uint64_t offset, std::uint64_t length)
{
	if (name == nullptr || *name == '\0' || length == 0) return;

	RunClass run;
	run.Name = name;
	run.Start = offset;
	run.Size = length;
	run.Done = 0;
	Runs.push_back(run);
	TotalBytes += length;
}


void FetchQueueClass::Set_Share(double share)
{
	if (share <= 0.0 || share > 1.0) {
		Share = 1.0;
		return;
	}

	Share = share;
}


bool FetchQueueClass::Step(void)
{
	double const now = emscripten_get_now();

	if (now < Resume) return(false);

	while (Cursor < Runs.size()) {
		RunClass & run = Runs[Cursor];

		if (run.Done >= run.Size) {
			Cursor++;
			continue;
		}

		std::uint64_t const remaining = run.Size - run.Done;
		std::uint32_t const span = (remaining < Chunk) ? (std::uint32_t)remaining : Chunk;

		// A range the store declines is not retried: it counts as done either way, or the
		// queue would never reach its end.
		Platform_Prefetch_File(run.Name.c_str(), (std::uint32_t)(run.Start + run.Done), span);

		// What the fetch cost is the only measure of the link the queue needs: standing
		// idle for as long again in proportion leaves the rest of it to everything else.
		// A range the store already held costs nothing and so waits for nothing.
		double const landed = emscripten_get_now();
		Resume = landed + (landed - now) * (1.0 - Share) / Share;

		run.Done += span;
		DoneBytes += span;
		return(true);
	}

	return(false);
}

#endif
