/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uiscreen.h"


void UIPresenterClass::Queue(UIIntent const & intent)
{
	Queued.push_back(intent);
}


void UIPresenterClass::Drain(void)
{
	if (Draining) {
		return;
	}

	Draining = true;

	// The count is taken first, so an intent an executing intent queues is left for the
	// next drain. That keeps one pass bounded and the order of execution the order of
	// arrival.
	std::size_t remaining = Queued.size();

	// Nothing queued behind the answer runs, since it could act on a screen that is closing.
	while (remaining > 0 && !Queued.empty() && !Has_Result()) {
		UIIntent const intent = Queued.front();
		Queued.pop_front();
		remaining--;

		Execute(intent);
	}

	Draining = false;
}


void UIPresenterClass::Discard(void)
{
	Queued.clear();
}


void UIPresenterClass::Finish(UIResultType type, int code)
{
	if (Result.Type != UI_RESULT_NONE) {
		return;
	}

	Result.Type = type;
	Result.Code = code;
}
