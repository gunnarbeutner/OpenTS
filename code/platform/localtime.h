/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <ctime>


// std::localtime shares one result between threads. Both runtimes have a reentrant form,
// under different names and argument orders.
inline std::tm Local_Calendar_Time(std::time_t time)
{
	std::tm parts {};
#if defined(_WIN32)
	localtime_s(&parts, &time);
#else
	localtime_r(&time, &parts);
#endif
	return(parts);
}
