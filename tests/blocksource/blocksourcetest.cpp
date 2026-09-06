/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises Attach_Whole and the generic Read/Hint extent-walking against block sources the
// test builds for itself -- what code/manifest.cpp relies on to serve a manifest-named
// archive as a single whole-file volume, with no ISO9660 descriptor or directory tree
// involved. No game disc, game data or original executable is involved, and none may
// become one.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "blocksource.h"

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-62s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		Failures++;
	}
}


/*
**	A repeatable byte pattern, so a read can be compared against what was written without
**	carrying a table of expected bytes around.
*/
std::vector<unsigned char> Pattern(unsigned int seed, std::size_t size)
{
	std::vector<unsigned char> data(size);
	unsigned int state = seed * 2654435761u + 1u;

	for (std::size_t index = 0; index < size; index++) {
		state = state * 1103515245u + 12345u;
		data[index] = (unsigned char)(state >> 16);
	}

	return data;
}


/*
**	A block source over a buffer already in memory, standing in for the range-request
**	transport the browser build supplies.
*/
class MemorySourceClass : public BlockSourceClass
{
	public:
		MemorySourceClass(std::vector<unsigned char> const & data) : Reads(0), Data(data) {}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override
		{
			if (offset > Data.size() || Data.size() - offset < length) return(false);
			std::memcpy(buffer, Data.data() + offset, length);
			Reads++;
			return(true);
		}

		virtual std::uint64_t Total_Size(void) override {return(Data.size());}

		int Reads;

	private:

		std::vector<unsigned char> Data;
};


/*
**	A block source with nothing at hand. It answers a read that has to be answered and
**	declines one that may be declined, which is what a source fetching over a network does
**	with a block it does not already hold.
*/
class DeferringSourceClass : public BlockSourceClass
{
	public:
		DeferringSourceClass(std::vector<unsigned char> const & data) :
			Fetches(0), From(0), Data(data) {}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override
		{
			if (offset > Data.size() || Data.size() - offset < length) return(false);

			if (DeferredReadClass::Deferring() && offset >= From) {
				DeferredReadClass::Decline();
				return(false);
			}

			std::memcpy(buffer, Data.data() + offset, length);
			Fetches++;
			return(true);
		}

		virtual std::uint64_t Total_Size(void) override {return(Data.size());}

		int Fetches;

		// Everything at or past here declines, so a read can be made to decline part way
		// through.
		std::uint64_t From;

	private:

		std::vector<unsigned char> Data;
};


bool Read_Whole(BlockFileClass const & volume, BlockEntryClass const & entry, std::vector<unsigned char> & out)
{
	out.assign(entry.Size, 0);
	if (entry.Size == 0) return(true);

	return(volume.Read(entry, 0, out.data(), entry.Size) == (int)entry.Size);
}

} // namespace


int main(int argc, char ** argv)
{
	(void)argc;
	(void)argv;

	std::vector<unsigned char> whole = Pattern(4, 3000);

	/*
	**	Attach_Whole itself: what it accepts and refuses, and the single-extent entry it
	**	hands back.
	*/
	{
		BlockFileClass volume;
		BlockEntryClass entry;

		Check(!volume.Attach_Whole(nullptr, 3000, entry), "refused a null source");

		std::unique_ptr<MemorySourceClass> oversized(new MemorySourceClass(whole));
		Check(!volume.Attach_Whole(std::move(oversized), 0, entry), "refused a size of zero");

		std::unique_ptr<MemorySourceClass> source(new MemorySourceClass(whole));
		Check(volume.Attach_Whole(std::move(source), whole.size(), entry),
			"attached a whole file with no descriptor to parse");
		Check(!entry.IsDirectory, "the whole-file entry is not a directory");
		Check(entry.Size == whole.size(), "the whole-file entry carries the size given");
		Check(entry.Extents.size() == 1 && entry.Extents[0].Start == 0
			&& entry.Extents[0].Length == whole.size(), "the whole-file entry is one extent from zero");
	}

	/*
	**	Byte exact reads through the entry Attach_Whole hands back.
	*/
	{
		BlockFileClass volume;
		BlockEntryClass entry;
		std::unique_ptr<MemorySourceClass> source(new MemorySourceClass(whole));

		volume.Attach_Whole(std::move(source), whole.size(), entry);

		std::vector<unsigned char> actual;
		Check(Read_Whole(volume, entry, actual), "read a whole-file entry in full");
		Check(actual == whole, "a whole-file entry read back byte for byte");

		unsigned char partial[300];
		Check(volume.Read(entry, 2000, partial, sizeof(partial)) == (int)sizeof(partial),
			"read a run in the middle of a whole-file entry");
		Check(std::memcmp(partial, whole.data() + 2000, sizeof(partial)) == 0,
			"the mid-file read matched");

		Check(volume.Read(entry, 2990, partial, sizeof(partial)) == 10,
			"a read past the end was cut short");
		Check(std::memcmp(partial, whole.data() + 2990, 10) == 0, "the short read at the end matched");
		Check(volume.Read(entry, 3000, partial, sizeof(partial)) == 0,
			"a read starting at the end returned nothing");
	}

	/*
	**	Reading what is not here yet. A read that may go without says so and delivers
	**	nothing, and the caller can tell that from the end of the file; a read that may not
	**	is answered exactly as it always was.
	*/
	{
		BlockFileClass volume;
		BlockEntryClass entry;
		std::unique_ptr<DeferringSourceClass> owned(new DeferringSourceClass(whole));
		DeferringSourceClass * const source = owned.get();

		Check(volume.Attach_Whole(std::move(owned), whole.size(), entry),
			"attached a whole file through a source that may decline");
		Check(!DeferredReadClass::Deferring(), "nothing declines outside a scope");

		std::vector<unsigned char> actual(3000, 0);
		Check(volume.Read(entry, 0, actual.data(), 3000) == 3000, "read the file with no scope in the way");
		Check(actual == whole, "the unscoped read came back byte for byte");

		int const fetched = source->Fetches;
		std::vector<unsigned char> spoiled(3000, 0xCD);

		{
			DeferredReadClass defer;

			Check(DeferredReadClass::Deferring(), "a scope lets a read decline");
			Check(volume.Read(entry, 0, spoiled.data(), 3000) == 0, "a declined read delivered nothing");
			Check(defer.Declined(), "the scope reported that the read declined");
		}

		Check(!DeferredReadClass::Deferring(), "the scope let go when it ended");
		Check(source->Fetches == fetched, "a declined read fetched nothing");
		Check(spoiled[0] == 0xCD, "a declined read left the buffer alone");

		/*
		**	The end of the file is also a read of nothing, and must not be mistaken for one
		**	that declined -- that difference is the whole of what tells a caller to come
		**	back later rather than to stop.
		*/
		{
			DeferredReadClass defer;

			Check(volume.Read(entry, 3000, spoiled.data(), 16) == 0, "reading past the end delivered nothing");
			Check(!defer.Declined(), "reading past the end did not report a decline");
		}

		/*
		**	A caller that must not be declined says so, and any scope around it stands
		**	aside for as long as it says.
		*/
		{
			DeferredReadClass defer;
			{
				DeferredReadClass insist(false);

				Check(!DeferredReadClass::Deferring(), "an inner scope suspended the deferral");
				Check(volume.Read(entry, 0, actual.data(), 3000) == 3000, "the insisting read was answered");
			}

			Check(DeferredReadClass::Deferring(), "the deferral came back when the inner scope ended");
			Check(!defer.Declined(), "the insisting read reported no decline");
		}
	}

	std::printf("\n%s\n", Failures == 0 ? "blocksource: all checks passed" : "blocksource: FAILURES");
	return(Failures == 0 ? 0 : 1);
}
