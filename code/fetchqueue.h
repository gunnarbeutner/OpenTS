/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#if defined(__EMSCRIPTEN__)

#include <cstdint>
#include <string>
#include <vector>


/*
 * A list of byte runs banked into the block store a chunk at a time, so a caller can spread
 * a large fetch over the engine's own frames rather than waiting for the whole of it at once.
 */
class FetchQueueClass
{
	public:
		// The chunk bounds how long one step may suspend the engine, which is what keeps a
		// queue drained behind an interactive screen from holding up that screen's input.
		explicit FetchQueueClass(std::uint32_t chunk) : Chunk(chunk) {}

		void Clear(void);

		// A run of a named file. A run of no length is ignored.
		void Add(char const * name, std::uint64_t offset, std::uint64_t length);

		// The share of wall-clock time the drain may spend fetching, between zero and one.
		// The queue stands idle for the rest of it, so the link it is on stays free in that
		// proportion whatever it is worth: no rate has to be known or guessed. One, the
		// default, lifts the bound.
		void Set_Share(double share);

		// Banks the next chunk, waiting for it, and is legal only where the engine may
		// suspend. False when nothing was banked, which is either the end of the queue or
		// a share that is spent for the moment; Is_Done tells those apart.
		bool Step(void);

		bool Is_Done(void) const {return(Cursor >= Runs.size());}
		bool Is_Empty(void) const {return(Runs.empty());}
		std::uint64_t Total_Bytes(void) const {return(TotalBytes);}
		std::uint64_t Done_Bytes(void) const {return(DoneBytes);}

	private:
		struct RunClass
		{
			std::string Name;
			std::uint64_t Start;
			std::uint64_t Size;
			std::uint64_t Done;
		};

		std::uint32_t Chunk;
		std::vector<RunClass> Runs;
		std::size_t Cursor = 0;
		std::uint64_t TotalBytes = 0;
		std::uint64_t DoneBytes = 0;

		double Share = 1.0;
		double Resume = 0.0;
};

#endif
