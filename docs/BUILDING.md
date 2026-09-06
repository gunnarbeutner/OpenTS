# Building OpenTS

> [!IMPORTANT]
> OpenTS supports Visual Studio 2022 `Win32` and `x64` builds, each in Debug
> and Release. All four were verified from a fresh CMake configuration. A
> successful build does not verify runtime behavior.

## Supported target

| Component | Requirement |
| --- | --- |
| Host | Windows |
| Target platforms | 32-bit (`Win32`) and 64-bit (`x64`) |
| Processor | SSE2, so a Pentium 4 or Athlon 64 onward; the `x64` build needs a 64-bit processor and Windows |
| Generator and compiler | Visual Studio 2022 MSVC 19.30 or newer |
| Windows SDK | A Visual Studio-installed Windows SDK |
| CMake | 3.23 or newer |
| C++ language level | C++20 |
| Configurations | Debug and Release, on both platforms |

Other generators, compilers, architectures, and configurations are not
supported by the current tree. A native macOS target is in progress and
unsupported; [Other toolchains](#other-toolchains) records what it builds.

Install Visual Studio 2022 with the **Desktop development with C++** workload,
a Windows SDK, and CMake 3.23 or newer. Git for Windows is needed to clone the
repository and initialize its dependencies, but not to compile a complete
source tree.

### Save and network compatibility between the platforms

A save records pointer identities at a fixed width, but the members and raw
structures around them travel at the build's own widths, so a `Win32` build and
an `x64` build do not read each other's saves. Their network packets differ for
the same reason.

Nothing detects this. The packed version stamp that saves and network packets
carry records the version, not the pointer width, so a build of either platform
accepts the other's save and admits it to a network game, and the result is a
failed load or a desync rather than a refusal. Until the stamp distinguishes
them, keep a saved game with the platform that wrote it, and play a network
game with peers running the same platform.

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

The user interface uses [RmlUi](https://github.com/mikke89/RmlUi) 6.3,
vendored through `thirdparty/RmlUi`, with
[FreeType](https://freetype.org/) 2.13.3 through `thirdparty/freetype` as its
font engine. Both build static against the static CRT. Only RmlUi's core
library is built: the engine supplies the render, system, and file interfaces,
so the bundled backends and samples are left out, and FreeType's optional
compression and shaping dependencies are disabled rather than taken from the
host.

For a fresh clone, use `git clone --recurse-submodules`. Configuration stops
with instructions if a submodule is missing. Update a pinned tag in a
separate change.

Compression uses [LZO](https://www.oberhumer.com/opensource/lzo/) 2.10,
vendored under `thirdparty/lzo` and built by `thirdparty/CMakeLists.txt`.
Upstream publishes releases as a tarball rather than through a repository, so
this copy is checked in instead of pinned as a submodule. It holds only the
LZO1X-1 sources the engine calls; take a later release by extracting it over
the files already there, in a separate change.

## Configure and build

Run these commands from the repository root in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug
cmake --build build --config Release
```

`-A` selects the platform, and a build directory holds one of them. Configure
`x64` beside the 32-bit build rather than over it:

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Debug
cmake --build build/x64 --config Release
```

CMake normally finds Visual Studio through the Visual Studio Installer. For an
unregistered installation, set `CMAKE_GENERATOR_INSTANCE` to its directory and
product version.

The solution contains only Debug and Release. Each writes its runtime files to
`<build directory>/bin/<configuration>/` and copies nothing anywhere else. The
test harnesses build into `<build directory>/test-bin/<configuration>/`, so
`bin/` holds only what the game runs. Compiler and linker intermediates stay in
the selected build directory.

| Configuration | Runtime files |
| --- | --- |
| Debug | `GameD.exe`, `GameD.pdb`, `GameD.map`, `ui/` |
| Release | `Game.exe`, `Game.pdb`, `Game.map`, `ui/` |

`ui/` is a copy of the repository's `ui/` directory, which holds the interface
screens' documents, styles and fonts.

Run a build from its output directory, naming the game data with `-DATADIR=`:

```powershell
build\bin\Debug\GameD.exe -DATADIR=Run
```

`OPENTS_GAME_DIR` names that data directory for the generated Visual Studio
debugger settings and defaults to `Run/`. The data directory is only read from.
Saved games, logs, and crash reports go to the user directory, which defaults to
the executable's own directory, so a build writes beside itself unless
`-USERDIR=` says otherwise.

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

## Other toolchains

> [!WARNING]
> Nothing in this section is a support claim. Visual Studio 2022 Win32 remains
> the supported target; what follows is how the macOS target builds, and it is
> verified only by the harnesses named below.

The build accepts Apple clang on macOS alongside MSVC. The top-level
`CMakeLists.txt` sets `OPENTS_MACOS` for it, and with it `OPENTS_POSIX`, which
compiles the engine with clang against POSIX and the C++ standard library; no
Windows SDK header is on the include path, and `WIN32` and `_WINDOWS` are
defined only for a Windows build. macOS is LP64, where Win32 x86 is ILP32.

The engine reaches the operating system and the host through `code/platform/`,
the game window interface in `code/hostwindow.h`, and the MSVC runtime
spellings in `code/crtcompat.h`; [the platform layer](PLATFORM.md) records what
each covers and which files implement it. Every toolchain, MSVC included, builds
`code/platform/` into the `OpenTSPlatform` library the engine and the harnesses
link. On a POSIX target the library also carries `code/blocksource.cpp`, which
the file layer reads a mounted image's archives through.

A POSIX target links the executable only when a host answers
`code/hostwindow.h` and names itself by setting `OPENTS_HOST`. On macOS that is
the SDL host in [the section below](#macos-in-progress-and-unsupported); a POSIX
target without one leaves the executable out of the default build, and the
platform library and the harnesses still build:

```bash
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-macos
ctest --test-dir build-macos
```

### Tests

`tests/` builds under both toolchains. None of the harnesses reads game data.

| Target | Tests registered |
| --- | --- |
| MSVC | 48: the fifteen below and 33 more from the directories listed under `if(MSVC)` in `tests/CMakeLists.txt` |
| macOS | 15 |

The fifteen that build everywhere are `blocksource`, `lcwstream`, `sosparity`,
`unvqdelta`, `movieformat-vqa`, `movieformat-mp4`, `lzoblock`, `zbufring`,
`priorityqueue`, `platformfile`, `save`, `uifontdialog`, `platformprocess`,
`utf8contract` and `keyname`.

`platformprocess` builds `code/dbgprint.cpp` with the process and diagnostics
files in `code/platform/`, and checks where the executable is found, the log
written beside it, and the pruning of old logs by name and age.

`keyname` checks how the keyboard screen spells a binding: the shape of the
answer on Windows, where the names come from the player's layout, and the US
layout's names elsewhere.

`save` drives the file a saved game is kept in. It builds `code/savefile.cpp`
with the engine's LZO codec on every target, so the same writer and reader are
checked everywhere; [the format](SAVE-FORMAT.md) lists what it covers.

## macOS, in progress and unsupported

> [!WARNING]
> The native macOS build is an experiment on the `macos-port` branch. Nothing
> in this section is a support claim; Visual Studio 2022 Win32 remains the
> supported target.

Under Apple clang the engine compiles as an LP64 POSIX target, as
[the section above](#other-toolchains) describes. `code/sdlhost.cpp` is the
host: an SDL2 window answers `code/hostwindow.h`, its pump turns SDL events into
the calls `code/gamewindow.cpp` and the keyboard take, `code/ui/uisdl.cpp` gives
the UI shell first refusal on each of them, and the renderer presents into the
window through Metal. `CMakeLists.txt` names the host by setting `OPENTS_HOST`,
which puts the executable back into the default build. SDL2 comes from Homebrew.

```bash
brew install sdl2
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build-macos
ctest --test-dir build-macos
```

The build leaves `bin/GameD` beside the copied `ui` directory; the game data is
not staged for it, so a run needs a working directory holding the mix files the
Win32 build expects.

What has been run, on macOS 26.5 with Apple clang 21 on arm64: `OpenTS` builds
and links, all fifteen portable harnesses pass, and `bin/GameD` started outside
a data directory opens the window, brings up the Metal renderer, and stops at
the missing bootstrap mix files. That is a runtime observation, not a test
result, and no part of the game has been played through this host since it was
written against `code/hostwindow.h`.

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
network sessions may still be incompatible. The stamp does not record the
target platform; see
[Save and network compatibility between the platforms](#save-and-network-compatibility-between-the-platforms).

The version resource in `Game.exe`, the title screen, version dialog, crash
report, and debug log banner all read these headers. A
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

Both use the reusable `Engine build` workflow. It runs one job per platform and
configuration, four by default, each on its own Windows runner with Visual
Studio 2022. A job configures and builds its platform with the commands above,
runs CTest, and uploads the executable, symbol file, `ui/` directory, and
license notices. Artifact names contain the platform, configuration, and short
commit, as in `opents-x64-Release-ab12cd3`. Linker maps are omitted because the
symbol files are sufficient. A failure on either platform fails the workflow.
After a successful pull-request build, `Engine build comment` maintains one
pull-request comment with direct nightly.link downloads.

Publishing a GitHub release runs `Engine release`. It builds the release commit
for both platforms with `-DOPENTS_OFFICIAL_BUILD=ON`, and packages each one's
`Game.exe`, `Game.pdb`, `ui/` directory, and the project and third-party license
notices in a zip named after the release tag and the platform, such as
`OpenTS-v0.2.0-x64.zip`. It attaches both to the release, and appends notes
generated from the manual's change records by
`python manual/tools/manage.py release-notes`. See
[Maintaining](../manual/MAINTAINING.md) for the full release procedure.

CI collects the uploaded artifacts, the `ui/` directory the build writes beside
the executable included, from `build/bin/<configuration>/`.

## Verification boundary

The supported matrix was verified on September 11, 2026 with CMake 4.3.3,
Visual Studio 2022 Community 17.14.37614.0, MSVC 19.44.35228, and Windows SDK
10.0.26100. Fresh Win32 and x64 builds completed successfully in both
configurations, and CTest passed all 40 tests in each of the four. The builds
retain inherited MSVC warnings; warnings are not treated as errors, but
contributions should not add new warnings.

This verifies only that the supported toolchain compiles, links, passes the
tests, and produces the listed files. Runtime behavior requires separate play
testing, and the x64 build has none of that history: only the Win32 build has
been played.

The repository contains no maps, movies, audio, or other original game assets.
Keep legally obtained runtime data local and outside version control. The
repository safety rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).
