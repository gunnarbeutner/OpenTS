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

#include "platform/disk.h"

#include <sys/statvfs.h>


bool Platform_Free_Space(char const * directory, std::uint64_t & bytes)
{
	char const * const path = (directory != nullptr && directory[0] != '\0') ? directory : ".";
	struct statvfs space;

	if (::statvfs(path, &space) != 0) {
		return(false);
	}

	std::uint64_t const unit = (space.f_frsize != 0) ? space.f_frsize : space.f_bsize;
	bytes = (std::uint64_t)space.f_bavail * unit;
	return(true);
}

#endif	// !_WIN32
