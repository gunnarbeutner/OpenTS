/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Block sources for a browser build. Game data stays on a web server and is
// read with HTTP range requests, one source per fetched object.

#pragma once

#include "blocksource.h"

#if defined(__EMSCRIPTEN__)

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>


// The unit an image is fetched, cached and stored in; the store holds nothing
// else.
enum {
	BLOCK_UNIT_SIZE = 65536
};


// What the browser's database holds for one image. The record is written beside
// the blocks in one transaction, so a transaction that fails leaves neither.
class BlockIndexClass
{
	public:

		// A read block may displace the oldest block held; a guessed block is
		// declined by a full store.
		enum AdmitType {
			ADMIT_READ,
			ADMIT_GUESS
		};

		BlockIndexClass(void);

		/// <summary>Builds the key that says which image a stored block belongs
		/// to.</summary>
		/// <param name="location">Where the image was asked for, made absolute;
		/// never the URL a redirect ended at.</param>
		/// <returns>The key, or an empty string when the image cannot be
		/// identified.</returns>
		static std::string Signature(char const * location, std::uint64_t length);

		/// <summary>Builds the key the store holds one image's blocks and
		/// record under.</summary>
		/// <param name="location">Where the image was asked for, made
		/// absolute.</param>
		/// <returns>The key, or an empty string when the image cannot be
		/// identified.</returns>
		static std::string Store_Slot(char const * location);

		void Reset(std::string const & signature);

		/// <summary>Takes on a stored record, if it was written for this
		/// image.</summary>
		/// <returns>bool; May the blocks the record lists be served? A false
		/// leaves the index empty, and the stored blocks must go.</returns>
		bool Adopt(char const * record, std::string const & signature);

		std::string Encode(void) const;

		bool Holds(std::uint64_t index) const {return(Held.count(index) != 0);}

		std::size_t Held_Count(void) const {return(Held.size());}

		/// <summary>Records a block as stored.</summary>
		/// <param name="evicted">Receives the blocks that must be deleted to
		/// make room.</param>
		void Note(std::uint64_t index, std::uint64_t size, std::vector<std::uint64_t> & evicted,
			AdmitType how = ADMIT_READ);

		/// <summary>Stops serving blocks a write turned out not to have
		/// stored.</summary>
		void Forget(std::vector<std::uint64_t> const & indices);

		/// <summary>Sets how much of this image may be kept, or leaves it when
		/// zero; whatever does not fit goes into evicted.</summary>
		void Cap(std::uint64_t bytes, std::vector<std::uint64_t> & evicted);

		std::uint64_t Cap(void) const {return(Ceiling);}

		std::uint64_t Bytes(void) const {return(Total);}
		std::size_t Count(void) const {return(Order.size());}
		std::string const & Key(void) const {return(Sig);}

		// The ceiling per image when the browser will not report its quota,
		// sized to hold a manifest's archives.
		static constexpr std::uint64_t STORE_LIMIT = 160ull * 1024ull * 1024ull;

		// The most one image may be given when the browser does report its
		// quota.
		static constexpr std::uint64_t STORE_MAX = 512ull * 1024ull * 1024ull;

		// The share of the origin's quota one image may take.
		static constexpr double STORE_SHARE = 0.125;

		// A record holds the key and the block list of a full store: four
		// thousand blocks at no more than twenty characters each.
		static constexpr std::size_t SIGNATURE_MAX = 512;
		static constexpr std::size_t RECORD_MAX = 262144;

	private:

		struct EntryType {
			std::uint64_t Index;
			std::uint64_t Size;
		};

		std::string Sig;
		std::uint64_t Total;
		std::uint64_t Ceiling;
		std::vector<EntryType> Order;
		std::unordered_set<std::uint64_t> Held;
};


// The text of one probe's answer, kept per location so a launch whose locations
// are unchanged does not probe again. Nothing here fetches or stores.
class BlockProbeClass
{
	public:
		BlockProbeClass(void);

		/// <summary>Takes on a stored record.</summary>
		/// <returns>bool; Does it describe an image? A false leaves the record
		/// empty.</returns>
		bool Decode(char const * text);

		std::string Encode(void) const;

		std::uint64_t Length;

		// Milliseconds and bytes a millisecond; zero means never measured.
		double Trip;
		double Rate;

		// Room for the length, the two figures, and a trailing field this build
		// ignores.
		static constexpr std::size_t RECORD_MAX = 1024;
};


// Estimates the round trip and the byte rate of the link to one image from the
// requests the engine makes anyway. Nothing here fetches or waits.
class BlockLinkClass
{
	public:
		BlockLinkClass(void);

		void Reset(void);

		/// <summary>Takes in what one completed request cost.</summary>
		void Note(std::uint64_t bytes, double milliseconds);

		/// <summary>Takes on what an earlier run measured of the same link;
		/// installed only where nothing has been measured yet.</summary>
		void Seed(double trip, double rate);

		double Trip(void) const {return(Round);}
		double Rate(void) const {return(Speed);}
		bool Measured(void) const {return(Round > 0.0 && Speed > 0.0);}

		/// <summary>How many blocks a run keeps in front of itself: the
		/// bandwidth-delay product, doubled, since a refill is asked for once
		/// the window is half spent.</summary>
		unsigned int Window(void) const;

		/// <summary>How many blocks one request asks for: the window split into
		/// a fixed number of requests.</summary>
		unsigned int Span(void) const;

		/// <summary>How many requests one image may have outstanding; a long
		/// trip is allowed more.</summary>
		unsigned int Flights(void) const;

		/// <summary>How many bytes are worth taking rather than paying another
		/// trip for: one round trip's worth.</summary>
		std::uint64_t Reach(void) const;

		enum {
			WINDOW_MIN = 2,		// Blocks in front of a run whatever the link costs.
			WINDOW_MAX = 128,	// Bounds one run's reach at eight megabytes.
			SPAN_MIN = 8,		// Blocks per request, so the first lands early.
			SPAN_MAX = 32,		// One request for two megabytes.
			SPLIT = 4,			// Requests a full window is asked for in.
			FLIGHTS_MIN = 4,	// Requests outstanding per image.
			FLIGHTS_MAX = 8
		};

		// A request no larger than TRIP_MAX is all round trip; one at least
		// RATE_MIN has a rate in it worth reading. Between them a request says
		// nothing.
		static constexpr std::uint64_t TRIP_MAX = 8192;
		static constexpr std::uint64_t RATE_MIN = 32768;

		// The least transfer time, in milliseconds, a rate is read out of: a
		// shorter one is timer jitter, not evidence of a faster link.
		static constexpr double RATE_FLOOR = 8.0;

		// How far one reading moves an estimate towards a smaller and a larger
		// value. Falling fast finds the link's floor; rising slowly keeps one
		// queued request from widening every window behind it.
		static constexpr double FALL = 0.34;
		static constexpr double RISE = 0.10;

		// How far out of step with the estimate one reading may be before it is
		// treated as a property of the moment rather than of the link.
		static constexpr double SURGE = 4.0;

		// Round trips a window covers, and how long a trip has to be, in
		// milliseconds, before one request at a time stops filling the link.
		static constexpr double COVER = 2.0;
		static constexpr double CROWD = 60.0;

	private:

		static double Follow(double current, double sample);

		double Round;
		double Speed;
};


// Where a run of reads is heading and how far in front of it fetching may go. A
// run reaches no further than the reading has covered, so a burst that stops
// has over-read by at most what it read. Nothing here fetches.
class BlockReadAheadClass
{
	public:
		BlockReadAheadClass(void);

		void Reset(void);

		/// <summary>Takes on a run the file layer has declared, which is
		/// believed at once.</summary>
		void Begin(std::uint64_t first, std::uint64_t stop);

		/// <summary>Has this run been declared, and does it end where one
		/// says?</summary>
		bool Bounded(void) const {return(Stop != 0);}
		std::uint64_t Limit(void) const {return(Stop);}

		/// <summary>Would a read starting at this block carry on the run? A
		/// read in the block the run has just been in, or the one after it,
		/// continues it.</summary>
		bool Continues(std::uint64_t first) const;

		/// <summary>Follows a read to the blocks it covered; a read the run
		/// does not continue begins it again where the read landed.</summary>
		void Note(std::uint64_t first, std::uint64_t last);

		/// <summary>Reports the span in front of the cursor worth asking for,
		/// as its first block and its count; false when there is
		/// none.</summary>
		bool Span(std::uint64_t blocks, unsigned int window, unsigned int span,
			std::uint64_t & start, std::uint64_t & count) const;

		/// <summary>Records that every block below one has been asked for or
		/// found.</summary>
		void Issued(std::uint64_t upto);

		unsigned int Run(void) const {return(Length);}
		std::uint64_t Cursor(void) const {return(Next);}
		std::uint64_t Edge(void) const {return(Filled);}

		enum {
			RUN_MIN = 2,		// Reads before an undeclared run is believed.

			// Least reach of a declared run whatever the link measures: a
			// window narrower than a reader's own top-up read never gets in
			// front of it.
			BOUND_MIN = 64,

			// And no less than this many times the last read, so a reader
			// taking large bites is fetched further ahead than it consumes in
			// one.
			BOUND_READS = 4
		};

	private:

		std::uint64_t Next;
		std::uint64_t Filled;
		std::uint64_t Wide;
		std::uint64_t From;
		std::uint64_t Stop;
		unsigned int Length;
};


// The runs an image is being read along at once, so a mission's own reads do
// not end a streaming clip's run. A read joins the run it carries on or takes
// over the one that has gone longest without one.
class BlockReadRunsClass
{
	public:
		BlockReadRunsClass(void);

		void Reset(void);

		/// <summary>Follows a read to the run it belongs to, which becomes the
		/// current one.</summary>
		/// <returns>bool; Did the read displace a run with a span outstanding?
		/// lost and stop then bound that span.</returns>
		bool Note(std::uint64_t first, std::uint64_t last, std::uint64_t & lost, std::uint64_t & stop);

		/// <summary>Remembers a run of blocks the file layer says is one file.
		/// It is acted on only when a read starts a run inside it.</summary>
		void Declare(std::uint64_t first, std::uint64_t stop);

		BlockReadAheadClass & Current(void) {return(Runs[Order[0]]);}
		BlockReadAheadClass const & Current(void) const {return(Runs[Order[0]]);}

		// Four runs is a clip streaming beside a mission's map, artwork and
		// music reads; a fifth stream takes the oldest over.
		enum {
			RUNS = 4,

			// Declared files remembered while they wait for a read; the oldest
			// goes when the set is full.
			BOUNDS = 8
		};

	private:

		struct BoundType {
			std::uint64_t First;
			std::uint64_t Stop;
		};

		/// <summary>Finds the declared file a read has landed in.</summary>
		/// <returns>The block that file ends before, or zero when none covers
		/// it.</returns>
		std::uint64_t Bound(std::uint64_t first) const;

		BlockReadAheadClass Runs[RUNS];
		std::size_t Order[RUNS];
		BoundType Declared[BOUNDS];
		std::size_t Written;
};


// Serves an image out of a URL with synchronous range requests, keeping fetched
// blocks in the browser's database for the next run. A server that answers a
// range with the whole image is rejected, since every read would then cost the
// whole file.
class HttpBlockSourceClass : public BlockSourceClass
{
	public:
		HttpBlockSourceClass(void);
		virtual ~HttpBlockSourceClass(void) override;

		HttpBlockSourceClass(HttpBlockSourceClass const &) = delete;
		HttpBlockSourceClass & operator = (HttpBlockSourceClass const &) = delete;

		/// <summary>Opens an image for reading.</summary>
		/// <param name="size">Its length when the caller knows it, which the manifest
		/// states for every image; zero asks the server instead.</param>
		bool Open(char const * url, std::uint64_t size = 0);
		void Close(void);
		bool Is_Open(void) const {return(Length != 0);}

		/// <summary>The key the store holds this image's blocks and record
		/// under.</summary>
		std::string const & Store_Key(void) const {return(Slot);}

		/// <summary>The key that says whether stored blocks still belong to
		/// this image.</summary>
		std::string const & Store_Signature(void) const {return(Signature);}

		/// <summary>Was this image opened out of a stored record rather than a
		/// probe?</summary>
		bool Recalled(void) const {return(FromRecord);}

		virtual bool Read_At(std::uint64_t offset, void * buffer, unsigned int length) override;
		virtual std::uint64_t Total_Size(void) override {return(Length);}
		virtual void Hint(BlockHintType kind, std::uint64_t offset, std::uint64_t length) override;
		virtual bool Prefetch(std::uint64_t offset, unsigned int length) override
			{return(Prefetch_Now(offset, length));}

		// Counted from the index rather than the store, so this costs nothing: what
		// the index lists is what a read would be answered from without the network.
		virtual std::uint64_t Stored_Bytes(void) const override
			{return((std::uint64_t)Index.Held_Count() * (std::uint64_t)BLOCK_SIZE);}

		/// <summary>Drains what every open image's background fetch has
		/// delivered into the store and writes back what has been staged a
		/// while. Called from Browser_Service whether or not anything was
		/// read.</summary>
		static void Service_All(void);

		// A read shorter than BLOCK_SIZE is served from a fetched block;
		// BLOCK_CACHE of them are kept, which bounds the windows at two
		// megabytes.
		enum {
			BLOCK_SIZE = BLOCK_UNIT_SIZE,
			BLOCK_CACHE = 32
		};

	private:

		struct BlockType {
			std::uint64_t Index;
			std::vector<unsigned char> Data;
		};

		// The read a fetch is serving, which says where the engine was waiting;
		// what is fetched for it may start earlier and run longer.
		struct ReadType {
			std::uint64_t Offset;
			unsigned int Length;

			/// <summary>The bytes of a fetched span this read is the one
			/// waiting for.</summary>
			ReadType Within(std::uint64_t offset, unsigned int length) const
			{
				std::uint64_t const first = (Offset > offset) ? Offset : offset;
				std::uint64_t const stop = offset + length;
				std::uint64_t const last = (Offset + Length < stop) ? (Offset + Length) : stop;

				if (last <= first) return(*this);

				return(ReadType{first, (unsigned int)(last - first)});
			}
		};

		// Whether the run has reached the point where the database may be
		// waited on at all.
		enum StoreStateType {
			STORE_UNTRIED,
			STORE_READY,

			// Writes refused; what is already held is still served.
			STORE_FULL,
			STORE_OFF
		};

		// Blocks staged before the batch is written, which bounds what an
		// unwritten batch costs and what a stopped run loses.
		enum {
			STORE_BATCH = 32
		};

		// How long a partly filled batch waits for the loading to resume before
		// it is written anyway, in milliseconds.
		static constexpr double STORE_IDLE = 250.0;

		// Least time between drains, in milliseconds; Service_All is reached
		// from every wait in the engine, not once a frame.
		static constexpr double SERVICE_INTERVAL = 16.0;

		// Runs one image may be told it will want, and the bytes they may cost;
		// the engine decides how much of a run is worth taking (see
		// PrefetchType).
		enum {
			SOON_QUEUE = 64
		};

		static constexpr std::uint64_t SOON_BUDGET = BlockIndexClass::STORE_MAX;

		// How many gaps one queued file may be cut into, so a second attempt
		// asks for the holes rather than the whole file.
		enum {
			SOON_RUNS = 8
		};

		// Blocks moved into the store per read, held to what one read can
		// afford to copy.
		enum {
			SOON_KEEP = 16
		};

	// The share of fetched bytes that may be unread guesses; over it the window
	// closes to a single request until the reading catches up.
	static constexpr double WASTE_SHARE = 0.10;
	static constexpr std::uint64_t WASTE_FLOOR = 1024ull * 1024ull;

		bool Transfer(std::uint64_t offset, void * buffer, unsigned int length,
			ReadType const & read);
		bool Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length,
			ReadType const & read);
		BlockType const * Block(std::uint64_t index, ReadType const & read);

		void Look_Ahead(void);
		void Ahead_Want(std::uint64_t offset, unsigned int length);

		/// <summary>Fetches a run synchronously and banks the blocks it covers
		/// in the store.</summary>
		/// <returns>bool; Did every block the run covers reach the
		/// store?</returns>
		/// <remarks>Suspends, so it is legal only after
		/// Block_Store_Mark_Main.</remarks>
		bool Prefetch_Now(std::uint64_t offset, unsigned int length);

		bool Ahead_Pending(std::uint64_t offset, unsigned int length);
		bool Ahead_Serve(std::uint64_t offset, void * buffer, unsigned int length,
			ReadType const & read);
		void Ahead_Drop(void);
		void Ahead_Drop(std::uint64_t first, std::uint64_t stop);
		void Soon(std::uint64_t offset, std::uint64_t length);
		void Soon_Keep(void);

		/// <summary>Asks the server what the image is, and keeps the
		/// answer.</summary>
		/// <returns>bool; Did it answer a ranged request and report the
		/// length?</returns>
		bool Probe(void);

		/// <summary>Writes what is known about the image back to where a later
		/// run reads it.</summary>
		void Learn(void);

		/// <summary>Re-establishes an image a stored record described wrongly;
		/// the record is dropped either way.</summary>
		/// <returns>bool; Was the record wrong and the image re-established
		/// from the server?</returns>
		bool Revive(void);


		bool Store_Ready(void);
		bool Store_Serve(std::uint64_t offset, void * buffer, unsigned int length,
			ReadType const & read);
		void Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length,
			BlockIndexClass::AdmitType how);
		void Store_Write(void);
		void Store_Drop(std::vector<std::uint64_t> const & evicted);
		void Store_Discard(void);

		/// <summary>Writes any open image's batch that has been left sitting,
		/// since an image the game has finished with has no reads of its own to
		/// flush it.</summary>
		static void Store_Settle(void);

		std::string Url;

		// The absolute form of the location, which identifies the image; the
		// URL a redirect ends at does not.
		std::string Location;

		std::uint64_t Length;
		std::vector<BlockType> Cache;

		// Index of this image's figures; a block number means nothing without
		// its image.
		std::size_t Meter;

		BlockReadRunsClass Ahead;
		BlockLinkClass Link;
		std::uint64_t Queued;

		// Whether the image was taken from a stored record, and when that
		// record last had this run's figures written into it.
		bool FromRecord;
		double Learned;

		// Least time between writes of the link estimates back to the record,
		// in milliseconds.
		static constexpr double LEARN_IDLE = 10000.0;

		std::string Signature;
		std::string Slot;
		std::string Removals;
		BlockIndexClass Index;
		StoreStateType StoreState;
		unsigned int Staged;
		double StagedAt;

		// Blocks of the unwritten batch, which a refused write leaves unstored.
		std::vector<std::uint64_t> Staging;

};


/// <summary>Says that main has been entered, which is the only place a
/// suspending wait on the store's database is legal. Called once from the top
/// of main.</summary>
void Block_Store_Mark_Main(void);

/// <summary>Drains every open image's background fetch into the store; see
/// HttpBlockSourceClass::Service_All.</summary>
void Block_Source_Service(void);


#endif
