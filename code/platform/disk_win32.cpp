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

#include "platform/disk.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


bool Platform_Free_Space(char const * directory, std::uint64_t & bytes)
{
	ULARGE_INTEGER available;
	char const * const root = (directory != nullptr && directory[0] != '\0') ? directory : nullptr;

	if (!GetDiskFreeSpaceExA(root, &available, nullptr, nullptr)) {
		return(false);
	}

	bytes = available.QuadPart;
	return(true);
}

#endif	// _WIN32
