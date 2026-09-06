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

#if defined(__EMSCRIPTEN__)

#include "browser.h"

#include <emscripten/emscripten.h>

#include <sys/statvfs.h>


// How long the page may take to answer before the request counts as unanswered.
static DWORD const DISK_ESTIMATE_TIMEOUT = 2000;

static bool _EstimateSampled = false;
static bool _EstimateValid = false;
static unsigned long long _EstimateFree = 0;
static unsigned long long _EstimateTotal = 0;


// A mount that knows its size supplies statfs and Emscripten asks it. NODERAWFS
// has no mount at all: it replaces the virtual filesystem with the host's own.
static bool Filesystem_Reports_Its_Own_Space(char const * path)
{
	int answers = EM_ASM_INT({
		try {
			if (typeof FS !== "object" || FS === null) return 0;
			if (typeof FS.lookupPath !== "function") return 1;

			var node = FS.lookupPath(UTF8ToString($0), { follow: true }).node;
			return (node && node.node_ops && node.node_ops.statfs) ? 1 : 0;
		} catch (error) {
			return 0;
		}
	}, path);

	return(answers != 0);
}


// The estimate is a promise and the caller is synchronous, so the wait is the
// engine's yield. It is asked once, because the quota moves with the device
// rather than with the page.
static bool Sample_Page_Storage(void)
{
	if (_EstimateSampled) {
		return(_EstimateValid);
	}

	_EstimateSampled = true;

	int offered = EM_ASM_INT({
		return (typeof navigator === "object" && navigator !== null && navigator.storage
			&& typeof navigator.storage.estimate === "function") ? 1 : 0;
	});

	if (offered == 0) {
		return(false);
	}

	if (!Browser_Yield_Is_Available()) {
		return(WIN32_UNSUPPORTED("GetDiskFreeSpaceEx: waiting on the page's storage estimate without the yield scaffold", false));
	}

	// Inside EM_ASM a comma outside parentheses splits the block.
	EM_ASM({
		globalThis.__opentsStorageEstimate = null;
		navigator.storage.estimate().then(function (estimate) {
			globalThis.__opentsStorageEstimate = Array(Number(estimate.quota) || 0, Number(estimate.usage) || 0);
		}).catch(function (error) {
			globalThis.__opentsStorageEstimate = Array(-1, -1);
		});
	});

	DWORD const start = timeGetTime();
	int state = 0;

	for (;;) {
		state = EM_ASM_INT({
			var estimate = globalThis.__opentsStorageEstimate;
			if (estimate === null || estimate === undefined) return 0;
			return (estimate[0] < 0) ? -1 : 1;
		});

		if (state != 0) break;
		if ((DWORD)(timeGetTime() - start) >= DISK_ESTIMATE_TIMEOUT) break;

		Browser_Yield();
	}

	if (state != 1) {
		return(false);
	}

	double const quota = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[0]; });
	double const usage = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[1]; });

	EM_ASM({ delete globalThis.__opentsStorageEstimate; });

	_EstimateTotal = (quota > 0.0) ? (unsigned long long)quota : 0;
	_EstimateFree = (quota > usage) ? (unsigned long long)(quota - usage) : 0;
	_EstimateValid = (_EstimateTotal > 0);

	return(_EstimateValid);
}


BOOL GetDiskFreeSpaceExA(LPCSTR root, PULARGE_INTEGER freetocaller, PULARGE_INTEGER total, PULARGE_INTEGER totalfree)
{
	char const * path = ((root != nullptr) && (root[0] != '\0')) ? root : ".";

	unsigned long long freebytes = 0;
	unsigned long long totalbytes = 0;
	bool answered = false;

	if (Filesystem_Reports_Its_Own_Space(path)) {
		struct statvfs space;

		if (statvfs(path, &space) == 0) {
			unsigned long long const unit = (space.f_frsize != 0) ? space.f_frsize : space.f_bsize;

			freebytes = (unsigned long long)space.f_bavail * unit;
			totalbytes = (unsigned long long)space.f_blocks * unit;
			answered = true;
		}
	}

	if (!answered && Sample_Page_Storage()) {
		freebytes = _EstimateFree;
		totalbytes = _EstimateTotal;
		answered = true;
	}

	if (!answered) {
		return(WIN32_UNSUPPORTED("GetDiskFreeSpaceEx: no filesystem and no page able to report free space", FALSE));
	}

	if (totalbytes < freebytes) totalbytes = freebytes;

	// Nothing here distinguishes the caller from other users of the space.
	if (freetocaller != nullptr) freetocaller->QuadPart = freebytes;
	if (total != nullptr) total->QuadPart = totalbytes;
	if (totalfree != nullptr) totalfree->QuadPart = freebytes;

	return(TRUE);
}


//------------------------------------------------------------------------------
// The drives.
//------------------------------------------------------------------------------


UINT GetDriveTypeA(LPCSTR) { return(WIN32_STUB(DRIVE_UNKNOWN)); }
DWORD GetLogicalDrives(void) { return(WIN32_STUB(0)); }
BOOL GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD) { return(WIN32_STUB(FALSE)); }

#elif defined(OPENTS_WIN32_SUBSTITUTE)

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
