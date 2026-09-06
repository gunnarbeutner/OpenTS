/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A whole-file volume over a block source, and the deferred-read scope the
// layers above it share. Nothing here touches the operating system beyond the
// block source, and nothing parses a disc image or a directory tree.

#pragma once

#include "blocksource.hh"

#include <cstdint>
#include <memory>
#include <vector>


// The only requirement is a synchronous read at an absolute offset, which a
// range-request transport can satisfy.
class BlockSourceClass
{
	public:
		virtual ~BlockSourceClass(void) {}

		/// <summary>
		/// Returns true only when every requested byte was delivered.
		/// </summary>
		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) = 0;

		/// <summary>
		/// Returns the size of the image in bytes, or zero when it is not
		/// known.
		/// </summary>
		virtual std::uint64_t Total_Size(void) = 0;

		/// <summary>
		/// Advisory notice of what a run is about to be used for. A source that
		/// fetches over a network reads ahead within the run and never past its
		/// end.
		/// </summary>
		virtual void Hint(BlockHintType kind, std::uint64_t offset, std::uint64_t length)
			{(void)kind; (void)offset; (void)length;}

		/// <summary>
		/// Fetches a run and waits until it is banked, so a later read of it
		/// costs nothing; returns false when the source has nowhere to bank it.
		/// </summary>
		virtual bool Prefetch(std::uint64_t offset, unsigned int length)
			{(void)offset; (void)length; return(false);}

		// How many bytes of this source a persistent store already holds, which
		// is zero for a source that keeps nothing between runs.
		virtual std::uint64_t Stored_Bytes(void) const {return(0);}
};


// A scope, entered around one read of an open file, in which the read may
// decline because the bytes are not here yet. A declined read delivers zero
// bytes and leaves the file position alone; Declined() tells it from the end
// of the file. Constructing with defer false suspends any enclosing scope.
class DeferredReadClass
{
	public:
		explicit DeferredReadClass(bool defer = true);
		~DeferredReadClass(void);

		DeferredReadClass(DeferredReadClass const &) = delete;
		DeferredReadClass & operator = (DeferredReadClass const &) = delete;

		bool Declined(void) const {return(IsDeclined);}

		static bool Deferring(void);

		/// <summary>
		/// Whether a read inside the innermost scope declined; false when there
		/// is no scope.
		/// </summary>
		static bool Declined_Now(void);

		static void Decline(void);

	private:

		bool IsDeferring;
		bool IsDeclined;
		DeferredReadClass * Outer;

		static thread_local DeferredReadClass * Innermost;
};


// A file normally has one extent, but ECMA-119 splits anything past the four
// gigabyte extent limit across several records.
struct BlockExtentType {
	std::uint32_t Start;   // First logical block of the run.
	std::uint32_t Length;  // Bytes this run contributes.
};


class BlockEntryClass
{
	public:
		BlockEntryClass(void) : IsDirectory(false), Size(0), DateTime(0) {}

		bool Is_Valid(void) const {return(!Extents.empty());}
		void Reset(void) {IsDirectory = false; Size = 0; DateTime = 0; Extents.clear();}

		bool IsDirectory;
		std::uint32_t Size;                   // Bytes across every extent.
		unsigned int DateTime;                // As Get_Date_Time packs it.
		std::vector<BlockExtentType> Extents;
};


class BlockFileClass
{
	public:
		BlockFileClass(void);
		~BlockFileClass(void);

		BlockFileClass(BlockFileClass const &) = delete;
		BlockFileClass & operator = (BlockFileClass const &) = delete;

		/// <summary>
		/// Attaches a source that is one whole file, with no volume descriptor
		/// or directory tree to parse.
		/// </summary>
		bool Attach_Whole(std::unique_ptr<BlockSourceClass> source, std::uint64_t size,
			BlockEntryClass & entry);

		void Close(void);

		bool Is_Open(void) const {return(Source != nullptr);}

		int Read(BlockEntryClass const & entry, std::uint32_t offset, void * buffer, unsigned int length) const;
		void Hint(BlockEntryClass const & entry, BlockHintType kind, std::uint32_t offset, std::uint32_t length) const;

		/// <summary>
		/// Fetches a run of an entry and waits until it is banked; returns
		/// false when the source has nowhere to bank it.
		/// </summary>
		bool Prefetch(BlockEntryClass const & entry, std::uint32_t offset, std::uint32_t length) const;

		/// <summary>How many bytes of this volume a persistent store already holds.</summary>
		std::uint64_t Stored_Bytes(void) const
			{return(Source ? Source->Stored_Bytes() : 0);}

	private:

		std::unique_ptr<BlockSourceClass> Source;
};
