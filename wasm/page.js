
	// The panel is transparent to the mouse so the game gets every click, which also means
	// its text cannot be selected. This hands the mouse back for as long as the reader
	// wants it, and the game keeps running underneath either way.
	(function () {
		var panel = document.getElementById("panel");
		var grab = document.getElementById("grab");

		grab.addEventListener("click", function () {
			var on = panel.classList.toggle("grabbed");
			grab.textContent = on ? "release" : "select";
		});

		// Collapsed unless this viewer has previously asked otherwise. Storage is per
		// origin and may be unavailable, in which case the default simply stands.
		var open = false;

		try {
			open = window.localStorage.getItem("opents.log") === "open";
		} catch (e) {}

		function apply() {
			panel.classList.toggle("collapsed", !open);
			if (!open) {
				panel.classList.remove("grabbed");
				grab.textContent = "select";
			}
		}

		document.getElementById("toggle").addEventListener("click", function () {
			open = !open;
			apply();
			try {
				window.localStorage.setItem("opents.log", open ? "open" : "closed");
			} catch (e) {}
		});

		apply();
	}());

	// Reads out of the disc images, when they are being used. A preloaded build never
	// fetches, so the whole clause is left out rather than reporting zeroes. Unique blocks
	// is the working set: the part of the discs the game actually turned out to need.
	function iso() {
		if (typeof Module === "undefined" || !Module._OpenTS_Iso_Requests) return("");

		// Said rather than left out: a build whose data is preloaded never reads an image,
		// and an empty clause reads the same as a broken one.
		var requests = Module._OpenTS_Iso_Requests();
		if (requests === 0) return("no image reads  |  ");

		var blocks = Module._OpenTS_Iso_Unique_Blocks();
		var size = Module._OpenTS_Iso_Block_Size();
		var mib = function (bytes) { return((bytes / 1048576).toFixed(1) + " MiB"); };

		window.OpenTS_State.isoRequests = requests;
		window.OpenTS_State.isoUniqueBlocks = blocks;

		// Reads that were told the bytes are not here yet and carried on without them. Each
		// is a stall that was there to be taken and was not, so the two are read together.
		if (Module._OpenTS_Iso_Deferred) {
			window.OpenTS_State.isoDeferred = Module._OpenTS_Iso_Deferred();
		}

		// The persistent store, when the build has one. Hits are blocks the browser already
		// held, so a warm run reads most of its data without going to the network at all.
		var store = "";

		if (Module._OpenTS_Iso_Store_State) {
			var state = Module._OpenTS_Iso_Store_State();
			var hits = Module._OpenTS_Iso_Store_Hits();
			var kept = Module._OpenTS_Iso_Store_Kept();

			window.OpenTS_State.isoStoreHits = hits;
			window.OpenTS_State.isoStoreState = state;

			store = "  |  cache " + (state === 2 ? "unavailable" :
				hits + " served, " + kept + " kept" +
				(Module._OpenTS_Iso_Store_Discarded() ? ", store discarded" : ""));
		}

		var discs = Module._OpenTS_Iso_Images ? Module._OpenTS_Iso_Images() : 1;

		window.OpenTS_State.isoImages = discs;

		// How the discs were established. A launch whose discs are the ones it was given
		// last time asks the server nothing about them and says so; one that asked has
		// been given a disc it had not seen before, or is a first run.
		if (Module._OpenTS_Iso_Probes) {
			window.OpenTS_State.isoProbes = Module._OpenTS_Iso_Probes();
			window.OpenTS_State.isoRecalls = Module._OpenTS_Iso_Recalls();

			store += "  |  discs " + (window.OpenTS_State.isoProbes === 0 ? "known" :
				window.OpenTS_State.isoProbes + " asked about");

			// And whether one of them turned out to have been replaced since, which
			// reloads the page rather than reading some of each.
		}

		return(discs + " discs  |  image " + requests + " requests  |  " +
			mib(Module._OpenTS_Iso_Fetched()) +
			" fetched  |  " + blocks + " unique blocks (" + mib(blocks * size) + ")" +
			store + "  |  ");
	}

	// What an automated check reads to tell an engine that is running from one that
	// started and then stopped.
	window.OpenTS_State = {
		started: false, frames: 0, waits: 0, syncs: 0,
		persistent: false, restored: false, movieMuted: false, lines: [],
		phase: "none", phases: "none", phaseDetail: "", phaseSerial: 0, pending: 0,
		events: [], eventBase: 0
	};

	// The engine reports each phase change and marker here as it happens, so a check
	// polling the state misses none of them. The list is bounded; eventBase is the
	// index of its first entry, so a reader keeps its place across the trim.
	var OpenTS_EVENT_LIMIT = 4000;

	window.OpenTS_Event = function (name, detail) {
		var state = window.OpenTS_State;
		state.events.push({at: performance.now(), name: name, detail: detail});
		if (state.events.length > OpenTS_EVENT_LIMIT) {
			var excess = state.events.length - OpenTS_EVENT_LIMIT;
			state.events.splice(0, excess);
			state.eventBase += excess;
		}
	};

	var logElement = document.getElementById("log");

	// The panel sits over the game, so only the tail of the output is shown. The whole of
	// it stays in OpenTS_State.lines, which is what an automated check reads.
	function OpenTS_Record(text) {
		window.OpenTS_State.lines.push(text);
		if (window.OpenTS_State.lines.length > 400) {
			window.OpenTS_State.lines.shift();
		}
		logElement.textContent = window.OpenTS_State.lines.slice(-6).join("\n");
	}

	// The diagnostic panel is not part of the game, so it is absent unless asked for with
	// "?hud=on". "?hud=off" stays accepted so an existing link keeps meaning what it did.
	if ((new URLSearchParams(window.location.search).get("hud") || "") !== "on") {
		document.getElementById("panel").classList.add("off");
	}

	// A page has no command line, so the query string stands in for one. "?scenario=GDI1A.MAP"
	// starts that mission directly; "arg" carries any other switch through verbatim.
	function OpenTS_Arguments() {
		var query = new URLSearchParams(window.location.search);
		var args = [];

		if (query.get("scenario")) {
			args.push("-SCENARIO=" + query.get("scenario"));
		}

		if (query.get("campaign")) {
			args.push("-CAMPAIGN=" + query.get("campaign"));
		}

		if (query.get("playmovie")) {
			args.push("-PLAYMOVIE=" + query.get("playmovie"));
		}

		if (query.get("nointro")) {
			args.push("-NOINTRO");
		}

		if (query.get("pgocapture")) {
			args.push("-PGOCAPTURE");
		}

		return args.concat(query.getAll("arg"));
	}

	// The discs the game data is read from, in search order. The engine takes the first disc
	// that carries a name, so Firestorm comes first: it is the newest of the three, and its
	// copies of MULTI.MIX and LANGUAGE.DLL are what an installed game would be running out
	// of. Those two are why the order matters. Firestorm's MULTI.MIX holds the fourteen
	// Firestorm multiplayer maps on top of the base game's, and its LANGUAGE.DLL holds a
	// hundred and thirty-eight strings that exist nowhere else, so the base discs' copies
	// would lose both. The discs repeat SCORES.MIX and SIDECD01.MIX as well, but every entry
	// in those is the same on all three -- the archives differ only in the random key their
	// headers are encrypted with -- so which one answers makes no difference.
	// OPENTS.iso comes first and is the whole disc set packed into one volume, with the
	// overlap between the discs already resolved. A deployment serving the three discs does
	// not carry it; the mount is refused, a line says so, and the discs answer as before.
	// "?image=" names other files instead, once per disc and in the order to search them.
	function OpenTS_Images() {
		var named = new URLSearchParams(window.location.search).getAll("image");

		return named.length > 0 ? named : ["OPENTS.iso", "FIRESTORM.iso", "TS1.iso", "TS2.iso"];
	}

	// Resolves assets.json, the manifest it names, and every archive or film they name
	// against a different origin than the page's own -- what a native shell wants when it
	// bundles this page locally but plays from a hosted release. "?manifestBase=" names it
	// for testing from a browser; unset, it all resolves against the page exactly as before.
	function OpenTS_Manifest_Base() {
		return new URLSearchParams(window.location.search).get("manifestBase") || undefined;
	}

	// The one directory that survives the tab. Everything else the engine can see is memory:
	// the game data is preloaded or fetched off a disc image, and neither belongs in a
	// browser's database. win32compat.cpp resolves a save into this directory and asks for it
	// to be written back; here it is created, mounted, and read in before the engine starts.
	var OpenTS_SaveDirectory = "/save";

	function OpenTS_Mount_Saves() {
		var fs = Module.FS;

		if (!fs || !fs.filesystems || !fs.filesystems.IDBFS) {
			OpenTS_Record("saves are not persistent: this build has no IDBFS");
			return false;
		}

		try {
			fs.mkdir(OpenTS_SaveDirectory);
			fs.mount(fs.filesystems.IDBFS, {}, OpenTS_SaveDirectory);
		} catch (e) {
			OpenTS_Record("saves are not persistent: " + e);
			return false;
		}

		return true;
	}

	// The engine is started by hand rather than by the runtime, because what IndexedDB holds
	// arrives asynchronously and a save has to be back on the filesystem before the engine
	// looks for one.
	function OpenTS_Start() {
		window.OpenTS_State.started = true;

		function go(restored) {
			window.OpenTS_State.restored = restored;
			try {
				Module.callMain(Module.arguments);
			} catch (e) {
				report("the engine stopped: " + ((e && e.stack) || e));
			}
		}

		if (!window.OpenTS_State.persistent) {
			go(false);
			return;
		}

		Module.FS.syncfs(true, function (error) {
			if (error) {
				OpenTS_Record("reading persistent storage failed: " + error);
			}
			go(!error);
		});
	}

	// The block cache moved from an IndexedDB database to OPFS, so a returning player may
	// still hold a dead "opents-iso" database. Drop it; the OPFS directory of the same name
	// is separate storage and is left alone.
	try { indexedDB.deleteDatabase("opents-iso"); } catch (e) {}

	var Module = {
		arguments: OpenTS_Arguments(),
		opentsImage: OpenTS_Images(),
		opentsManifestBase: OpenTS_Manifest_Base(),
		canvas: document.getElementById("canvas"),
		noInitialRun: true,
		print: function (text) { console.log(text); OpenTS_Record(text); },
		// The engine's diagnostic log goes to stderr (code/dbgprint.cpp), which is where a
		// console window would have shown it; routing that to console.error would mark
		// every ordinary line an error. What actually fails is reported by report().
		printErr: function (text) { console.log(text); OpenTS_Record(text); },
		preRun: [function () { window.OpenTS_State.persistent = OpenTS_Mount_Saves(); }],
		onRuntimeInitialized: function () { OpenTS_Start(); },

		// The gate the engine's frame wait passes through. While held, each pass parks
		// until a frame is granted, so a check can advance the engine a frame at a time.
		OpenTS_FrameGate: {
			held: false,
			credits: 0,
			parked: false,
			waiter: null,
			take: function () {
				var gate = this;
				if (gate.credits > 0) {
					gate.credits--;
					return Promise.resolve();
				}
				gate.parked = true;
				return new Promise(function (resolve) { gate.waiter = resolve; });
			},
			grant: function (count) {
				this.credits += count;
				this.wake();
			},
			wake: function () {
				if (this.waiter === null) return;
				if (this.held && this.credits <= 0) return;
				if (this.held) this.credits--;
				var waiter = this.waiter;
				this.waiter = null;
				this.parked = false;
				waiter();
			},
			hold: function () { this.held = true; },
			release: function () {
				this.held = false;
				this.credits = 0;
				this.wake();
			}
		},
	};

	// The engine keeps the thread between yields, so the status is polled from a timer
	// rather than asked of the module on a frame it may not be holding.
	// What is still left to arrive, from the pool's own count of what every span still
	// in flight has yet to deliver. A handful of samples a run happens to take was never
	// a trustworthy rate reading -- this only ever moves toward zero, and hides itself
	// the moment it gets there rather than fading out on a guess that nothing is coming.
	var speedElement = document.getElementById("speed");
	var loadingElement = document.getElementById("loading");
	var loadingFill = document.getElementById("loading-fill");
	var loadingNote = document.getElementById("loading-note");
	var loadingDone = 0;
	var loadingTotal = 0;

	var soundElement = document.getElementById("sound");
	var soundSpent = false;

	// The bar stands over a canvas the engine has not drawn on yet, so it is shown only
	// while a profile's fetch is running and is gone the moment that fetch is done -- the
	// engine reaches its first frame just after, and nothing of the page's own belongs on
	// top of the game. Without a profile there is no total to show a bar against, and the
	// reading in the corner is all there is.
	// How much of the span the prefetch is presently waiting on has arrived. The engine is
	// suspended inside that wait, so the pool is the only thing that knows.
	function OpenTS_Active_Fill() {
		var active = globalThis.__opentsPgoActive;
		var pool = globalThis.__opentsIsoAhead;

		if (!active || !pool) return 0;

		var list = pool.spans.get(active.key);
		if (!list) return 0;

		for (var index = 0; index < list.length; index++) {
			var span = list[index];
			if (span.start <= active.start && span.stop >= active.start + active.bytes) {
				return Math.max(0, Math.min(active.bytes,
					span.start + span.filled - active.start));
			}
		}

		return 0;
	}

	function OpenTS_Show_Loading() {
		var total = Module._OpenTS_PGO_Total ? Module._OpenTS_PGO_Total() : 0;

		if (total <= 0 || Module.opentsLocalDiscs) {
			loadingElement.hidden = true;
			return false;
		}

		// Ranges already banked, plus how much of the one being awaited has arrived --
		// without the second the bar would stand still for the whole of the largest run
		// and then jump by its share of the profile.
		//
		// Each stage is a profile of its own -- the menu's, then the first mission's --
		// and the second starts over from nothing. Without this the mark the first left
		// behind is already past the whole of the second, and the bar never appears for it.
		if (total !== loadingTotal) {
			loadingTotal = total;
			loadingDone = 0;
		}

		// Held to the furthest it has reached. The two are read a moment apart and the
		// engine suspends between them, so a sample can catch a run counted in neither;
		// what that is worth showing is the last figure, never a smaller one.
		var done = Math.max(loadingDone, Module._OpenTS_PGO_Done() + OpenTS_Active_Fill());
		loadingDone = done;

		if (done >= total) {
			loadingElement.hidden = true;
			return false;
		}

		loadingFill.style.width = (100 * Math.min(done, total) / total) + "%";
		loadingNote.textContent = (Math.min(done, total) / 1048576).toFixed(1) + " of " +
			(total / 1048576).toFixed(1) + " MB";
		loadingElement.hidden = false;
		return true;
	}

	// What a mission's background fill has left: the drainer's own queue as well as what it
	// has in flight. The pool alone reports only the latter, which empties between one run
	// landing and the next starting and so reads as the fetching having finished and begun
	// again -- the whole of the queue behind it is what makes the figure fall once.
	function OpenTS_Background_Left() {
		return Module._OpenTS_Iso_Background_Left
			? Module._OpenTS_Iso_Background_Left()
			: Module._OpenTS_Iso_Bytes_Remaining();
	}

	function OpenTS_Show_Speed() {
		// The panel stands in the same corner and already reports a total, so the two are
		// never shown together. Nothing is left to arrive off a disc read from local
		// storage, so a host that reads one says so and the reading is left off.
		if (!Module._OpenTS_Iso_Bytes_Remaining || Module.opentsLocalDiscs ||
			!document.getElementById("panel").classList.contains("off")) {
			return;
		}

		// The bar says the same thing in the middle of the window while it is up.
		if (!loadingElement.hidden) {
			speedElement.classList.remove("on");
			return;
		}

		// A profile knows the whole of what it is going to fetch before it starts, so what
		// is left of it only ever falls. The pool's own figure is what is in flight and
		// nothing more: it drops to nothing between one range landing and the next
		// starting, which reads as the download having finished and begun again.
		var total = Module._OpenTS_PGO_Total ? Module._OpenTS_PGO_Total() : 0;
		var profileLeft = total > 0
			? Math.max(0, total - Module._OpenTS_PGO_Done() - OpenTS_Active_Fill())
			: 0;

		// The profile's own figure only while it is still fetching. What a mission starts
		// in the background afterwards is the drainer's, and a profile that has finished
		// would otherwise read as nothing left to arrive for the rest of the run.
		var remaining = profileLeft > 0 ? profileLeft : OpenTS_Background_Left();

		if (remaining > 0) {
			var text = remaining >= 1048576
				? (remaining / 1048576).toFixed(1) + " MB left"
				: Math.round(remaining / 1024) + " KB left";
			speedElement.textContent = text;
			speedElement.classList.add("on");
		} else {
			speedElement.classList.remove("on");
		}
	}

	/*
	 * The prompt is only ever asking for the first gesture, so it is spent on the first one
	 * the reader gives, by whatever route and whether or not the sound actually started.
	 * Left standing it would go on swallowing the clicks the game is waiting for.
	 */
	function OpenTS_Spend_Sound_Prompt() {
		soundSpent = true;
		soundElement.hidden = true;
	}

	/*
	 * A suspended context is not yet a browser refusing to play: the device opens with one
	 * and the engine resumes it a moment later, which on a host that never blocks anything
	 * is the whole story. Asking for a gesture in that window puts the prompt up and takes
	 * it away again, so it is only offered once the block has lasted.
	 */
	var soundBlockedSince = 0;
	var SOUND_PROMPT_DELAY = 1000.0;

	function OpenTS_Show_Sound() {
		if (soundSpent) return;

		// A film playing without its sound asks for the same gesture, and is the one case
		// where the engine's own audio may be running perfectly well.
		var blocked = !!window.OpenTS_State.movieMuted ||
			!!(Module._OpenTS_Audio_Blocked && Module._OpenTS_Audio_Blocked());

		// Nothing wants to be heard while the bar is still filling.
		if (blocked && document.getElementById("loading").hidden) {
			if (soundBlockedSince === 0) soundBlockedSince = performance.now();
		} else {
			soundBlockedSince = 0;
		}

		soundElement.hidden = soundBlockedSince === 0 ||
			(performance.now() - soundBlockedSince) < SOUND_PROMPT_DELAY;
	}

	/*
	 * The worker that keeps the page and its modules, so a later visit starts with no
	 * network at all. It is registered after load rather than during it, since its own
	 * fetch would otherwise compete with the module the visitor is waiting for.
	 *
	 * A shell that bundles the page has one already and serves it from its own origin, so
	 * there is nothing here for it to keep.
	 */
	if ("serviceWorker" in navigator &&
		(new URLSearchParams(window.location.search).get("hosted") || "") !== "1") {
		window.addEventListener("load", function () {
			// "all" lets the worker script obey its own caching headers. Without it the
			// browser asks for sw.js on essentially every navigation; with it the check is
			// still forced at least daily, which no cache lifetime can extend.
			navigator.serviceWorker.register("sw.js", { updateViaCache: "all" }).catch(function (error) {
				report("no offline support: " + ((error && error.message) || error));
			});
		});
	}

	/*
	 * A prototype of a touch-reachable control group bar. Every action it takes is the
	 * command the keyboard already reaches, so a group made here is the same group Ctrl+1
	 * makes; only the way in is new.
	 */
	var groupsShown = "";
	var groupsHeld = 0;

	// A press holds the bar still. Rebuilding it drops the node the finger is on, and a
	// button removed between its press and its release is never clicked at all.
	var groupsPressed = false;
	var groupsTapSlot = 0;
	var groupsTapAt = 0;

	// Acts on the release rather than on a click, which a browser may hold back after a
	// touch. The press has to have started on the same button, so a finger that slides onto
	// one on its way off the bar does not press it.
	function OpenTS_On_Tap(element, action) {
		var armed = false;

		element.addEventListener("pointerdown", function () { armed = true; });
		element.addEventListener("pointercancel", function () { armed = false; });
		element.addEventListener("pointerup", function () {
			if (!armed) return;
			armed = false;
			action();
		});
	}

	function OpenTS_Group_Records() {
		var records = [];

		for (var slot = 1; slot <= 9; slot++) {
			var count = Module._OpenTS_Group_Count(slot);
			if (count > 0) records.push({ slot: slot, count: count });
		}

		return records;
	}

	function OpenTS_Group_Chip(record, current, mergeable) {
		var holder = document.createElement("div");
		holder.className = "chip";

		var chip = document.createElement("button");
		chip.type = "button";
		chip.dataset.slot = String(record.slot);

		if (current) chip.className = "current";

		var slot = document.createElement("span");
		slot.className = "slot";
		slot.textContent = String(record.slot);

		var what = document.createElement("span");
		what.className = "what";
		what.textContent = "\u00d7" + record.count;
		chip.title = record.count + " unit" + (record.count === 1 ? "" : "s") +
			" in group " + record.slot;

		chip.appendChild(slot);
		chip.appendChild(what);

		// Tap selects, a second tap centres the view, and holding reassigns the group to
		// whatever is selected now.
		// A hold that has already reassigned the group must not also select it: the press
		// still ends in a click, and a pen that wobbles off the chip still ends the press.
		var spent = false;

		chip.addEventListener("pointerdown", function () {
			spent = false;
			groupsHeld = window.setTimeout(function () {
				groupsHeld = 0;
				spent = true;
				Module._OpenTS_Group_Do(record.slot, 2);
				groupsShown = "";
			}, 600);
		});

		["pointerup", "pointercancel"].forEach(function (name) {
			chip.addEventListener(name, function () {
				if (groupsHeld === 0) return;
				window.clearTimeout(groupsHeld);
				groupsHeld = 0;
			});
		});

		// The first press selects the group and a second one takes the view to it. Which
		// press this is comes from the bar's own memory rather than from what is selected
		// now, because the engine answers that on its next frame and the second press can
		// arrive first.
		OpenTS_On_Tap(chip, function () {
			if (spent) { spent = false; return; }

			var now = Date.now();
			var again = (groupsTapSlot === record.slot) && (now - groupsTapAt) < 600;

			groupsTapSlot = record.slot;
			groupsTapAt = now;

			var already = Module._OpenTS_Selection_Group() === record.slot;
			Module._OpenTS_Group_Do(record.slot, (again || already) ? 1 : 0);
			groupsShown = "";
		});

		holder.appendChild(chip);

		// Offered only when there is a selection this group does not already hold, so it
		// never appears for a press that would change nothing.
		if (mergeable) {
			var merge = document.createElement("button");
			merge.type = "button";
			merge.className = "merge";
			merge.title = "Add the selected units to this group";

			var plus = document.createElement("span");
			plus.className = "slot";
			plus.textContent = "+";

			var adds = document.createElement("span");
			adds.className = "what";
			adds.textContent = "add";

			merge.appendChild(plus);
			merge.appendChild(adds);
			OpenTS_On_Tap(merge, function () {
				Module._OpenTS_Group_Do(record.slot, 3);
				groupsShown = "";
			});
			holder.insertBefore(merge, chip);
		}

		return holder;
	}

	// Runs on every tick rather than from the rebuild, because a group flashes and stops
	// flashing far more often than the bar changes shape, and rebuilding it that often would
	// drop the node under a finger.
	function OpenTS_Show_Group_Alarms() {
		if (!Module._OpenTS_Group_Alarm) return;

		var chips = document.querySelectorAll("#groups button[data-slot]");

		for (var index = 0; index < chips.length; index++) {
			var chip = chips[index];
			var alarm = Module._OpenTS_Group_Alarm(Number(chip.dataset.slot)) === 1;

			chip.classList.toggle("alarm", alarm);
		}
	}

	function OpenTS_Show_Groups() {
		if (!Module._OpenTS_Group_Count || !Module._OpenTS_In_Mission) return;

		var wrapper = document.getElementById("groups");

		if (!wrapper.dataset.pressWatched) {
			wrapper.dataset.pressWatched = "1";
			wrapper.addEventListener("pointerdown", function () { groupsPressed = true; });

			// On the window, because a finger that slides off the bar releases elsewhere.
			["pointerup", "pointercancel"].forEach(function (name) {
				window.addEventListener(name, function () { groupsPressed = false; });
			});
		}

		var playing = Module._OpenTS_In_Mission() === 1;
		var records = playing ? OpenTS_Group_Records() : [];
		var selected = playing ? Module._OpenTS_Selection_Count() : 0;
		var current = playing ? Module._OpenTS_Selection_Group() : 0;

		/*
		 * A unit carries one group, so assigning a selection that holds all of some group
		 * elsewhere would empty that group without saying so. Those slots are the ones no
		 * offer is made for: storing a new group, and merging into any group but the one
		 * being swallowed.
		 */
		var swallowed = records.filter(function (record) {
			return Module._OpenTS_Group_Overlap(record.slot) === 2;
		}).map(function (record) { return record.slot; });

		// A free slot is offered only while something is selected; the bar has nothing to
		// say over a menu.
		var free = 0;

		if (selected > 0 && swallowed.length === 0) {
			var taken = records.map(function (record) { return record.slot; });

			for (var slot = 1; slot <= 9; slot++) {
				if (taken.indexOf(slot) === -1) { free = slot; break; }
			}
		}

		// The home button is always there, so the bar is never empty while playing.
		var signature = (playing ? "h" : "") + records.map(function (record) {
			return record.slot + ":" + record.count;
		}).join("|") + "+" + free + ":" + selected + ":" + current +
			":" + swallowed.join(",");

		if (signature === groupsShown) return;

		// Held until the finger is off the bar; the rebuild happens on the frame after.
		if (groupsPressed) return;

		groupsShown = signature;

		wrapper.textContent = "";

		if (!playing) { wrapper.hidden = true; return; }

		// First and always present, so it never moves: it is the one thing on the bar that
		// does not depend on what is selected.
		var home = document.createElement("button");
		home.type = "button";
		home.className = "clear home";
		home.title = "Go to your base, and back again";

		var mark = document.createElement("span");
		mark.className = "slot";
		mark.textContent = "\u2302";

		var label = document.createElement("span");
		label.className = "what";
		label.textContent = "base";

		home.appendChild(mark);
		home.appendChild(label);
		OpenTS_On_Tap(home, function () { Module._OpenTS_Center_Base(); });
		wrapper.appendChild(home);

		records.forEach(function (record) {
			// Merging is offered when it would take nothing from anywhere else, and when
			// there is anything to add: a group already holding all of the selection would
			// gain nothing from it.
			var takes = swallowed.filter(function (slot) { return slot !== record.slot; });
			var holds = Module._OpenTS_Selection_In_Group(record.slot) === 2;

			wrapper.appendChild(OpenTS_Group_Chip(record, record.slot === current,
				selected > 0 && !holds && takes.length === 0));
		});

		if (free !== 0) {
			var store = document.createElement("button");
			store.type = "button";
			store.className = "store";

			var slot = document.createElement("span");
			slot.className = "slot";
			slot.textContent = "+" + free;

			var what = document.createElement("span");
			what.className = "what";
			what.textContent = "\u00d7" + selected;
			store.title = "Store the " + selected + " selected as group " + free;

			store.appendChild(slot);
			store.appendChild(what);
			OpenTS_On_Tap(store, function () {
				Module._OpenTS_Group_Do(free, 2);
				groupsShown = "";
			});

			wrapper.appendChild(store);
		}

		// A pen has no right click to clear a selection with, so the bar carries one, after
		// the groups it acts on.
		if (selected > 0) {
			var clear = document.createElement("button");
			clear.type = "button";
			clear.className = "clear drop";
			clear.title = "Deselect everything";

			var mark = document.createElement("span");
			mark.className = "slot";
			mark.textContent = "\u00d7";

			var what = document.createElement("span");
			what.className = "what";
			what.textContent = "clear";

			clear.appendChild(mark);
			clear.appendChild(what);
			OpenTS_On_Tap(clear, function () {
				Module._OpenTS_Selection_Clear();
				groupsShown = "";
			});

			wrapper.appendChild(clear);
		}

		wrapper.hidden = !playing || wrapper.childElementCount === 0;
	}

	// A pointer that hovers is a mouse, and a reader with one clicks rather than taps.
	if (window.matchMedia && window.matchMedia("(hover: none)").matches) {
		document.getElementById("sound-label").textContent = "Tap for sound";
	}

	/*
	 * The offer of a native build, which downloads.json names one of per platform. It is
	 * withheld from the shells themselves -- they pass "hosted=1" and are already the thing
	 * being offered -- and from a reader who has dismissed it before.
	 *
	 * Every build is listed rather than only the one that matches, since a visitor is
	 * often not on the machine they mean to install on.
	 */
	// Dismissing shrinks the offer to its icon rather than taking it away, so a reader who
	// wants it back later has something to press. Nothing here is ever unreachable.
	var DOWNLOADS_SMALL = "opents-downloads-small";

	var OpenTS_Download_Names = {
		macos: "macOS", windows: "Windows", linux: "Linux"
	};

	var OpenTS_Arch_Names = {
		aarch64: "Apple Silicon", x86_64: "Intel", i686: "32-bit"
	};

	function OpenTS_Download_Label(build) {
		var system = OpenTS_Download_Names[build["os"]] || build["os"];
		var arch = OpenTS_Arch_Names[build["arch"]] || build["arch"];

		// Naming the chip after Apple's own marketing only reads on a Mac; elsewhere the
		// architecture is what tells two builds apart.
		if (build["os"] !== "macos") arch = build["arch"];

		return system + " (" + arch + ")";
	}

	function OpenTS_Download_Size(bytes) {
		return (bytes / (1024 * 1024)).toFixed(1) + " MB";
	}

	/*
	 * Whether this is a device none of these installers can be put on, which is asked in
	 * four ways because no single one of them is reliable.
	 *
	 * userAgentData.mobile is purpose-built and honest, but Safari has none and it reports
	 * false for an Android tablet. The user agent string catches the phones that name
	 * themselves. An iPad claims to be a Mac, and a touch screen is what separates them.
	 * What is left is the property that actually decides it: a device with no mouse or
	 * trackpad anywhere cannot run an installer, while a touchscreen laptop has both a
	 * coarse pointer and a fine one and is a machine like any other.
	 */
	function OpenTS_Is_Handheld() {
		var data = navigator.userAgentData;

		if (data && data.mobile === true) return true;

		var agent = navigator.userAgent || "";

		if (/Android|iPhone|iPod|IEMobile|Opera Mini|Windows Phone/i.test(agent)) return true;
		if ((navigator.maxTouchPoints || 0) > 1 &&
			/Mac/i.test((data && data.platform) || navigator.platform || "")) {
			return true;
		}

		if (!window.matchMedia) return false;

		return window.matchMedia("(any-pointer: coarse)").matches &&
			!window.matchMedia("(any-pointer: fine)").matches;
	}

	/*
	 * Which build belongs to this machine, answered to a callback because the architecture
	 * has to be asked for rather than read.
	 *
	 * Every Mac reports an Intel user agent whatever it runs on, so reading the string
	 * would offer an Apple Silicon visitor nothing. userAgentData answers truthfully where
	 * it exists; Safari has none, and there the architecture stays unknown. It is a
	 * tiebreaker rather than a requirement for that reason: one build for the platform is
	 * the match whether or not the architecture could be established.
	 */
	function OpenTS_This_Platform(done) {
		var data = navigator.userAgentData;
		var platform = (data && data.platform) || navigator.platform || "";
		var agent = navigator.userAgent || "";
		var system = null;

		if (/mac/i.test(platform) || /Mac OS X/.test(agent)) system = "macos";
		else if (/win/i.test(platform) || /Windows/.test(agent)) system = "windows";
		else if (/linux/i.test(platform) || /Linux/.test(agent)) system = "linux";

		function answer(arch) { done({ os: system, arch: arch }); }

		if (data && data.getHighEntropyValues) {
			data.getHighEntropyValues(["architecture"]).then(function (high) {
				var named = (high && high.architecture) || "";
				answer(/arm/i.test(named) ? "aarch64" : (named ? "x86_64" : null));
			}).catch(function () { answer(null); });
			return;
		}

		answer(/Win64|x86_64/i.test(agent) ? "x86_64" : null);
	}

	function OpenTS_Render_Downloads(builds, platform) {
		var listElement = document.getElementById("downloads-builds");
		var othersElement = document.getElementById("downloads-others");
		var moreElement = document.getElementById("downloads-more");
		var arrowTemplate = document.getElementById("downloads-arrow");

		var ordered = builds.slice().sort(function (left, right) {
			var mine = (platform.os === right["os"]) - (platform.os === left["os"]);
			if (mine !== 0) return mine;
			return OpenTS_Download_Label(left).localeCompare(OpenTS_Download_Label(right));
		});

		var forPlatform = ordered.filter(function (build) {
			return build["os"] === platform.os;
		});

		// The architecture only has to decide between two builds for the same platform.
		var mine = forPlatform.length === 0 ? null :
			(forPlatform.length === 1 ? forPlatform[0] :
				forPlatform.filter(function (build) {
					return build["arch"] === platform.arch;
				})[0] || null);

		var matched = null;

		ordered.forEach(function (build) {
			var link = document.createElement("a");

			if (build === mine) {
				matched = build;
				link.className = "mine";
			}

			link.href = new URL(build["path"], document.baseURI).href;
			link.setAttribute("download", build["name"]);
			link.appendChild(arrowTemplate.content.cloneNode(true));

			var text = document.createElement("span");
			text.textContent = OpenTS_Download_Label(build);

			var meta = document.createElement("span");
			meta.className = "meta";
			meta.textContent = build["version"] + "  \u00b7  " +
				OpenTS_Download_Size(build["size"]) +
				(build["signed"] ? "" : "  \u00b7  unsigned, the system will warn");

			text.appendChild(meta);
			link.appendChild(text);

			// A build for another platform is one press further away: it is not what this
			// visitor came for, and a list of four is a decision where one is an offer.
			(mine !== null && build !== mine ? othersElement : listElement).appendChild(link);
		});

		var others = othersElement.childElementCount;

		if (others > 0 && mine !== null) {
			moreElement.hidden = false;
			moreElement.textContent = "Other platforms (" + others + ")";
			moreElement.addEventListener("click", function () {
				othersElement.hidden = !othersElement.hidden;
				moreElement.textContent = (othersElement.hidden ? "Other platforms (" :
					"Hide other platforms (") + others + ")";
			});
		}

		document.getElementById("downloads-heading").textContent = matched === null ?
			"Desktop app" : "Desktop app for " + OpenTS_Download_Names[matched["os"]];
	}

	function OpenTS_Offer_Downloads() {
		var listElement = document.getElementById("downloads-list");
		var openElement = document.getElementById("downloads-open");

		// Keeping the game here does not depend on a native build existing, so the panel
		// is shown for either and the sections inside it appear on their own terms.
		OpenTS_Setup_Offline();
		downloadsReady = OpenTS_Offline_Available();

		fetch(new URL("downloads.json", document.baseURI).href).then(function (answer) {
			return answer.ok ? answer.json() : null;
		}).then(function (pointer) {
			if (!pointer || pointer["format"] !== "opents-web-downloads-v1") return;
			if (!Array.isArray(pointer["builds"]) || pointer["builds"].length === 0) return;

			OpenTS_This_Platform(function (platform) {
				OpenTS_Render_Downloads(pointer["builds"], platform);
				document.getElementById("downloads-heading").hidden = false;
				downloadsReady = true;
			});
		}).catch(function () {});

		var labelElement = document.getElementById("downloads-label");

		function shrink(small) {
			labelElement.hidden = small;
			openElement.classList.toggle("small", small);
		}

		try { shrink(localStorage.getItem(DOWNLOADS_SMALL) === "1"); } catch (error) {}

		openElement.addEventListener("click", function () {
			listElement.hidden = !listElement.hidden;
		});

		document.getElementById("downloads-close").addEventListener("click", function () {
			listElement.hidden = true;
			shrink(true);
			try { localStorage.setItem(DOWNLOADS_SMALL, "1"); } catch (error) {}
		});
	}

	var downloadsAsked = false;
	var downloadsReady = false;

	/*
	 * Banking every archive into the block store, so a later visit plays with no network.
	 * The engine does the fetching a chunk at a time and this only keeps asking, since one
	 * call for 150 MB would hold the frame for as long as it took.
	 *
	 * Films are not banked. They are streamed by the page's own video element and never
	 * enter the store, and the whole release is ten times the size without them.
	 */
	function OpenTS_Offline_Available() {
		return !!(Module._OpenTS_Offline_Request && Module._OpenTS_Offline_Active);
	}

	// Enough of the set to call it kept. The last blocks of a file are short, and a store
	// that dropped a little under pressure is still a store worth playing from.
	var OFFLINE_ENOUGH = 0.98;

	function OpenTS_Offline_Kept() {
		if (!Module._OpenTS_Offline_Set || !Module._OpenTS_Offline_Stored) return false;

		var set = Module._OpenTS_Offline_Set();
		return set > 0 && (Module._OpenTS_Offline_Stored() / set) >= OFFLINE_ENOUGH;
	}

	function OpenTS_Offline_Amount(bytes) {
		return (bytes / (1024 * 1024)).toFixed(0) + " MB";
	}

	/*
	 * What the browser will do with what was just kept.
	 *
	 * Persistent storage is not a likelihood: once an origin has it the browser does not
	 * clear it, and only the reader can. What is uncertain is being granted it, which
	 * Chrome decides from how the page is used rather than by asking, and which installing
	 * the page as an app is the surest way to earn. The state is read again here rather
	 * than taken from the earlier request, since it can be granted in between.
	 */
	function OpenTS_Say_Durability(noteElement) {
		if (!navigator.storage || !navigator.storage.persisted) return;

		navigator.storage.persisted().then(function (granted) {
			noteElement.textContent += granted ?
				" It is kept until you clear it." :
				" The browser may clear it if it runs short of space; installing this page " +
					"as an app is what stops that.";
		}).catch(function () {});
	}

	function OpenTS_Setup_Offline() {
		var section = document.getElementById("offline");
		var startElement = document.getElementById("offline-start");
		var noteElement = document.getElementById("offline-note");

		if (!OpenTS_Offline_Available()) return;

		section.hidden = false;

		// Already kept, so there is nothing to offer: what a reader wants to know here is
		// that it is, not that it could be again.
		if (OpenTS_Offline_Kept()) {
			startElement.hidden = true;
			noteElement.textContent = "Available offline. The game data is on this device, " +
				"so it starts without a network. Films keep streaming.";
			OpenTS_Say_Durability(noteElement);
			return;
		}

		noteElement.textContent = "Keeps the game data here so it starts without a network. " +
			"Films keep streaming and are not kept.";

		startElement.addEventListener("click", function () {
			if (startElement.disabled) return;
			startElement.disabled = true;

			// Asked for before the store is filled rather than after, since a browser that
			// refuses may evict everything this is about to fetch.
			var persisted = (navigator.storage && navigator.storage.persist) ?
				navigator.storage.persist() : Promise.resolve(false);

			persisted.catch(function () { return false; }).then(function (granted) {

				// The engine banks the archives on its own frames; asking from here is all
				// this can do, because a fetch started from a page callback has nothing to
				// suspend into.
				Module._OpenTS_Offline_Request(0x5A5A0001);
				OpenTS_Keep_Streamed(noteElement);

				var watch = setInterval(function () {
					var total = Module._OpenTS_Offline_Total();
					var done = Module._OpenTS_Offline_Done();
					var running = Module._OpenTS_Offline_Active() === 1;

					if (total > 0) {
						startElement.textContent = running ?
							"Keeping " + OpenTS_Offline_Amount(done) + " of " +
								OpenTS_Offline_Amount(total) :
							"Kept " + OpenTS_Offline_Amount(done);
					}

					if (!running && total > 0) {
						clearInterval(watch);
						startElement.hidden = true;
						noteElement.textContent =
							"Available offline. The game now starts without a network.";
						OpenTS_Say_Durability(noteElement);
					}
				}, 400);
			});
		});
	}

	/*
	 * The score tracks and the transmissions the radar plays are streamed by the page's own
	 * audio and video elements, so they never reach the engine's store. Fetching each one
	 * here puts it in the service worker's cache instead, which is where those elements
	 * will look for it with no network.
	 */
	function OpenTS_Keep_Streamed(noteElement) {
		var manifest = globalThis.__opentsManifest;
		var base = globalThis.__opentsManifestBase || document.baseURI;

		if (!manifest || !Array.isArray(manifest["files"])) return;

		var wanted = manifest["files"].filter(function (record) {
			return record["offline"] === true &&
				/\.(mp4|m4a)$/i.test(record["path"] || "");
		});

		if (wanted.length === 0) return;

		var index = 0;

		// One at a time, so this never competes with the archive fetches the engine is
		// making on its own frames.
		function next() {
			if (index >= wanted.length) return;

			var record = wanted[index++];

			fetch(new URL(record["path"], base).href).then(function (answer) {
				return answer.arrayBuffer();
			}).catch(function () {}).then(next);
		}

		next();
	}

	var downloadsAsked = false;
	var downloadsReady = false;

	/*
	 * Shown only while the main menu itself is up, which the engine reports: the intro
	 * films, the screens the menu leads to and a running game each want the whole window,
	 * and an offer over any of them is in the way rather than on offer.
	 */
	function OpenTS_Show_Downloads() {
		if (!downloadsAsked) {
			if (window.OpenTS_State.frames < 1) return;
			downloadsAsked = true;

			if ((new URLSearchParams(window.location.search).get("hosted") || "") === "1") {
				return;
			}

			// Nothing published here can be installed on a phone or a tablet, so the offer
			// is not made rather than made and left unmatched.
			if (OpenTS_Is_Handheld()) return;

			OpenTS_Offer_Downloads();
			return;
		}

		if (!downloadsReady || !Module._OpenTS_Browser_Main_Menu) return;

		var wrapper = document.getElementById("downloads");
		var wanted = Module._OpenTS_Browser_Main_Menu() === 1;

		if (wrapper.hidden !== wanted) return;

		wrapper.hidden = !wanted;

		// Leaving the menu closes the list as well, so returning to it opens on the chip
		// rather than on whatever was left expanded over the game.
		if (!wanted) document.getElementById("downloads-list").hidden = true;
	}

	/*
	 * The gesture reaches the engine's own listener on the way down, which is what actually
	 * restarts the audio; this only takes the prompt away. A gesture the reader spends
	 * somewhere else counts just as well, so the same handler is armed on the document.
	 */
	soundElement.addEventListener("click", OpenTS_Spend_Sound_Prompt);
	["pointerdown", "touchstart", "keydown"].forEach(function (name) {
		document.addEventListener(name, OpenTS_Spend_Sound_Prompt, true);
	});

	/*
	 * The exports that report a value, named rather than discovered.
	 *
	 * An automated check reads what this collects instead of looking through Module for
	 * something callable: an export that does work rather than reporting it -- asking the
	 * engine to keep the game for offline play, say -- would otherwise be called merely by
	 * something taking the engine's measurements.
	 */
	var OpenTS_Counters = [
		"OpenTS_Audio_Blocked", "OpenTS_Browser_Frame_Height", "OpenTS_Browser_Frame_Width",
		"OpenTS_Browser_Frames", "OpenTS_Browser_Main_Menu", "OpenTS_Browser_Waits",
		"OpenTS_Iso_Ahead_Bytes", "OpenTS_Iso_Ahead_Requests", "OpenTS_Iso_Ahead_Served",
		"OpenTS_Iso_Ahead_Waited", "OpenTS_Iso_Ahead_Waste", "OpenTS_Iso_Background_Left",
		"OpenTS_Iso_Block_Size", "OpenTS_Iso_Bytes_Remaining", "OpenTS_Iso_Deferred",
		"OpenTS_Iso_Fetched", "OpenTS_Iso_Hint_Done", "OpenTS_Iso_Hint_Runs", "OpenTS_Iso_Hint_Soon",
		"OpenTS_Iso_Images", "OpenTS_Iso_Link_Trip", "OpenTS_Iso_Link_Window", "OpenTS_Iso_Probes",
		"OpenTS_Iso_Recalls", "OpenTS_Iso_Requests", "OpenTS_Iso_Soon_Bytes",
		"OpenTS_Iso_Soon_Requests", "OpenTS_Iso_Stall_Ms", "OpenTS_Iso_Stall_Worst",
		"OpenTS_Iso_Stall_Worst_Length", "OpenTS_Iso_Stall_Worst_Offset", "OpenTS_Iso_Stalls",
		"OpenTS_Iso_Store_Bytes", "OpenTS_Iso_Store_Discarded", "OpenTS_Iso_Store_Hits",
		"OpenTS_Iso_Store_Kept", "OpenTS_Iso_Store_State", "OpenTS_Iso_Unique_Blocks",
		"OpenTS_Offline_Active", "OpenTS_Offline_Done", "OpenTS_Offline_Set",
		"OpenTS_Offline_Stored", "OpenTS_Offline_Total", "OpenTS_PGO_Done",
		"OpenTS_PGO_Total"
	];

	function OpenTS_Read_Counters() {
		var counters = {};

		OpenTS_Counters.forEach(function (name) {
			var entry = Module["_" + name];
			if (typeof entry !== "function") return;
			try { counters[name] = entry(); } catch (error) {}
		});

		window.OpenTS_State.counters = counters;
	}

	setInterval(function () {
		if (!window.OpenTS_State.started || !Module._OpenTS_Browser_Frames) {
			return;
		}
		OpenTS_Read_Counters();
		window.OpenTS_State.frames = Module._OpenTS_Browser_Frames();
		window.OpenTS_State.waits = Module._OpenTS_Browser_Waits();
		window.OpenTS_State.syncs = Module.OpenTS_Syncs || 0;
		if (Module._OpenTS_Phase_Stack && Module.UTF8ToString) {
			window.OpenTS_State.phase = Module.UTF8ToString(Module._OpenTS_Phase());
			window.OpenTS_State.phases = Module.UTF8ToString(Module._OpenTS_Phase_Stack());
			window.OpenTS_State.phaseDetail = Module.UTF8ToString(Module._OpenTS_Phase_Detail());
			window.OpenTS_State.phaseSerial = Module._OpenTS_Phase_Serial();
			window.OpenTS_State.pending = Module._OpenTS_Input_Pending();
		}
		OpenTS_Show_Loading();
		OpenTS_Show_Sound();
		OpenTS_Show_Speed();
		OpenTS_Show_Downloads();
		OpenTS_Show_Groups();
		OpenTS_Show_Group_Alarms();
		document.getElementById("status").textContent =
			iso() +
			"phase " + window.OpenTS_State.phases +
			"  |  frames " + window.OpenTS_State.frames +
			"  |  waits carried by the scaffold " + window.OpenTS_State.waits +
			"  |  canvas " + Module.canvas.width + "x" + Module.canvas.height +
			" device pixels at " + (window.devicePixelRatio || 1) + "x" +
			"  |  saves written to storage " + window.OpenTS_State.syncs +
			(window.OpenTS_State.persistent ? "" : "  |  storage is not persistent");
	}, 250);

	// The canvas takes the focus on a click so that a page with other focusable content
	// still sends the game its keys.
	Module.canvas.addEventListener("mousedown", function () { Module.canvas.focus(); });

	// The engine runs inside a suspended call, so a trap surfaces as a rejected promise or
	// as an error thrown at an animation frame rather than as an exception anyone sees.
	// Without this the page simply stops advancing frames and is indistinguishable from a
	// hang.
	function report(what) {
		var line = document.getElementById("fault");
		if (line === null) {
			line = document.createElement("pre");
			line.id = "fault";
			line.style.cssText = "color:#ff8b7a;white-space:pre-wrap;margin:8px 0";
			document.getElementById("status").after(line);
		}
		line.textContent = what;
		console.error(what);
	}

	window.addEventListener("unhandledrejection", function (event) {
		var reason = event.reason;
		report("the engine stopped: " + ((reason && reason.stack) || reason));
	});

	window.addEventListener("error", function (event) {
		report("the engine stopped: " + (event.message || event.error));
	});

	// A JSPI module wraps its suspending imports in WebAssembly.Suspending and its promising
	// exports in WebAssembly.promising, both of them while the module is being created and
	// so before any of the reporting above is reachable. Those two are the whole of what the
	// runtime touches, which is why they and not a version are what is asked for.
	function OpenTS_Has_JSPI() {
		return typeof WebAssembly !== "undefined" &&
			typeof WebAssembly.Suspending === "function" &&
			typeof WebAssembly.promising === "function";
	}

	// "?jspi=off" forces the non-JSPI fallback when diagnosing a browser's JSPI support.
	// "?jspi=ignore" takes the answer above out of the decision and loads the plainly named
	// module whatever the browser said, which is how a NONE build is still reachable from
	// this page.
	var OpenTS_JSPI = new URLSearchParams(window.location.search).get("jspi");
	var OpenTS_Disable_JSPI = OpenTS_JSPI === "off";
	var OpenTS_Ignore_JSPI = OpenTS_JSPI === "ignore";

	// The same engine, twice, differing only in how a wait hands the thread back. The
	// Asyncify build is plain WebAssembly and runs anywhere, and pays for that in size and
	// speed, so it is the fallback rather than the default. Both may sit here at once and
	// this page is generated identically by either configuration, so it names both.
	var OpenTS_Module = "$<TARGET_PROPERTY:OpenTS,OPENTS_MODULE_BASE>.js";
	var OpenTS_Module_Fallback = "$<TARGET_PROPERTY:OpenTS,OPENTS_MODULE_BASE>-asyncify.js";

	function OpenTS_Gate(status, reason, note) {
		// Nothing is going to arrive, so the bar that says something is stops.
		document.getElementById("loading").hidden = true;
		document.getElementById("reason").textContent = reason;
		document.getElementById("note").textContent = note || "";
		document.getElementById("unsupported").hidden = false;
		document.getElementById("status").textContent = "stopped: " + status;
	}

	// Loaded from here rather than from a tag of its own, so that a browser never fetches a
	// module it could not have run. A directory holding only one of the two answers the
	// other's name with a 404, which is a deployment that is missing half of itself rather
	// than a browser that cannot play, and it is said as one.
	function OpenTS_Load(module, gate) {
		var loader = document.createElement("script");
		loader.async = true;
		loader.src = module;
		loader.onerror = gate;
		document.body.appendChild(loader);
	}

	function OpenTS_Boot() {
		if (!OpenTS_Disable_JSPI && (OpenTS_Has_JSPI() || OpenTS_Ignore_JSPI)) {
			OpenTS_Load(OpenTS_Module, function () {
				OpenTS_Gate(OpenTS_Module + " is not on this server",
					"This browser can run the game, but " + OpenTS_Module +
					" is not on this server.");
			});
		} else {
			OpenTS_Load(OpenTS_Module_Fallback, function () {
				OpenTS_Gate(OpenTS_Disable_JSPI ?
					OpenTS_Module_Fallback + " was requested but is not on this server" :
					"this browser has no JSPI",
					(OpenTS_Disable_JSPI ?
						"The non-JSPI build was requested, but " :
						"This browser has no JavaScript Promise Integration, and ") +
					OpenTS_Module_Fallback + ", the build that runs without it, is not on " +
					"this server.",
					OpenTS_Disable_JSPI ? "" :
						"JSPI ships in Chrome and Edge 137 and up and in Firefox 153 and up. " +
						"Safari has it in Technology Preview.");
			});
		}
	}

	OpenTS_Boot();

	// The engine's manifest fetch (manifest.cpp) records here when the game data at the
	// configured base cannot be used, which is otherwise a canvas that never draws. It is
	// surfaced through the same gate as a missing module: incompatible when the server
	// answered with a tree this build cannot read, unavailable when it could not be reached
	// and nothing was cached.
	var OpenTS_Manifest_Gated = false;

	function OpenTS_Check_Manifest() {
		if (OpenTS_Manifest_Gated) return;

		var reason = globalThis.__opentsManifestError;
		if (!reason) return;

		OpenTS_Manifest_Gated = true;
		var base = globalThis.__opentsManifestErrorBase || "the game data server";

		if (reason === "incompatible") {
			OpenTS_Gate("the game data is the wrong version",
				"The game data at " + base + " is not compatible with this version of OpenTS.",
				"The server may be mid-update. Try again later, or point OpenTS at another server.");
		} else {
			OpenTS_Gate("the game data could not be reached",
				"OpenTS could not reach the game data at " + base + ", and no copy is stored " +
				"on this device.",
				"Check your connection, then restart OpenTS.");
		}
	}

	setInterval(OpenTS_Check_Manifest, 250);
