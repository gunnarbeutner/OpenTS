# Building OpenTS

> [!IMPORTANT]
> OpenTS supports Visual Studio 2022 Win32 Debug and Release builds. Both were
> verified from a fresh CMake configuration. A successful build does not
> verify runtime behavior.

## Supported target

| Component | Requirement |
| --- | --- |
| Host and architecture | Windows, 32-bit (`Win32`) target |
| Processor | SSE2, so a Pentium 4 or Athlon 64 onward |
| Generator and compiler | Visual Studio 2022 MSVC 19.30 or newer |
| Windows SDK | A Visual Studio-installed Windows SDK |
| CMake | 3.23 or newer |
| C++ language level | C++20 |
| Configurations | Debug and Release |

Other generators, compilers, architectures, and configurations are not
supported by the current tree. A WebAssembly target and a native macOS target
are in progress on branches of their own; the
[Win32 substitute](#other-toolchains-and-the-win32-substitute) they build on is
in this tree, and it is not supported.

Install Visual Studio 2022 with the **Desktop development with C++** workload,
a Windows SDK, and CMake 3.23 or newer. Git for Windows is needed to clone the
repository and initialize its dependencies, but not to compile a complete
source tree.

## Dependencies

The renderer uses [bgfx](https://github.com/bkaradzic/bgfx), vendored through
`thirdparty/bgfx.cmake` at a tested tag. That submodule contains bgfx, bx, and
bimg as nested submodules, so initialize it recursively:

```powershell
git submodule update --init --recursive
```

The audio layer uses [miniaudio](https://github.com/mackron/miniaudio),
vendored through `thirdparty/miniaudio` at a tested tag and compiled as one
translation unit from `thirdparty/miniaudio-impl.c`.

For a fresh clone, use `git clone --recurse-submodules`. Configuration stops
with instructions if a submodule is missing. Update a pinned tag in a
separate change.

## Configure and build

Run these commands from the repository root in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug
cmake --build build --config Release
```

CMake normally finds Visual Studio through the Visual Studio Installer. For an
unregistered installation, set `CMAKE_GENERATOR_INSTANCE` to its directory and
product version.

The solution contains only Debug and Release. Builds write the engine executable
under `build/bin/<configuration>/` with its runtime name and copy these runtime
files to `TS_RUN_DIR`, which defaults to `Run/`:

| Configuration | Runtime files |
| --- | --- |
| Debug | `GameD.exe`, `GameD.pdb`, `GameD.map`, `Language.dll` |
| Release | `Game.exe`, `Game.pdb`, `Game.map`, `Language.dll` |

`Language.dll` has the same name in both configurations, so the most recently
built configuration replaces the previous copy in `Run/`. Compiler and linker
intermediates stay in the selected build directory.

## Experimental clang-cl cross-build

An unsupported Linux cross-build is available for compiler-portability work. It
uses native `clang-cl`, LLD, and LLVM library and resource tools with the
MSVC headers and libraries. It does not expand the supported build matrix or
establish runtime behavior.

The reconstructed codebase may still contain undefined behavior that the
supported MSVC build happens not to expose. A successful clang-cl build may
therefore run incorrectly or fail at runtime; validate any result separately.

Provide a directory containing a Visual Studio layout and Windows SDK. The
cross-build uses the layout's default MSVC toolset and newest complete SDK.
Configure a single-configuration Ninja build:

```bash
cmake -S . -B build/clang-cl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-msvc.cmake \
  -DOPENTS_MSVC_ROOT=/path/to/msvc
cmake --build build/clang-cl
```

The toolchain requires `clang-cl`, `lld-link`, `llvm-lib`, `llvm-mt`, and
`llvm-rc` on `PATH`. It exports `compile_commands.json`; one configuration in
`.vscode/c_cpp_properties.clang.example.json` reads that file for IntelliSense.

## Build from Visual Studio Code

With the recommended extensions installed, the repository provides:

- CMake Tools settings;
- a configure task, a configuration picker, and hidden per-configuration tasks
  used by the launch configurations;
- launch and attach configurations;
- Test Explorer integration.

Standard VS Code shortcuts such as `Ctrl+Shift+B`, `F5`, and `Ctrl+F5` work as
usual.

## Other toolchains and the Win32 substitute

> [!WARNING]
> Nothing in this section is a support claim. Visual Studio 2022 Win32 remains
> the supported target; what follows is the platform layer the other targets
> are built on, and it is verified only by the harnesses named below.

The build accepts the Emscripten toolchain and Apple clang on macOS alongside
MSVC. Under either the engine compiles against the Win32 substitute described
below rather than the Windows SDK: `code/win32compat/` carries the substitute's
headers under the SDK's own names, the build puts that directory on the include
path, and every include site stays as it is written for MSVC. Emscripten's
wasm32 is ILP32 like Win32 x86; macOS is the one LP64 host, and the substitute
pins every Windows scalar to its Windows width there.

The substitute answers the operating system, not the machine the game runs on.
What it needs from a host, the window the renderer presents into, the pointer
and keys, the frame pacing, and the yields the engine's waits make, is declared
in `code/browser.h` and supplied by a host on a branch of its own. Without a
host the executable is left out of the default build on these toolchains, and
the substitute library and its harnesses still build:

```bash
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-macos
ctest --test-dir build-macos

source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-wasm
ctest --test-dir build-wasm
```

A host branch names itself by setting `OPENTS_HOST` in the build, which puts the
executable back into the default build, and adds the sources that answer
`code/browser.h`.

### The Win32 substitute

`code/win32compat/` holds headers named and partitioned as the Windows SDK and
the MSVC C runtime partition theirs, so `#include <windows.h>` and its kin
resolve there on a substitute target and to the SDK under MSVC, where the
directory is never on the include path. The build defines
`OPENTS_WIN32_SUBSTITUTE` for every toolchain but MSVC; what remains guarded by
it in the engine is behaviour, not includes.

`windows.h` is the umbrella it is under the SDK: it takes in `windef.h`,
`winbase.h`, `wingdi.h`, `winuser.h`, `winnls.h`, `wincon.h`, `winver.h` and
`winreg.h`, and without `WIN32_LEAN_AND_MEAN` also `mmsystem.h`,
`winsock.h` and `shellapi.h`. Each header owns what its SDK namesake owns:
`winnt.h` and `basetsd.h` the fundamental types at their Win32 widths,
`winerror.h` the error codes, `commctrl.h` and `windowsx.h` the controls and
their message wrappers, and `iphlpapi.h`, `winioctl.h`, `tlhelp32.h` and
`dbghelp.h` the subsystems the engine reaches by name. Nothing of COM or OLE
is in the tree: the engine creates its objects through its own class table
and names them by an identifier of its own.

On the runtime side `crtcompat.h` carries the MSVC spellings that MSVC keeps in
the standard headers (`stricmp`, `itoa`, `_MAX_PATH`, the `ctype` masks,
`__int64`); `always.h` includes it on every toolchain and it is inert under
MSVC. What MSVC keeps in headers of its own sits under those names: `io.h`,
`conio.h`, `malloc.h`, `sys/timeb.h`, `sal.h`, `new.h`, `direct.h`, `dos.h`,
`share.h`, `eh.h` and `intrin.h`.

`substitute.h` is the one header with no SDK namesake: the substitute's own
contract for how an unimplemented entry point reports itself.

The definitions are
split by subject, and on a substitute target they compile once into the
`Win32Substitute` static library that the engine and the harnesses link:

| File | Implements |
| --- | --- |
| `code/win32compat/win32compat.cpp` | The filesystem and its manifest lookup, the layout assertions, clocks, mutexes and events, the heap, resources and version information, locale formatting, the C runtime, the Windows-only half of Winsock, and stubs for the registry, profile and console entry points |
| `code/win32compat/win32user.cpp` | An in-process window manager: window classes, handles, the message queue, `SendMessage` and `DispatchMessage`, the dialog-item protocol, dialog templates, and a message box drawn into the page |
| `code/win32compat/win32ctrl.cpp` | The stock controls: button, static, edit, list box, combo box, scroll bar, track bar, progress bar and hot key |
| `code/win32compat/win32gdi.cpp` | Device contexts, GDI objects, font measurement and text drawing onto the engine's surfaces; the raster half is stubbed |
| `code/win32compat/win32window.cpp` | The canvas: its size, the pointer position, the cursor, the code page, and display-mode enumeration |
| `code/win32compat/win32process.cpp` | The module name and handle, the command line, locks and critical sections on a single thread, and stubs for the module, thread, process and DbgHelp entry points |
| `code/win32compat/win32timer.cpp` | `Sleep` and the multimedia timers as main-loop polls |
| `code/win32compat/win32disk.cpp` | Free-space reporting from `navigator.storage.estimate` |

[The Win32 substitute](WIN32-SUBSTITUTE.md) records the layout and ABI
constraints the substitute is held to and how stubs report themselves.

### Tests

`tests/` builds under both toolchains. The Emscripten toolchain file points
`CMAKE_CROSSCOMPILING_EMULATOR` at the emsdk's node, so `ctest` runs the
harnesses there without further configuration. Twelve tests build on any
substitute target: `blocksource`, `lcwstream`, `sosparity`, `unvqdelta`, `movieformat-vqa`,
`movieformat-mp4`, `win32file`, `resources`, `win32process`, `win32user`,
`win32window`, and `save`;
`timer` substitutes the millisecond clock and builds under Emscripten alone.
None of them reads game data.

`save` drives the file a saved game is kept in. It builds `code/savefile.cpp`
with the engine's LZO codec on every target, so the same writer and reader are
checked everywhere; [the format](SAVE-FORMAT.md) lists what it covers.

## WebAssembly, in progress and unsupported

> [!WARNING]
> The WebAssembly target builds, links, and runs, and it is still unsupported.
> Continuous integration does not build it, no part of the port has been
> compiled with MSVC, and the observations recorded under
> [what has been run](#what-has-been-run) are the whole of the evidence for it.
> Visual Studio 2022 Win32 remains the supported target.

Emscripten's wasm32 is ILP32, which gives the engine the 4-byte pointers and
4-byte `long` of Win32 x86, so none of the width pinning the substitute does
for an LP64 host is exercised here. `code/browser.cpp` is the host: the page
answers the calls declared in `code/browser.h`, and `CMakeLists.txt` names it
by setting `OPENTS_HOST`, which puts the executable back into the default
build.

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-wasm
```

Emscripten splits an executable in two, so a successful build writes the loader
and the module it looks for beside it:

| Configuration | Build output under `build-wasm/bin/` |
| --- | --- |
| Debug | `GameD.js`, `GameD.wasm` |
| Release | `Game.js`, `Game.wasm` |

A configuration built with `-DOPENTS_WASM_SUSPEND=ASYNCIFY` writes
`Game-asyncify.js` and `Game-asyncify.wasm` instead, so the two artifacts can
sit in one served directory; see [build options](#build-options).

A post-build step stages both halves into `TS_RUN_DIR` (`code/CMakeLists.txt:556`
and `:566`), which is what a node run needs. The page a browser run is served
from is generated separately, as `build-wasm/bin/index.html` from
`wasm/game.html` (`wasm/CMakeLists.txt:85`); it is not staged into the run
directory, so a browser run is served out of `build-wasm/bin`.

`wasm/demo.cpp` builds a second, unrelated target, `opents-wasm-demo`, that
links the renderer seam and nothing else (`wasm/CMakeLists.txt:22`). It is not
part of the game.

### Build options

`OPENTS_MOVIE_FORMAT` exists for both toolchains. `VQA`, the default, builds the
existing VQA player and requests `.VQA` archive members. `MP4` builds the browser
player and requests `.MP4` members instead; it is accepted only under Emscripten
and requires either JSPI or Asyncify. The two formats are selected at configure
time and are not probed at runtime, so a build and its game-data image must make
the same choice.

The remaining options exist only under Emscripten and do not appear in an MSVC
cache.

| Option | Default | Effect |
| --- | --- | --- |
| `OPENTS_MOVIE_FORMAT` | `VQA` | Selects the movie filenames and player. `VQA` uses `.VQA`; `MP4` uses H.264 video and MPEG AAC audio in `.MP4` files through the browser media element. `MP4` fails configuration outside Emscripten or with `OPENTS_WASM_SUSPEND=NONE`. |
| `OPENTS_WASM_SUSPEND` | `JSPI` | How the engine's not-yet-flattened waits hand the thread back to the page (`code/CMakeLists.txt:378`). One of `JSPI`, `ASYNCIFY`, or `NONE`; any other value fails the configure. |
| `OPENTS_WASM_NODERAWFS` | `ON` | Links `-sNODERAWFS=1` (`code/CMakeLists.txt:454`), handing the module the host filesystem. It is node-only: a page built with it throws before `main` runs, so a browser build is configured with `-DOPENTS_WASM_NODERAWFS=OFF`. |

`OPENTS_WASM_SUSPEND` decides both the mechanism and the artifact's name, since
the two suspending builds are the same engine and a served directory holds both:

| Value | Links | Module written | What it buys |
| --- | --- | --- | --- |
| `JSPI` | `-sJSPI` | `Game.js`, `Game.wasm` | The virtual machine suspends a real WebAssembly stack. Nothing is instrumented, so nothing is paid for at run time. Not in a released Safari. |
| `ASYNCIFY` | `-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536` | `Game-asyncify.js`, `Game-asyncify.wasm` | Binaryen rewrites the module so an instrumented function can unwind and rewind. Plain WebAssembly, so it runs anywhere, and it is [paid for in size and speed](#the-cost-of-the-asyncify-build). |
| `NONE` | nothing | `Game.js`, `Game.wasm` | Nothing carries a wait. The engine keeps the thread, the page stops answering at the first one, and says so once. This is the destination rather than a way to run the game: what such a build fails at is the work [the port design](WASM-PORT.md#14-the-destination) still has left. |

Both suspending values define `OPENTS_WASM_JSPI` for the compiler, which the
source reads as "a wait can suspend" rather than as a named mechanism; `NONE`
defines nothing.

### How the target differs from the Win32 one

| Component | Treatment |
| --- | --- |
| `.rc` resources | Excluded; a Visual Studio toolchain input (`code/CMakeLists.txt:72`). The version and icon resources have no replacement. |
| `code/language/` | Not built (`CMakeLists.txt:71`), because `Language.dll` is a Win32 resource library. The strings are not lost: `code/win32compat/peresource.cpp` reads the shipped library as a data file and walks its PE resource directory, so `Fetch_String` and `Fetch_Resource` answer out of the same library the Win32 build loads. |
| `code/wonline.cpp` | Excluded (`code/CMakeLists.txt:53`); it drives a service retired in 2004 through ATL. `wonlinestub.cpp` supplies what the rest of the engine references. |
| `tests/` | Built. `logstress` is Win32-only (`tests/CMakeLists.txt:2`) and `tests/audio` builds only here (`:18`). |
| Renderer | bgfx's OpenGL ES 3 renderer, reached through WebGL 2 (`code/CMakeLists.txt:318`). `thirdparty/CMakeLists.txt:32` compiles the bgfx tree with `-msimd128`, because Emscripten reports an x86 processor and bx therefore asks for SSE4.2 intrinsics that clang lowers to WebAssembly SIMD only when that feature is enabled. |
| Exceptions | `-fwasm-exceptions` rather than `-fexceptions` (`code/CMakeLists.txt:165`), because `-fexceptions` routes unwinding through `invoke_` imports that Emscripten declares suspending whenever JSPI is on, which traps in a static initializer. |
| Movies | VQA remains the default. `-DOPENTS_MOVIE_FORMAT=MP4` reads the selected `.MP4` member through the ordinary file layer, gives it to the browser as a Blob, and copies decoded frames into the engine's 16-bit surfaces. A browser that blocks the AAC track shows a **Tap to play movie** control and waits for that gesture. |

### The substitute on this target

The substitute the target compiles against is the one
[the section above](#other-toolchains-and-the-win32-substitute) describes;
nothing about it is particular to this target beyond the `__EMSCRIPTEN__`
branches inside its file layer, locale formatting and disk reporting.

### Where the WebAssembly target finds game data

The engine opens its archives out of the directory it runs in, through the Win32
file API that `code/win32compat/win32compat.cpp` puts on POSIX. Paths are resolved
case-insensitively when the exact spelling is missing, so an archive installed as
`tibsun.mix` answers the engine's `TIBSUN.MIX` on a case-sensitive filesystem.

**Under node**, the directory that reaches is the host's own, which
`-sNODERAWFS=1` hands the module directly, so a run is

```bash
cd Run && node GameD.js
```

with the game data staged in `Run` exactly as the Win32 build expects it.

**In a browser** there is no host filesystem, so the data is left on a web server
and read with HTTP range requests. Configure with
`-DOPENTS_WASM_NODERAWFS=OFF`, then use [the browser harness](HARNESS.md), which
owns serving a build with the archives beside it, starting and tearing down a
browser, and driving the engine once it is up:

```bash
python3 tools/harness/harness.py doctor --bin build-wasm/bin
python3 tools/harness/harness.py serve --bin build-wasm/bin
```

Do not write another one; [the engine source instructions](../code/AGENTS.md)
make that a rule. It reads game data of the developer's own, so it is not part
of the CTest suite.

Serving the directory and the archives by hand from any server that answers
ranges and opening `index.html` works too. `code/win32compat/win32compat.cpp` no longer
mounts a disc image: a name the host has no file for is looked up in
`manifest.json`, fetched once from beside the page (`code/manifest.cpp`),
which names every archive and film this content set holds, each at its own
URL. A name the manifest does not carry either simply resolves to nothing --
there is no disc-search order to fall back to, because this archive set is not
laid out as a disc's ever was. [The browser harness](HARNESS.md#setup)'s
`--asset` serves a `manifest.json` and the files it names for a run; see there
for the manifest's shape.

A movie in the manifest's `files` list is not read through the archive lookup:
its URL is handed straight to the browser's own `<video>` element
(`code/mp4.cpp`), which fetches and caches it exactly as it does the page's own
modules, with no archive or block reader involved.

The engine accepts `opents-web-assets-v2`, whose single `files` list replaces
v1's separate archive and movie lists. Deploy the matching engine and asset tree
together; the engine rejects the old manifest shape instead of interpreting it
as v2.

An archive the manifest does name is opened the way a disc image's own files
were: a volume attached lazily, on the first read that name gets
(`BlockFileClass::Attach_Whole`, `code/blocksource.cpp`), with reads shorter than a
block served from a per-archive block cache rather than one request apiece
(`code/httpsource.h:581`). A server that ignores the range and answers with the
whole archive is rejected rather than accommodated: the transport requires a
`206` and a `Content-Range` it can read (`code/httpsource.cpp:84`, `:152`).

Opening an image the browser has read before costs no request. The one thing the
server has to be asked — how long the image is — is written to `localStorage`
under the location made absolute, and a launch that names the same locations opens
every image out of that record instead (`BlockProbeClass`, `code/httpsource.h:183`).
A location the browser holds no record
for is probed exactly as before, so adding a disc to `?image=` or moving the
images asks about the ones that moved and nothing else. `OpenTS_Iso_Probes` and
`OpenTS_Iso_Recalls` report which of the two each image took, and the page's
status line says `discs known` when nothing was asked. A host with no such
storage — node, a private window, a browser told to keep nothing — reports
holding nothing, and every launch probes.

The round trip and rate the last run measured of the same location ride on that
record too, and are seeded into `BlockLinkClass`, so the first read is fetched at
the size the link was last found to want rather than at the floor.

The stored blocks are keyed on the location made absolute and the image's length,
and on nothing else (`BlockIndexClass::Signature`, `code/httpsource.h:52`). What
is deliberately not part of that key is the server's `ETag`, which is not a hash
of the content: these images are a 1999 game on discs that will not be reissued,
so a tag that moves is a mirror re-ingesting an item or a cache regenerating its
own far more often than it is a new file, and a key carrying one discarded a
store worth about two gigabytes every time that happened. Two Tiberian Sun images
of exactly equal length that are not the same image is not a case worth insuring
against at that price.

What the key gives up is checked for by the length instead. Fifteen seconds after
an image opens out of a record, a `fetch` nothing waits on asks the server how
long it is (`HttpBlockSourceClass::Watch`); a length that disagrees drops the record
and sets `OpenTS_State.isoStale`, the run carries on with what it holds, and the
next launch probes, finds a signature the stored blocks do not answer to, and
clears them. A read the server refuses before then re-establishes the image on
the spot (`HttpBlockSourceClass::Revive`), and if the length it now reports is the
length that was believed, the store stays where it is and the read fails on its
own account. Between them the two cover both directions: `Revive` catches an
image that was shortened or withdrawn, and `Watch` catches one that grew, which
answers every range the engine asks of it and would otherwise be misread for the
rest of the run.

Because the key changed, a browser holding blocks from a build before this one
finds a record written under the old signature, refuses it, and clears those
blocks once — the ordinary path a record written for another image takes
(`BlockIndexClass::Adopt`), counted by `OpenTS_Iso_Store_Discarded`.

Every read is synchronous, so one the caches miss stops the engine until the server
answers. A single reader is exempt. `DeferredReadClass` (`code/blocksource.h:81`)
marks a scope in which a block source may answer that the bytes are not here yet:
it asks for them without waiting and reports a read of nothing, which the layers
above pass up unchanged rather than retrying (`code/blocksource.cpp:89`,
`code/rawfile.cpp:491`, `code/win32compat/win32compat.cpp:1059`) and which the scope tells
apart from the end of a file. The score player is the only caller —
`ThemeClass::Play_Song` asks for a stream that may decline (`code/theme.cpp:439`)
and the two refill sites in `code/dsaudio.cpp` enter the scope around that
stream's reads alone, so sound effects, speech, and every ordinary file read still
wait for what they asked for. A block source with the bytes at hand never
declines, so nothing changes on any other target. `OpenTS_Iso_Deferred` counts the
reads that declined, beside the stall figures the same page reports.

The build offers no `--preload-file` bundling. [README](../README.md) is
explicit that OpenTS supplies the engine and not the game data, so a deployment
that serves an image is one serving data it has the right to serve.

The menu withholds what a page cannot play. The three network games and the World
Domination Tour are shown but not offered, and the exit is dropped entirely, since
there is nothing to quit back to (`code/newmenu.cpp:199`). An unavailable choice wears a disabled face
dimmed from its own lettering rather than the one the game data drew for it, so that
every withheld choice reads the same way whether artwork was supplied for it or not
(`code/grphmimg.cpp:127`). Every other target still uses the supplied artwork.

The first-run `EVA` movie does not play here. It belongs to a first run
that follows an installation, and a page installs nothing, so it covers no
setup; `PlayIntro=true` under `[Intro]` in `SUN.INI` asks for it anyway
(`code/startup.cpp:642`). Every other target still plays it once.

### Which module a page loads

How a wait hands the thread back is decided at link time, and no one module runs
everywhere, so a deployment that means to be reachable from every browser holds
both artifacts. Build the tree twice into the same served directory:

```bash
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DOPENTS_WASM_NODERAWFS=OFF -DOPENTS_MOVIE_FORMAT=MP4
emcmake cmake -S . -B build-wasm-asyncify -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DOPENTS_WASM_NODERAWFS=OFF -DOPENTS_MOVIE_FORMAT=MP4 \
    -DOPENTS_WASM_SUSPEND=ASYNCIFY
```

Either configuration generates the same `index.html`, which names both modules
and picks between them before it fetches anything. It asks for
`WebAssembly.Suspending` and `WebAssembly.promising`, the whole of what a JSPI
module touches while it is being created, and loads `Game.js` when both are
there and `Game-asyncify.js` when they are not. A browser therefore never
fetches a module it could not have run. `?jspi=off` forces
`Game-asyncify.js`, which isolates the non-JSPI path when diagnosing a browser.
`?jspi=ignore` takes the answer out of the decision and loads `Game.js`
regardless, which is how a `NONE` build is reached from the same page.

The gate screen remains, and it now reports a deployment rather than a browser:
it is shown when the module the page chose answers with a `404`, and it names
which one was missing. Serving only `Game.js` leaves a browser without JSPI
looking at it.

The side stack an unwind spills its locals to is sized explicitly because
Binaryen's 4KB default is not enough for this engine: linked with the default it
reaches the second frame and aborts with `RuntimeError: unreachable` out of
`maybeStopUnwind`, which is why `-sASYNCIFY_STACK_SIZE=65536` is on the link.

Emscripten warns that `ASYNCIFY=1` is not compatible with `-fwasm-exceptions`
and that "parts of the program that mix ASYNCIFY and exceptions will not
compile". The engine links and runs anyway, and a mission plays; take the
warning as the standing reason to run the Asyncify artifact against the JSPI one
rather than as a settled question, because a throw across a suspended frame is
not something the observations below exercised.

#### The cost of the Asyncify build

Measured on the tree and toolchain under [what has been run](#what-has-been-run).
Size, as the container builds and serves the two — `Release`, `-O1 -DNDEBUG`:

| | JSPI | Asyncify |
| --- | --- | --- |
| `.wasm` | 3,859,221 bytes | 11,759,933 bytes (3.05x) |
| `.wasm`, gzipped | 1,247,022 bytes | 5,182,539 bytes (4.16x) |
| `.js` | 446,915 bytes | 456,785 bytes |

The same pair built with `-O1 -g2 -DNDEBUG` is 4,371,890 against 12,276,262
bytes. Nothing is contained: `ASYNCIFY_ONLY` is not used, so the rewrite reaches
the whole engine, and the size is what that costs.

Speed was measured in one campaign mission, `GDI1A.MAP`, in three interleaved
pairs of runs against those same served modules — a 60 second window opened 130
seconds after the page did, so past the briefing movie, with the same disc
images on the same machine. Every one of the six runs held the display's
refresh rate exactly: 7,200 to 7,209 yields in 60 seconds on a 120Hz panel. The
cost therefore shows in what a frame took rather than in how many arrived:

| | JSPI | Asyncify |
| --- | --- | --- |
| Main-thread task time per frame | 0.783, 0.814, 0.789 ms | 0.993, 0.915, 0.967 ms |
| Share of the wall clock | 9.4%, 9.8%, 9.5% | 11.9%, 11.0%, 11.6% |

**About a fifth more CPU for the same frame** — the three pairs give 27%, 12%
and 23% — against a frame budget that is four fifths idle either way, so
nothing was dropped. It is a good deal short of the "something like 50% or so"
Emscripten's own documentation warns of on size and speed together, though the
size is well past it. This is one mission on one machine with headroom to
spare; a build already missing frames would show the same tax as lost frames
instead.

### In a container

`Dockerfile` builds the page with the pinned Emscripten and serves the result
from nginx, which answers ranges without being asked to. `compose.yaml` publishes
it and mounts an asset tree, so a run needs neither a toolchain nor a server of
one's own:

```bash
OPENTS_ASSETS=~/OpenTS-Assets/build/web docker compose up
```

`OPENTS_ASSETS` names an `OpenTS-Assets` `build/web` tree — the publishable
web set that repository's own `build.py` writes to `build/web` by default (or
wherever `--web-root` names), never anything from the OpenTS-Assets
repository itself. It is mounted read only and exposed at the site root
exactly as it is laid out: `assets.json`, `assets/<hash>.json` and
`files/<name>.<hash>.<ext>`. Movie paths also include their source archive as a
directory. `code/manifest.cpp` fetches `assets.json`
relative to the page by default, so
nothing further has to name it. The tree is never copied into the image —
`.dockerignore` keeps `Run/`, every `*.iso` and the default `assets/` mount
point out of the build context, because an image carrying the game data would
be redistributing it. `OPENTS_PORT` moves the published port from its default
of 8765.

`OPENTS_DOWNLOADS` names a tree of native installers, served at `/downloads/`
and mounted read only in the same way. `tools/downloads/publish.py` writes it:

```bash
python3 tools/downloads/publish.py --tree downloads \
    tauri/src-tauri/target/release/bundle/dmg/OpenTS_0.2.0_aarch64.dmg
python3 tools/downloads/publish.py --tree downloads --check
```

It reads the platform, architecture and version out of the file name, stores the
installer under its own content hash, and rewrites `downloads.json` to name one
build per operating system and architecture. Publishing merges into what the
tree already holds, so each build is published from whichever machine can
produce it, and an installer the pointer no longer names is removed. A macOS
disk image is recorded as signed only when `stapler validate` accepts it;
everything else is recorded as unsigned unless `--signed` says otherwise, and
the page tells the visitor which is which.

The page offers every build rather than only the matching one, since a visitor
is often not on the machine they mean to install on. It asks for
`downloads.json` once the game is drawing, and offers nothing when the file is
absent or when the page is running inside a shell (`?hosted=1`).

Dismissing the offer shrinks it to its icon rather than removing it, and that is
remembered. Pressing the icon opens it again, so nothing the reader turns off
needs a URL or a cleared browser store to get back.

`compose.yaml` also builds and publishes [the network relay](RELAY.md) on
8766, which `OPENTS_RELAY_PORT` moves. It is a separate image, `opents-relay`,
and a separate service; neither one needs the other to run.

Fetch profiles need no configuration of their own. A profile names the byte
ranges reaching a point in the game reads, so the engine fetches them up front
in a few large requests rather than discovering them one read at a time. The two
the engine asks for are `menu` and `first-mission`.

`assets.json` names them, and they are served out of `assets/files/` like any
other object, so a profile is reached only through the release that owns it and
is cached forever like the rest of that release. OpenTS-Assets publishes each
release with its own, capturing them through `tools/harness/pgo_pipeline.py` as
the last stage of its build; [the harness](HARNESS.md#fetch-profiles) covers
what that capture does. A release that names none reads as the engine did
before.

The image is built twice over, once for each of the two modules the page chooses
between, so it serves a browser with JSPI and one without alike.

It is also configured for a reverse proxy in front of it. nginx builds a
redirect's `Location` out of the scheme and address it sees itself, which behind
a TLS terminator is plain HTTP and the container's own address, and it does not
consult `X-Forwarded-Proto` or `X-Forwarded-Host` to correct that; the image
therefore turns `absolute_redirect` off, so a redirect carries only the path and
the browser resolves it against the origin it actually used.

Nothing the image serves is offered compressed. A cache in front of it
(Cloudflare included) may fill itself for a byte-range request with a plain
`GET` of its own -- the `Range` header dropped, its own `Accept-Encoding`
attached -- and a compressed reply to that fetch is cached as though it were the
whole object; every client range the cache then answers is sliced against that
compressed length rather than the true size, which is silently wrong data. The
image cannot tell such a fetch from an ordinary one, so it compresses neither
its assets nor its own modules.

The build context is the working tree rather than a fresh clone, so
`thirdparty/bgfx.cmake` has to be populated first:

```bash
git submodule update --init --recursive
```

### Tests under Emscripten

The harnesses named in [Tests](#tests) build here through the emsdk's node, and
two of the target's own build alongside them: `timer`, which substitutes the
millisecond clock, and `httpsource`, which drives the image overlay against a
stand-in for the transport. None of them reads game data.

```bash
ctest --test-dir build-wasm
```

### What has been run

| | |
| --- | --- |
| Date | August 30, 2026 |
| Tree | `732f984` |
| Toolchain | Emscripten 6.0.8, CMake 4.4.2, Ninja 1.13.2, macOS host; node 24.19.0 from the emsdk for `ctest` |
| Configuration | `Debug`, both options at their defaults |

A fresh `emcmake` configure, an engine build, and `ctest --test-dir build-wasm`
completed: eleven tests, eleven passed. Debug and Release engine binaries have
both been produced.

Observed in a browser, from a disc image over HTTP: the graphical main menu, a
campaign mission started and played, unit movement and selection, building
placement, terrain, the radar, the sidebar and its cameos, an audio device
opening, and movies playing.

The three values of `OPENTS_WASM_SUSPEND` were run separately, on August 30,
2026, `Release` with `-O1 -g2 -DNDEBUG` and `-DOPENTS_WASM_NODERAWFS=OFF`, in
Chrome 151 on a macOS host, from the same three disc images over HTTP:

- `JSPI` and `ASYNCIFY` each reached the disc chooser and each played `GDI1A.MAP`
  from `?scenario=`, and under Asyncify the in-game **Options** dialog opened
  over a still-advancing game — the `Dialog_Message_Handler` re-entry of
  `Main_Loop` that [the port design](WASM-PORT.md#1-the-thread-and-the-waits) names as the nesting an Asyncify
  unwind has to survive.
- A browser without JSPI was simulated by deleting `WebAssembly.Suspending` and
  `WebAssembly.promising` before any document script ran. The page then fetched
  `Game-asyncify.js` and `Game-asyncify.wasm` and nothing else; with only
  `Game.js` on the server it fetched neither module and showed the gate screen.
  **This was not run in a real browser that lacks JSPI**, Safari included.
- `NONE` loaded through `?jspi=ignore`, started, read the discs, and then wedged
  the tab, which is what that configuration means.
- `ctest --test-dir` passed against the `JSPI` and the `ASYNCIFY` build
  directories alike: 17 of 17 then, and 18 of 18 on August 31, 2026 once `save`
  was registered.

The runs above read from a disc image. The runs below went through
[the browser harness](HARNESS.md); the second of them reads from a manifest,
which is what the browser build resolves its game data from.

#### Through the harness

| | |
| --- | --- |
| Date | August 31, 2026 |
| Host | macOS 26.5.1, Python 3.14.7, Google Chrome 151.0.7922.175, headless |
| Build | A `Release` JSPI browser build's `Game.js`, `Game.wasm` and `index.html` |
| Data | `FIRESTORM.iso`, `TS1.iso` and `TS2.iso`, served over the harness's own range server |

- `menu` was reached eleven to thirteen seconds after the page opened, on the
  disc chooser. A `click 330 400` took it to the main menu and a `tap 640 700`
  opened the **Options** dialog there, both given in game coordinates.
- `GDI1A.MAP` from `--scenario` reached `playing` on the map itself, two seconds
  after `scenario`. A click and a tap were dispatched into the running mission.
- `--display 1024x768` in the same 1280x800 window letterboxed the frame to
  `dest [107, 0, 1066, 800]`, and a `click 589 370` in game coordinates landed
  on the Firestorm disc. A click that ignored the offset and the scale would
  have landed on the Tiberian Sun one, so the translation is established rather
  than assumed.
- `--ini Video.UIScale=200` was written and read: the engine printed
  `UIScale = 200 (drawn at 2)` out of `Options::Load_Settings`.
- Two screenshots of a still screen compared identical over all 1,024,000
  pixels; screenshots either side of the click compared as 1,021,540 differing
  with a bounding box, and `--budget 1000` failed the comparison as it should.
- `state` returned `OpenTS_State` with every `OpenTS_Iso_*` and
  `OpenTS_Browser_*` counter, the stall record and the deferred-read count among
  them.
- A `touch` step naming two fingers panned the tactical map, and a `touch down`
  held across several steps kept a dialog button drawn pressed until the
  matching `touch up`.
- The server answered `206` with a `Content-Range` for a byte range, `200` for a
  whole file, and `416` for a multi-range request rather than the whole file.
- Teardown was checked after a run that succeeded, a run whose step failed, a
  `SIGINT` and a `SIGTERM`: each left no browser process, no listening port and
  no profile directory. A harness killed with `SIGKILL` did leave its browser,
  which is what `doctor` then reported and `doctor --reap` ended; another
  developer's run was in progress throughout and was neither reported nor
  touched.

Not established: any browser other than Chrome, any host other than macOS, an
Asyncify build, a device pixel ratio other than 1, and the headed mode.

| | |
| --- | --- |
| Date | September 2, 2026 |
| Host | macOS 26.5.1, Python 3.14.7, Google Chrome 152.0.7977.65, headless |
| Build | A `Release` JSPI browser build's `Game.js`, `Game.wasm` and `index.html` |
| Data | A hand-built `manifest.json` naming the archive set's own MIX and MP4 objects, served with `--asset`, no disc image involved |

- With no `--disc`/`--discs`, `run` served only the manifest and the archives
  and film it named. The engine booted straight through
  `code/win32compat/win32compat.cpp`'s manifest lookup: `Bootstrap` and
  `Init_Secondary_Mixfiles` opened every archive by name over HTTP range
  requests (`OpenTS_Iso_Images: 8`, `OpenTS_Iso_Requests: 17` in `state`), with
  no disc mount anywhere in the run.
- `--playmovie NAME` resolved the name against the manifest's movie entry
  and handed the browser's own `<video>` element the manifest URL directly --
  `document.querySelector("video").currentSrc` read back the served URL, with
  no archive read and no copy of the file through WASM memory.
- Leaving a wildcard name the manifest carries nothing for --
  `Search_Files("MOVIES*.MIX")`, since no `MOVIES??.MIX` was in the manifest --
  failed `Init_Secondary_Mixfiles` and stopped the run on the engine's own
  "Failed to initialize" message box, the same way a disc missing a required
  archive already did; a same-shaped, deliberately empty stand-in archive
  answered it.

| | |
| --- | --- |
| Date | September 6, 2026 |
| Host | macOS 26.5.1, Python 3.14.7, Google Chrome 152.0.7977.83, headless |
| Build | A `Release` JSPI browser build with `OPENTS_MOVIE_FORMAT=MP4` and `OPENTS_WASM_NODERAWFS=OFF` |
| Data | An OpenTS-Assets `build/web` tree (manifest `53ec0173…`, 151 files, both profiles), served by `--assets` with no symlink into the build directory and no disc image |

- The engine reported its phases as it went: `movie` for the startup film,
  `menu` with `MainMenu` for the disc chooser and `TiberianSunMenu` after it,
  `dialog` over the menu for the options, campaign and skirmish dialogs, then
  `loading` and `game` for a skirmish. The `init` and `scenario` events
  arrived beside the lines they stand next to, and `progress` counted the load
  up.
- `to-menu` skipped the film and chose the Tiberian Sun badge by name;
  `click @NSEL_OPTIONS`, `@NSEL_START_NEW_GAME` and `@NSEL_SKIRMISH` each
  landed on the item the engine described, and `click text:Cancel` and
  `click text:OK` on the dialog buttons by their text. Every click reported
  `drained: true` within a frame or two. Escape did not close the skirmish
  dialog; its Cancel button did.
- A skirmish started from the dialog's OK. The MCV's position differed
  between runs, so `click unit:MCV` twice, which reads the position off the
  engine's own object description, is what deployed it: the selection count
  read one after the first click, and `wait ui:unit:GACNST` saw the
  construction yard after the second. `OpenTS_Center_Base` followed by a click
  at the frame centre selected an infantry unit instead.
- `GDI1A.MAP` from `--scenario` reached `loading` and stayed there on the
  mission's restatement screen, whose gadgets read `Resume Mission`; the
  settle rule the harness used to have had taken that screen for `playing`.
  `try click text:Resume Mission` reached `game`.
- `hold` parked the engine in the game; two screenshots taken while parked
  compared identical over all 1,024,000 pixels; `step 30` advanced exactly
  thirty frames and parked again; `release` let it run.
- A run against the tree with the VQA build stopped on the engine's own
  "Failed to initialize" alert, which the harness reported as phase `alert`
  with the box's text; against an assets tree named by symlink into a build
  directory the same alert came up when the tree was absent. Both failures
  were explained by the failure context alone.
- `tools/harness/pgo_pipeline.py` drove both captures through `to-menu`,
  `click @NSEL_START_NEW_GAME`, `click text:Cancel`, `try click text:Resume
  Mission` and `wait game`, and wrote the two profiles.
- `ctest --test-dir build-wasm` ran 19 tests: 18 passed, with `win32user` now
  covering the window description an automated check reads, and `httpsource`
  failed one check ("a record for this image holding nothing is taken on
  empty"). That test compiles none of the files this change touched.

Not established: a Firestorm menu and the map editor.

| | |
| --- | --- |
| Date | September 6, 2026 |
| Host | macOS 26.5.1, Python 3.14.7, Google Chrome 152.0.7977.83, headless |
| Build | A `Release` JSPI browser build with `OPENTS_MOVIE_FORMAT=MP4` |
| Data | The same OpenTS-Assets `build/web` tree, served by `--assets` |

- `--scenario GDI1A.MAP`, `try click text:Resume Mission` and `wait game`
  reached the mission; `key escape` opened the options dialog, which the
  engine described as `game/dialog` with its buttons by text. Escape did not
  close it again.
- `click text:"Save Game"` opened the save dialog over it, `text` typed onto
  the description already in its edit control, and `click @1` pressed its
  Save button; `click text:Save` lands on the dialog's `SAVE` label instead,
  since the match ignores case. The engine logged `SAVING GAME [SAVE0000.SAV
  ...] - Complete` and the options dialog was back on screen.
- `click text:"Load Game"` opened the load dialog with that save listed and
  selected, `click @1` pressed Load, and `wait game` was satisfied after the
  engine logged `LOADING GAME [SAVE0000.SAV] - Complete`; the player's
  objects were back in the description afterwards. Both files were written
  and read by `code/savefile.cpp`, so this is the first save written by
  `Save_Game` and read back by `Load_Game` observed in a browser.


Not established, and not to be read into the above:

- **Sound.** The backend queues samples to OpenAL over Web Audio
  (`code/audiobackend.cpp:280`), and it also carries a silent fallback that
  advances the play cursors off the wall clock when the page will not start
  audio (`code/audiobackend.cpp:226`). An advancing cursor is therefore not
  evidence of a sound, and nobody has confirmed hearing one.
- **Saving and loading across sessions.** A save written and read back
  within one run is established above. Whether a save survives the tab, which
  the page's IndexedDB mount of `/save` is for, has not been observed: every
  harness run starts with a fresh browser profile.
- **The owner-draw Win32 front end.** `code/win32compat/win32user.cpp` is a real in-process
  window manager and the dialog-template entry points are no longer stubs:
  `CreateDialogIndirectParamA` builds the dialog and its controls out of the
  template (`code/win32compat/win32user.cpp:2432`), converting dialog units at `:2463` and
  `:2485`. Dialogs open and are drawn. What has not been established is each
  dialog doing its job: the options and load screens have been used, and
  skirmish setup has had its check boxes toggled and its name typed into, but no
  game has been started from it.
- **The mouse cursor.** `code/win32compat/win32window.cpp:526` encodes each cursor frame as
  a PNG data URL for `canvas.style.cursor`, but what a player sees is still the
  browser's own arrow, and that path is under active work.
- **Anything under MSVC.** No part of this port has been compiled on Windows.

[WebAssembly port design](WASM-PORT.md) records the Win32 substitute the
target is built on and the design of the subsystems behind it.

## Build identity

The top-level `CMakeLists.txt` declares the project version in
`project(OpenTS VERSION ...)`. Since `project()` accepts only numbers, any
SemVer prerelease label goes in `OPENTS_VERSION_PRERELEASE`. Both values must
match the development entry in the manual's release registry;
`python manual/tools/manage.py check` verifies this. That tool runs on its own
pinned Python and packages rather than on whatever `python` resolves to; the
[manual's README](../manual/README.md) owns setting it up, and
`manage.py doctor` reports what is missing.

Each build writes two generated headers from that version and the repository
state:

| Header | Contents |
| --- | --- |
| `opents_version.h` | The version components, the version string, a prerelease flag, and the packed version number |
| `opents_build.h` | The commit, branch, commit date, whether tracked files were modified, and the version as it is displayed |

The packed version stores the major, minor, and patch components in one byte
each. Saves and network peers reject a different number. Builds within one
release cycle, including prereleases, share it, but their saves, replays, and
network sessions may still be incompatible.

The version resources in `Game.exe` and `Language.dll`, the title screen,
version dialog, crash report, and debug log banner all read these headers. A
normal build shows the version and commit, such as `0.1.0 (ab12cd3)`, plus a
marker when tracked files are modified. The commit identifies the build for
diagnostics; it is not a save or network compatibility stamp. An official
build configured with `-DOPENTS_OFFICIAL_BUILD=ON` shows only its declared
version.

`opents_version.h` changes only with the version, so an ordinary commit does not
rebuild code that reads only that header. `opents_build.h` is checked on every
build, so a new commit appears without reconfiguring; an unchanged header is
not rewritten.

A tag or pull-request build uses a detached checkout with no branch. Its stamp
uses a ref that points to the commit, preferring a tag, so a pull-request CI
build names the pull request instead of `HEAD`.

Git is optional at build time once the complete source tree is present. Without
Git or repository metadata, the build records the commit as `unknown` and shows
the version without one.

## Continuous integration

The `Engine` workflow runs for ready pull requests and pushes to `main` when
their changed paths match its engine and build filters. Draft pull requests do
not build until marked ready; the workflow then builds their current commit.

`Engine nightly` runs daily. A scheduled run cancels itself when the newest
commit is at least 25 hours old; manually started runs always build. This keeps
the latest successful scheduled run attached to downloadable artifacts.

Both use the reusable `Engine build` workflow. On a Windows runner with Visual
Studio 2022, it configures and builds Win32 Debug and Release with the commands
above, runs CTest, and uploads each configuration's executable, language
library, symbol file, and license notices. Artifact names contain the
configuration and short commit. Linker maps are omitted because the symbol
files are sufficient.
After a successful pull-request build, `Engine build comment` maintains one
pull-request comment with direct nightly.link downloads.

Publishing a GitHub release runs `Engine release`. It builds the release commit
with `-DOPENTS_OFFICIAL_BUILD=ON`, packages `Game.exe`, `Language.dll`,
`Game.pdb`, and the project and third-party license notices in a zip named
after the release tag, and attaches it to the release. It also appends notes
generated from the manual's change records by
`python manual/tools/manage.py release-notes`. See
[Maintaining](../manual/MAINTAINING.md) for the full release procedure.

CI redirects `TS_RUN_DIR` to an empty directory, keeping uploaded artifacts
free of unrelated runtime files.

## Verification boundary

The supported matrix was verified on August 16, 2026 with CMake 4.3.3, Visual
Studio 2022 Community 17.14.37328.6, MSVC 19.44.35228, and Windows SDK
10.0.26100. Fresh Win32 Debug and Release builds completed successfully. The
builds retain inherited MSVC warnings; warnings are not treated as errors, but
contributions should not add new warnings.

This verifies only that the supported toolchain compiles, links, and produces
the listed files. Runtime behavior requires separate play testing.

The repository contains no maps, movies, audio, or other original game assets.
Keep legally obtained runtime data local and outside version control. The
repository safety rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).
