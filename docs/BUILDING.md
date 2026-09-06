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
| `code/win32compat/win32compat.cpp` | The filesystem, the layout assertions, clocks, mutexes and events, the heap, resources and version information, locale formatting, the C runtime, the Windows-only half of Winsock, and stubs for the registry, profile and console entry points |
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
harnesses there without further configuration. Eight tests build on any
substitute target: `sosparity`, `unvqdelta`, `win32file`, `resources`, `win32process`,
`win32user`, `win32window`, and `save`;
`timer` substitutes the millisecond clock and builds under Emscripten alone.
None of them reads game data.

`save` drives the file a saved game is kept in. It builds `code/savefile.cpp`
with the engine's LZO codec on every target, so the same writer and reader are
checked everywhere; [the format](SAVE-FORMAT.md) lists what it covers.

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
