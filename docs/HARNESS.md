# The browser harness

`tools/harness/harness.py` is the one way to run the WebAssembly build of the
engine in a browser and act on it. It serves a build over range requests, starts
a browser it owns and takes it down again, waits for the engine to reach a named
state, sends input in the game's own coordinates, and compares screenshots.

Everybody who needed those things wrote them again, and every hand-written
version went wrong the same ways: two runs picking the same port, a profile
directory nobody removed, a browser outliving the run that started it, a click
translated into canvas pixels by hand and landing somewhere else. So it is done
once. **Use this rather than writing another one**;
[the engine source instructions](../code/AGENTS.md) make that a rule.

[Building OpenTS](BUILDING.md) owns how a build is produced and what the
WebAssembly target is; this page owns what to do with a build once it exists.

## Contents

- [Setup](#setup)
- [Running](#running)
- [States](#states)
- [Game coordinates](#game-coordinates)
- [Targets](#targets)
- [Startup options and SUN.INI](#startup-options-and-sunini)
- [Observing](#observing)
- [Frame stepping](#frame-stepping)
- [Comparing screenshots](#comparing-screenshots)
- [Fetch profiles](#fetch-profiles)
- [Cleanup](#cleanup)
- [Why it is not a test](#why-it-is-not-a-test)
- [What has been run](#what-has-been-run)

## Setup

There is nothing to install. The harness has no third-party dependencies:
it speaks the DevTools protocol over a socket, answers range requests out of
`http.server`, and decodes a PNG with `zlib`, all on the standard library.
`tools/harness/requirements.txt` owns that decision and pins nothing; it is not
the manual's requirements file and must not be merged into it. The harness does
not use the manual's virtual environment and does not need one.

It needs Python 3.9 or newer, a Chromium-family browser already on the machine
(Chrome, Chromium or Edge; `OPENTS_CHROME` or `--browser` names another), and
the game data. Ask what is missing:

```bash
python3 tools/harness/harness.py doctor --bin build-wasm/bin
```

`doctor` reports the Python, the browser and its version, the asset tree --
its manifest hash, how many files it names, which profiles it carries, and any
named file that is missing -- the build directory's page and modules, and any
browser an earlier run left running. `doctor --reap` ends those and removes
their profiles.

The game data is an OpenTS-Assets web tree, the `build/web` that repository's
`build.py` writes: `assets.json` at the root and everything else under
`assets/`, laid out exactly as [a deployment serves it](BUILDING.md#in-a-container).
The harness serves that tree beside the build directory at the same paths, so
the page and the engine read it the way they read a deployment's. `--assets
DIR` names the tree; unset, `OPENTS_ASSETS` names it, the same variable the
container is pointed at it with, and failing that `~/OpenTS-Assets/build/web`
is looked for. The tree is read from wherever it is, and is never copied or
linked into the repository or into a build directory.

The tree decides the film format: an OpenTS-Assets web tree carries the films
as MP4 under their archive's name, so the build it is served beside has to be
[the MP4 configuration](BUILDING.md#build-options), as the container's
is. A VQA build fails secondary initialization against it, since the film
archives it looks for are not files in the tree.

`--asset PATH` serves one extra file at its own basename, repeatable, ahead of
the build directory: a hand-built `manifest.json` and the archives or films it
names, for a run against data that is not a published tree.

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin \
    --asset manifest.json --asset TIBSUN.MIX --asset MULTI.MIX ...
```

Disc images are not served. The browser build resolves every archive and film
through the manifest, and nothing in it reads `?image=` any more.

## Running

`run` serves a build, drives it through a list of steps, and tears everything
down. Steps are given with `--do`, one to a flag, carried out in the order
written; `--script FILE` reads them one to a line instead, and `-` reads them
from standard input.

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --out /tmp/run \
    --do "to-menu" \
    --do "shot menu.png" \
    --do "click @NSEL_OPTIONS" \
    --do "wait dialog" \
    --do "shot options.png" \
    --do "click text:Cancel" \
    --do "wait menu"
```

The steps:

| Step | What it does |
| --- | --- |
| `wait <what> [seconds]` | Waits for a [state](#states): a phase, a milestone, `not:STATE`, `any:STATE\|STATE`, `event:NAME`, `ui:TARGET`, `log:REGEX`, `frames:+N`, `js:EXPR`, or a number of seconds. The second word is the timeout. |
| `sleep <seconds>` | Waits, and asks nothing. |
| `try <step>` | Carries out the step, and reports rather than fails the run when it cannot: a button that is only sometimes there. |
| `ui [path]` | What is on screen, as the engine describes it: the phase, the menu items, the windows, the gadgets. Printed as one line, written as JSON when a path is given. |
| `move <position> [mods]` | Moves the mouse. A position is `<x> <y>` in game coordinates or a [target](#targets). |
| `click`, `down`, `up` `<position> [button] [mods]` | `left`, `middle` or `right`; `click` presses and releases. |
| `drag <x1> <y1> <x2> <y2> [button] [mods]` | Press, two moves, release. |
| `wheel <x> <y> <dx> <dy>` | A wheel event at a position. |
| `tap <position>` | A touch down and up in one place. |
| `touch <down\|move\|up> <x> <y> [<x> <y> ...]` | A gesture spread over several steps. Every step names every finger still on the glass, so a second pair of coordinates is a second finger arriving or moving. |
| `key <name>` | `escape`, `enter`, `f1`, `a`, `1`, `ctrl+s`, and the rest of what the engine's own key table names. |
| `keydown <name>`, `keyup <name>` | The two halves of `key`, so a modifier can be held across the steps that follow. Pair them: a `keydown` the run never releases leaves the key held for the rest of it. |
| `text <string>` | Typed one key at a time. |
| `skip-movies [seconds]` | Sends escape to each film in turn until the engine is in a phase that stays: a menu, a dialog, a load, or the game. |
| `to-menu [tibsun\|firestorm] [seconds]` | Through the films and the disc chooser to a main menu, Tiberian Sun's unless told otherwise. Reports the menu page reached and its items. |
| `hold`, `step [N]`, `release` | [Frame stepping](#frame-stepping): park the engine at its next frame wait, let it through N waits, let it run. |
| `profile start [interval]` | Starts Chrome's sampling CPU profiler, at this many microseconds between samples (100 by default). |
| `profile stop <path>` | Stops it and writes a `.cpuprofile`, which Chrome's own performance panel opens. Reports the samples taken and the span covered. |
| `shot <path>` | A PNG screenshot. |
| `state [path]` | `OpenTS_State` and every counter the module exports, as JSON. |
| `log [path]` | Everything the page and the engine printed. |
| `diff <a> <b> [pixels]` | [Compares two screenshots](#comparing-screenshots); fails the run over a pixel budget. |
| `eval <javascript>` | An escape hatch. Its value is printed. |
| `expect <javascript>` | The same, and a false value fails the run. |

`mods` on a mouse step is one word of `ctrl`, `alt`, `shift` or `meta`, joined
with `+`, and it rides on the event itself the way a browser reports the
modifiers a click was made under. That is what a chord such as Ctrl to force
fire, Alt to force move or Shift to add to the selection is expressed with; the
engine reads the modifiers off the event, so `click 300 430 left ctrl` is a
Ctrl+click and nothing outside the tab is touched. `keydown ctrl` holds the key
as well, for the code that reads the key rather than the event.

**Every input step waits for its own consequence.** After the event is sent,
the step waits until the engine reports nothing pending -- the page's event
queue, the keyboard buffer and the window message queue all empty
(`OpenTS_Input_Pending`) -- and one more frame has been drawn, up to `--settle`
seconds (three by default). The step's result says whether that happened
(`drained`) and how many frames it took. A click that lands during a film or a
load is not read by anything; it is reported with `drained: false` and a line
on the console, and the run goes on, because that is sometimes the point.
`--settle 0` sends and moves on.

A relative path in a step lands under `--out` (the working directory by
default). `--report PATH` writes the whole run — every step, its result and how
long it took, the complete log, every event the engine reported, the last
state, and what the server was asked for — as JSON. A failed step adds
`failure`: the step, the error, the phase, what was on screen (`ui`) and a
`failure.png` written under `--out`, so a run that stops explains itself
without being run again. `served` totals the requests by path; `requests` is every request in the
order it arrived, each with `at` (seconds since the server started), `path`,
`status`, `start` and `length` (the byte range answered) -- for a run that
wants the shape of its own traffic, such as whether a prefetch is asking for
many consecutive small ranges instead of the one request a whole span would
take.

The run exits non-zero when a step fails or a wait times out, and `130` when it
is interrupted. `--headed` shows the browser, `--hold` keeps the run open after
the last step, and `--verbose` prints the engine's output as it arrives.

`--throttle KBIT/S[@MS]` caps the connection, symmetric upload and download,
from before the page's very first request -- `--throttle 4000` for a 4 Mbit/s
line, `--throttle 4000@40` to also add 40ms of latency. Combined with
`--do "wait menu"`, whose own `seconds` in `--report`'s `steps` is the time
that took, this is how long an initial load takes on a connection slower than
whatever the machine running the harness actually has. Unset, the run goes at
whatever the network offers, which is what every other measurement in this
document used.

`serve --bin DIR` runs only the server, for driving a build from a browser of
one's own.

**The port is never chosen.** The operating system picks a free one and the
harness prints it; `8765` is refused even if offered, because the container and
the developer's own server use it.

## States

`wait` takes a name so that "the menu is up" means the same thing in every run.
The engine says which loop it is waiting in, so a state is what the engine
reports and never a guess from the log or the picture.

**Phases.** `code/phase.cpp` keeps a stack of the loops the engine has entered,
and the page shows it as `OpenTS_State.phases`, such as `game/dialog`. The top
of the stack is the phase; `wait` takes any of them:

| Phase | The engine is in |
| --- | --- |
| `none` | No loop below: starting up, or moving from one phase to the next. |
| `menu` | `GraphicMenu::Presentation`, waiting for a choice. The disc chooser and the main menus are all this; the [UI description](#targets) carries the page's INI section, `MainMenu` for the chooser. |
| `movie` | `Movie_Play`, whichever film format the build plays. |
| `loading` | `Start_Scenario`, from just before it reads the scenario to its return. |
| `game` | The `Game_Frame` loop in `code/conquer.cpp`. |
| `dialog` | One layer per open dialog: an owner-draw dialog from `Begin_Dialog` to `End_Dialog`, or a modal Win32 one. Layers sit on the phase they opened over, so the options dialog in a mission reads `game/dialog`, and a message box over it `game/dialog/dialog`. |
| `alert` | The page-side message box `code/win32compat/win32user.cpp` puts up, such as "Failed to initialize". |

The stack nests the way the code does: a briefing film during a load is
`loading/movie`, so `wait movie` and `wait not:movie` see it, and `wait game`
is not satisfied until the load has ended and the loop is turning. `playing`
is kept as another name for `game`.

**Milestones** are what the page or the engine reports once:

| State | Reached when |
| --- | --- |
| `module` | The module's exports are on `Module`: the WebAssembly is instantiated. |
| `main` | `OpenTS_State.started`, which the page sets as it enters `callMain`. |
| `frame` | The first frame is drawn (`OpenTS_Browser_Frames` is at least one). |
| `init` | The engine reported the `init` event, beside "Game Init Completed." in `code/init.cpp`. |
| `scenario` | The engine reported the `scenario` event, beside "Reading scenario:" in `code/scenario.cpp`. |
| `idle` | Nothing is pending and a frame was drawn since the wait began. |

**Events** are how the engine reports these. Every phase change and every
marker is handed to the page as it happens (`Phase_Event` in `code/phase.cpp`,
`window.OpenTS_Event` in `wasm/page.js`), so a phase that comes and goes
between two polls is still seen. The events are `phase` with the stack as its
detail, `init`, `scenario` with the scenario name, and `progress` with the
loading percentage each time the bar moves. `wait event:NAME` waits for one
seen at any point since the run began, the log shows each as a line at level
`event`, and `--report` carries them all under `events`.

`wait not:STATE` inverts any of the above, and `wait any:A|B` is satisfied by
either. `wait ui:TARGET` waits for a [target](#targets) to be on screen and
usable. `wait log:REGEX` and `wait js:EXPR` are there for everything these do
not name.

A mission with a briefing shows it inside `loading`, on a screen whose
gadgets read `Resume Mission`, and stays there until it is clicked, so
`wait any:game|ui:text:Resume Mission` followed by `try click text:Resume
Mission` and `wait game` reaches the map whether or not the scenario has one.

## Game coordinates

Every position a step takes is in the game's frame — the same coordinates
`Window_Point_To_Game` hands the engine — and the harness does the translation
onto the page. It is the translation people got wrong by hand: the canvas is
sized in CSS pixels, its drawing buffer in device pixels, and the frame is
letterboxed inside that buffer.

The run pins what the translation depends on, so it is the same twice:
`--window WIDTHxHEIGHT` (1280x800 by default) is imposed on the page whether the
browser is headed or headless, and `--scale` fixes the device pixel ratio at 1.
With the page's default `?display=native` the frame follows the window, and a
game coordinate is usually a page coordinate. Only usually: a resolution change
waits for the window to settle and for the engine to reach a point that can take
one, so after a resize the frame is the size it was until the engine takes the
new one. The harness therefore asks the engine what its frame is rather than
assuming the canvas, and falls back to the canvas only where the engine will not
say. `--display WIDTHxHEIGHT` pins a frame instead and the harness follows
`Update_Scale_Info` (`code/video.cpp:148`) to find where it lands.

`eval JSON.stringify(window.OpenTS_Harness.geometry())` prints what the harness
believes: the canvas box, the device pixel ratio, the drawing buffer, the game
frame, and the frame's destination rectangle inside the buffer.

Touch is presented to the page unless `--no-touch` is given. Nothing in the
engine asks whether the device has a touch screen; the page asks once, to decide
whether its sound prompt says tap or click, so that is the only thing a
mouse-only run reads differently.

The browser is told to start audio without waiting for a gesture, because
otherwise every run's films would play silently and the sound paths would never
be exercised. `--no-autoplay` leaves that policy alone instead, which is the only
way to see what a visitor sees: a page that makes no sound until it is
interacted with, and the **Tap for sound** control the page shows while that is
true.

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --no-autoplay \
    --playmovie INTR0 --do "wait log:Movie" --do "sleep 8" \
    --do "expect Module._OpenTS_Audio_Blocked() === 1" \
    --do "shot prompt.png"
```

## Targets

A position can name what it means instead of where it is. The engine describes
what is on screen (`OpenTS_UI` in `code/inspect.cpp`), and the harness finds
the target in that description at the moment of the step, so a layout change, a
different window size or a UI scale moves nothing in a script.

| Target | Names |
| --- | --- |
| `@NSEL_OPTIONS`, `@GMENU_TIBSUN` | A graphic menu item by its enumerator in `code/newmenu.h`. |
| `@7` | A menu item, a window or a gadget by its number. |
| `text:Cancel`, `text:"Load Mission"` | A window or a gadget by the text it shows, compared without case. |
| `unit:MCV`, `unit:"Mobile Construction Vehicle"` | One of the player's own units, infantry, aircraft or buildings inside the tactical view, by its type's INI name or its display name. The first one found. |

Only what is visible and enabled counts. A target that is on screen but
disabled fails the step and says so; one that is not on screen fails with a
line saying what is: the phase, the menu page and its items, the window texts,
the gadget count and the object types. The click lands on the centre of the
target's rectangle.

`ui` shows the same description. A menu carries its INI section and each item
with its identifier, name, rectangle, and whether it is enabled, visible or
under the pointer. The windows are the window manager's, front to back and
parents before children, each with its identifier, class, text, rectangle,
depth, and whether it is enabled and holds the focus. The gadgets are the
`GadgetClass` list most recently handed to `Input`, the sidebar's buttons in a
mission, each with its identifier, rectangle, caption and state. The objects
are the player's own within the tactical view, each with its kind, type,
name, rectangle and whether it is selected. Every rectangle is in game
coordinates.

## Startup options and SUN.INI

The page has no command line, so the query string stands in for one, and the
harness fills it in from flags rather than from a hand-edited copy of the page:

| Flag | Becomes |
| --- | --- |
| `--scenario NAME` | `?scenario=` |
| `--campaign NAME` | `?campaign=` |
| `--playmovie NAME` | `?playmovie=`, the named movie in place of the startup sequence |
| `--display native\|scaled\|WxH` | `?display=` |
| `--arg SWITCH` | `?arg=`, repeatable, passed to the engine verbatim |
| `--query NAME=VALUE` | anything else the page reads, such as `hud=on`, `jspi=off`, or `jspi=ignore` |

Settings that exist only in the configuration file are given as
`--ini SECTION.KEY=VALUE`, repeatable:

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --ini Video.UIScale=200 ...
```

The harness writes those into a `SUN.INI` beside the module, before `main` runs
and outside the persistent directory the run reconciles with IndexedDB. The
engine's file layer looks there before it asks the manifest
(`code/win32compat/win32compat.cpp`'s `Host_Path`), so this is what an installed settings
file would have been. Nothing on disk is edited and no copy of the page is
made.

Only the keys given are written, so every other setting is whatever the engine
defaults to; a `SUN.INI` the manifest names is shadowed for that run.

## Observing

`state` returns, and optionally writes, one snapshot: `OpenTS_State` — frames,
waits, saves written, whether storage is persistent — together with every
zero-argument `OpenTS_*` counter the module exports, which is where the image
request, fetch, read-ahead, stall and deferred-read figures live. The list is
read off the module rather than kept here, so a counter added to the engine
appears without this tool being changed.

`log` is everything the page and the engine printed, in order, with a timestamp
and the console level, including the lines the page's own diagnostic panel drops
once it is full, and every [event](#states) the engine reported. `--report`
carries the same log, so a failed run explains itself without a second run.

The snapshot also carries the phase stack, its serial (which rises on every
change), what the innermost phase is showing (the menu page, the scenario, the
alert's text), how many places still hold unread input, and whether the engine
is [held](#frame-stepping).

## Frame stepping

The engine's frame wait (`Browser_Await_Frame` in `code/browser.cpp`) passes
through a gate the page owns. `hold` closes it: at its next frame wait the
engine parks and stays parked, still answering `state`, `ui` and `shot`, still
taking input into its queues. `step N` lets it through N frame waits and waits
for it to park again, reporting the frames it advanced; `step` alone is one.
`release` opens the gate.

This is what makes a comparison deterministic: two runs that `hold` at the same
phase, send the same input and `step` the same number of frames have drawn the
same number of frames, and the picture no longer drifts with how long the
harness took between steps.

```bash
python3 tools/harness/harness.py run --bin build-wasm/bin --scenario GDI1A.MAP \
    --do "wait game" --do "hold" --do "shot before.png" \
    --do "click 556 400" --do "step 30" --do "shot after.png"
```

A held engine reads input only when stepped, so an input step made while held
reports `drained: false` until the next `step`.

## Comparing screenshots

`diff A B` reports whether two screenshots are identical, and when they are not,
how many pixels differ, the box that encloses them, and the largest difference
any one channel showed. `--threshold` lets a channel differ by that much and
still count as the same. As a step it takes a pixel budget and fails the run
when the difference is over it, which is how "this change is pixel-identical" is
established rather than asserted.

`harness.py diff A B` does the same to two files without running anything.

## Fetch profiles

The browser build can be told, before it opens anything, which byte ranges of
which archives a session is going to read. `tools/harness/pgo_pipeline.py`
records that by driving two runs and writing the two profiles a deployment
serves:

```bash
python3 tools/harness/pgo_pipeline.py --bin build-wasm/bin --assets build/web \
    --scenario GDI1A.MAP --out profiles/
```

`--assets` is the tree to profile and defaults the way the harness's does; the
pipeline drives the harness and sets up nothing of its own. The manifest a
capture is tied to is read back out of that tree.

Running it by hand is for working on the capture itself. Publishing a profile
belongs to whoever publishes the release, since only that release's `assets.json`
names one: OpenTS-Assets calls this script as the last stage of its own build,
points a build directory at the release it just wrote, and installs what comes
back under its content hash (its `tools/tsprofile.py`, which refuses a profile
holding no ranges).
[Building OpenTS](BUILDING.md#in-a-container) covers serving the result.

Both runs pass `?pgocapture=1`, which is `-PGOCAPTURE`. It makes the engine
prefetch nothing at all and stand any profile it finds down, so what a capture
records is what the session read rather than what a guess fetched ahead of it or
what the previous profile named. Without it a profile grows every time it is
regenerated, each generation naming the last one's guesses as though the game
had asked for them.

The menu run takes the way a player reaches the menu rather than the shortest
way a harness can: the startup films play and are dismissed, the side is chosen
off the screen offering Tiberian Sun against Firestorm, and the campaign list is
opened and cancelled. Each of those reads art the one before it does not --
`GMENU.MIX` most of all, which `?nointro=1` never opens -- and a profile that
skips them leaves the real path fetching that art a piece at a time. The run is
`to-menu`, `click @NSEL_START_NEW_GAME` and `click text:Cancel`, each waiting
on the phase it leads to, so a change to the menu's layout moves nothing.

The mission run then plays a scenario, and what it names beyond the menu profile
becomes the second one. Neither profile names a film or a music track: those are
handed to the page's own `<video>` and `<audio>` elements by URL and never read
through the block reader, so naming one only fetches it twice.

A profile carries the hash of the manifest it was captured against and is
ignored unless the deployment is serving that same manifest, so a stale one
costs a fetch and nothing else. Regenerate after any asset build.

## Cleanup

A run leaves nothing behind, however it ends.

- The browser is launched into a profile directory of its own under the system
  temporary directory, named with a fixed prefix, and into its own process
  group. Teardown signals the group, so the renderer and GPU children go with
  the browser process, and removes the profile.
- The server is shut down and its port closed.
- The teardown runs on success, on a failed step, on an exception, and on an
  interrupt. `SIGTERM` is turned into an interrupt for the same reason.
- Nothing is shared with the developer's own browser: not the profile, not a
  port, not a window.

`doctor` reports any browser that survived anyway — there is one way for that
to happen, which is the harness itself being killed outright — and
`doctor --reap` ends it. A browser whose harness is still running belongs to
somebody else's live run: it is never reported and never reaped, because a
stray hunt once ended a run that was in progress.

## Why it is not a test

The harness reads the game data off the developer's own asset tree, and
[automated checks must not depend on proprietary game assets](../CONTRIBUTING.md#validation).
It is therefore deliberately not registered with CTest and no part of
`ctest --test-dir <build>` reaches it. The CTest suite covers the engine's own
layers and reads no game data; this is the tool for the evidence a behavior
change needs on top of that, and its results are reported as runtime
observations, never as build or test results.

## What has been run

[Building OpenTS](BUILDING.md#what-has-been-run) owns every runtime
observation of the WebAssembly target, including the runs made through this
harness, and what each run did not establish.
