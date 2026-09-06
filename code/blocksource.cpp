/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Uses nothing but the standard library so that it builds for the browser
// target and for a native test harness alongside the Win32 engine.

#include "blocksource.h"


BlockFileClass::BlockFileClass(void)
{
}


BlockFileClass::~BlockFileClass(void)
{
	Close();
}


bool BlockFileClass::Attach_Whole(std::unique_ptr<BlockSourceClass> source, std::uint64_t size, BlockEntryClass & entry)
{
	Close();

	entry.Reset();

	if (!source || size == 0 || size > 0xFFFFFFFFULL) return(false);

	Source = std::move(source);

	entry.IsDirectory = false;
	entry.Size = (std::uint32_t)size;
	entry.Extents.push_back(BlockExtentType{0, entry.Size});

	return(true);
}


void BlockFileClass::Close(void)
{
	Source.reset();
}


/// <summary>
/// Returns the number of bytes delivered, short at the end of the file. A
/// declined deferred read delivers nothing, so the caller's position does not
/// move and asking again reads the same run.
/// </summary>
int BlockFileClass::Read(BlockEntryClass const & entry, std::uint32_t offset, void * buffer, unsigned int length) const
{
	if (!Source || buffer == nullptr) return(0);
	if (entry.IsDirectory || offset >= entry.Size) return(0);

	if (length > entry.Size - offset) {
		length = entry.Size - offset;
	}

	unsigned int total = 0;
	std::uint32_t skip = offset;

	for (BlockExtentType const & extent : entry.Extents) {
		if (length == 0) break;

		if (skip >= extent.Length) {
			skip -= extent.Length;
			continue;
		}

		unsigned int available = extent.Length - skip;
		unsigned int chunk = length < available ? length : available;

		std::uint64_t at = (std::uint64_t)extent.Start * BLOCK_SECTOR_SIZE + skip;

		if (!Source->Read_At(at, (char *)buffer + total, chunk)) {

			if (DeferredReadClass::Declined_Now()) return(0);
			break;
		}

		total += chunk;
		length -= chunk;
		skip = 0;
	}

	return((int)total);
}


/// <summary>
/// Hints exactly the bytes a Read of the same span would ask for. Each extent
/// is hinted separately because ECMA-119 does not require a file's runs to be
/// adjacent.
/// </summary>
void BlockFileClass::Hint(BlockEntryClass const & entry, BlockHintType kind, std::uint32_t offset, std::uint32_t length) const
{
	if (!Source) return;
	if (entry.IsDirectory || offset >= entry.Size) return;

	if (length > entry.Size - offset) {
		length = entry.Size - offset;
	}

	std::uint32_t skip = offset;

	for (BlockExtentType const & extent : entry.Extents) {
		if (length == 0) break;

		if (skip >= extent.Length) {
			skip -= extent.Length;
			continue;
		}

		std::uint32_t const available = extent.Length - skip;
		std::uint32_t const chunk = length < available ? length : available;

		Source->Hint(kind, (std::uint64_t)extent.Start * BLOCK_SECTOR_SIZE + skip, chunk);

		length -= chunk;
		skip = 0;
	}
}


bool BlockFileClass::Prefetch(BlockEntryClass const & entry, std::uint32_t offset, std::uint32_t length) const
{
	if (!Source) return(false);
	if (entry.IsDirectory || offset >= entry.Size) return(false);

	if (length > entry.Size - offset) {
		length = entry.Size - offset;
	}

	bool banked = true;
	std::uint32_t skip = offset;

	for (BlockExtentType const & extent : entry.Extents) {
		if (length == 0) break;

		if (skip >= extent.Length) {
			skip -= extent.Length;
			continue;
		}

		std::uint32_t const available = extent.Length - skip;
		std::uint32_t const chunk = length < available ? length : available;

		if (!Source->Prefetch((std::uint64_t)extent.Start * BLOCK_SECTOR_SIZE + skip, chunk)) {
			banked = false;
		}

		length -= chunk;
		skip = 0;
	}

	return(banked);
}


// The innermost scope a read on this thread is inside.
thread_local DeferredReadClass * DeferredReadClass::Innermost = nullptr;


DeferredReadClass::DeferredReadClass(bool defer) :
	IsDeferring(defer),
	IsDeclined(false),
	Outer(Innermost)
{
	Innermost = this;
}


DeferredReadClass::~DeferredReadClass(void)
{
	Innermost = Outer;
}


bool DeferredReadClass::Deferring(void)
{
	return(Innermost != nullptr && Innermost->IsDeferring);
}


bool DeferredReadClass::Declined_Now(void)
{
	return(Innermost != nullptr && Innermost->IsDeclined);
}


void DeferredReadClass::Decline(void)
{
	if (Innermost != nullptr) Innermost->IsDeclined = true;
}


