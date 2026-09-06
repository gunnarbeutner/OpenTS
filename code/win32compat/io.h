/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the engine takes from MSVC's <io.h> and the open flags of its <fcntl.h>.

#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// POSIX does not distinguish text and binary streams; MSVC's <fcntl.h> names
// these beside its <io.h> opens.
#ifndef O_BINARY
#define O_BINARY	0
#endif
#ifndef O_TEXT
#define O_TEXT		0
#endif
#ifndef O_RAW
#define O_RAW		O_BINARY
#endif

#ifndef _A_NORMAL
#define _A_NORMAL	0x00
#define _A_RDONLY	0x01
#define _A_HIDDEN	0x02
#define _A_SYSTEM	0x04
#define _A_SUBDIR	0x10
#define _A_ARCH		0x20
#endif


inline long filelength(int handle)
{
	struct stat info;

	if (fstat(handle, &info) != 0) return(-1L);
	return((long)info.st_size);
}
