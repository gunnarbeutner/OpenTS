/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "pgoprofile.h"

#include "globals.h"
#include <windows.h>
#include "substitute.h"

#include <emscripten/emscripten.h>


namespace {

// Only the release's own pointer names a profile, so one captured against any
// other release cannot be reached at all.
EM_JS(int, PGO_Profile_Load, (char const * kind), {
	try {
		var base = (typeof Module !== "undefined" && Module["opentsManifestBase"]) || document.baseURI;
		var name = UTF8ToString(kind);

		// The manifest layer has usually read the pointer already, and it is the one
		// mutable file in the release, so re-reading it costs a round trip for a value
		// this run has in hand.
		var current = globalThis.__opentsAssetsPointer;

		if (current === undefined) {
			var pointer = new XMLHttpRequest();
			pointer.open("GET", new URL("assets.json", base).href, false);
			pointer.send(null);
			if (pointer.status !== 200) return 0;
			current = JSON.parse(pointer.responseText);
		}

		var relative = current["profiles"] && current["profiles"][name];
		if (typeof relative !== "string") return 0;

		var profileRequest = new XMLHttpRequest();
		profileRequest.open("GET", new URL(relative, base).href, false);
		profileRequest.send(null);
		if (profileRequest.status !== 200) return 0;
		var profile = JSON.parse(profileRequest.responseText);

		if (profile["format"] !== "opents-fetch-profile-v1") return 0;
		if (!Array.isArray(profile["entries"])) return 0;

		globalThis.__opentsPgoProfile = profile;
		return profile["entries"].length;
	} catch (error) {
		return 0;
	}
});


// Copies one range at a time so the fetch it names starts before the rest
// are read.
EM_JS(int, PGO_Profile_Entry_Range, (int entry_index, int range_index,
	char * name_buf, int name_buf_size, unsigned int * offset, unsigned int * length), {
	var profile = globalThis.__opentsPgoProfile;
	if (!profile) return 0;

	var entry = profile["entries"][entry_index];
	if (!entry || !entry["ranges"] || !entry["ranges"][range_index]) return 0;

	var name = entry["name"];
	var written = Math.min(name.length, name_buf_size - 1);
	for (var index = 0; index < written; index++) {
		HEAPU8[name_buf + index] = name.charCodeAt(index) & 255;
	}
	HEAPU8[name_buf + written] = 0;

	var range = entry["ranges"][range_index];
	HEAPU32[offset >> 2] = range[0];
	HEAPU32[length >> 2] = range[1] - range[0];
	return 1;
});


// The profile's total bytes, so progress counts down toward a known total.
EM_JS(double, PGO_Profile_Total, (void), {
	var profile = globalThis.__opentsPgoProfile;
	if (!profile) return 0;

	var total = 0;
	profile["entries"].forEach(function (entry) {
		entry["ranges"].forEach(function (range) { total += range[1] - range[0]; });
	});
	return total;
});

}	// namespace


// Both are zero when no profile is in effect, which tells the page to report
// the pool.
static double PgoTotalBytes = 0.0;
static double PgoDoneBytes = 0.0;

extern "C" {
EMSCRIPTEN_KEEPALIVE double OpenTS_PGO_Total(void) {return(PgoTotalBytes);}
EMSCRIPTEN_KEEPALIVE double OpenTS_PGO_Done(void) {return(PgoDoneBytes);}
}


void PGO_Profile_Apply(PgoProfileKind kind)
{
	// Applying a profile while capturing one would write its ranges into the
	// next capture.
	if (Debug_PGO_Capture) return;

	char const * name = (kind == PGO_PROFILE_MENU) ? "menu" : "first-mission";
	int const entry_count = PGO_Profile_Load(name);

	if (entry_count > 0) PGO_Profile_In_Effect = true;

	PgoTotalBytes = PGO_Profile_Total();
	PgoDoneBytes = 0.0;

	unsigned int banked = 0;

	for (int entry_index = 0; entry_index < entry_count; entry_index++) {
		char archive_name[64];

		for (int range_index = 0; ; range_index++) {
			unsigned int offset = 0;
			unsigned int length = 0;

			if (!PGO_Profile_Entry_Range(entry_index, range_index, archive_name,
				sizeof(archive_name), &offset, &length)) {
				break;
			}

			// A hint only queues work for idle time, and the reads this must
			// get ahead of start right after, so the fetch waits here.
			if (Win32_Prefetch_File(archive_name, offset, length)) banked++;

			// A range the store declined is not retried, so it counts as done
			// either way.
			PgoDoneBytes += (double)length;
		}
	}

	// When nothing could be banked, the flag would only suppress the
	// per-archive heuristic, so the run continues as one with no profile.
	if (banked == 0) {
		PGO_Profile_In_Effect = false;
		PgoTotalBytes = 0.0;
		PgoDoneBytes = 0.0;
	}
}

#endif
