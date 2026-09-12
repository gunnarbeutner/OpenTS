/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if !defined(_MSC_VER)


static void Copy_Component(char * destination, char const * start, char const * end)
{
	if (destination == nullptr) return;

	while (start < end) *destination++ = *start++;
	*destination = '\0';
}


void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext)
{
	char const * cursor = path;
	char const * drive_end = cursor;

	if (path[0] != '\0' && path[1] == ':') drive_end = path + 2;
	Copy_Component(drive, path, drive_end);

	char const * dir_end = drive_end;
	for (cursor = drive_end; *cursor != '\0'; cursor++) {
		if (*cursor == '\\' || *cursor == '/') dir_end = cursor + 1;
	}
	Copy_Component(dir, drive_end, dir_end);

	char const * ext_start = cursor;
	for (char const * scan = dir_end; *scan != '\0'; scan++) {
		if (*scan == '.') ext_start = scan;
	}
	Copy_Component(fname, dir_end, ext_start);
	Copy_Component(ext, ext_start, cursor);
}


void _makepath(char * path, char const * drive, char const * dir, char const * fname, char const * ext)
{
	char * out = path;

	if (drive != nullptr && drive[0] != '\0') {
		*out++ = drive[0];
		*out++ = ':';
	}

	if (dir != nullptr && dir[0] != '\0') {
		while (*dir != '\0') *out++ = *dir++;
		if (out[-1] != '\\' && out[-1] != '/') *out++ = '\\';
	}

	if (fname != nullptr) {
		while (*fname != '\0') *out++ = *fname++;
	}

	if (ext != nullptr && ext[0] != '\0') {
		if (ext[0] != '.') *out++ = '.';
		while (*ext != '\0') *out++ = *ext++;
	}

	*out = '\0';
}

#endif	// !_MSC_VER
