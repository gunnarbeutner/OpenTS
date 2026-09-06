/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// GetDiskFreeSpaceEx, answered by statvfs under -sNODERAWFS=1 and by the origin's
// storage quota on a page, where Emscripten's in-memory filesystem reports
// fixed figures its own source labels untrue. The quota is the nearest real
// figure, not yet the one that bounds a save.

#include "always.h"
#include "substitute.h"

#include "windows.h"

#if   defined(OPENTS_WIN32_SUBSTITUTE)

#include <sys/statvfs.h>

BOOL GetDiskFreeSpaceExA(LPCSTR root, PULARGE_INTEGER freetocaller, PULARGE_INTEGER total, PULARGE_INTEGER totalfree)
{
	struct statvfs info;
	if (::statvfs((root != nullptr && *root != '\0') ? root : ".", &info) != 0) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return(FALSE);
	}

	unsigned long long const unit = (info.f_frsize != 0) ? info.f_frsize : info.f_bsize;
	if (freetocaller != nullptr) freetocaller->QuadPart = (ULONGLONG)info.f_bavail * unit;
	if (total != nullptr) total->QuadPart = (ULONGLONG)info.f_blocks * unit;
	if (totalfree != nullptr) totalfree->QuadPart = (ULONGLONG)info.f_bfree * unit;
	SetLastError(NO_ERROR);
	return(TRUE);
}

UINT GetDriveTypeA(LPCSTR) { return(WIN32_STUB(DRIVE_UNKNOWN)); }
DWORD GetLogicalDrives(void) { return(WIN32_STUB(0)); }
BOOL GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD) { return(WIN32_STUB(FALSE)); }

#endif	// OPENTS_WIN32_SUBSTITUTE
