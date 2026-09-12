/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Free space from statvfs, and on a page from the origin's storage quota, where Emscripten's
// in-memory filesystem reports fixed figures its own source labels untrue. The quota is the
// nearest real figure, not yet the one that bounds a save.

#include "always.h"

#if !defined(_WIN32)

#include "platform/disk.h"

#include <sys/statvfs.h>

#include <cstdio>

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>

#include <chrono>

// The page host's yield, as browser.h declares it; this file does without that header's window types.
bool Browser_Yield_Is_Available(void);
void Browser_Yield(void);

#endif


namespace {

bool Statvfs_Free_Space(char const * path, std::uint64_t & bytes)
{
	struct statvfs space;

	if (::statvfs(path, &space) != 0) {
		return(false);
	}

	std::uint64_t const unit = (space.f_frsize != 0) ? space.f_frsize : space.f_bsize;
	bytes = (std::uint64_t)space.f_bavail * unit;
	return(true);
}


#if defined(__EMSCRIPTEN__)

// How long the page may take to answer before the request counts as unanswered.
constexpr std::chrono::milliseconds ESTIMATE_TIMEOUT{2000};

bool EstimateSampled = false;
bool EstimateValid = false;
std::uint64_t EstimateFree = 0;


// A mount that knows its size supplies statfs and Emscripten asks it. NODERAWFS has no mount
// at all: it replaces the virtual filesystem with the host's own.
bool Filesystem_Reports_Its_Own_Space(char const * path)
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


// The estimate is a promise and the caller is synchronous, so the wait is the engine's
// yield. It is asked once, because the quota moves with the device rather than with the page.
bool Sample_Page_Storage(void)
{
	if (EstimateSampled) {
		return(EstimateValid);
	}

	EstimateSampled = true;

	int offered = EM_ASM_INT({
		return (typeof navigator === "object" && navigator !== null && navigator.storage
			&& typeof navigator.storage.estimate === "function") ? 1 : 0;
	});

	if (offered == 0) {
		return(false);
	}

	if (!Browser_Yield_Is_Available()) {
		std::fprintf(stderr, "OpenTS: the page's storage estimate cannot be waited on without the yield scaffold.\n");
		return(false);
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

	std::chrono::steady_clock::time_point const start = std::chrono::steady_clock::now();
	int state = 0;

	for (;;) {
		state = EM_ASM_INT({
			var estimate = globalThis.__opentsStorageEstimate;
			if (estimate === null || estimate === undefined) return 0;
			return (estimate[0] < 0) ? -1 : 1;
		});

		if (state != 0) break;
		if (std::chrono::steady_clock::now() - start >= ESTIMATE_TIMEOUT) break;

		Browser_Yield();
	}

	if (state != 1) {
		return(false);
	}

	double const quota = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[0]; });
	double const usage = EM_ASM_DOUBLE({ return globalThis.__opentsStorageEstimate[1]; });

	EM_ASM({ delete globalThis.__opentsStorageEstimate; });

	EstimateFree = (quota > usage) ? (std::uint64_t)(quota - usage) : 0;
	EstimateValid = (quota > 0.0);

	return(EstimateValid);
}

#endif	// __EMSCRIPTEN__

}	// namespace


bool Platform_Free_Space(char const * directory, std::uint64_t & bytes)
{
	char const * const path = (directory != nullptr && directory[0] != '\0') ? directory : ".";

#if defined(__EMSCRIPTEN__)
	if (Filesystem_Reports_Its_Own_Space(path) && Statvfs_Free_Space(path, bytes)) {
		return(true);
	}

	if (Sample_Page_Storage()) {
		bytes = EstimateFree;
		return(true);
	}

	std::fprintf(stderr, "OpenTS: neither a filesystem nor the page can report free space.\n");
	return(false);
#else
	return(Statvfs_Free_Space(path, bytes));
#endif
}

#endif	// !_WIN32
