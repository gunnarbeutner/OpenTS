# WebAssembly port design

The WebAssembly target runs the engine in a browser page. It is unsupported;
[Building OpenTS](BUILDING.md) owns how it is built, what it needs, and
[what has been run](BUILDING.md#what-has-been-run). This document records how
the target is put together: how the engine borrows the page's thread, the
Win32 substitute it compiles against, how it reads game data over HTTP, and
the design of each subsystem behind that. It also keeps the judgements that
still bind the port: no threads, one floating-point model, and no
project-served game data.

## Contents

- [1 The thread and the waits](#1-the-thread-and-the-waits)
- [2 The platform seam](#2-the-platform-seam)
- [3 The Win32 substitute](#3-the-win32-substitute)
- [4 Game data over HTTP](#4-game-data-over-http)
- [5 Display, input and the shell](#5-display-input-and-the-shell)
- [6 Audio and movies](#6-audio-and-movies)
- [7 Saved games](#7-saved-games)
- [8 Networking](#8-networking)
- [9 Determinism and threading](#9-determinism-and-threading)
- [10 What remains](#10-what-remains)

## 1 The thread and the waits

A browser tab runs the engine on the thread that services input, layout and
paint, so a function that does not return starves all three. The engine does
not return. A scan of `code/*.cpp` for loop bodies that service `Sleep`,
`Call_Back`, `Windows_Message_Handler` or `OwnerDraw::Dialog_Message_Handler`
finds about ninety loops in thirty-odd files. Roughly nine are incidental
(the frame pacer `Sync_Delay`, the `while (!GameInFocus)` spins, the dialog
reveal wipe), about eighteen are bounded iterations that only service the
callback while doing finite work, and the remaining sixty-odd are genuine
waits on external state: a peer's packets, a click, an audio stream ending, a
whole movie playing inside one call.

The waits nest across subsystems. `Main_Loop` runs a dialog that runs
`Main_Loop` (`OwnerDraw::Dialog_Message_Handler`, capped at one level by a
static guard); a lockstep stall runs a whole frame inside `Wait_For_Players`;
a desync opens a modal message box from inside `Queue_AI`; the modal pumps in
`code/windlg.cpp` and `code/netdlg2.cpp` dispatch messages whose handlers open
further dialogs. Only six sites run a raw message pump; everything else reaches
`Windows_Message_Handler`, which is what makes the thread recoverable from one
place.

### 1.1 What was built

`main` in `code/startup.cpp` calls `Browser_Init`, which attaches the engine to
the canvas and the page's event callbacks, and then `WinMain`. `main` returns
while the engine is still inside it: under the yield scaffold the return is a
promise the page holds, and the runtime is kept alive for it.

Every wait reaches `Windows_Message_Handler` or `Call_Back`, and both reach
`code/browser.cpp`, where the thread is handed back. With `OPENTS_WASM_JSPI`
defined, `Browser_Yield` suspends the engine's stack against an animation
frame and resumes where it left off, so the engine's own loops run unaltered.
The yield is paced: the engine reaches its callback hook far more often than it
draws (a single keyboard read goes through it), so `Browser_Yield_If_Due` hands
the thread back only once an animation frame's worth of time has passed. A
hidden tab gets no animation frames, so the await falls back to a zero-delay
timer; a lockstep peer that simply stops is a game everyone else waits on.

Only `main` is a promising export, so a suspend is legal only once the engine
is inside `main`. Static initialization must return normally, and the engine's
first file open happens there (a static `MapSeedClass` reaches `Fetch_String`
and `RawFileClass::Is_Available` before `main`), which is why the read
transport in [section 4](#4-game-data-over-http) is synchronous.
`Block_Store_Mark_Main` is called from the top of `main` to say a suspension
is now allowed; detecting that by wrapping `Module.callMain` was tried and is
always one call too late, because any C++ code that could ask is already
running inside `main`.

A build without the scaffold (`OPENTS_WASM_SUSPEND=NONE`) still counts the
waits rather than pretending they are gone: `Browser_Blocking_Wait_Count` is
the number that has to reach zero before the option can go, and a build that
reaches a wait freezes the tab and says so once.

`emscripten_set_main_loop` cannot carry the engine. A callback the runtime
invokes is not underneath a promising export, so a JSPI suspend taken inside
it throws `SuspendError`; that was established with a standalone test. It
survives in one place, `wasm/demo.cpp`, which takes no suspend and models the
frame loop the engine has to reach: one call advances the demo by a bounded
number of frames and returns, the pacing `Sync_Delay` does with a sleep is a
predicate, the drawable size is read back each pass, and vsync is the
browser's to decide.

The build uses `-fwasm-exceptions` rather than `-fexceptions` because
`-fexceptions` routes every call that can unwind through an `invoke_` import,
and Emscripten declares every `invoke_` import suspending whenever JSPI is on,
so a static initializer would try to suspend outside the promising boundary
and trap.

### 1.2 Asyncify

A released Safari has no JSPI, so `OPENTS_WASM_SUSPEND=ASYNCIFY` links the same
engine a second way and the page loads whichever module the browser can run.
Nothing is contained: `ASYNCIFY_ONLY` is not used and the whole engine is
instrumented, which is what the size buys. The nesting above did not defeat
the unwind; the in-game dialog that re-enters `Main_Loop` opens and the game
keeps advancing behind it. [Building OpenTS](BUILDING.md#the-cost-of-the-asyncify-build)
measures the price. JSPI stays the default and stays the scaffold to remove.

### 1.3 The outer loop

`Game_Frame` performs exactly one pass of the outer game loop: the editor loop
in a debug build, a parked pass that only pumps the message queue when the
game is suspended, or `Main_Loop` followed by the special dialog. A game with
peers never parks; it pumps the queue once and plays on. On a page
`GameInFocus` follows tab visibility, so a parked game waits on the yield until
the tab is shown. Focus itself deliberately does not park the game: clicking
another window does not stop a browser compositing this one, and a lockstep
game that paused there would stall its peers.

The frame pacer is split into `Frame_Is_Due` (the network timer with peers,
the local one otherwise) and `Service_Frame`, one pass of the remainder of a
frame: the maintenance callback keeps audio, network and the message queue
alive, and unless a dialog is up or the game is parked the tactical view keeps
taking input and redrawing. That wait runs every frame of every game and is
the engine's hottest; the yield there is what keeps the tab answering.
`Call_Back` also yields, because several waits (an audio stream finishing,
mostly) spin on it alone and never reach the message handler. A dialog over a
single-player game runs neither pacer: `OwnerDraw::Dialog_Message_Handler`
pumps the queue and calls `Call_Back`, whose yield is paced, so the pass yields
once itself and the loop waits a frame instead of spinning through it.

On this target `Windows_Message_Handler` has two jobs it does not have on
Windows: it turns what the page reported into messages (`Browser_Service`,
`Win32_User_Service`) and hands the thread back before it leaves. It also
services the audio backend, because the sound driver's own pass runs only from
`Call_Back` and the loading and waiting stretches never reach it, and it runs
`Video_Service_Display`, because it is the one pump the movie player and the
dialog loops all come through.

Startup differs from Windows in the ways a page forces: the mutexes that
exclude a second copy and the AutoPlay shell are skipped (a module is the
whole process), startup does not wait for `GameInFocus` (that would park it
while the tab is in the background), the install intro defaults to off, and
the `ReadyToQuit` wait is skipped because closing the tab ends the run.

### 1.4 The destination

The scaffold preserves exactly the reentrancy [Project direction](DIRECTION.md)
wants removed, and it pins a shipping build to a young VM feature. Flattening
the waits into explicit state is the destination, and the `SpecialDialog`
request-plus-transition-table in `code/conquer.cpp` is the pattern, written by
the engine's own authors. The order that remains, each step independent of
the others: `Wait_For_Players` and `Wait_For_End_Of_Queue` become `Game_Frame`
states; the six `Theme.Still_Playing()` and `Is_Speaking()` loops become
deferred continuations; each dialog that moves onto the portable `GadgetClass`
system takes its modal pump with it; the VQA player becomes a per-frame
advance; then the scaffold is turned off and deleted. The count of remaining
yield points is a number that only goes down, and the likeliest way the port
ends up permanently half-finished is that count being left alone.

## 2 The platform seam

`code/bgfxbackend.h` is the model for every seam: a header of free functions
with no backend type in it, one implementation file per backend, one caller.
Only `bgfxbackend.cpp` includes bgfx and only `code/video.cpp` calls it.
`Backend_Present` takes raw 16-bit 565 pixels the caller keeps owning, which
is what every engine surface holds; a browser backend is a WebGL texture upload
of that buffer, through bgfx's OpenGL ES renderer.

| Seam | State |
| --- | --- |
| Renderer | `code/bgfxbackend.h`, unchanged above the backend. |
| Present policy | `Video_Present_If_Dirty` never waits. On a page presentation is paced by animation frame rather than a refresh-rate interval, one present per `Browser_Frame_Serial`; the engine reaches the present hook from every wait it has, and each present is a texture upload and a draw. |
| File I/O | `FileClass` and its `RawFileClass` specialization sit on the Win32 file API, which [the file layer](WIN32-SUBSTITUTE.md) supplies. Nothing above `FileClass` changed. |
| Surfaces | Every surface scales in software: `XSurface::Blit_From` dispatches to `XSurface::Blit_Scaled` whenever the two rectangles differ in size, so `StretchBlt` and the `AllowStretchBlits` flag are gone. Clipping a scaled blit trims each rectangle independently and changes the scale, so a scaled blit is clipped to its boundary first. |
| In-game GUI | `GadgetClass` and its file pairs draw through `LogicalSurface` with no window handle or message anywhere. Nothing to do. |
| Mouse cursor | `code/wincursor.cpp` turns a shape frame into the page's own pointer; see [5.5](#55-the-cursor). |
| Input | A push interface: `Post_Key_Event` and `Post_Mouse_Event` in `code/keyboard.cpp` take engine-native codes from the page's callbacks. A page reports modifiers with each event and has no key state to ask afterwards, so `code/browser.cpp` holds them and `Put_Key_Message` reads them from there; the browser reports the character with the event, so `To_ASCII` reads back what the platform layer recorded as the key went down. Buttons are never swapped in `Down`, because the desktop has applied that setting before the event names a button. |
| Audio | miniaudio's Web Audio backend under `AudioDeviceClass`; see [section 6](#6-audio-and-movies). |
| Time | `timeGetTime` and the multimedia timers become main-loop polls; see [3.7](#37-process-timers-and-disk). |
| Resources | `code/win32compat/peresource.cpp` reads the shipped language library; see [3.8](#38-language-resources). |
| Sockets | A carrier seam under the tunnel framing; see [section 8](#8-networking). |

### 2.1 The GDI residue

`DSurface` hands out a Windows device context, and four places draw text
through it. `Tactical::Draw_Screen_Text` checks `Is_GDI_Backed`, which is
always false on this target, and where the surface has no device context it
hands the line to `Browser_Draw_Caption`: the page's own text renderer lays it
out on a canvas, a plain sans-serif stretched horizontally until its average
character width over the alphabet matches the cell the GDI call asked for, and
the coverage is blended toward white into the composite surface's 565 pixels
with the line's top on the given row. Flat white needs no dither because only
the antialiased edges carry intermediate values.

The `ownrdraw.cpp` uses are font metrics, not painting: the pixels go to the
engine's own surfaces through its own blitter. [3.6](#36-dialogs-and-controls)
describes the metrics provider that answers them.

## 3 The Win32 substitute

The substitute is shared with every host that builds against it, and [The
Win32 substitute](WIN32-SUBSTITUTE.md) is its document: the layout and ABI
constraints it is held to, how stubs report themselves, the file layer, the
window manager, dialogs and controls, process, timers and disk, language
resources, and exceptions. What this page adds is the page's
half of each seam: the image overlay the file layer answers out of
([section 4](#4-game-data-over-http)), the canvas the window manager draws
into ([section 5](#5-display-input-and-the-shell)), and the yields the waits
make ([section 1](#1-the-thread-and-the-waits)).

## 4 Game data over HTTP

### 4.1 Licensing

OpenTS supplies the engine, not the game data. The engine is GPLv3-or-later
with EA's additional terms; the data is EA's, and the project neither has nor
claims a right to distribute it. So a browser build cannot be a link a
stranger clicks and plays, and two designs are ruled out outright: a preloaded
data bundle, and lazy fetching from a project-hosted origin. The archive set a
page reads is built from files the player owns, by a separate packaging step
(OpenTS-Assets), and the project serves none of it. A self-hosted deployment,
a modder serving their own build beside data they have the right to serve, is
a legitimate configuration that falls out of the same design; it is never the
default.

### 4.2 The manifest

The browser build reads the OpenTS-Assets release format as published:
`assets.json` is the one mutable pointer and names a hashed manifest listing
every archive and film the release holds, each at its own content-addressed
path. `assets.json` must carry `format` `opents-web-assets-pointer-v1` and a
string `manifest`; the manifest must carry `format` `opents-web-assets-v2`, an
array `files` of `{name, path, sha256, size}` records, and no `movies` key.
The archive set carries no disc layout, so none of the search-directory or
mount-order machinery of a real installation exists: one name answers to
exactly one file. Both fetches are synchronous, because the first name is
asked for during static construction; the manifest is fetched once, and a
page with no `assets.json` costs two failed requests once and every later
name resolves to nothing. A run with no game data is not a silent black
canvas: `Manifest_Http_Fetch` records on `globalThis.__opentsManifestError`
why the manifest is unusable, `incompatible` when the server answered with a
tree this build cannot read and `unavailable` when it could not be reached and
nothing was cached, and the page ([`wasm/game.html`](../wasm/game.html))
surfaces it through the same gate it shows for a missing module.
`Module.opentsManifestBase`, set before the module loads, resolves the release
against another origin, which is what a native shell wants when it bundles the
engine and plays from a hosted release.
[Building OpenTS](BUILDING.md#where-the-webassembly-target-finds-game-data)
owns the deployment side.

Lookups scan the flat array rather than an object keyed by name, because
OpenTS-Assets dedupes by content. Names match without regard to case, as the
engine's file layer does; callers still spell one movie two ways.
`Manifest_Find` attaches one HTTP-fetched archive as a single whole-file extent
through `BlockFileClass::Attach_Whole`, reusing `blocksource.h`'s extent-walking
`Read` and `Hint` with no volume descriptor or directory tree to parse, and
caches the volume under the name so a second open reuses the read-ahead and
store the first built up. Once the source is open its own length is trusted over
the manifest's figure. `Manifest_Find_Movie` returns the URL the browser's own
video element fetches directly, and `Manifest_List_Files` serves wildcard
searches such as `MAPS*.MIX`.

The engine's startup path enumerates directories as well as opening named
archives, and `Init_Secondary_Mixfiles` fails the run when no `MOVIES*.MIX` or
`MAPS*.MIX` matches. On this target `SCORES.MIX` is not required: themes play
through the browser's own audio element when the manifest carries a copy
([6.2](#62-music)), and `ThemeClass::Scan` checks both sources before marking
a theme unavailable. The native check is unchanged.

### 4.3 The block source

`code/httpsource.cpp` answers `blocksource.h`'s one question, a synchronous
read at an absolute offset, with a ranged GET. Every count is in 64 KiB blocks:
a read too short to be worth its own request is served from one block, a run's
window is measured in blocks, and the store holds nothing but whole blocks.

**The transport is synchronous and has to be.** Suspending the engine's stack
across a fetch would cost less and not stall the page, but a suspend is legal
only under a promising export and the engine's first file open is not
([1.1](#11-what-was-built)). The thread the page draws on is held for the
length of a request, and a document may answer a synchronous request only as
text, so bytes arrive through a string at two bytes each. A worker is allowed
the array buffer, which is half of why moving the module onto a worker would
be worth doing. A server that ignores the range and answers with the entire
image is rejected, because every read would then cost the whole file. An
answer the browser's HTTP cache served has no network time in it;
`performance` resource entries report `transferSize` zero for exactly that
case, and the caller does not read a rate from it.

**Windows and the store.** Reads arrive small and clustered, so a read shorter
than a block is served from a block-sized window kept in a small LRU set of
thirty-two. Whole blocks are asked for together, so an extent costs one
request rather than one per window, and every block is either wholly fetched
or not at all, which is what makes it fit to store. Fetched blocks are kept in
the Origin Private File System, one directory `opents-iso` holding a sparse
`.dat` of the image's blocks, a `.idx` bitmap of which blocks are present, and
a `.meta` record, per image; so a reload does not pay for the image again. An
earlier build kept the blocks in an IndexedDB database of the same name, which
the page deletes once on load. The store is opened at the first read after
`main` is entered, because the filesystem is reached asynchronously and the
wait is the engine's own suspension. Reads before that, and a build without
the scaffold, go to the network; the store is an optimization, never a
requirement. The record kept beside the blocks is what lets the next run know
what is stored without reading them, and it is arithmetic over a signature and
a list of block numbers, which is what lets "may a stored block be believed"
be tested without a server. Record and blocks are written in one transaction
per batch of thirty-two, so they cannot disagree; another tab on the same
origin can make them disagree, and then the store is left alone for the rest
of the run. Eviction is by age of arrival and decided by the index rather than
the database. A block the engine read is working set and may displace the
oldest stored block; a block nobody has read is a guess that has not come
good, and a full store declines it, or the guessing would leave a full store
holding whichever part of a disc the pass finished on rather than what the
game read. A refused write (quota) leaves earlier batches served and stops
further writes for every image, since the quota belongs to the origin; any
other database error gives the store up for the run.

The store's ceiling comes from `navigator.storage.estimate().quota`, the
allowance rather than what is left of it, because sizing a cache from a figure
the cache itself moves makes it chase its own tail. Each image takes an eighth
of the allowance, bounded at 512 MiB, and a browser that reports nothing gets
160 MiB, sized for the archives a manifest names (a little over 140 MB) rather
than one mission's reading. When `Module.opentsLocalDiscs` is set the host
reads its discs off local storage and the block store is off, because a second
copy of something already at hand costs quota and saves nothing.

**Identity.** An image is identified by the location the page named, made
absolute against the document, not by the URL a request ends at: a pool that
redirects each request to a different node does not change the image, and
keying on the node would refetch it once per node. The signature is location
plus length. The entity tag is deliberately not part of it: a tag is whatever
the server chose to say, and the realistic event for a 1999 game on discs
nobody will reissue is a tag that moved under unchanged bytes, which must not
throw away a store worth gigabytes. The store slot is the location alone, so a
new version overwrites the old rather than sitting beside it unfindable.

**The probe record.** A probe costs a round trip before the engine has read a
byte, one per archive in sequence because the transport is synchronous, and
nothing it answers changes while the location does not. So the answer (length,
round trip and rate) is kept in `localStorage` under the location, one line of
text per image in the form `opents-probe-1|length|trip|rate|`, and a launch
whose locations are unchanged does not probe; `localStorage` rather than
IndexedDB because it must answer synchronously during static construction. A
record from another build reads either way, since anything after the fourth
separator is ignored and an unreadable magic means "probe". What not probing
gives up is noticing an image that changed under its location. A shortened
image refuses a range, which `Revive` catches: it drops the record, re-probes,
and reloads the page if the length moved, because continuing would read some
blocks from the old image and some from the new. A grown image answers every
range, which nothing in the run would notice, so `Watch` re-probes from a timer
well after loading, waited on by nobody, and reloads the page on a mismatch.

**The link.** A request costs a round trip before its first byte and then the
bytes, and the two scale differently, so nothing in the window sizing is a
constant: a server on the same machine and one across the world are the same
code path with the two numbers three orders of magnitude apart. `BlockLinkClass`
measures both from requests the engine makes anyway. A read of at most 8 KiB
is all round trip; one of at least 32 KiB has a rate in what is left once the
trip is subtracted, and only when that remainder is at least 8 ms, because
subtracting two noisy readings is reliable only once what is left is a real
duration rather than timer jitter; between the two sizes a request says
nothing. Both estimates follow readings quickly towards a better one and
slowly towards a worse, and one reading may move an estimate by at most four
times, so a request that queued behind another is treated as a property of the
moment. The window is the bandwidth-delay product doubled (a refill is asked
for once the window is half spent, so the other half must carry the round trip
the refill costs), between two and 128 blocks; a request is the window split
in four, between eight and thirty-two blocks, so a distant link asks for more
at a time rather than more often; the number of requests outstanding grows by
one per 60 ms of round trip from four to eight, because a connection to a
distant server spends its first round trips opening its congestion window.
`Reach` is one round trip's worth of bytes, which is what decides whether a
small file is worth fetching whole. A run opened from a record seeds its link
from what the previous run measured, an opening guess the first readings move
away from.

**Runs.** A movie is read a frame at a time, and a read that leaves the last
block costs a round trip the decoding cannot overlap. So a read that
continues a run is also the moment the blocks ahead are asked for, with an
ordinary asynchronous fetch left in flight rather than waited on, which lands
while the engine is decoding; the asking happens before the read that opened
the window is paid for, so a clip opened inside one frame spends one round
trip on two requests. A run is followed because it was declared or because it
was noticed. The file layer declares one when it opens a file (a whole-file
attach is one extent with a known end), and a declared run is believed at
once, opens its window at once, reaches at least 4 MiB and at least four times
the last read whatever the link is measured to be worth, and never past the
end of its file. An undeclared run has to be noticed, which costs two forward
reads, and may reach no further ahead than the reading has already covered.
Four runs are followed at once, because a clip playing in the sidebar streams
beside a mission's map, artwork, speech and music, and with a single cursor
each of those reads would end the movie's run; a read that continues none
takes over the run that has gone longest without one, and only that run's
outstanding span is given up. A declaration is only remembered, never acted on,
since the engine opens far more than it reads; the reading decides. Reading
ahead is a guess paid for on somebody's connection, so its cost is held to a
tenth of what has been fetched (at least 1 MiB); over that the window closes to
a single request until the reading catches up.

**Hints and the idle pool.** A hint exists because the file layer knows three
things the source cannot: that a run is one file to be read front to back
(`BLOCK_HINT_SEQUENTIAL`), that a file not yet opened is likely to be wanted
and worth fetching only while nothing else is (`BLOCK_HINT_SOON`), and that a
run it said that about has been given up (`BLOCK_HINT_DONE`), which is what
stops a skipped cutscene being fetched to its end. `Hint` is advisory;
`Prefetch` waits for what it asked for and banks it, so a caller that returns
has the bytes, and it is legal only under the promising export.

A soon hint is queued for a drainer that asks while the image is idle, and its
answers go to the store whether anything reads them, which is what makes a
guess worth having even when this run does not take it. The drainer does not
wait for the reading to stop, because on a distant link the gap between two
reads is shorter than one round trip and a guess made only while nothing is
read is never made at all. What is held down is how many guesses are
outstanding at once over every image: two, because a browser gives a page about
six connections to an origin and the synchronous read must be able to take one
the moment it wants it. A queued range a span already covers is not queued
again, so a whole-file precache and a later narrower hint for one entry inside
it cost one request between them. A hint covering a whole file asks for it in
one plain GET, which is what a CDN caches and serves most simply, rather than
a request per block; a store holding part of the file asks for the missing
patches instead, at most eight, the last running to the end of the file. The
request size is kept on the range rather than the plan, so a whole-file gap and
an ordinary guess queued to one image are not fragmented alike. A span's bytes
are read as they arrive, so a span answers as soon as the bytes a read wants
are in it, and a read far ahead of a slow whole-file download is answered by
the transport rather than by waiting for the gap to stream in, once that gap
exceeds one round trip's worth. Sixteen blocks are moved into the store per
read, because the copying happens inside a read. When a run wants the
connection back, further guessing stops at once and what is in flight is left
to finish. The pool is keyed by URL rather than by instance, because a
`MixFileClass` entry closes and reopens its archive several times a run.
`Service_All` runs at most once per 16 ms from every wait, so what a large
background fetch delivered while nothing was reading is not left in the pool
unpersisted when the page closes.

**What the engine says.** `PrefetchType` says how the engine will read a name
it registers, which decides how much of it is worth having before it asks: an
archive read across a session (`PREFETCH_WHOLE`) is worth having whole, since
there is no run in the reading to get in front of; an archive the engine
streams one entry out of (`PREFETCH_STREAMED`), a disc of video whose entries
are films, is worth its directory at the front and its first 2 MiB, because
only the player knows which film they will watch. Size is not consulted.
`CONQUER.MIX` is read on demand because most of its entries are art a session
never opens; the map, multiplayer and speech archives are read on demand; the
movie archives are the only ones streamed. `CDFileClass::Prefetch` names a
file the engine knows it will want before it opens it (a menu reads the names
of the videos its items play when it is built), which turns the seconds the
player spends reading the screen into the time the bytes arrive in; nothing in
it fetches, it only resolves a name to the run it occupies. `Abandon` marks the
end of reading, which is what stops the rest of a skipped film being fetched.
`Read_Scenario` hints `TIBSUN.MIX` and `EXPAND01.MIX` whole as soon as a mission
has a name, because the speech, theatre and local data a mission reads a
piece at a time would otherwise each arrive as a stall.

**Deferred reads.** `DeferredReadClass` is a scope in which a read may answer
that the bytes are not here yet instead of fetching them. For a map or a cameo
waiting is the right trade; for music it is the wrong one, because a score the
game waits for costs a frame and one it does not play yet costs nothing. A read
that declines delivers nothing and leaves the file position where it was, so
asking again reads the same run; `Declined_Now` tells that apart from the end
of the file. A source with the bytes at hand never declines, so every other
target behaves the same with or without a scope. Music streams are opened
deferrable and speech is not; a deferred stream leaves the disc alone for
100 ms between attempts and gives up deferring after 8 s, so a score whose
bytes never arrive does not hold its sample slot for the run.

**Fetch profiles.** A fetch profile names every range reaching a point in the
game reads, recorded by [the harness](HARNESS.md#fetch-profiles) and published
as an object of the release it was captured against, which `assets.json` names
alongside the manifest. `PGO_Profile_Apply` can therefore reach only the profile
belonging to the release being served; the ranges are fetched and banked ahead of every archive registration, and the
per-archive prefetch heuristic stands down while a profile is in effect, since
the profile has already fetched what it names. A profile that banked nothing
(a private window, a store that declined) leaves the heuristic running. A
capture (`-PGOCAPTURE`) reads only what the game asks for, so applying a
profile while recording one would write the profile's own ranges into the
next; `Prefetch` and `Read_Scenario`'s hints also stand down during a capture.

**Figures.** The source exports its counters (requests, bytes, store hits,
read-ahead waste, stalls with the read that stalled) to the page as
`OpenTS_Iso_*` values and a stall log on `OpenTS_State`;
[the harness](HARNESS.md#observing) reads them. The rate is not exported,
because a handful of samples is only a floor the window sizing reads out of;
a progress page wants `OpenTS_Iso_Bytes_Remaining`, which only moves toward
zero.

## 5 Display, input and the shell

### 5.1 The frame follows the canvas

The page sizes the canvas and resizes it whenever it likes, so the frame
follows the canvas. The drawing buffer is matched to the laid-out box in
device pixels, which keeps the browser from compositing the frame through a
second, softening rescale; the frame itself is sized in CSS pixels, or a
sidebar of a fixed number of device pixels would come out half as wide on a
display with two device pixels per CSS pixel. The game starts at the canvas's
size rather than the configured resolution; `?display=WIDTHxHEIGHT` pins a
frame the presenter scales into the window and `?display=scaled` keeps the
configured resolution and scales it. The display options offer no resolutions
on a page; they offer interface sizes instead ([5.2](#52-interface-scale)).

Noticing a new size is cheap and acting on it destroys and rebuilds every
drawing surface, so the two are separate. `Browser_Service` reports the
canvas each pass, and `Video_Service_Display`, at the bottom of the message
pump, is the only place the frame is resized. It waits until the canvas has
held still for 250 ms, so a drag is one mode change at the end rather than one
per animation frame, and it is refused while a scenario is loading, while a
movie holds a surface (a movie keeps the surface it was created on until it is
torn down), while a shell screen is up (shell screens lay their artwork out
against the size they came up at), and while a dialog is painting. A shell
screen that sees a pending resize puts itself away and the resize proceeds
with nothing laid out against the surfaces. The frame is clamped to a multiple
of four, both minimums scale together so the shape survives, and above
2560 by 1600 the window is filled by scaling because every frame is a full
upload. While the frame follows the window the video options are kept equal to
the frame size, since a scenario sizes its surfaces from them.

The window the engine believes it has covers the screen, and on a page the
screen is the canvas, so on a size change the main window is moved to cover
the new canvas before anything else, or clicks past the old edge are dropped
while the cursor still tracks. The presenter follows the canvas at once by
scaling the frame it already holds, so the picture is never absent while the
window is dragged. The list of display modes the engine can enumerate is the
canvas's own size plus the familiar 4:3 and widescreen sizes that fit the
display, in CSS pixels, rebuilt at the start of each enumeration.

### 5.2 Interface scale

The sidebar takes a column of the screen and the tab strip a row of it; the
tactical view is what is left. Every surface a scenario draws into is sized
from that division, which `Compute_Screen_Layout` in `code/screenlayout.cpp`
owns. The sidebar surface keeps the interface's historical metrics whatever
the screen is and `Blit_Sidebar` magnifies it by the interface scale on its way
to the screen; every sidebar gadget is clicked on the screen but drawn onto
the sidebar surface, so a button carries screen and surface coordinates both.
The tab strip is not magnified, because it is drawn into the composite,
sidebar and tile surfaces alike. The option's range, the automatic rule and
the clamps are documented on the manual's `UIScale` page. A new scale lays the
screen out again through `Change_Display_Mode` with the same size on both
sides, the rebuild a resolution change makes.

### 5.3 Shell screens

Shell pages are pre-rendered artwork with clean edges, so they are magnified
with the sharp filter, a sinc windowed by the first three lobes of a wider
sinc; magnified lettering reads as drawn rather than as blocks or blur. A
shell screen claims its design space with `Set_Shell_Size` as it comes up and
maps rectangles by both edges rather than corner and size, so neighbouring
rectangles meet exactly and a scaled blit is never handed a region clipping
would rescale. The magnified page is cached with the design-space picture it
was resampled from, and only what differs is resampled again, grown by the
filter's tap count so every page pixel that read a changed source pixel is
recomputed. `Fill_Out_Shell` carries the magnification into the surfaces
themselves for a screen that stays on the display behind a dialog laid out in
frame pixels, which is what the title screen does as it loads. The graphic
menu reads its design space from the backdrop rather than assuming 640 by 400,
picks an item on release rather than press, and puts itself away for a pending
resize so `Display_Menu` rebuilds it against the new frame.

A page has no sockets and no serial line, so the menu offers a LAN game only
where the deployment names a relay ([section 8](#8-networking)), shows
Internet, modem and World Domination Tour games disabled, and drops the exit
entirely, because a canvas the engine has stopped drawing to is worse than no
choice. A withheld choice is dimmed from a copy of its ordinary face, using the
highlight image to find the lettering, because only a few choices were ever
drawn a disabled face. A menu backdrop movie keeps its 640 by 400 rectangle
whatever the movie stretching option says, and all three page backdrops are
prefetched when the shell comes up.

### 5.4 Pointer, wheel, touch and typing

The engine records a mouse event as a key plus a position. Page callbacks may
run while the engine is suspended part way through a wait, so callbacks only
write scalars and queue events, and `Browser_Service` drains the queue in
engine context. Key repeats are dropped as Windows drops them. The keyboard
listener is on the window, because a canvas only receives key events while
focused; the context menu is suppressed because the right button is the
engine's primary command. A full-screen game's cursor cannot leave the screen,
and the engine reads a pointer held against an edge as a standing request to
scroll, so the position a pointer left the canvas through is pinned to the
edge until it re-enters or the page loses the keyboard.

A wheel bypasses the event queue: the page reports distance travelled rather
than notches, so travel is accumulated and one `WM_MOUSEWHEEL` is posted per
notch's worth, or a trackpad's stream of small deltas would fly the build list
past what the player reached for. A scroll over the tactical view pans the map
instead, the one place the browser layer decides something by pointer position,
because the engine has no notion of a wheel over the map. `Mouse::Is_Hovering`
tells the tooltip, the edge scroll and the placement cursor whether a pointer is
resting there; a finger that only reports where it touched leaves nothing
resting, and a wheel implies a resting pointer.

Touch gestures are documented on the manual's touch controls page. What the
code depends on: slop is 10 CSS pixels and a hold 450 ms whatever the pixel
ratio, a pan is carried as a fraction with no inertia (a pan that continues
after the finger lifts cannot be stopped over a unit), a third finger is
ignored, a second finger landing releases a button the first held, which
control owns a point is the engine's own hit test, and a touch during a movie
is delivered as escape to the keyboard buffer only, so it cannot open the
options dialog behind the movie.

A device whose only keyboard is on screen shows it only while a focusable
element holds the focus, so `Browser_Begin_Text_Input` focuses a hidden input
inside a form and `Browser_End_Text_Input` blurs it; the focus request is
renewed on every touch while text is wanted, because a page raises its
keyboard only in answer to a gesture. Ordinary keys bubble to the window
listener. Characters that arrive without a real keydown (a soft keyboard
correcting or composing) are diffed against the field's previous value rather
than the field being emptied, because emptying takes the text out from under a
composition, and delivered to the dialog path and, where a US layout key
produces the character, as a press and release of that key. The action key is
`go` rather than `done`, because `done` only dismisses the keyboard, and the
form submit is the last resort for a return, skipped within 200 ms of one
already accounted for.

### 5.5 The cursor

A page composites a cursor over the frame through the canvas's CSS cursor
property, so the game's shape becomes a PNG data URL the page draws, and the
cursor never touches a game surface. A page scales the image by the device
pixel ratio itself and refuses one over 128 pixels, showing nothing rather
than a clipped one, so the image is built at the largest whole scale that
fits. Windows leaves the screen without a cursor only until the next mouse
move restores the class cursor; a page raises no such message, so the null
cursor falls back to the page's own pointer and the blank cursor stands for
"none". Capture is bookkeeping, because a page delivers every mouse event over
the canvas to the canvas.

## 6 Audio and movies

### 6.1 The backend

The engine mixes its own audio in `code/audio/` and reaches the speakers
through `AudioDeviceClass`, so the page needs no driver of its own: miniaudio
carries the mix, and its Web Audio backend is the one this target enables.
`thirdparty/CMakeLists.txt` asks for that backend and the null one and leaves
the rest out, and it does not ask for AudioWorklets, because those need WASM
workers and the cross-origin isolation headers a page may not have. The
ScriptProcessorNode path miniaudio falls back to needs neither.

A page will not start audio until the reader has interacted with it. The
library installs its own handlers for the gesture event types and resumes
every started context from them, so nothing here polls for the refusal being
lifted.

What the page does have to supply is the thread the feeder would run on.
`AudioFeederClass` keeps the streams filled and restarts a device that stopped
on its own, and it runs those passes on a thread of its own everywhere else; a
page has no thread to give it, so `AudioEngineClass::Init` leaves the thread
unstarted here and `AudioEngineClass::Service` runs one pass instead. The
engine's waiting stretches reach it from `Windows_Message_Handler`, and the
movie player's loop reaches it from `VQA_Message_Handler`, which is that
loop's whole service point. Both film loops wait for a frame on every pass
rather than only when one is already due: a film advances a frame at a time,
so a pass that returns early only spins.

### 6.2 Music

A track the manifest carries a browser copy of plays through the page's own
audio element, looked up like a movie from the theme's legacy name, so it does
not pass through the mixer at all; `Music_Browser_Play` returns -1 when the
manifest carries none and the caller falls back to the stream the mixer opens,
which is opened deferrable ([4.3](#43-the-block-source)). `ThemeClass` holds
the page's handle apart from the mixer's, because only one of the two ever
plays. Fades are in milliseconds, as the mixer's are. A track that autoplay
blocked is retried on the same first-gesture unlock the movies use.

`AudioEngineClass::Focus_Loss` pauses the mix, and the audio element is not in
it, so `Focus_Loss` silences the element itself and `Focus_Restore` starts it
again. Only what that pause silenced resumes: a track autoplay is still
holding stays held, and one that ended in the meantime is left alone.

### 6.3 Movies

`OPENTS_MOVIE_FORMAT=MP4` builds `code/mp4.cpp` behind the same `Movie_*` seam
the VQA player sits behind (`code/movieformat.cpp` selects the extension). A
movie the manifest names is streamed by the browser's video element from its
URL, fetched and cached like the page's modules, with no archive read and no
whole-file copy through WASM memory; the fallback loads the file into a blob
URL. A browser refuses autoplay with sound but not muted, so a blocked movie
starts muted and the gesture gate only brings the sound back. Firefox on
Android hands a hardware-decoded frame to WebGL but not to a 2D canvas, so the
frame is uploaded as a texture and read back off a framebuffer, through one
WebGL context for every movie, with the 2D canvas as the fallback.

A fullscreen movie is read at the decoder's colour depth and queued on its own
32-bit layer over the frame texture, against a rectangle fitted to the window
fresh every frame, because a resize during playback is not serviced by
`Video_Service_Display` and a rectangle computed once would keep the aspect
the window had then. An inline movie composited into a panel writes through
the 16-bit draw buffer, with a 4 by 4 ordered dither scaled to each channel's
step, as the studio's own VQA encodes carry one. A stretched frame is
resampled with the smooth filter, a tent widened to 1.4 source pixels, which
takes out most of the ordered dither a 16-bit quantization carries while
keeping the structure a frame holds between a tenth and a quarter of a cycle
per pixel. A movie stepped alongside the game (the radar, the mission screen)
has no playback loop of its own, so `Movie_Advance_Frame` services the timer
that carries its sound. Anything that replaces the drawing surfaces waits until
no movie handle is live.

## 7 Saved games

Every object record begins with the 16-byte class identifier its
`GetClassID` reports, and the reader creates the object through the class
table `startup.cpp` fills, so the identifiers on disk are the registered ones
on every target. The record body is written and read through
`SaveStreamClass` over a byte buffer, and the file around it is the engine's
own, [documented on its own page](SAVE-FORMAT.md). The same
`code/savefile.cpp` writes and reads it everywhere, so a save crosses between
the builds, and `tests/save` covers it.

Where a save lives is [the file layer](WIN32-SUBSTITUTE.md#3-the-file-layer)'s
persistent directory.

A save that only exists inside one browser's storage is easy to lose and
impossible to move, so the load and save dialogs carry an export and an import
button on this target. `code/loaddlg.cpp` adds them at `WM_INITDIALOG` from the
accept button's own rectangle, because the owner draw system has already
rescaled the dialog by then and a rectangle worked out from the client area
would be in the wrong units. They are `BS_OWNERDRAW`: that system paints a
button only as a group box, an owner drawn button or an auto checkbox, and
`OwnerDraw::Adopt_Control` hooks each one, since `Subclass_Dialog` has run
before a dialog procedure hears about the dialog at all.

Neither direction lets the page decide where a save belongs. Export reads
through `RawFileClass` and hands the bytes to a download; import picks a file,
checks the MS-CFB signature, and returns the bytes for `Pick_Filename` to name
and `RawFileClass` to write, which also gets the IndexedDB flush that any write
through the file layer already triggers. Import needs no selection and the load
option is offered even when no save exists, or the one dialog that can bring a
save in could not be opened until one already had.

## 8 Networking

The only socket in the tree is one UDP datagram socket in `code/wspudp.cpp`,
and its tunnel mode already makes every datagram carry a four-byte header
naming sender and recipient by id, sent to a single relay address.
`Carrier_Send` and `Carrier_Receive` are the seam under that framing: the
routing header, the send queue, the retry logic and the game are written once
above them. A page can neither broadcast nor open a datagram socket, so
`code/wsrelay.cpp` supplies a WebSocket carrier that reaches
[the relay](RELAY.md) in place of a network, one server forwarding by the
recipient id the header already carries; `docs/RELAY.md` owns the protocol.

The relay URL comes from `?relay=`, falling back to `relay.json` beside the
page, and `?room=` defaults to `lan`, so one relay is one LAN. The connection
names the build by the module's hashed file name, so the relay can hold a
room to one build; two builds in a lockstep match desync, and a desync does not
look like the version mismatch it is. `Open_Socket` waits for the greeting,
yielding to the page, bounded at ten seconds, because the engine sets its
network up in one call. Message arrivals are queued rather than announced,
because the page's event loop can run while the engine is suspended inside a
fetch, and the transport's service pass drains that queue as it would drain a
socket. A relayed broadcast arrives naming the broadcast id rather than the
receiving player, so the interface registers that id as its own. Cross-play
with native peers is a relay question, not a wire-format one; what stands in
its way is [determinism](#9-determinism-and-threading). The engine's unique
session id comes from `crypto.getRandomValues` on a page, because the Win32
identity's inputs are all constants there and two tabs opened together would
share one.

Westwood Online (`code/wonline.cpp`) drives a retired service through ATL and
is excluded from the target; `wonlinestub.cpp` supplies what the rest of the
engine references.

## 9 Determinism and threading

The engine is lockstep with a per-frame CRC over integer positions and facings
and the random seed. Float state is not in the sync check, so drift is
invisible until it crosses an integer boundary. The basic operations already
agree bit for bit: the MSVC build is `/fp:precise` with no reassociation, and
wasm's arithmetic and `sqrt` are correctly rounded instructions, as long as no
fast-math option is introduced. The risk is confined to the transcendental
functions, about a hundred and ten calls over twenty-four files plus
`code/velocity.h`, where musl's libm and the MSVC CRT differ in the last bits,
and the sites that turn one into integer simulation state (a launch pitch from
`acos`, a heading from `atan2`, a weaving missile's turn rate from `sin`)
decide a desync. Wasm against wasm is safe by construction; wasm against
native is not without a libm pinned inside the simulation, which changes the
native build's simulation too and is a project decision at a compatibility
boundary, not a porting task. `Object_CRCs` hashes floats by bit pattern and
has no callers; wired into a replay harness it would pinpoint the first
diverging object.

The port needs no threads, and the reason matters. The engine's concurrency
(two multimedia-timer callbacks feeding audio, and the crash reporter's own
threads) exists only to keep audio fed while the main thread blocks. Once the
timers are main-loop calls ([3.7](#37-process-timers-and-disk)), the locks are
statements that there is one thread: every mutex acquisition succeeds at once,
ownership is still counted so an unmatched release fails, a named mutex is
always the first of its name because one module cannot see another's, and the
interlocked operations are plain arithmetic. An event is something one thread
waits on for another to signal, so the event functions keep no state and
report themselves. So the build needs no pthreads, no `SharedArrayBuffer` and
no cross-origin isolation, can be served from any static host, and keeps
testing deterministic. If a second thread is ever introduced, the mutexes
become wrong rather than merely unimplemented.

## 10 What remains

- The waits are carried by the scaffold rather than flattened
  ([1.4](#14-the-destination)).
- The native build and the browser build cannot share a game until a libm is
  pinned in the simulation, which may reasonably be declined; then browser and
  native players do not share games.
- A browser build is "bring your own install" by licensing, which narrows its
  audience to existing owners, modders and contributors.
- The version and icon resources are not built; nothing replaces them.
- `SetCursorPos` is a stub, so the edge-scroll warp does nothing.
- [Building OpenTS](BUILDING.md#what-has-been-run) records what has and has not
  been observed at runtime, including that no part of the port has been
  compiled with MSVC.
