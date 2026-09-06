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

#include "offline.h"

#include "manifest.h"
#include <windows.h>
#include "substitute.h"

#include <emscripten/emscripten.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>


namespace {

// What the page streams for itself rather than reading through the store: films go to a
// video element and score tracks to an audio one, both by URL. A release marks these for
// offline too, and the page keeps them in its own cache; they are skipped here because
// banking them into the store would not put them where either element looks.
bool Is_Streamed(std::string const & name)
{
	static char const * const streamed[] = {".MP4", ".M4A"};

	for (char const * suffix : streamed) {
		std::size_t const length = std::strlen(suffix);

		if (name.size() < length) continue;

		std::string tail = name.substr(name.size() - length);

		for (char & letter : tail) letter = (char)std::toupper((unsigned char)letter);

		if (tail == suffix) return(true);
	}

	return(false);
}


// One range request per step. Large enough that a release is banked in a few hundred of
// them, small enough that the fetch each one waits for does not stall a drawing frame.
std::uint32_t const CHUNK = 4u * 1024u * 1024u;

struct OfflineItemClass
{
	std::string Name;
	std::uint64_t Size;
	std::uint64_t Done;
};

std::vector<OfflineItemClass> Items;
std::size_t Cursor = 0;
std::uint64_t TotalBytes = 0;
std::uint64_t DoneBytes = 0;

// The page asks; the engine answers on its own frame. A fetch suspends, and a call that
// enters WebAssembly from an ordinary page callback has nothing to suspend into: it raises
// "trying to suspend without WebAssembly.promising" and stops the engine.
// Sentinels rather than flags: a single byte set to anything non-zero would start a
// 150 MB fetch nobody asked for, and these values are not something a stray write lands on.
unsigned int const REQUESTED = 0x5A5A0001u;
unsigned int const RUNNING = 0x5A5A0002u;

unsigned int State = 0;

// How much of a frame this is allowed to take. Each is one range request, and the engine
// keeps drawing between frames rather than between these.
int const STEPS_PER_FRAME = 2;

}	// namespace


bool Offline_Begin(void)
{
	Items.clear();
	Cursor = 0;
	TotalBytes = 0;
	DoneBytes = 0;

	for (std::string const & name : Manifest_List_Files()) {
		BlockEntryClass entry;

		// Opening the volume is what the manifest resolves; a name it cannot is skipped
		// rather than failing the whole run, since one absent archive still leaves the
		// rest worth having.
		if (!Manifest_Offline(name.c_str())) continue;
		if (Is_Streamed(name)) continue;
		if (!Manifest_Find(name.c_str(), entry)) continue;
		if (entry.Size == 0) continue;

		OfflineItemClass item;
		item.Name = name;
		item.Size = entry.Size;
		item.Done = 0;
		Items.push_back(item);
		TotalBytes += entry.Size;
	}

	return(!Items.empty());
}


bool Offline_Step(void)
{
	while (Cursor < Items.size()) {
		OfflineItemClass & item = Items[Cursor];

		if (item.Done >= item.Size) {
			Cursor++;
			continue;
		}

		std::uint64_t const remaining = item.Size - item.Done;
		std::uint32_t const span = (remaining < CHUNK) ? (std::uint32_t)remaining : CHUNK;

		// A range the store declines is not retried: it counts as done either way, or the
		// run would never reach its end.
		Win32_Prefetch_File(item.Name.c_str(), (unsigned int)item.Done, span);

		item.Done += span;
		DoneBytes += span;
		return(true);
	}

	return(false);
}


// What the store already holds of the offline set, which is what tells a page that has
// been kept before from one that has not.
double Offline_Stored_Bytes(void)
{
	std::uint64_t stored = 0;

	for (std::string const & name : Manifest_List_Files()) {
		if (!Manifest_Offline(name.c_str())) continue;
		if (Is_Streamed(name)) continue;

		stored += Win32_Stored_Bytes(name.c_str());
	}

	return((double)stored);
}


// The offline set's size, whether or not a run has begun.
double Offline_Set_Bytes(void)
{
	std::uint64_t total = 0;

	for (std::string const & name : Manifest_List_Files()) {
		BlockEntryClass entry;

		if (!Manifest_Offline(name.c_str())) continue;
		if (Is_Streamed(name)) continue;
		if (!Manifest_Find(name.c_str(), entry)) continue;

		total += entry.Size;
	}

	return((double)total);
}


double Offline_Total_Bytes(void)
{
	return((double)TotalBytes);
}


double Offline_Done_Bytes(void)
{
	return((double)DoneBytes);
}


void Offline_Service(void)
{
	if (State == REQUESTED) {
		State = Offline_Begin() ? RUNNING : 0;
	}

	if (State != RUNNING) return;

	for (int step = 0; step < STEPS_PER_FRAME; step++) {
		if (!Offline_Step()) {
			State = 0;
			return;
		}
	}
}


extern "C" {

// Asks for the banking to start; the work itself happens on the engine's own frames.
/*
 * Takes a token, and every call without one is ignored.
 *
 * An export that takes an argument is a command rather than a counter, and the token says
 * so twice over: anything that calls every export it can find to read the engine's state --
 * the harness did exactly that -- passes nothing and is ignored here.
 */
EMSCRIPTEN_KEEPALIVE void OpenTS_Offline_Request(unsigned int token)
{
	if (token != REQUESTED) return;

	State = REQUESTED;
}
EMSCRIPTEN_KEEPALIVE int OpenTS_Offline_Active(void) {return(State != 0 ? 1 : 0);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Offline_Total(void) {return(Offline_Total_Bytes());}
EMSCRIPTEN_KEEPALIVE double OpenTS_Offline_Set(void) {return(Offline_Set_Bytes());}
EMSCRIPTEN_KEEPALIVE double OpenTS_Offline_Stored(void) {return(Offline_Stored_Bytes());}
EMSCRIPTEN_KEEPALIVE double OpenTS_Offline_Done(void) {return(Offline_Done_Bytes());}

}

#endif
