# The platform layer

This document records how the engine reaches the operating system and the host
it runs in: the interfaces it calls, the rules each implementation follows, and
how the Win32 substitute that the other targets used to compile against was
removed. [Building OpenTS](BUILDING.md#other-toolchains) owns how each target
is built and what has been run. Visual Studio 2022 Win32 is the supported
target; nothing here extends that.

Off Windows no engine file sees the Windows SDK: `code/win.h` includes it only
under `_WIN32`, and the other targets compile with clang against POSIX and the
C++ standard library.

| Interface | Covers | Windows | Other targets |
| --- | --- | --- | --- |
| `code/platform/` | Files, directory searches, file times, free space, waits, the process (its path, the single-instance lock, the timer resolution), the debug log's console and debugger output, and the machine registry | `*_win32.cpp` | `*_posix.cpp`; waits on the page in `wait_page.cpp` |
| `code/hostwindow.h` | The game window, the pointer and cursor, key state, message boxes and display modes | `code/hostwindow_win32.cpp` | `code/hostwindow_page.cpp`, over the host calls in `code/browser.h` |
| `code/crtcompat.h`, `code/crtcompat.cpp` | The MSVC runtime spellings the tree is written against | Inert under MSVC | Defined here |

Each implementation file guards itself on the platform it serves and compiles
to nothing elsewhere.

## 1 Layout and ABI

Emscripten's wasm32 is ILP32 exactly as Win32 x86 is, and that equivalence is
why the engine's structures survive the move. macOS is LP64 and builds only the
platform library and the harnesses. Not everything matches, and the build
settles the differences that matter:

| Property | MSVC Win32 x86 | Emscripten wasm32 | Settled by |
| --- | --- | --- | --- |
| `void *`, `long`, `size_t` | 4 bytes | 4 bytes | nothing |
| `char` signedness | signed | signed | nothing |
| `wchar_t` | 2 bytes | 4 bytes | `-fshort-wchar` |
| Strict aliasing | not assumed | assumed | `-fno-strict-aliasing` |
| `long double` | 8 bytes | 16 bytes | nothing; nothing under `code/` declares one |

`size_t` is four bytes on both but `unsigned int` under MSVC and
`unsigned long` under Emscripten, so a `std::min(unsigned, size_t)` that
deduced one type under MSVC deduces two here; the fix is an explicit template
argument at the call. The two-byte `wchar_t` is compiled against a library
that assumes four, and the compiler answers a length loop with that library's
`wcslen`, so `-fno-builtin-wcslen` is passed and the save version strings in
`code/savever.cpp` never measure wide text after the fact.

`__declspec` is resolved by `-fdeclspec`, including the `__declspec(property)`
accessors. Clang accepts the calling-convention keywords on every target and
ignores the conventions a target lacks; their single-underscore spellings are
erased in `code/crtcompat.h`. WebAssembly arithmetic is strict IEEE-754 with no
excess precision and no reassociation, which is what `/arch:SSE2 /fp:precise`
buy under MSVC, so the simulation's floating-point model carries over as long
as no fast-math option is introduced; `-fno-fast-math` is passed explicitly.

`code/crtcompat.h` gives other compilers what MSVC declares in its standard
headers beyond the standard: `stricmp`, `strnicmp`, `memicmp`, `strupr`,
`strlwr`, `strrev`, `freopen_s`, `_MAX_PATH` and its kin, the `ctype`
character-class masks and `__int64`. `code/crtcompat.cpp` defines
`_splitpath` and `_makepath`. `always.h` includes the header on every
toolchain, and both files compile to nothing under MSVC.

## 2 The file layer

The engine reaches its files through `code/platform/`. It supplies an open file
(`PlatformFileClass`), the path operations, a directory search and the file
time in `platform/file.h` and `platform/filetime.h`, and free space in
`platform/disk.h`. Each has a Win32 implementation (`file_win32.cpp`,
`disk_win32.cpp`) and a POSIX one (`file_posix.cpp`, `disk_posix.cpp`).
`tests/platformfile` holds both implementations to one account. This section
describes the POSIX one. On a POSIX target the library also carries
`code/blocksource.cpp`, which the file layer reads a page's archives through.

Backslashes are accepted as separators. A path that exists as spelled is used
as spelled; only a path that does not is walked component by component, each
missing component matched against its directory without regard to case, so the
upper-case names the engine asks for reach assets a player supplied in either
case on a case-sensitive filesystem. A component with no match keeps its
spelling, so a file is created under the name the caller chose. Two entries
differing only in case resolve to the first in sort order.

A search runs whole before `Platform_Find_Files` returns, because the engine
scans a directory it is also writing into (the debug log's folder is swept
while a log is open in it). The matches are sorted in case-insensitive name
order on every target, Windows included: the order decides which
`ECACHE*.MIX` overrides which, and leaving it to the host would let two
machines with the same files disagree. Matching uses the DOS rule that `*.*`
means every file; on Windows the host matches, short names included, and only
the order is imposed.

An entry reports its size, its write time and three flags. A dot-file is
hidden, so the engine's scans skip it as they skip hidden, system and
temporary files on Windows; a directory and a file without the owner's write
bit report themselves as such. No creation or access time is kept on either
target. A host read that comes back short is resumed rather than reported, so
only the end of the file stops the loop early.

**Persistent storage.** Everything the engine can reach is gone with the tab:
the game data arrives over HTTP and the filesystem it lands in is memory. One
directory, `/save`, is mounted on IndexedDB before `main` runs, and only that
one, because copying the game data into the browser's database would cost
hundreds of megabytes of quota to store what the page already has. `Host_Path`
looks a relative path up in the persistent directory first and the game
directory after, and one that is in neither resolves into the persistent
directory. That rule puts a saved game somewhere it survives the tab without
the file layer being told which opens are writes: a file about to be created
exists nowhere, and a file about to be read exists where it was written. The
whole relative path is carried across, not only its last component, because
saved games sit in a folder of their own under the user's directory. A search
of the game directory also reports what the persistent directory holds, and a
name the game directory already answered is left alone so the two agree.
IndexedDB is reached asynchronously and the engine cannot wait on it, so the
transfer is started when a file written there is closed, replaced, copied or
removed, and finishes on its own; the page counts the transfers that complete,
which is what the harness waits for before it reloads.

**Manifest names.** A name neither directory answers is looked up in the
manifest, the WebAssembly host's catalogue of the archives it serves. The
manifest sits underneath the host, never over it, so a file the engine writes
shadows the one it shipped with. Only the last path component is looked up,
since the manifest carries no directories and one name answers to exactly one
archive, and the lookup ignores case. A wildcard search of the root adds what
the manifest holds, after what the host answered. An open for reading that
resolves to an archive also tells the block source what the reads cannot say:
these bytes are one file, about to be read front to back, ending where the file
does.

**Free space** is asked at startup, before a save, and as one input to the
session's unique identifier, and `Platform_Free_Space` in `disk_posix.cpp`
answers it. Under node the host
answers about itself. A page has no filesystem to ask, and Emscripten's
in-memory one reports a fixed four gigabytes whatever the machine, so the
origin's storage quota from `navigator.storage.estimate` stands in, waited on
through the engine's yield with a two-second timeout and asked once.

## 3 Waits and the process

`Platform_Sleep` in `platform/wait.h` is `::Sleep` on Windows and
`std::this_thread::sleep_for` on macOS. On the page (`wait_page.cpp`) it hands
the thread back until the time has passed, so a request shorter than a frame
costs a frame. Zero is included in that: a page has no timeslice to hand back,
so a wait that yielded only when a frame was already due would leave an idle
loop such as `MSEngine::Wait_Delay` spinning through the whole interval.
Without the yield scaffold it reports itself once and returns at once.

`code/platform/process.h` supplies the executable's path, directory and image
range, the single-instance lock, and the timer resolution, each empty or a
no-op where the platform has none. `main(argc, argv)` is the entry point on
every target, with a `WinMain` on Windows that builds the arguments.
`code/platform/diagnostics.h` supplies the console window, the debugger
channel, the version and code pages in the log's banner, and error text; off
Windows the output goes to standard error.

`Platform_Read_Machine_Registry` in `platform/registry.h` reads a value under
`HKEY_LOCAL_MACHINE`. The network lobby reads the Westwood serial through it;
`registry_posix.cpp` answers false.

## 4 The game window

`code/hostwindow.h` is what the engine asks of the window it draws into:
opening and closing it (`Host_Create_Window` makes `Has_Main_Window` in
`code/mainwindow.h` true), its drawable size and refresh rate, a repaint, the
pointer's position, visibility, confinement and capture, the cursor image, the
modifier and key state and the character a key types, a message box, and the
display modes. `code/hostwindow_win32.cpp` answers it on Windows and holds the
window procedure. `code/hostwindow_page.cpp` answers it on the page, over the
calls `code/browser.h` declares, which `code/browser.cpp` answers. Every page
file and every page call in a shared file sits under `__EMSCRIPTEN__`, so any
other POSIX target compiles the whole engine and leaves only this header's
functions for its host to supply.

Both hosts feed input into the same code. Keys go to
`Keyboard->Post_Key_Event`. Mouse buttons go to `Game_Window_Mouse_Button` in
`code/gamewindow.cpp`, which offers each to the tactical map and then puts it
in the keyboard buffer; the same file takes double clicks, wheel notches, lost
capture, focus loss and return (`Focus_Loss`, `Focus_Restore`) and the window's
creation and destruction. The Windows window procedure translates its messages
into these calls, and the keyboard has no window-message handler of its own. On
the page `Browser_Service` makes the same calls from the events the page
queued, and `Windows_Message_Handler` paints when `Host_Invalidate_Window`
asked for it. Tooltips time themselves from that pump rather than from a window
timer.

The page answers what it can:

- It cannot move the pointer, and every canvas mouse event reaches the canvas,
  so capture is bookkeeping the engine reads back.
- A cursor becomes a PNG data URL for the canvas's CSS `cursor`. A browser
  scales the image by the device pixel ratio and refuses one over 128 pixels a
  side, so `code/wincursor.cpp` builds it at the largest whole scale that fits.
  The page has no class cursor to restore, so the game's own image stands in
  for it.
- A message box is laid out in the page and waited on through the engine's
  yield, as the `alert` phase. Without the yield scaffold the question is
  logged and answered as a dismissed box.
- The display modes are the common sizes no larger than the screen, the
  canvas's own size and the current mode, in CSS pixels, rebuilt at the start
  of each enumeration (`Host_Display_Mode`).
- It does not report a refresh rate.

`code/keyname.cpp` spells a hotkey for the keyboard screen. Windows names each
key with `GetKeyNameText` from the player's layout; elsewhere the names come
from a US-layout table, because the page reports which physical key was
pressed and never the layout. `tests/keyname` checks both.

## 5 Strings and version text

`Fetch_String` in `code/data.cpp` looks an identifier up in
`code/languagestrings.cpp`, which every target compiles in and which holds the
text in UTF-8. `code/language/language.h` names the identifiers, and
`cmake/StringNames.cmake` generates the name table UI documents use from it.
No target loads `Language.dll`. `Version_Name` returns the displayed version
from the generated `opents_build.h`, and `Get_Language_Version` a fixed line
with the version from `opents_version.h`
([Building OpenTS](BUILDING.md#build-identity)). The Windows executable keeps the version and icon
resources `code/Sun.rc` builds from the generated headers; the other targets
have none.

## 6 Exceptions

`code/except.cpp` holds every `__try` in the tree and is a complete post-mortem
crash reporter over structured exception handling, DbgHelp and the minidump
format. None of the three exists in a browser: a wasm trap unwinds to the host,
and the program cannot read its own call stack. The file is compiled rather
than excluded, with the reporter behind `_WIN32` and empty stubs of its entry
points for every other target. A fault reaches the host as a trap and the
browser or node reports it with the only stack there is. The engine uses no C++
exceptions of its own; the exception option the build passes is decided by the
host's yield scaffold.

Off MSVC, `CPU_Id` in `code/getcpu.cpp` does not run CPUID and reports family 4
with the vendor `Not available`.

## 7 The Win32 substitute, removed

Until September 2026 every toolchain but MSVC compiled the engine against
`code/win32compat/`: headers named and partitioned as the Windows SDK and the
MSVC runtime partition theirs, and the `Win32Substitute` library behind them.
The build defined `OPENTS_WIN32_SUBSTITUTE` for those toolchains. The library
held an in-process window manager with the stock controls and enough GDI to
measure and draw text for the owner-draw dialogs, the multimedia timers as
main-loop polls, a PE resource reader for the copy of `Language.dll`'s
resources the page carried, and stubs that named themselves when called.

It was retired from the engine outward. Each subsystem first moved to a
portable interface as a change that stood on its own under MSVC, and the part
of the substitute it had used was deleted afterwards. These moves came first:

- **Time.** `mstimer.cpp` and `milsectmr.cpp` read
  `std::chrono::steady_clock` and hand out milliseconds since the first
  reading. Every caller compared readings against one another, so the change
  of origin is not observable.
- **The logging lock.** `dbgprint.cpp` holds a `std::mutex` and identifies the
  owning thread with `std::thread::id`.
- **Byte order and address text.** `netsocket.h` supplies the conversions and
  the broadcast address, and `netsocket.cpp` reads a dotted quad, so no caller
  outside the socket implementations includes a socket header.
- **Files and file times** ([section 2](#2-the-file-layer)). No signature
  carries a `HANDLE`, `FILETIME`, `SYSTEMTIME` or `WIN32_FIND_DATA` any more. A
  time is a `FileTimeType`, the 100 ns ticks since 1601 a `FILETIME` holds,
  with its halves named, so saves store the same eight bytes. The load and save
  screens format a date with the standard library as `MM/DD/YY` and a time as
  `HH:MM`, in place of the user locale's short forms.
- **The string table** ([section 5](#5-strings-and-version-text)), converted
  once from the `STRINGTABLE` blocks of the resource script.
- **Debug output and process start-up** ([section 3](#3-waits-and-the-process)).
- **Waits.** The engine sleeps through `Platform_Sleep`. `::Sleep` is left
  only in the Windows crash reporter and `process_win32.cpp`.
- **Glyph code pages.** `UTF8::OEM_437_Glyph` and `UTF8::Windows_1252_Glyph`
  read static tables in `code/codepage.cpp` on every target. The tables hold
  what `WideCharToMultiByte` writes with no flags, best-fit substitutes
  included, and `tests/utf8` compares them with it when it runs on Windows.

The rest went in `4fefefc7..2a6a0517`. The network lobbies became RmlUi
screens, the last owner-draw dialogs to go, which left the window manager, the
controls and the GDI subset nothing to serve; OwnerDraw was deleted with them
([UI system design](UI_DESIGN.md#migration-plan), step 13). The pointer, the
cursor, the keys and the window moved behind `code/hostwindow.h`, with the
shared input dispatch in `code/gamewindow.cpp`, and `code/msgroute.cpp`, which
routed mouse messages to child windows, went with those windows.
`Language.dll` went because nothing read its dialog templates any more. The
lobby took message names of its own (`code/lobbymsg.h`) and the serial read
moved to `platform/registry.h`. Commit `56ac3255` deleted `code/win32compat/`,
the library and the definition, with the harnesses that tested the substitute
(`win32user`, `win32window`, `win32process`, `timer` and `resources`), and
`code/lzoinclude.h` followed once no target claimed to be Windows.

Before the deletion, every engine file was syntax-checked with the substitute's
headers replaced by `#error`, and none included them. The Windows side of the
same changes was syntax-checked with clang against MinGW-w64 headers. It has
not been compiled with MSVC.
