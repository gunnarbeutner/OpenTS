/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if !defined(_WIN32)

#include "platform/wait.h"

#include <chrono>
#include <thread>


void Platform_Sleep(unsigned int milliseconds)
{
	if (milliseconds == 0) {
		std::this_thread::yield();
		return;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

#endif	// !_WIN32
