/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The browser half of the file layer's data path. ReadFile is synchronous and
// the web is not, so the transport is a synchronous request: a suspending fetch
// is legal only under a promising export, and the engine's first file open
// happens in a static constructor.

#include "httpsource.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <vector>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>


// Reports the bytes delivered, or a negative value for a failed request or one
// answered without the range asked for.
EM_JS(int, Block_Http_Transfer, (char const * url, double offset, void * buffer, unsigned int length, double * cached), {
	HEAPF64[cached >> 3] = 0;

	try {
		var where = UTF8ToString(url);
		var request = new XMLHttpRequest();
		request.open("GET", where, false);
		request.setRequestHeader("Range", "bytes=" + offset + "-" + (offset + length - 1));

		var astext = false;
		try {
			request.responseType = "arraybuffer";
		} catch (error) {
			astext = true;
		}
		if (astext) request.overrideMimeType("text/plain; charset=x-user-defined");

		request.send(null);

		// An answer from the browser's HTTP cache has no network time in it;
		// transferSize is 0 for exactly that case, so the caller is told not to
		// read a rate out of it.
		try {
			var entries = performance.getEntriesByName(where, "resource");
			var last = entries.length > 0 ? entries[entries.length - 1] : null;
			if (last && last.transferSize === 0) HEAPF64[cached >> 3] = 1;
		} catch (error) {}

		if (request.status !== 206) return -1;

		if (!astext) {
			var bytes = new Uint8Array(request.response);
			var count = Math.min(bytes.length, length);
			HEAPU8.set(bytes.subarray(0, count), buffer);
			return count;
		}

		var text = request.responseText;
		var written = Math.min(text.length, length);
		for (var index = 0; index < written; index++) {
			HEAPU8[buffer + index] = text.charCodeAt(index) & 255;
		}
		return written;
	} catch (error) {
		return -1;
	}
});


// The location made absolute against the document, which identifies the image;
// the URL a redirect ends at names a node, not the image. A host with no
// document resolves nothing.
EM_JS(void, Block_Http_Identity, (char const * url, char * identity, int size), {
	var text = UTF8ToString(url);

	try {
		text = new URL(text, location.href).href;
	} catch (error) {}

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[identity + count] = (code > 126 || code < 32) ? 63 : code;
		count++;
	}
	HEAPU8[identity + count] = 0;
});


// Answers the image's length, which with the location identifies the image to a
// later run. The entity tag is deliberately not read: it changes under bytes
// that do not.
EM_JS(double, Block_Http_Probe, (char const * url), {
	try {
		var request = new XMLHttpRequest();
		request.open("GET", UTF8ToString(url), false);
		request.setRequestHeader("Range", "bytes=0-0");
		request.send(null);
		if (request.status !== 206) return -1;

		var range = request.getResponseHeader("Content-Range");
		if (!range) return -1;

		var total = range.split("/")[1];
		if (!total || total === "*") return -1;

		return parseFloat(total);
	} catch (error) {
		return -1;
	}
});


// What a probe learned goes in local storage rather than the block database,
// because it must be readable before the engine has a wait to spend. A browser
// that will not hand it over reports nothing, and the run probes.

EM_JS(int, Block_Probe_Recall, (char const * key, char * record, int size), {
	HEAPU8[record] = 0;

	try {
		var text = localStorage.getItem(UTF8ToString(key));
		if (typeof text !== "string" || text.length === 0) return 0;

		var count = 0;
		while (count < text.length && count + 1 < size) {
			var code = text.charCodeAt(count);
			HEAPU8[record + count] = (code > 126 || code < 32) ? 63 : code;
			count++;
		}
		HEAPU8[record + count] = 0;
		return 1;
	} catch (error) {
		return 0;
	}
});


EM_JS(void, Block_Probe_Remember, (char const * key, char const * record), {
	try {
		localStorage.setItem(UTF8ToString(key), UTF8ToString(record));
	} catch (error) {}
});


EM_JS(void, Block_Probe_Forget, (char const * key), {
	try {
		localStorage.removeItem(UTF8ToString(key));
	} catch (error) {}
});


// Checks, from a timer nobody waits on, that the image is still as long as the
// record said, and reloads the page when it is not: a grown image answers every
// range, so no read would notice. A cross-origin answer without Content-Range
// is not evidence.


//------------------------------------------------------------------------------
// The persisted store. Everything below waits on the browser's database, so all
// of it is behind the suspension scaffold.
#if defined(OPENTS_WASM_JSPI)

// Whether a suspending wait is legal yet, which Block_Store_Mark_Main sets from
// the top of main.
EM_JS(int, Block_Store_Under_Main, (void), {
	return (globalThis.__opentsIsoStore && globalThis.__opentsIsoStore.main) ? 1 : 0;
});


EM_JS(void, Block_Store_Set_Main, (void), {
	var state = globalThis.__opentsIsoStore;

	if (state === undefined) {
		state = Object.create(null);

		// The OPFS directory the store's files live under, opened once.
		state.root = null;
		state.usable = false;
		state.given = false;

		// Whether the origin has refused a write; reads carry on.
		state.full = false;

		// What the origin says it may hold, once asked; negative until then.
		state.room = -1;

		// Whether the sweep below has already run this session, and how recently a file
		// must have been written to be left alone. "?sweepgrace=0" closes that window,
		// which is how a check watches the sweep remove something it has just planted.
		state.swept = false;
		state.sweepGrace = 3600 * 1000;

		try {
			var asked = new URLSearchParams(location.search).get("sweepgrace");
			if (asked !== null && isFinite(Number(asked))) state.sweepGrace = Number(asked);
		} catch (error) {}

		// One staged batch and the present-block set, each per image.
		state.staged = new Map();
		state.present = new Map();

		// A stable, filesystem-safe file-name stem for an image's slot.
		state.key = function (slot) {
			var hash = 5381;
			for (var i = 0; i < slot.length; i++) hash = ((hash << 5) + hash + slot.charCodeAt(i)) >>> 0;
			return "iso-" + ("0000000" + hash.toString(16)).slice(-8) + "-" + slot.length;
		};

		globalThis.__opentsIsoStore = state;
	}

	state.main = true;
});


// Opens the shared database and hands back the record for one image; a database
// that cannot be opened reports nothing. Version 1 keyed blocks by number
// alone, so the upgrade clears it.
EM_ASYNC_JS(int, Block_Store_Open, (char const * slot, char * record, int size), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || state.given) return 0;

	try {
		if (state.root === null) {
			var root = await navigator.storage.getDirectory();
			state.root = await root.getDirectoryHandle("opents-iso", { create: true });
		}

		var slotstr = UTF8ToString(slot);
		var key = state.key(slotstr);

		var text = "";
		try {
			var meta = await state.root.getFileHandle(key + ".meta", { create: false });
			text = await (await meta.getFile()).text();
		} catch (error) {}

		// The record's third line is the block list the engine keeps, so the blocks on
		// disk are read from it rather than from a bitmap saying the same thing twice.
		// Only a well formed pair counts: a line this cannot read costs a refetch, where
		// an index invented from a bad one would be served as the zeros a hole holds.
		var present = new Set();
		var listed = text.split("\n")[2];

		if (listed) {
			listed.split(",").forEach(function (pair) {
				if (/^[0-9]+:[0-9]+$/.test(pair)) present.add(parseInt(pair, 10));
			});
		}

		state.present.set(slotstr, present);

		// Written by a build that kept the block list twice; nothing reads it now.
		try { await state.root.removeEntry(key + ".idx"); } catch (error) {}

		// The record is copied a byte at a time, so it is held to ASCII plus
		// its own line ends.
		var count = 0;
		while (count < text.length && count + 1 < size) {
			var code = text.charCodeAt(count);
			var plain = (code === 10) || (code >= 32 && code <= 126);
			HEAPU8[record + count] = plain ? code : 63;
			count++;
		}
		HEAPU8[record + count] = 0;

		state.usable = true;
		return 1;
	} catch (error) {
		state.root = null;
		state.usable = false;
		return 0;
	}
});


// What the origin is allowed to store, in bytes, or zero when it will not say.
// It is the allowance rather than what is left of it, so the store does not
// size itself from a figure it moves.
EM_ASYNC_JS(double, Block_Store_Room, (void), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null) return 0;
	if (state.room >= 0) return state.room;

	state.room = 0;

	try {
		if (navigator.storage && typeof navigator.storage.estimate === "function") {
			var estimate = await navigator.storage.estimate();

			if (estimate && typeof estimate.quota === "number" && estimate.quota > 0) {
				state.room = estimate.quota;
			}
		}
	} catch (error) {}

	return state.room;
});


// Reads a span in one transaction, serving staged blocks from the batch. A
// block is keyed by image and number.
// 1 delivered, 0 not held -- an answer the caller pays for over the network --
// and -1 storage that has stopped answering, which gives the store up.
EM_ASYNC_JS(int, Block_Store_Read, (char const * slot, double offset, void * buffer, unsigned int length, unsigned int blocksize), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.root === null) return -1;

	try {
		var slotstr = UTF8ToString(slot);

		var batch = state.staged.get(slotstr);
		var present = state.present.get(slotstr);
		var first = Math.floor(offset / blocksize);
		var last = Math.floor((offset + length - 1) / blocksize);

		var fromFile = false;
		for (var index = first; index <= last; index++) {
			var staged = batch && batch.has(index);
			if (!staged && !(present && present.has(index))) return 0;
			if (!staged) fromFile = true;
		}

		var spanStart = first * blocksize;
		var combined = new Uint8Array((last + 1) * blocksize - spanStart);

		if (fromFile) {
			var handle = await state.root.getFileHandle(state.key(slotstr) + ".dat", { create: false });
			var file = await handle.getFile();
			var to = Math.min(file.size, (last + 1) * blocksize);
			var got = (to > spanStart)
				? new Uint8Array(await file.slice(spanStart, to).arrayBuffer())
				: new Uint8Array(0);

			combined.set(got, 0);

			// The record and the file can disagree -- a batch noted but never written, a
			// file truncated under us -- and a short read would otherwise be handed back as
			// zeros with nothing to say the bytes are missing. Reporting a miss sends the
			// caller to the network instead.
			for (var check = first; check <= last; check++) {
				if (batch && batch.has(check)) continue;

				var covered = spanStart + got.length;
				var wanted = Math.min((check + 1) * blocksize, offset + length);

				if (covered < wanted) return 0;
			}
		}

		if (batch) {
			for (var staged = first; staged <= last; staged++) {
				if (batch.has(staged)) combined.set(batch.get(staged), staged * blocksize - spanStart);
			}
		}

		HEAPU8.set(combined.subarray(offset - spanStart, offset - spanStart + length), buffer);
		return 1;
	} catch (error) {
		return -1;
	}
});


// Copies a block until the batch is written; the heap moves when it grows and
// the buffer is the engine's.
EM_JS(int, Block_Store_Stage, (char const * slot, double index, void const * buffer, unsigned int length), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.root === null) return 0;

	try {
		var key = UTF8ToString(slot);
		var batch = state.staged.get(key);

		if (!batch) {
			batch = new Map();
			state.staged.set(key, batch);
		}

		batch.set(index, HEAPU8.slice(buffer, buffer + length));
		return 1;
	} catch (error) {
		return 0;
	}
});


// Writes the batch, its evictions and the record. The blocks land before the
// record that names them, so an interrupted write leaves the record behind the
// file rather than ahead of it, and what it omits is fetched again. Quota
// exhaustion is remembered for every image and stops writes; reads carry on.
//
// The block size is passed rather than inferred: a batch holding only the
// archive's short final block would otherwise name its own length as the size
// and write every block in it at the wrong offset.
EM_ASYNC_JS(int, Block_Store_Write, (char const * slot, char const * record, char const * removals, unsigned int blocksize), {
	var state = globalThis.__opentsIsoStore;
	if (state === undefined || state === null || !state.usable || state.root === null) return 0;
	if (state.full) {
		state.staged.delete(UTF8ToString(slot));
		return -1;
	}

	var slotstr = UTF8ToString(slot);
	var key = state.key(slotstr);
	var text = UTF8ToString(record);
	var drop = UTF8ToString(removals);

	try {
		var batch = state.staged.get(slotstr);
		var present = state.present.get(slotstr);
		if (!present) { present = new Set(); state.present.set(slotstr, present); }

		// Only this image's blocks are ever dropped.
		if (drop === "*") {
			try { await state.root.removeEntry(key + ".dat"); } catch (error) {}
			present.clear();
		} else if (drop.length > 0) {
			drop.split(",").forEach(function (number) {
				var index = parseFloat(number);
				if (!batch || !batch.has(index)) present.delete(index);
			});
		}

		if (batch && batch.size > 0 && blocksize > 0) {
			var data = await (await state.root.getFileHandle(key + ".dat", { create: true })).createWritable({ keepExistingData: true });
			var entries = Array.from(batch.entries());
			for (var e = 0; e < entries.length; e++) {
				await data.write({ type: "write", position: entries[e][0] * blocksize, data: entries[e][1] });
				present.add(entries[e][0]);
			}
			await data.close();
		}

		var meta = await (await state.root.getFileHandle(key + ".meta", { create: true })).createWritable();
		await meta.write(text);
		await meta.close();

		state.staged.delete(slotstr);
		return 1;
	} catch (error) {
		state.staged.delete(slotstr);

		if ((error && error.name) === "QuotaExceededError") {
			state.full = true;
			return -1;
		}

		// Anything else is storage that has stopped answering, so it is given up
		// for every image for the rest of the run.
		state.usable = false;
		state.given = true;
		return 0;
	}
});

// Lets go of a batch that will not be written, which is the only thing teardown
// may do.
// Removes what no archive this release names can reach any more. A slot is named for the
// asset URL it was read from, and every release gives its archives new content-addressed
// URLs, so each one strands a whole set of files that nothing will open again. Left alone
// they never go: the store only ever touches a slot it has opened.
//
// Returns the number of files removed, or -1 when it declined to look.
EM_ASYNC_JS(int, Block_Store_Sweep, (void), {
	var state = globalThis.__opentsIsoStore;
	if (!state || state.swept || !state.usable || state.root === null) return -1;

	var manifest = globalThis.__opentsManifest;
	var base = globalThis.__opentsManifestBase;
	if (!manifest || !base) return -1;

	var live = new Set();

	Object.keys(manifest).forEach(function (section) {
		var group = manifest[section];
		if (!Array.isArray(group)) return;

		group.forEach(function (entry) {
			if (entry && typeof entry["path"] === "string") {
				live.add(state.key(new URL(entry["path"], base).href));
			}
		});
	});

	// An empty set would condemn every file here. That is what a manifest read too early
	// looks like, so it is treated as "ask again later" rather than as "nothing is live".
	if (live.size === 0) return -1;

	state.swept = true;

	// A second tab is mid-session on its own release, and its slots look stale from here.
	// Anything written recently is left for whoever is writing it. The window is named on
	// the state so a test can close it; nothing in the engine changes it.
	var settled = Date.now() - state.sweepGrace;
	var removed = 0;

	try {
		var stale = [];

		for await (var handle of state.root.values()) {
			// Character classes rather than "\\d" and "\\.": this body is a C string
			// first, which eats a backslash escape it does not know and leaves a pattern
			// that matches nothing.
			var match = /^(iso-[0-9a-f]{8}-[0-9]+)[.](dat|idx|meta)$/.exec(handle.name);
			if (!match || live.has(match[1])) continue;

			var when = 0;
			try { when = (await handle.getFile()).lastModified; } catch (error) {}
			if (when > settled) continue;

			stale.push(handle.name);
		}

		// The record goes before the blocks it names: a sweep that stops halfway leaves a
		// slot holding data nothing claims, which the next open discards by itself. The
		// other order would leave a record naming blocks that are gone.
		stale.sort();

		for (var pass = 0; pass < 3; pass++) {
			var suffix = [".meta", ".idx", ".dat"][pass];

			for (var index = 0; index < stale.length; index++) {
				if (!stale[index].endsWith(suffix)) continue;
				try { await state.root.removeEntry(stale[index]); removed++; } catch (error) {}
			}
		}
	} catch (error) {
		return removed;
	}

	return removed;
});


EM_JS(void, Block_Store_Forget, (char const * slot), {
	var state = globalThis.__opentsIsoStore;
	if (state !== undefined && state !== null && state.staged) state.staged.delete(UTF8ToString(slot));
});


// A host reading the discs off local storage has no use for a second copy.
EM_JS(int, Block_Store_Wanted, (void), {
	return (typeof Module !== "undefined" && Module.opentsLocalDiscs) ? 0 : 1;
});


//------------------------------------------------------------------------------
// The look-ahead pool: a request left in flight and then the bytes it
// delivered. Starting one never suspends, and the answer lands while the page
// has the thread.

// Builds the pool, which everything below assumes exists.
EM_JS(void, Block_Http_Ahead_Ready, (unsigned int blocksize), {
	if (globalThis.__opentsIsoAhead !== undefined) return;

	var pool = {
		spans: new Map(),

		// What one image has been told it will probably want.
		plans: new Map(),

		active: 0,
		timer: null,
		waste: 0,

		// How much of a guess may be held unread, in bytes.
		HOLD: 12 * 1024 * 1024,
		// Guesses outstanding at once over every image: a page gets about six
		// connections to an origin, and the synchronous reading must always
		// find one free.
		GUESSES: 2,

		// The unit stored and served, which a part-delivered span is cut back
		// to.
		BLOCK: blocksize,

		find: function (key, offset, length) {
			var list = this.spans.get(key);
			if (!list) return null;

			for (var index = 0; index < list.length; index++) {
				var span = list[index];
				if (span.start <= offset && offset + length <= span.stop) return span;
			}
			return null;
		},

		// Answers from what has arrived, not from whether the request finished.
		ready: function (span, offset, length) {
			return span.data !== null && offset >= span.start &&
				offset + length <= span.start + span.filled;
		},

		// Wakes the waiters whose bytes have arrived; a finished span wakes
		// them all, since a wait never answered would hold the engine.
		wake: function (span) {
			var edge = span.start + span.filled;
			var keep = [];

			for (var index = 0; index < span.waiters.length; index++) {
				if (span.done || span.waiters[index].need <= edge) {
					span.waiters[index].resolve(0);
				} else {
					keep.push(span.waiters[index]);
				}
			}

			span.waiters = keep;
		},

		forget: function (key, span) {
			var list = this.spans.get(key);
			if (list) {
				var at = list.indexOf(span);
				if (at >= 0) list.splice(at, 1);
			}
		},

		// Answers what was received and never read; bytes that never arrived
		// are not waste.
		drop: function (key, span) {
			this.forget(key, span);

			try { span.control.abort(); } catch (error) {}

			span.done = true;
			this.wake(span);

			var unread = span.filled - span.used;
			return (unread > 0) ? unread : 0;
		},

		// Abandons a span still arriving but keeps its whole blocks as a guess
		// of its own; only the part block at the end is lost.
		release: function (key, span) {
			try { span.control.abort(); } catch (error) {}

			span.done = true;

			var edge = span.start + span.filled;
			var whole = Math.floor(edge / this.BLOCK) * this.BLOCK;

			if (whole > span.start + span.taken) {
				var waste = edge - whole;

				span.stop = whole;
				span.filled = whole - span.start;
				span.ok = true;
				span.idle = true;
				this.wake(span);
				return (waste > 0) ? waste : 0;
			}

			this.wake(span);
			return this.drop(key, span);
		},

		copy: function (key, span, offset, buffer, length) {
			if (!this.ready(span, offset, length)) {
				if (span.done) this.waste += this.drop(key, span);
				return 0;
			}

			var at = offset - span.start;
			HEAPU8.set(span.data.subarray(at, at + length), buffer);

			// Reads only move forward, so used is what the span has given up.
			if (at + length > span.used) span.used = at + length;
			if (span.used >= span.stop - span.start) this.drop(key, span);
			return 1;
		},

		// Stops further guessing at once; what is in flight is left to finish,
		// since its trip is paid for and its bytes go to the store.
		yield: function (key) {
			this.active = (typeof performance === "object") ? performance.now() : Date.now();
		},

		// A guess never waits for the reading to stop, or on a distant link it
		// would never be made; the count is held over every image so a read
		// never queues behind a guess.
		flying: function () {
			var count = 0;

			this.spans.forEach(function (list) {
				for (var index = 0; index < list.length; index++) {
					if (list[index].idle && !list[index].done) count++;
				}
			});

			return count;
		},

		drain: function () {
			var pool = this;
			var flying = pool.flying();

			this.plans.forEach(function (plan, key) {
				if (plan.ranges.length === 0) return;

				var list = pool.spans.get(key) || [];
				var held = 0;

				for (var index = 0; index < list.length; index++) {
					if (!list[index].idle || !list[index].done) continue;
					held += list[index].filled - list[index].used;
				}

				if (held >= pool.HOLD) return;

				while (flying < pool.GUESSES && plan.ranges.length > 0) {
					var range = plan.ranges[0];
					var take = Math.min(range.span, range.stop - range.at);
					var whole = !!range.whole && take === range.stop - range.at;

					if (pool.open(key, range.at, range.at + take, true, whole) === 0) break;

					plan.spent += take;
					plan.asked++;
					range.at += take;
					if (range.at >= range.stop) plan.ranges.shift();
					flying++;
				}
			});
		},

		// The body is read as it arrives, so a read is answered as soon as its
		// bytes are in the span. A whole-file span asks without a Range header,
		// which a CDN caches most simply.
		open: function (key, start, stop, idle, whole) {
			var list = this.spans.get(key);
			if (!list) {
				list = [];
				this.spans.set(key, list);
			}

			var pool = this;
			var total = stop - start;
			var wanted = whole ? 200 : 206;

			// used is how far a read has walked the span and taken how far the
			// idle drain has; a whole-file span answers reads in any order, so
			// the two cannot share.
			var span = {
				start: start, stop: stop, data: null, filled: 0, done: false, ok: false,
				used: 0, taken: 0, idle: idle, waiters: [], control: new AbortController()
			};

			var take = function (chunk) {
				var room = (span.stop - span.start) - span.filled;
				var count = (chunk.length < room) ? chunk.length : room;

				if (count > 0) {
					span.data.set(chunk.subarray(0, count), span.filled);
					span.filled += count;
					pool.wake(span);
				}
			};

			// No cache override: the URLs are content-hashed and immutable, so
			// the browser's own HTTP cache may answer.
			var init = whole
				? { signal: span.control.signal }
				: { headers: { "Range": "bytes=" + start + "-" + (stop - 1) },
				    signal: span.control.signal };

			span.promise = fetch(key, init)
				.then(function (response) {
			// An answer of the wrong shape is refused before its body is
			// touched.
					if (response.status !== wanted) throw new Error("fetch refused");

					span.data = new Uint8Array(total);

					if (!response.body || typeof response.body.getReader !== "function") {
						return response.arrayBuffer().then(function (delivered) {
							take(new Uint8Array(delivered));
						});
					}

					var reader = response.body.getReader();
					var pump = function () {
						return reader.read().then(function (piece) {
							if (piece.done) return;
							take(piece.value);
							return pump();
						});
					};

					return pump();
				})
				.then(function () {
					span.ok = (span.filled === (span.stop - span.start));
					span.done = true;
					pool.wake(span);
				})
				.catch(function () {
					span.done = true;
					pool.wake(span);
				});

			list.push(span);
			return total;
		}
	};

	globalThis.__opentsIsoAhead = pool;
});


// Reports the bytes asked for, a negative number for a span already on its way,
// and zero for a request declined.
EM_JS(double, Block_Http_Ahead_Start, (char const * url, double offset, double length, int flights), {
	var pool = globalThis.__opentsIsoAhead;

	if (pool === undefined) return 0;
	if (typeof fetch !== "function" || typeof AbortController !== "function") return 0;

	var key = UTF8ToString(url);
	var start = offset;
	var stop = offset + length;

	if (!(stop > start)) return 0;

	pool.active = (typeof performance === "object") ? performance.now() : Date.now();

	var list = pool.spans.get(key);
	if (!list) {
		list = [];
		pool.spans.set(key, list);
	}

	// Nothing already in flight is asked for again; guesses do not count
	// against the window.
	var outstanding = 0;

	for (var index = 0; index < list.length; index++) {
		if (list[index].start < stop && list[index].stop > start) return -1;
		if (!list[index].idle && !list[index].done) outstanding++;
	}
	if (outstanding >= flights) return 0;

	return pool.open(key, start, stop, false);
});


// Says the image is being read and hands back the bytes that cost; a read that
// wants the connection stops further guessing.
EM_JS(double, Block_Http_Ahead_Busy, (char const * url, int give), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	pool.active = (typeof performance === "object") ? performance.now() : Date.now();

	if (give === 0) return 0;

	var before = pool.waste;

	pool.yield(UTF8ToString(url));
	return pool.waste - before;
});


// Queues a run for the drainer, in the order named; a run already covered is
// not queued again.
EM_JS(double, Block_Http_Idle_Add, (char const * url, double offset, double length, double span, int depth, int whole), {
	var pool = globalThis.__opentsIsoAhead;

	if (pool === undefined) return 0;
	if (typeof fetch !== "function" || typeof setInterval !== "function") return 0;
	if (!(length > 0) || !(span > 0)) return 0;

	var key = UTF8ToString(url);

	// A range a span already covers, fetched or on its way, is not queued
	// again, whichever asked for it.
	if (pool.find(key, offset, length) !== null) return 0;

	var plan = pool.plans.get(key);

	if (!plan) {
		plan = { ranges: [], spent: 0, asked: 0 };
		pool.plans.set(key, plan);
	}

	for (var index = 0; index < plan.ranges.length; index++) {
		if (plan.ranges[index].at <= offset && offset + length <= plan.ranges[index].stop) return 0;
	}

	if (plan.ranges.length >= depth) return 0;

	// The span size is kept per range, since ranges queued by different calls
	// wanted different request sizes.
	plan.ranges.push({ at: offset, stop: offset + length, whole: !!whole, span: span });

	if (pool.timer === null) {
		pool.timer = setInterval(function () { pool.drain(); }, 250);
	}

	return length;
});


// Hands over the whole blocks a span holds that nothing has read, marking them
// taken so the span can be let go. Any span, not only an idle one: a declined
// read's span would otherwise sit fetched and unused.
EM_JS(double, Block_Http_Idle_Take, (char const * url, void * buffer, unsigned int max, unsigned int blocksize, double * at), {
	var pool = globalThis.__opentsIsoAhead;

	HEAPF64[at >> 3] = 0;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	for (var index = 0; index < list.length; index++) {
		var span = list[index];

		if (span.data === null) continue;

		var from = span.start + span.taken;
		var first = Math.ceil(from / blocksize) * blocksize;
		var edge = Math.floor((span.start + span.filled) / blocksize) * blocksize;
		var count = Math.min(max, edge - first);

		count -= count % blocksize;

		if (count <= 0) {
			var end = span.start + span.filled;

			// A finished span's short tail past the last whole boundary is
			// handed over as it is; Store_Keep stores it only when it is the
			// file's own last block.
			if (!span.done || !(from < end) || end - from > max) continue;

			HEAPU8.set(span.data.subarray(from - span.start, end - span.start), buffer);
			HEAPF64[at >> 3] = from;

			span.taken = end - span.start;
			if (span.taken > span.used) span.used = span.taken;
			if (span.taken >= span.stop - span.start) pool.forget(key, span);
			return end - from;
		}

		HEAPU8.set(span.data.subarray(first - span.start, first - span.start + count), buffer);
		HEAPF64[at >> 3] = first;

		span.taken = (first + count) - span.start;
		if (span.taken > span.used) span.used = span.taken;
		if (span.done && span.taken >= span.stop - span.start) pool.forget(key, span);
		return count;
	}

	return 0;
});


// Gives up everything one image was told it would want, including spans nothing
// read: a guess that cannot be banked would hold the pool at its limit.
EM_JS(double, Block_Http_Idle_Cancel, (char const * url), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);

	pool.plans.delete(key);

	var list = pool.spans.get(key);
	if (!list) return 0;

	var wasted = 0;

	for (var index = list.length - 1; index >= 0; index--) {
		if (!list[index].idle) continue;
		wasted += pool.drop(key, list[index]);
	}

	return wasted;
});


// Bytes every span still arriving has left to deliver, across every image.
EM_JS(double, Block_Http_Pool_Remaining, (void), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var total = 0;

	pool.spans.forEach(function (list) {
		for (var index = 0; index < list.length; index++) {
			var span = list[index];
			if (!span.done) total += (span.stop - span.start) - span.filled;
		}
	});

	return total;
});


// Bytes the drainer has queued and not yet asked for, across every image.
EM_JS(double, Block_Http_Idle_Outstanding, (void), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var total = 0;

	pool.plans.forEach(function (plan) {
		for (var index = 0; index < plan.ranges.length; index++) {
			total += plan.ranges[index].stop - plan.ranges[index].at;
		}
	});

	return total;
});


// Bytes the drainer asked for since last asked, so a guess is counted where
// other requests are.
EM_JS(double, Block_Http_Idle_Spent, (char const * url, unsigned int * asked), {
	var pool = globalThis.__opentsIsoAhead;

	HEAPU32[asked >> 2] = 0;
	if (pool === undefined) return 0;

	// Drained from here as well as from the timer: no timer runs during a
	// synchronous request, and loading is nearly all of those.
	pool.drain();

	var plan = pool.plans.get(UTF8ToString(url));
	if (!plan) return 0;

	var spent = plan.spent;

	HEAPU32[asked >> 2] = plan.asked;
	plan.spent = 0;
	plan.asked = 0;
	return spent;
});


// 0 nothing, 1 a request still arriving that is within patience, 2 the bytes
// themselves. A finished span without the bytes is let go here. Past patience
// the answer is 0, since asking the transport directly would answer sooner.
EM_JS(int, Block_Http_Ahead_State, (char const * url, double offset, unsigned int length, double patience), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null) return 0;
	if (pool.ready(span, offset, length)) return 2;

	if (!span.done) {
		var gap = offset - (span.start + span.filled);
		return (gap <= patience) ? 1 : 0;
	}

	pool.waste += pool.drop(key, span);
	return 0;
});


// Takes bytes the pool is already holding: a copy and nothing else.
EM_JS(int, Block_Http_Ahead_Copy, (char const * url, double offset, void * buffer, unsigned int length), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null || !span.done) return 0;
	return pool.copy(key, span, offset, buffer, length);
});


// Names the span a profile's prefetch is waiting on, or clears it with an empty
// URL; the page reads the pool's fill for it.
EM_JS(void, Block_Http_Prefetch_Active, (char const * url, double offset, double length), {
	var key = UTF8ToString(url);

	globalThis.__opentsPgoActive = (key === "")
		? null
		: { key: key, start: offset, bytes: length };
});


// Waits for the part of a span a read needs, not the whole request. It
// suspends, so the caller must be under main.
EM_ASYNC_JS(int, Block_Http_Ahead_Wait, (char const * url, double offset, void * buffer, unsigned int length), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var span = pool.find(key, offset, length);

	if (span === null) return 0;

	if (!pool.ready(span, offset, length) && !span.done) {
		await new Promise(function (resolve) {
			span.waiters.push({ need: offset + length, resolve: resolve });
		});
	}

	return pool.copy(key, span, offset, buffer, length);
});


// Bytes the pool holds that no read has taken: speculation the connection has
// paid for. What never arrived is not counted.
EM_JS(double, Block_Http_Ahead_Unread, (void), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var unread = 0;

	pool.spans.forEach(function (list) {
		for (var index = 0; index < list.length; index++) {
			var held = list[index].filled - list[index].used;

			if (held > 0) unread += held;
		}
	});

	return unread;
});


// Lets go of everything outstanding for one image and answers the bytes never
// read. An idle span belongs to the URL rather than to this instance and
// survives a Close.
EM_JS(double, Block_Http_Ahead_Drop, (char const * url), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	var waste = 0;
	var index = list.length;

	while (index-- > 0) {
		if (index >= list.length) continue;
		if (list[index].idle) continue;
		waste += pool.release(key, list[index]);
	}

	return waste;
});


// Lets go of what was asked for in front of one displaced run. An idle span is
// the background precache and is left alone; Soon_Keep drains it and Close
// gives it up.
EM_JS(double, Block_Http_Ahead_Drop_Range, (char const * url, double start, double stop), {
	var pool = globalThis.__opentsIsoAhead;
	if (pool === undefined) return 0;

	var key = UTF8ToString(url);
	var list = pool.spans.get(key);

	if (!list) return 0;

	var waste = 0;

	for (var index = list.length - 1; index >= 0; index--) {
		var span = list[index];
		if (span.idle) continue;
		if (span.start < stop && span.stop > start) waste += pool.release(key, span);
	}

	return waste;
});

#endif	// OPENTS_WASM_JSPI


void Block_Store_Mark_Main(void)
{
#if defined(OPENTS_WASM_JSPI)
	Block_Store_Set_Main();
#endif
}


void Block_Source_Service(void)
{
#if defined(OPENTS_WASM_JSPI)
	HttpBlockSourceClass::Service_All();
#endif
}


// Bumped from opents-iso-1 because a store written before the block size was
// passed in can hold a block at the wrong offset, over data it overwrote. The
// damage cannot be found from the record, so such a store is discarded whole the
// first time this build opens it.
static char const BLOCK_STORE_MAGIC[] = "opents-iso-2";


BlockIndexClass::BlockIndexClass(void) :
	Total(0),
	Ceiling(STORE_LIMIT)
{
}


/// <summary>Builds the key that says which image a stored block belongs to: the
/// location and the length, never the server's entity tag.</summary>
std::string BlockIndexClass::Signature(char const * location, std::uint64_t length)
{
	if (location == nullptr || *location == '\0' || length == 0) return(std::string());

	std::string key(location);

	key += '|';

	char count[32];
	std::snprintf(count, sizeof(count), "%llu", (unsigned long long)length);
	key += count;

	// The key is a line of the stored record and is copied a byte at a time.
	for (char & character : key) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (key.size() > SIGNATURE_MAX) key.resize(SIGNATURE_MAX);
	return(key);
}


/// <summary>Builds the key the store holds one image's blocks and record under:
/// the location alone, so a new version overwrites the old.</summary>
std::string BlockIndexClass::Store_Slot(char const * location)
{
	if (location == nullptr || *location == '\0') return(std::string());

	std::string slot(location);

	for (char & character : slot) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (slot.size() > SIGNATURE_MAX) slot.resize(SIGNATURE_MAX);
	return(slot);
}


void BlockIndexClass::Reset(std::string const & signature)
{
	Sig = signature;
	Total = 0;
	Order.clear();
	Held.clear();
}


/// <summary>Takes on a stored record, if it was written for this
/// image.</summary>
bool BlockIndexClass::Adopt(char const * record, std::string const & signature)
{
	Reset(signature);

	if (record == nullptr || signature.empty()) return(false);

	std::string const text(record);

	std::size_t const magic = text.find('\n');
	if (magic == std::string::npos) return(false);
	if (text.compare(0, magic, BLOCK_STORE_MAGIC) != 0) return(false);

	std::size_t const key = text.find('\n', magic + 1);
	if (key == std::string::npos) return(false);
	if (text.compare(magic + 1, key - magic - 1, signature) != 0) return(false);

	std::size_t cursor = key + 1;

	while (cursor < text.size()) {
		std::size_t stop = text.find(',', cursor);
		if (stop == std::string::npos) stop = text.size();

		std::size_t const colon = text.find(':', cursor);
		if (colon == std::string::npos || colon >= stop) {
			Reset(signature);
			return(false);
		}

		char * end = nullptr;
		unsigned long long const index = std::strtoull(text.c_str() + cursor, &end, 10);
		if (end != text.c_str() + colon) {
			Reset(signature);
			return(false);
		}

		unsigned long long const size = std::strtoull(text.c_str() + colon + 1, &end, 10);
		if (end != text.c_str() + stop || size == 0) {
			Reset(signature);
			return(false);
		}

		if (Held.insert((std::uint64_t)index).second) {
			EntryType entry;
			entry.Index = (std::uint64_t)index;
			entry.Size = (std::uint64_t)size;
			Order.push_back(entry);
			Total += (std::uint64_t)size;
		}

		cursor = stop + 1;
	}

	return(true);
}


std::string BlockIndexClass::Encode(void) const
{
	std::string text(BLOCK_STORE_MAGIC);

	text += '\n';
	text += Sig;
	text += '\n';

	char entry[48];

	for (std::size_t position = 0; position < Order.size(); position++) {
		std::snprintf(entry, sizeof(entry), "%s%llu:%llu", (position != 0) ? "," : "",
			(unsigned long long)Order[position].Index, (unsigned long long)Order[position].Size);
		text += entry;
	}

	return(text);
}


/// <summary>Records a block as stored, evicting oldest first; a guessed block
/// displaces nothing, so a long guessing pass cannot push out what the game
/// read.</summary>
void BlockIndexClass::Note(std::uint64_t index, std::uint64_t size,
	std::vector<std::uint64_t> & evicted, AdmitType how)
{
	if (size == 0 || Held.count(index) != 0) return;
	if (size > Ceiling) return;

	if (how == ADMIT_GUESS) {
		if (Total + size > Ceiling) return;
	} else {
		while (!Order.empty() && Total + size > Ceiling) {
			EntryType const oldest = Order.front();

			Order.erase(Order.begin());
			Held.erase(oldest.Index);
			Total -= oldest.Size;
			evicted.push_back(oldest.Index);
		}

		if (Total + size > Ceiling) return;
	}

	EntryType entry;
	entry.Index = index;
	entry.Size = size;
	Order.push_back(entry);
	Held.insert(index);
	Total += size;
}


/// <summary>Stops serving blocks a write turned out not to have
/// stored.</summary>
void BlockIndexClass::Forget(std::vector<std::uint64_t> const & indices)
{
	for (std::uint64_t index : indices) {
		if (Held.erase(index) == 0) continue;

		for (std::size_t position = 0; position < Order.size(); position++) {
			if (Order[position].Index != index) continue;

			Total -= Order[position].Size;
			Order.erase(Order.begin() + (std::ptrdiff_t)position);
			break;
		}
	}
}


/// <summary>Sets how much of this image may be kept.</summary>
void BlockIndexClass::Cap(std::uint64_t bytes, std::vector<std::uint64_t> & evicted)
{
	if (bytes == 0) return;

	Ceiling = bytes;

	while (!Order.empty() && Total > Ceiling) {
		EntryType const oldest = Order.front();

		Order.erase(Order.begin());
		Held.erase(oldest.Index);
		Total -= oldest.Size;
		evicted.push_back(oldest.Index);
	}
}


// Tells a record this build cannot read from one that is absent; either way the
// image is probed.
static char const * const BLOCK_PROBE_MAGIC = "opents-probe-1";


BlockProbeClass::BlockProbeClass(void) :
	Length(0),
	Trip(0.0),
	Rate(0.0)
{
}


/// <summary>Takes on a stored record. Whatever follows the fourth separator is
/// ignored, so a record with a trailing field still reads.</summary>
bool BlockProbeClass::Decode(char const * text)
{
	Length = 0;
	Trip = 0.0;
	Rate = 0.0;

	if (text == nullptr) return(false);

	std::string const line(text);
	std::size_t cursor = 0;
	std::string field[4];

	for (std::string & part : field) {
		std::size_t const stop = line.find('|', cursor);
		if (stop == std::string::npos) return(false);

		part = line.substr(cursor, stop - cursor);
		cursor = stop + 1;
	}

	if (field[0] != BLOCK_PROBE_MAGIC) return(false);

	char const * const count = field[1].c_str();
	char * end = nullptr;
	double const length = std::strtod(count, &end);

	if (end == count || *end != '\0' || !(length > 0.0)) return(false);

	Length = (std::uint64_t)length;
	Trip = std::strtod(field[2].c_str(), nullptr);
	Rate = std::strtod(field[3].c_str(), nullptr);

	if (!(Trip > 0.0)) Trip = 0.0;
	if (!(Rate > 0.0)) Rate = 0.0;

	return(true);
}


std::string BlockProbeClass::Encode(void) const
{
	char head[128];

	std::snprintf(head, sizeof(head), "%s|%llu|%.3f|%.3f|", BLOCK_PROBE_MAGIC,
		(unsigned long long)Length, Trip, Rate);

	std::string record(head);

	// Copied a byte at a time, so held to printable ASCII.
	for (char & character : record) {
		if (character < 0x20 || character > 0x7E) character = '?';
	}

	if (record.size() > RECORD_MAX - 1) record.resize(RECORD_MAX - 1);
	return(record);
}


// Local storage, not the block database; see Block_Probe_Recall.
static std::string Probe_Key(std::string const & location)
{
	return("opents-iso-probe|" + location);
}


BlockLinkClass::BlockLinkClass(void)
{
	Reset();
}


void BlockLinkClass::Reset(void)
{
	Round = 0.0;
	Speed = 0.0;
}


// Moves an estimate towards a reading, quickly towards a better one and slowly
// towards a worse, so a queued request does not widen every window behind it.
double BlockLinkClass::Follow(double current, double sample)
{
	if (!(sample > 0.0)) return(current);
	if (!(current > 0.0)) return(sample);

	// One reading may say the link is SURGE times better or worse and no more;
	// a changed link says so again on the next reading.
	if (sample > current * SURGE) sample = current * SURGE;
	if (sample < current / SURGE) sample = current / SURGE;

	double const weight = (sample < current) ? FALL : RISE;

	return(current + (sample - current) * weight);
}


/// <summary>Takes in what one completed request cost. A request between
/// TRIP_MAX and RATE_MIN says nothing about either estimate.</summary>
void BlockLinkClass::Note(std::uint64_t bytes, double milliseconds)
{
	if (bytes == 0 || !(milliseconds >= 0.0)) return;

	if (bytes <= TRIP_MAX) {
		Round = Follow(Round, milliseconds);
		return;
	}

	if (bytes >= RATE_MIN && Round > 0.0) {
		double const moving = milliseconds - Round;

		if (moving >= RATE_FLOOR) Speed = Follow(Speed, (double)bytes / moving);
	}
}


/// <summary>Takes on what an earlier run measured of the same link.</summary>
void BlockLinkClass::Seed(double trip, double rate)
{
	if (Round <= 0.0 && trip > 0.0) Round = trip;
	if (Speed <= 0.0 && rate > 0.0) Speed = rate;
}


/// <summary>How many blocks a run keeps in front of itself.</summary>
unsigned int BlockLinkClass::Window(void) const
{
	if (!Measured()) return((unsigned int)WINDOW_MIN);

	double const bytes = COVER * Round * Speed;
	double const blocks = bytes / (double)BLOCK_UNIT_SIZE;

	if (!(blocks > (double)WINDOW_MIN)) return((unsigned int)WINDOW_MIN);
	if (blocks >= (double)WINDOW_MAX) return((unsigned int)WINDOW_MAX);

	return((unsigned int)(blocks + 0.5));
}


/// <summary>How many blocks one request asks for.</summary>
unsigned int BlockLinkClass::Span(void) const
{
	unsigned int const split = (Window() + (unsigned int)SPLIT - 1) / (unsigned int)SPLIT;

	if (split <= (unsigned int)SPAN_MIN) return((unsigned int)SPAN_MIN);
	if (split >= (unsigned int)SPAN_MAX) return((unsigned int)SPAN_MAX);

	return(split);
}


/// <summary>How many requests one image may have outstanding.</summary>
unsigned int BlockLinkClass::Flights(void) const
{
	unsigned int extra = 0;

	if (Round > 0.0) extra = (unsigned int)(Round / CROWD);

	unsigned int const flights = (unsigned int)FLIGHTS_MIN + extra;

	return((flights > (unsigned int)FLIGHTS_MAX) ? (unsigned int)FLIGHTS_MAX : flights);
}


/// <summary>How many bytes are worth taking rather than paying another trip
/// for.</summary>
std::uint64_t BlockLinkClass::Reach(void) const
{
	std::uint64_t const floor = (std::uint64_t)BLOCK_UNIT_SIZE;
	std::uint64_t const ceiling = (std::uint64_t)SPAN_MAX * (std::uint64_t)BLOCK_UNIT_SIZE;

	if (!Measured()) return(floor);

	double const bytes = Round * Speed;

	if (!(bytes > (double)floor)) return(floor);
	if (bytes >= (double)ceiling) return(ceiling);

	return((std::uint64_t)bytes);
}


BlockReadAheadClass::BlockReadAheadClass(void)
{
	Reset();
}


void BlockReadAheadClass::Reset(void)
{
	Next = 0;
	Filled = 0;
	Wide = 0;
	From = 0;
	Stop = 0;
	Length = 0;
}


/// <summary>Takes on a run the file layer has declared.</summary>
void BlockReadAheadClass::Begin(std::uint64_t first, std::uint64_t stop)
{
	if (stop <= first) {
		Reset();
		return;
	}

	Next = first;
	Filled = first;
	Wide = 0;
	From = first;
	Stop = stop;

	// Believed at once; the window is still the smallest, since nothing has
	// been read yet.
	Length = (unsigned int)RUN_MIN;
}


/// <summary>Would a read carry on where this run has reached?</summary>
bool BlockReadAheadClass::Continues(std::uint64_t first) const
{
	return((Length != 0) && (first + 1 >= Next) && (first <= Next));
}


/// <summary>Follows a read to the blocks it covered.</summary>
void BlockReadAheadClass::Note(std::uint64_t first, std::uint64_t last)
{
	bool const continues = Continues(first);

	Wide = last - first + 1;

	if (continues) {
		if (last + 1 > Next) {
			Length++;
			Next = last + 1;
		}
	} else {
		Length = 1;
		Next = last + 1;
		Filled = Next;
		From = first;
		Stop = 0;
	}

	if (Filled < Next) Filled = Next;
}


/// <summary>Reports the span in front of the cursor worth asking for.</summary>
bool BlockReadAheadClass::Span(std::uint64_t blocks, unsigned int window, unsigned int span,
	std::uint64_t & start, std::uint64_t & count) const
{
	if (Length < (unsigned int)RUN_MIN) return(false);
	if (span == 0 || window == 0) return(false);

	std::uint64_t end = blocks;

	if (Bounded() && Stop < end) end = Stop;
	if (Filled >= end) return(false);

	// An undeclared run whose reads already span a request is not reached in
	// front of; a declared run cannot run into the next file, so it is.
	if (!Bounded() && Wide >= (std::uint64_t)span) return(false);

	// An undeclared run reaches no further than it has covered. A declared run
	// opens the whole window at once and no less than BOUND_MIN, since a window
	// narrower than the reader's own reads never gets in front of it.
	std::uint64_t reach = (std::uint64_t)window;
	std::uint64_t chunk = (std::uint64_t)span;

	if (!Bounded()) {
		std::uint64_t const covered = Next - From;

		if (covered < reach) reach = covered;
		if (reach < 2) reach = 2;
	} else {
		std::uint64_t const bites = Wide * (std::uint64_t)BOUND_READS;

		if (reach < (std::uint64_t)BOUND_MIN) reach = (std::uint64_t)BOUND_MIN;
		if (reach < bites) reach = bites;

		// One request asks for more than one read takes.
		if (chunk < Wide * 2) chunk = Wide * 2;
		if (chunk > reach) chunk = reach;
	}

	// Refilled once half spent, so a refill is one request for many blocks
	// rather than a round trip per block.
	if (Filled >= Next + reach / 2) return(false);

	std::uint64_t stop = Next + reach;
	if (stop > Filled + chunk) stop = Filled + chunk;
	if (stop > end) stop = end;
	if (stop <= Filled) return(false);

	start = Filled;
	count = stop - Filled;
	return(true);
}


void BlockReadAheadClass::Issued(std::uint64_t upto)
{
	if (upto > Filled) Filled = upto;
}


BlockReadRunsClass::BlockReadRunsClass(void)
{
	Reset();
}


void BlockReadRunsClass::Reset(void)
{
	for (std::size_t place = 0; place < (std::size_t)RUNS; place++) {
		Runs[place].Reset();
		Order[place] = place;
	}

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		Declared[place].First = 0;
		Declared[place].Stop = 0;
	}

	Written = 0;
}


/// <summary>Takes in a run of blocks the file layer says is one file.</summary>
void BlockReadRunsClass::Declare(std::uint64_t first, std::uint64_t stop)
{
	if (stop <= first) return;

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		if (Declared[place].First == first && Declared[place].Stop == stop) return;
	}

	Declared[Written].First = first;
	Declared[Written].Stop = stop;
	Written = (Written + 1) % (std::size_t)BOUNDS;
}


/// <summary>Finds the declared file a read has landed in.</summary>
std::uint64_t BlockReadRunsClass::Bound(std::uint64_t first) const
{
	std::uint64_t found = 0;

	for (std::size_t place = 0; place < (std::size_t)BOUNDS; place++) {
		BoundType const & bound = Declared[place];

		if (bound.Stop <= bound.First) continue;
		if (first < bound.First || first >= bound.Stop) continue;

		// The narrowest declaration is the file being read: a mixfile and a
		// file inside it are both declared.
		if (found == 0 || bound.Stop < found) found = bound.Stop;
	}

	return(found);
}


/// <summary>Follows a read to the run it belongs to, which becomes the current
/// one. Displacing the run that has gone longest without a read bounds the set;
/// only that run's outstanding span is reported.</summary>
bool BlockReadRunsClass::Note(std::uint64_t first, std::uint64_t last, std::uint64_t & lost, std::uint64_t & stop)
{
	std::size_t place = 0;

	while (place < (std::size_t)RUNS && !Runs[Order[place]].Continues(first)) place++;

	bool displaced = false;

	std::uint64_t bound = 0;

	if (place == (std::size_t)RUNS) {
		place = (std::size_t)RUNS - 1;

		BlockReadAheadClass & oldest = Runs[Order[place]];

		if (oldest.Edge() > oldest.Cursor()) {
			lost = oldest.Cursor();
			stop = oldest.Edge();
			displaced = true;
		}

		oldest.Reset();

		// A read starting inside a declared file takes that file's end and is
		// believed from this first read.
		bound = Bound(first);
		if (bound > last + 1) oldest.Begin(first, bound);
	}

	if (place != 0) {
		std::size_t const chosen = Order[place];

		for (std::size_t step = place; step > 0; step--) Order[step] = Order[step - 1];
		Order[0] = chosen;
	}

	Runs[Order[0]].Note(first, last);
	return(displaced);
}


// Requests and Fetched count every source; Touched marks the block windows
// requested per image, which together are the working set.
static unsigned int _Requests = 0;
static std::uint64_t _Fetched = 0;
static std::vector<std::vector<bool>> _Touched;

// Every open image, so one can flush a batch another is holding.
static std::vector<HttpBlockSourceClass *> _Open;

// Hits and Bytes are what the store answered, Kept what was written, State what
// became of it.
static unsigned int _StoreHits = 0;
static std::uint64_t _StoreBytes = 0;
static unsigned int _StoreKept = 0;
static unsigned int _StoreSwept = 0;
static bool _Swept = false;
static unsigned int _StoreState = 0;
static unsigned int _StoreDiscarded = 0;

// Requests and Bytes are the part asked for before anything wanted it; Served
// the reads answered and Waited those that reached the read first; Waste the
// bytes no read took.
static unsigned int _AheadRequests = 0;
static std::uint64_t _AheadBytes = 0;
static unsigned int _AheadServed = 0;
static unsigned int _AheadWaited = 0;
static std::uint64_t _AheadWaste = 0;

// The part of that asked for on the engine's word, which a player can be left
// waiting on for nothing.
static unsigned int _SoonRequests = 0;
static std::uint64_t _SoonBytes = 0;

// Runs the file layer named, Soon the files it said would be wanted, Done the
// runs it gave up.
static unsigned int _HintRuns = 0;
static unsigned int _HintSoon = 0;
static unsigned int _HintDone = 0;

// Reads that declined rather than stalled.
static unsigned int _Deferred = 0;

// Reads that stopped the engine and for how long; a read the cache or a landed
// span answered is not counted.
static unsigned int _Stalls = 0;
static double _StallMs = 0.0;
static double _StallWorst = 0.0;
static std::uint64_t _StallWorstAt = 0;
static unsigned int _StallWorstLength = 0;

// A wait on the store is a stall the same as a request is.
enum StallSourceType {
	STALL_TRANSFER,		// A synchronous request the engine waited on.
	STALL_AHEAD,		// A look-ahead span that had not landed yet.
	STALL_STORE			// The browser's database.
};

// The location of every image opened, so a stall names the image.
static std::vector<std::string> _Images;


// Hands one stall to the page as a line on OpenTS_State.stalls, written from
// the module's own glue because the page cannot read the heap. Only the leading
// number is meant to be parsed; a gap in it says lines were dropped.
EM_JS(void, Block_Stall_Record, (char const * line, double seconds), {
	var state = globalThis.OpenTS_State;

	if (state === undefined || state === null) {
		state = {};
		globalThis.OpenTS_State = state;
	}

	if (!state.stalls) state.stalls = [];

	state.stalls.push(UTF8ToString(line));
	state.stallSeconds = seconds;

	// The oldest go once the array is full; the sequence number says so.
	if (state.stalls.length > 4096) state.stalls.splice(0, state.stalls.length - 4096);
});


static void Account_For_Stall(std::size_t meter, std::uint64_t offset, unsigned int length,
	double milliseconds, StallSourceType source)
{
	if (!(milliseconds > 0.0)) return;

	double const began = emscripten_get_now() - milliseconds;

	_Stalls++;
	_StallMs += milliseconds;

	if (milliseconds > _StallWorst) {
		_StallWorst = milliseconds;
		_StallWorstAt = offset;
		_StallWorstLength = length;
	}

	// Named by the last part of the location; whitespace would split the line
	// into the wrong number of fields.
	std::string name = (meter < _Images.size()) ? _Images[meter] : std::string();
	std::size_t const cut = name.find_last_of('/');

	if (cut != std::string::npos) name.erase(0, cut + 1);
	if (name.size() > 64) name.erase(64);
	if (name.empty()) name = "image";

	for (char & letter : name) {
		if ((unsigned char)letter <= ' ') letter = '_';
	}

	char line[192];

	std::snprintf(line, sizeof(line), "%u %.3f %s %llu %u %.1f %s", _Stalls, began / 1000.0,
		name.c_str(), (unsigned long long)offset, length, milliseconds,
		(source == STALL_AHEAD) ? "ahead" : "demand");

	Block_Stall_Record(line, _StallMs / 1000.0);
}

// The link as whichever image measured last; the images share one origin.
static double _LinkTrip = 0.0;
static unsigned int _LinkWindow = 0;

// How many images were probed and how many recalled from the browser.
static unsigned int _Probes = 0;
static unsigned int _Recalls = 0;


static std::size_t Account_For_Image(char const * url)
{
	_Touched.emplace_back();
	_Images.emplace_back(url != nullptr ? url : "");
	return(_Touched.size() - 1);
}


static void Account_For_Transfer(std::size_t meter, std::uint64_t offset, unsigned int length)
{
	_Requests++;
	_Fetched += length;

	if (meter >= _Touched.size()) return;

	std::vector<bool> & touched = _Touched[meter];
	std::uint64_t const first = offset / HttpBlockSourceClass::BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / HttpBlockSourceClass::BLOCK_SIZE;

	if (touched.size() <= last) {
		touched.resize((std::size_t)last + 1, false);
	}

	for (std::uint64_t index = first; index <= last; index++) {
		touched[(std::size_t)index] = true;
	}
}


extern "C" {

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Requests(void) {return(_Requests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Fetched(void) {return((double)_Fetched);}

/// <summary>Bytes every span still arriving has left to deliver, which a page
/// showing progress wants over a rate.</summary>
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Bytes_Remaining(void)
{
#if defined(OPENTS_WASM_JSPI)
	return(Block_Http_Pool_Remaining());
#else
	return(0.0);
#endif
}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Block_Size(void) {return(HttpBlockSourceClass::BLOCK_SIZE);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Images(void) {return((unsigned int)_Touched.size());}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Unique_Blocks(void)
{
	unsigned int count = 0;

	for (std::vector<bool> const & image : _Touched) {
		for (bool touched : image) {
			if (touched) count++;
		}
	}

	return(count);
}

// 0 never reached, 1 serving, 2 given up, 3 serving what it has after the
// origin refused a write.
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_State(void) {return(_StoreState);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Hits(void) {return(_StoreHits);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Store_Bytes(void) {return((double)_StoreBytes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Kept(void) {return(_StoreKept);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Swept(void) {return(_StoreSwept);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Store_Discarded(void) {return(_StoreDiscarded);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Requests(void) {return(_AheadRequests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Ahead_Bytes(void) {return((double)_AheadBytes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Served(void) {return(_AheadServed);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Ahead_Waited(void) {return(_AheadWaited);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Ahead_Waste(void)
{
#if defined(OPENTS_WASM_JSPI)
	return((double)_AheadWaste + Block_Http_Ahead_Unread());
#else
	return((double)_AheadWaste);
#endif
}

// Time the engine sat in reads: the count, the total and the worst. A span that
// landed before it was wanted counts as nothing.
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Stalls(void) {return(_Stalls);}

// Reads answered without their bytes rather than stalled.
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Deferred(void) {return(_Deferred);}

EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Ms(void) {return(_StallMs);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Worst(void) {return(_StallWorst);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Stall_Worst_Offset(void) {return((double)_StallWorstAt);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Stall_Worst_Length(void) {return(_StallWorstLength);}

// The stalls themselves go to OpenTS_State.stalls as lines of text; see
// Block_Stall_Record.

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Runs(void) {return(_HintRuns);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Done(void) {return(_HintDone);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Hint_Soon(void) {return(_HintSoon);}

EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Soon_Requests(void) {return(_SoonRequests);}
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Soon_Bytes(void) {return((double)_SoonBytes);}

/// <summary>What the background fetch still has to deliver: the drainer's queue
/// plus what is in flight.</summary>
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Background_Left(void)
{
#if defined(OPENTS_WASM_JSPI)
	return(Block_Http_Idle_Outstanding() + Block_Http_Pool_Remaining());
#else
	return(0.0);
#endif
}

// A launch whose locations are unchanged reports only recalls.
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Probes(void) {return(_Probes);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Recalls(void) {return(_Recalls);}

// The round trip in milliseconds and the blocks a run reaches in front of
// itself. The rate is not reported: a run's few samples are only a floor the
// window sizing reads out of.
EMSCRIPTEN_KEEPALIVE double OpenTS_Iso_Link_Trip(void) {return(_LinkTrip);}
EMSCRIPTEN_KEEPALIVE unsigned int OpenTS_Iso_Link_Window(void) {return(_LinkWindow);}

}


HttpBlockSourceClass::HttpBlockSourceClass(void) :
	Length(0),
	Meter(0),
	Queued(0),
	FromRecord(false),
	Learned(0.0),
	StoreState(STORE_UNTRIED),
	Staged(0),
	StagedAt(0.0)
{
}


HttpBlockSourceClass::~HttpBlockSourceClass(void)
{
	Close();
}


/// <summary>Asks the server what the image is, and keeps the answer.</summary>
bool HttpBlockSourceClass::Probe(void)
{
	double const began = emscripten_get_now();
	double const length = Block_Http_Probe(Url.c_str());

	_Probes++;

	if (!(length > 0.0)) return(false);

	// One byte, so the cost is a round trip: the only reading available before
	// the engine has read anything.
	Link.Note(1, emscripten_get_now() - began);
	_LinkTrip = Link.Trip();

	Length = (std::uint64_t)length;

	Learn();
	return(true);
}


/// <summary>Writes what is known about the image back to where a later run
/// reads it.</summary>
void HttpBlockSourceClass::Learn(void)
{
	if (Location.empty() || Length == 0) return;

	BlockProbeClass known;

	known.Length = Length;
	known.Trip = Link.Trip();
	known.Rate = Link.Rate();

	Block_Probe_Remember(Probe_Key(Location).c_str(), known.Encode().c_str());
	Learned = emscripten_get_now();
}


/// <summary>Opens an image served from a URL, absolute or relative to the
/// page.</summary>
/// <returns>bool; Is the image's length known, from a stored record or from the
/// server?</returns>
/// <remarks>Reached from a static constructor, where a wait is not yet legal,
/// so the store is not opened here and the record comes from a synchronous
/// store.</remarks>
bool HttpBlockSourceClass::Open(char const * url, std::uint64_t size)
{
	Close();

	if (url == nullptr || *url == '\0') return(false);

	Url = url;

	std::vector<char> identity(BlockIndexClass::SIGNATURE_MAX);
	identity[0] = '\0';
	Block_Http_Identity(Url.c_str(), identity.data(), (int)identity.size());

	Location = (identity[0] != '\0') ? identity.data() : Url;

	// Everything a probe would answer, from an earlier run at this location.
	BlockProbeClass known;
	std::vector<char> record(BlockProbeClass::RECORD_MAX);

	record[0] = '\0';
	FromRecord = (Block_Probe_Recall(Probe_Key(Location).c_str(), record.data(),
		(int)record.size()) == 1) && known.Decode(record.data());

	if (FromRecord) {
		_Recalls++;
		Length = known.Length;
		Link.Seed(known.Trip, known.Rate);
		_LinkTrip = Link.Trip();
		_LinkWindow = Link.Window();
	} else if (size != 0) {
		Length = size;

		// Every image is named for its own content hash, so the manifest's length is the
		// length; a probe would spend a round trip re-reading it. The first one is still
		// worth making, because the round trip it costs is also the only measurement of
		// the link available before anything has been read.
		if (_Probes == 0) Probe();
	} else if (!Probe()) {
		Url.clear();
		Location.clear();
		return(false);
	}

	Meter = Account_For_Image(Url.c_str());
	_Open.push_back(this);

	Signature = BlockIndexClass::Signature(Location.c_str(), Length);
	Slot = BlockIndexClass::Store_Slot(Location.c_str());

	return(true);
}


/// <summary>Reloads the page, for an image caught having changed underneath a
/// running run.</summary>
EM_JS(void, Block_Reload_Page, (void), {
	globalThis.location.reload();
});


/// <summary>Re-establishes an image a stored record described
/// wrongly.</summary>
bool HttpBlockSourceClass::Revive(void)
{
	if (!FromRecord) return(false);

	// Once, whatever comes of it: the record is gone either way.
	FromRecord = false;
	Block_Probe_Forget(Probe_Key(Location).c_str());

	std::uint64_t const believed = Length;

	if (!Probe()) return(false);
	if (Length == believed) return(false);

	// Some of what was read came from another file; reloading stops that
	// reaching what has not been read yet.
	Block_Reload_Page();
	return(false);
}


void HttpBlockSourceClass::Close(void)
{
	// Dropped rather than written: Close is reached from the destructor, where
	// a wait may not be legal.
	Store_Discard();
	Ahead_Drop();

	_Open.erase(std::remove(_Open.begin(), _Open.end(), this), _Open.end());

	Url.clear();
	Location.clear();
	Signature.clear();
	Slot.clear();
	Removals.clear();
	Length = 0;
	Cache.clear();
	Index.Reset(std::string());
	Ahead.Reset();
	Link.Reset();
	Queued = 0;
	FromRecord = false;
	Learned = 0.0;
	StoreState = STORE_UNTRIED;
	Staged = 0;
}


//------------------------------------------------------------------------------
// The look-ahead as the source uses it. Without the suspension scaffold the
// engine never hands the page the thread, so nothing left in flight could land.

/// <summary>Asks for the run of missing blocks in front of the cursor, without
/// waiting; blocks the store holds are stepped over.</summary>
void HttpBlockSourceClass::Look_Ahead(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (Length == 0) return;

	// Before main nothing yields, so a request left in flight could never land.
	if (Block_Store_Under_Main() == 0) return;

	Block_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	unsigned int window = Link.Window();
	unsigned int span = Link.Span();

	// Over the waste share the window closes to a single request, the least
	// that still covers a round trip.
	if (_Fetched > WASTE_FLOOR) {
		double const unread = (double)_AheadWaste + Block_Http_Ahead_Unread();

		if (unread > WASTE_SHARE * (double)_Fetched) {
			window = (unsigned int)BlockLinkClass::SPAN_MIN;
			span = (unsigned int)BlockLinkClass::SPAN_MIN;
		}
	}

	// The connection belongs to the reading, so guesses are let go rather than
	// left in front of it.
	double const given = Block_Http_Ahead_Busy(Url.c_str(), 1);

	if (given > 0.0) _AheadWaste += (std::uint64_t)given;

	std::uint64_t const blocks = (Length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;
	unsigned int const flights = Link.Flights();

	BlockReadAheadClass & run = Ahead.Current();

	// The whole window is asked for in requests beside each other, one round
	// trip for all of them.
	for (unsigned int flight = 0; flight < flights; flight++) {

		std::uint64_t start = 0;
		std::uint64_t count = 0;

		if (!run.Span(blocks, window, span, start, count)) return;

		while (count > 0 && Index.Holds(start)) {
			start++;
			count--;
			run.Issued(start);
		}

		std::uint64_t missing = 0;
		while (missing < count && !Index.Holds(start + missing)) missing++;

		if (missing == 0) return;

		std::uint64_t const at = start * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t bytes = missing * (std::uint64_t)BLOCK_SIZE;

		if (at + bytes > Length) bytes = Length - at;

		double const asked = Block_Http_Ahead_Start(Url.c_str(), (double)at, (double)bytes,
			(int)flights);

		// A declined request leaves the window where it is, so the blocks are
		// asked for at the next read.
		if (asked == 0.0) return;

		run.Issued(start + missing);

		if (asked > 0.0) {
			Account_For_Transfer(Meter, at, (unsigned int)asked);
			_AheadRequests++;
			_AheadBytes += (std::uint64_t)asked;
		}
	}
#endif
}


/// <summary>Asks for the blocks a read wants, without waiting. A declined read
/// fetches nothing, so this is what asks for the block it stands on; blocks
/// held or in flight are left alone.</summary>
void HttpBlockSourceClass::Ahead_Want(std::uint64_t offset, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || Length == 0 || length == 0) return;
	if (Block_Store_Under_Main() == 0) return;

	Block_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	std::uint64_t const at = first * (std::uint64_t)BLOCK_SIZE;
	std::uint64_t bytes = (last - first + 1) * (std::uint64_t)BLOCK_SIZE;

	if (at + bytes > Length) bytes = Length - at;
	if (bytes == 0) return;

	if (Block_Http_Ahead_State(Url.c_str(), (double)at, (unsigned int)bytes, (double)Link.Reach()) != 0) return;

	// A run the store holds is answered locally, so guessing at it wins
	// nothing.
	if (Store_Ready()) {
		bool held = true;

		for (std::uint64_t index = first; index <= last && held; index++) {
			if (!Index.Holds(index)) held = false;
		}

		if (held) return;
	}

	double const asked = Block_Http_Ahead_Start(Url.c_str(), (double)at, (double)bytes,
		(int)Link.Flights());

	if (asked > 0.0) {
		Account_For_Transfer(Meter, at, (unsigned int)asked);
		_AheadRequests++;
		_AheadBytes += (std::uint64_t)asked;
	}
#else
	(void)offset;
	(void)length;
#endif
}


bool HttpBlockSourceClass::Prefetch_Now(std::uint64_t offset, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || Length == 0 || length == 0) return(false);
	if (Block_Store_Under_Main() == 0) return(false);
	if (!Store_Ready() || StoreState != STORE_READY) return(false);
	if (offset >= Length) return(false);

	// The store keeps only whole blocks, so the run is widened to block
	// boundaries.
	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	std::uint64_t const at = first * (std::uint64_t)BLOCK_SIZE;
	std::uint64_t bytes = (last - first + 1) * (std::uint64_t)BLOCK_SIZE;

	if (at + bytes > Length) bytes = Length - at;
	if (bytes == 0) return(false);

	// A profile's ranges overlap freely, so what is held is not fetched again.
	bool held = true;

	for (std::uint64_t index = first; index <= last && held; index++) {
		if (!Index.Holds(index)) held = false;
	}

	if (held) return(true);

	Block_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	double const asked = Block_Http_Ahead_Start(Url.c_str(), (double)at, (double)bytes,
		(int)Link.Flights());

	// -1 is a span already in flight, which the wait below finds; only 0 is a
	// failure.
	if (asked == 0.0) return(false);

	std::vector<unsigned char> buffer((std::size_t)bytes);

	// Naming the span lets the page follow the pool filling it through a wait
	// that may last most of a minute.
	Block_Http_Prefetch_Active(Url.c_str(), (double)at, (double)bytes);

	// Answers 1 for the bytes being in the buffer, not a count.
	int const waited = Block_Http_Ahead_Wait(Url.c_str(), (double)at, buffer.data(),
		(unsigned int)bytes);

	if (waited != 1) {
		Block_Http_Prefetch_Active("", 0.0, 0.0);
		return(false);
	}

	if (asked > 0.0) {
		Account_For_Transfer(Meter, at, (unsigned int)asked);
		_AheadRequests++;
		_AheadBytes += (std::uint64_t)asked;
	}

	Store_Keep(at, buffer.data(), (unsigned int)bytes, BlockIndexClass::ADMIT_READ);
	Store_Write();

	// Cleared only after the write, which suspends; clearing first shows the
	// page progress going backwards.
	Block_Http_Prefetch_Active("", 0.0, 0.0);
	return(true);
#else
	(void)offset;
	(void)length;
	return(false);
#endif
}


/// <summary>Is any of what a read wants already on its way? A declining read
/// whose bytes are coming should only come back later; asking again would take
/// the window off the reading.</summary>
bool HttpBlockSourceClass::Ahead_Pending(std::uint64_t offset, unsigned int length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || Length == 0 || length == 0) return(false);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	for (std::uint64_t index = first; index <= last; index++) {
		std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t span = (std::uint64_t)BLOCK_SIZE;

		if (at + span > Length) span = Length - at;

		if (Block_Http_Ahead_State(Url.c_str(), (double)at, (unsigned int)span, (double)Link.Reach()) == 1) return(true);
	}

	return(false);
#else
	(void)offset;
	(void)length;
	return(false);
#endif
}


/// <summary>Serves a span the look-ahead asked for: a copy when the bytes are
/// here, a wait when they are in flight. The wait is legal only under
/// main.</summary>
/// <returns>bool; Was the whole span delivered without a request of its
/// own?</returns>
bool HttpBlockSourceClass::Ahead_Serve(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty()) return(false);

	// Costs no request, but says the image is in use.
	Block_Http_Ahead_Busy(Url.c_str(), 0);

	int const state = Block_Http_Ahead_State(Url.c_str(), (double)offset, length, (double)Link.Reach());

	if (state == 2) {
		if (Block_Http_Ahead_Copy(Url.c_str(), (double)offset, buffer, length) != 1) return(false);
		_AheadServed++;
		return(true);
	}

	if (state != 1 || Block_Store_Under_Main() == 0) return(false);

	// A read that may decline comes back later; only one that must have the
	// bytes waits.
	if (DeferredReadClass::Deferring()) return(false);

	double const stalled = emscripten_get_now();

	if (Block_Http_Ahead_Wait(Url.c_str(), (double)offset, buffer, length) != 1) return(false);

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_AHEAD);
	_AheadServed++;
	_AheadWaited++;
	return(true);
#else
	return(false);
#endif
}


/// <summary>Abandons everything asked for ahead of this image.</summary>
void HttpBlockSourceClass::Ahead_Drop(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty()) return;

	double const wasted = Block_Http_Ahead_Drop(Url.c_str());

	if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
#endif
}


/// <summary>Takes in what the file layer says about a run: a sequential hint
/// declares a file, a soon hint queues it for idle fetching, and a done hint
/// abandons what was asked for in front of it, keeping the whole blocks that
/// arrived.</summary>
void HttpBlockSourceClass::Hint(BlockHintType kind, std::uint64_t offset, std::uint64_t length)
{
	if (Length == 0 || length == 0 || offset >= Length) return;

	if (length > Length - offset) length = Length - offset;

	if (kind == BLOCK_HINT_SOON) {
		_HintSoon++;
		Soon(offset, length);
		return;
	}

	if (kind == BLOCK_HINT_DONE) {
		_HintDone++;
		Ahead_Drop(offset / (std::uint64_t)BLOCK_SIZE,
			(offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE);
		return;
	}

	_HintRuns++;

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const stop = (offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	Ahead.Declare(first, stop);
}


/// <summary>Queues a run the engine says it will want, as the patches the store
/// does not hold; the engine has already decided how much of the run is worth
/// taking.</summary>
void HttpBlockSourceClass::Soon(std::uint64_t offset, std::uint64_t length)
{
#if defined(OPENTS_WASM_JSPI)
	if (Block_Store_Under_Main() == 0) return;
	if (Queued >= SOON_BUDGET) return;

	// A guess pays for itself only in the store; with none, it is bandwidth
	// taken off the reading.
	if (!Store_Ready() || StoreState != STORE_READY) return;

	// A whole-file hint asks for the file in one request rather than
	// round-trip-sized slices, since nothing is guessing which part is wanted.
	// Block_Http_Idle_Add refuses a span already covered, and a read that needs
	// a block sooner reaches the network on its own.
	if (offset == 0 && length >= Length) {
		std::uint64_t const whole = (Length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;
		bool complete = true;
		bool any = false;

		for (std::uint64_t index = 0; index < whole; index++) {
			if (Index.Holds(index)) any = true; else complete = false;
		}

		// A store an earlier launch filled has nothing left to ask for.
		if (complete) return;

		// A part-filled store falls through to the gap scan below.
		if (!any) {
			Block_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

			double const queued = Block_Http_Idle_Add(Url.c_str(), 0.0, (double)Length,
				(double)Length, (int)SOON_QUEUE, 1);

			Queued += (std::uint64_t)queued;
			return;
		}
	}

	bool const is_whole = (offset == 0 && length >= Length);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const stop = (offset + length + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	Block_Http_Ahead_Ready((unsigned int)BLOCK_SIZE);

	std::uint64_t cursor = first;
	unsigned int runs = 0;

	while (cursor < stop && runs < (unsigned int)SOON_RUNS && Queued < SOON_BUDGET) {

		while (cursor < stop && Index.Holds(cursor)) cursor++;
		if (cursor >= stop) break;

		std::uint64_t edge = cursor;

		while (edge < stop && !Index.Holds(edge)) edge++;

		// The last patch runs to the end of the file; cutting the tail into
		// every hole costs more trips than the blocks are worth.
		if (runs + 1 == (unsigned int)SOON_RUNS) edge = stop;

		std::uint64_t const at = cursor * (std::uint64_t)BLOCK_SIZE;
		std::uint64_t bytes = (edge - cursor) * (std::uint64_t)BLOCK_SIZE;

		if (at + bytes > Length) bytes = Length - at;
		if (bytes == 0) break;

		if (Queued + bytes > SOON_BUDGET) bytes = SOON_BUDGET - Queued;

		// One round trip's worth per request, so an abandoned guess is small; a
		// whole-file gap is an interrupted launch's leftovers rather than a
		// guess, and goes in one request.
		double const queued = Block_Http_Idle_Add(Url.c_str(), (double)at, (double)bytes,
			is_whole ? (double)bytes : (double)Link.Reach(), (int)SOON_QUEUE, 0);

		if (queued == 0.0) break;

		Queued += (std::uint64_t)queued;
		cursor = edge;
		runs++;
	}
#else
	(void)offset;
	(void)length;
#endif
}


/// <summary>Banks whatever the guessing fetched and nothing read, a little per
/// read so the copying does not cost the frame.</summary>
void HttpBlockSourceClass::Soon_Keep(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready() || StoreState != STORE_READY) {

		// A store that stopped taking blocks leaves the guesses nowhere to go.
		if (StoreState == STORE_FULL || StoreState == STORE_OFF) {
			double const wasted = Block_Http_Idle_Cancel(Url.c_str());

			if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
		}

		return;
	}

	static std::vector<unsigned char> harvest((std::size_t)BLOCK_SIZE * (std::size_t)SOON_KEEP);

	double at = 0.0;
	double const taken = Block_Http_Idle_Take(Url.c_str(), harvest.data(),
		(unsigned int)harvest.size(), (unsigned int)BLOCK_SIZE, &at);

	if (!(taken > 0.0)) return;

	Store_Keep((std::uint64_t)at, harvest.data(), (unsigned int)taken,
		BlockIndexClass::ADMIT_GUESS);
#endif
}


/// <summary>Abandons what was asked for in front of one displaced run, between
/// first and stop.</summary>
void HttpBlockSourceClass::Ahead_Drop(std::uint64_t first, std::uint64_t stop)
{
#if defined(OPENTS_WASM_JSPI)
	if (Url.empty() || stop <= first) return;

	double const wasted = Block_Http_Ahead_Drop_Range(Url.c_str(),
		(double)(first * (std::uint64_t)BLOCK_SIZE), (double)(stop * (std::uint64_t)BLOCK_SIZE));

	if (wasted > 0.0) _AheadWaste += (std::uint64_t)wasted;
#else
	(void)first;
	(void)stop;
#endif
}


//------------------------------------------------------------------------------
// The store as the source uses it; without the suspension scaffold none of it
// is reached.

/// <summary>Is the store open and serving this image? The first call under main
/// opens the database, clears blocks a record for another image describes, and
/// caps the index at the origin's allowance.</summary>
bool HttpBlockSourceClass::Store_Ready(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState == STORE_READY || StoreState == STORE_FULL) return(true);
	if (StoreState == STORE_OFF) return(false);

	if (Signature.empty() || Slot.empty() || Block_Store_Wanted() == 0) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	if (Block_Store_Under_Main() == 0) return(false);

	std::vector<char> record(BlockIndexClass::RECORD_MAX);
	record[0] = '\0';

	if (Block_Store_Open(Slot.c_str(), record.data(), (int)record.size()) != 1) {
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	double const room = Block_Store_Room();
	std::uint64_t ceiling = BlockIndexClass::STORE_LIMIT;

	if (room > 0.0) {
		double const share = room * BlockIndexClass::STORE_SHARE;

		ceiling = (share >= (double)BlockIndexClass::STORE_MAX)
			? BlockIndexClass::STORE_MAX : (std::uint64_t)share;
	}

	std::vector<std::uint64_t> evicted;

	if (!Index.Adopt(record.data(), Signature)) {
		_StoreDiscarded++;
		Index.Cap(ceiling, evicted);

		if (Block_Store_Write(Slot.c_str(), Index.Encode().c_str(), "*", (unsigned int)BLOCK_SIZE) != 1) {
			StoreState = STORE_OFF;
			_StoreState = 2;
			return(false);
		}

		StoreState = STORE_READY;
		_StoreState = 1;
		return(true);
	}

	Index.Cap(ceiling, evicted);

	StoreState = STORE_READY;
	_StoreState = 1;

	if (!evicted.empty()) {
		Store_Drop(evicted);
		Store_Write();
	}

	return(StoreState != STORE_OFF);
#else
	return(false);
#endif
}


/// <summary>Notes blocks the index has let go of, so the batch deletes
/// them.</summary>
void HttpBlockSourceClass::Store_Drop(std::vector<std::uint64_t> const & evicted)
{
	char key[32];

	for (std::uint64_t gone : evicted) {
		std::snprintf(key, sizeof(key), "%s%llu", Removals.empty() ? "" : ",",
			(unsigned long long)gone);
		Removals += key;
	}
}


/// <summary>Serves a span out of the store, if the store holds all of
/// it.</summary>
/// <returns>bool; Was the whole span delivered without a request?</returns>
bool HttpBlockSourceClass::Store_Serve(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready()) return(false);

	std::uint64_t const first = offset / (std::uint64_t)BLOCK_SIZE;
	std::uint64_t const last = (offset + length - 1) / (std::uint64_t)BLOCK_SIZE;

	for (std::uint64_t index = first; index <= last; index++) {
		if (!Index.Holds(index)) return(false);
	}

	double const stalled = emscripten_get_now();

	int const answer = Block_Store_Read(Slot.c_str(), (double)offset, buffer, length,
		(unsigned int)BLOCK_SIZE);

	if (answer < 0) {

		// Storage itself has stopped answering, so it is left alone for the rest
		// of the run.
		StoreState = STORE_OFF;
		_StoreState = 2;
		return(false);
	}

	if (answer != 1) {

		// The record and the blocks disagree: another tab, or a store written
		// before the block size was passed in. Forgetting the span costs this one
		// read and lets the blocks be stored properly, where giving the store up
		// would cost every read for the rest of the run.
		std::vector<std::uint64_t> stale;

		for (std::uint64_t index = first; index <= last; index++) {
			stale.push_back(index);
		}

		Index.Forget(stale);
		return(false);
	}

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_STORE);
	_StoreHits += (unsigned int)(last - first + 1);
	_StoreBytes += length;
	return(true);
#else
	return(false);
#endif
}


/// <summary>Stages every whole block a fetch delivered; the part blocks at the
/// ends are left to the window.</summary>
void HttpBlockSourceClass::Store_Keep(std::uint64_t offset, void const * buffer, unsigned int length,
	BlockIndexClass::AdmitType how)
{
#if defined(OPENTS_WASM_JSPI)
	if (!Store_Ready() || StoreState != STORE_READY) return;

	std::uint64_t const stop = offset + length;
	std::uint64_t index = (offset + (std::uint64_t)BLOCK_SIZE - 1) / (std::uint64_t)BLOCK_SIZE;

	for (;;) {
		std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
		if (at >= Length || at >= stop) break;

		std::uint64_t size = Length - at;
		if (size > (std::uint64_t)BLOCK_SIZE) size = (std::uint64_t)BLOCK_SIZE;
		if (at + size > stop) break;

		// A block the index will not take is not staged, since a block without
		// an entry is never read back.
		bool const room = (how != BlockIndexClass::ADMIT_GUESS) ||
			(Index.Bytes() + size <= Index.Cap());

		if (!Index.Holds(index) && room) {
			if (Block_Store_Stage(Slot.c_str(), (double)index, (unsigned char const *)buffer + (at - offset),
					(unsigned int)size) == 1) {

				std::vector<std::uint64_t> evicted;

				Index.Note(index, size, evicted, how);
				Store_Drop(evicted);
				Staging.push_back(index);

				Staged++;
				StagedAt = emscripten_get_now();
				_StoreKept++;
			}
		}

		index++;
	}

	if (Staged >= (unsigned int)STORE_BATCH) Store_Write();
#endif
}


/// <summary>Writes the staged batch, its evictions and the record in one
/// transaction. A refused write gives up only this batch; a broken database
/// gives up the store.</summary>
void HttpBlockSourceClass::Store_Write(void)
{
#if defined(OPENTS_WASM_JSPI)
	if (StoreState != STORE_READY) return;
	if (Staged == 0 && Removals.empty()) return;

	int const written = Block_Store_Write(Slot.c_str(), Index.Encode().c_str(), Removals.c_str(),
		(unsigned int)BLOCK_SIZE);

	Staged = 0;
	Removals.clear();

	if (written == 1) {
		Staging.clear();
		return;
	}

	if (written < 0) {
		Index.Forget(Staging);
		Staging.clear();
		StoreState = STORE_FULL;
		_StoreState = 3;
		return;
	}

	Index.Reset(Signature);
	Staging.clear();
	StoreState = STORE_OFF;
	_StoreState = 2;
#endif
}


/// <summary>Writes any open image's batch that has been left sitting.</summary>
void HttpBlockSourceClass::Store_Settle(void)
{
#if defined(OPENTS_WASM_JSPI)
	double const now = emscripten_get_now();

	for (HttpBlockSourceClass * source : _Open) {
		if (source->Staged > 0 && (now - source->StagedAt) > STORE_IDLE) source->Store_Write();
	}
#endif
}


void HttpBlockSourceClass::Service_All(void)
{
#if defined(OPENTS_WASM_JSPI)
	static double last = 0.0;
	double const now = emscripten_get_now();

	if (now - last < SERVICE_INTERVAL) return;
	last = now;

	for (HttpBlockSourceClass * source : _Open) {
		source->Soon_Keep();
	}

	Store_Settle();

	// Once a store is open the manifest has certainly been read, which is what the sweep
	// needs to tell a live slot from a stranded one. It declines until then and is asked
	// again on the next pass; it runs at most once either way.
	if (!_Swept && !_Open.empty()) {
		int const removed = Block_Store_Sweep();

		if (removed >= 0) {
			_Swept = true;
			_StoreSwept += (unsigned int)removed;
		}
	}
#endif
}


void HttpBlockSourceClass::Store_Discard(void)
{
#if defined(OPENTS_WASM_JSPI)
	Block_Store_Forget(Slot.c_str());
	Staged = 0;
	Removals.clear();
	Staging.clear();
#endif
}


bool HttpBlockSourceClass::Transfer(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;
	double const stalled = emscripten_get_now();

	Account_For_Transfer(Meter, offset, length);

	// The engine is about to wait, so nothing guessed is left running in front
	// of it.
#if defined(OPENTS_WASM_JSPI)
	double const given = Block_Http_Ahead_Busy(Url.c_str(), 1);

	if (given > 0.0) _AheadWaste += (std::uint64_t)given;
#endif

	// A server may answer a range short, so the request is repeated from where
	// it stopped.
	while (remaining > 0) {

		// Timed here because this is where the link says what it costs.
		double const began = emscripten_get_now();
		double cached = 0.0;
		int const got = Block_Http_Transfer(Url.c_str(), (double)offset, cursor, remaining, &cached);

		if (got <= 0) {

			// On a recalled image, a refused range is the first evidence the
			// record was wrong.
			if (Revive()) continue;

			return(false);
		}

		// An answer from the browser's HTTP cache says nothing about the link.
		if (!(cached > 0.0)) Link.Note((std::uint64_t)got, emscripten_get_now() - began);

		_LinkTrip = Link.Trip();
		_LinkWindow = Link.Window();

		// The estimates go back to the record at intervals, so the next launch
		// opens its window on a reading rather than the floor.
		if (Link.Measured() && (emscripten_get_now() - Learned) > LEARN_IDLE) Learn();

		cursor += (unsigned int)got;
		offset += (unsigned int)got;
		remaining -= (unsigned int)got;
	}

	Account_For_Stall(Meter, read.Offset, read.Length, emscripten_get_now() - stalled, STALL_TRANSFER);
	return(true);
}


/// <summary>Delivers a span the store may already hold, and stores it when it
/// does not.</summary>
/// <remarks>The span is a whole number of whole blocks, which is what makes it
/// storable.</remarks>
bool HttpBlockSourceClass::Fetch_Run(std::uint64_t offset, void * buffer, unsigned int length,
	ReadType const & read)
{
	// The stall is recorded for the part of the span the read asked for.
	ReadType const part = read.Within(offset, length);

	// The look-ahead first: what it holds is paid for and on the heap.
	if (Ahead_Serve(offset, buffer, length, part)) {
		Store_Keep(offset, buffer, length, BlockIndexClass::ADMIT_READ);
		Look_Ahead();
		return(true);
	}

	// A span the store answered cost no round trip, so it opens no window.
	if (Store_Serve(offset, buffer, length, part)) return(true);

	// A read that may decline puts the blocks in flight, opens the window and
	// returns empty.
#if defined(OPENTS_WASM_JSPI)
	if (DeferredReadClass::Deferring()) {
		Ahead_Want(offset, length);
		_Deferred++;
		DeferredReadClass::Decline();
		return(false);
	}
#endif

	// The window is opened before this read is paid for, so its request runs
	// alongside this one rather than behind it.
	Look_Ahead();

	if (!Transfer(offset, buffer, length, part)) return(false);

	Store_Keep(offset, buffer, length, BlockIndexClass::ADMIT_READ);
	return(true);
}


HttpBlockSourceClass::BlockType const * HttpBlockSourceClass::Block(std::uint64_t index,
	ReadType const & read)
{
	for (std::size_t position = 0; position < Cache.size(); position++) {
		if (Cache[position].Index != index) continue;

		if (position != 0) {
			std::rotate(Cache.begin(), Cache.begin() + (std::ptrdiff_t)position,
				Cache.begin() + (std::ptrdiff_t)position + 1);
		}
		return(&Cache.front());
	}

	std::uint64_t const at = index * (std::uint64_t)BLOCK_SIZE;
	if (at >= Length) return(nullptr);

	std::uint64_t available = Length - at;
	if (available > (std::uint64_t)BLOCK_SIZE) available = (std::uint64_t)BLOCK_SIZE;

	BlockType fetched;
	fetched.Index = index;
	fetched.Data.resize((std::size_t)available);

	if (!Fetch_Run(at, fetched.Data.data(), (unsigned int)available, read)) return(nullptr);

	if (Cache.size() >= (std::size_t)BLOCK_CACHE) Cache.pop_back();
	Cache.insert(Cache.begin(), std::move(fetched));
	return(&Cache.front());
}


bool HttpBlockSourceClass::Read_At(std::uint64_t offset, void * buffer, unsigned int length)
{
	if (buffer == nullptr) return(false);
	if (length == 0) return(true);
	if (Length == 0 || offset > Length || (std::uint64_t)length > Length - offset) return(false);

	ReadType const read = {offset, length};

	// A declining read whose bytes are coming declines before anything else, or
	// each retry would be followed as a run.
#if defined(OPENTS_WASM_JSPI)
	if (DeferredReadClass::Deferring() && Ahead_Pending(offset, length)) {
		_Deferred++;
		DeferredReadClass::Decline();
		return(false);
	}
#endif

	// Every open image is settled, since an image the game has finished with
	// has no reads of its own.
	Store_Settle();

	// What the drainer asked for is counted here, since it runs on a timer of
	// its own.
#if defined(OPENTS_WASM_JSPI)
	Soon_Keep();

	unsigned int guesses = 0;
	double const guessed = Block_Http_Idle_Spent(Url.c_str(), &guesses);

	if (guessed > 0.0) {
		_Requests += guesses;
		_Fetched += (std::uint64_t)guessed;
		_AheadRequests += guesses;
		_AheadBytes += (std::uint64_t)guessed;
		_SoonRequests += guesses;
		_SoonBytes += (std::uint64_t)guessed;
	}

#endif

	// The run is followed before this read is served, so a displaced run's
	// outstanding span is abandoned rather than paid for.
	std::uint64_t lost = 0;
	std::uint64_t stop = 0;

	bool const displaced = Ahead.Note(offset / (std::uint64_t)BLOCK_SIZE,
		(offset + length - 1) / (std::uint64_t)BLOCK_SIZE, lost, stop);

	if (displaced) Ahead_Drop(lost, stop);

	unsigned char * cursor = (unsigned char *)buffer;
	unsigned int remaining = length;

	while (remaining > 0) {
		std::uint64_t const index = offset / (std::uint64_t)BLOCK_SIZE;
		std::size_t const within = (std::size_t)(offset - index * (std::uint64_t)BLOCK_SIZE);

		// Whole blocks are asked for in one request; the part blocks at either
		// end come through the window, so every block is fetched whole or not
		// at all.
		std::uint64_t const whole = Length / (std::uint64_t)BLOCK_SIZE;

		if (within == 0 && remaining >= (unsigned int)BLOCK_SIZE && index < whole) {
			std::uint64_t count = remaining / (unsigned int)BLOCK_SIZE;
			if (count > whole - index) count = whole - index;

			unsigned int const span = (unsigned int)(count * (std::uint64_t)BLOCK_SIZE);

			if (!Fetch_Run(offset, cursor, span, read)) return(false);

			cursor += span;
			offset += span;
			remaining -= span;
			continue;
		}

		BlockType const * const block = Block(index, read);
		if (block == nullptr || within >= block->Data.size()) return(false);

		std::size_t chunk = block->Data.size() - within;
		if (chunk > (std::size_t)remaining) chunk = (std::size_t)remaining;

		std::memcpy(cursor, block->Data.data() + within, chunk);
		cursor += chunk;
		offset += chunk;
		remaining -= (unsigned int)chunk;
	}

	return(true);
}


#endif
