# The platform layer

This document records how the engine reaches the operating system and the host
it runs in: the interfaces it calls, the rules each implementation follows, and
how Windows-only code is kept apart from the rest of the engine.
[Building OpenTS](BUILDING.md#other-toolchains) owns how each target is built
and what has been run. Visual Studio 2022 Win32 is the supported target;
nothing here extends that.

Off Windows no engine file sees the Windows SDK: `code/win.h` includes it only
under `_WIN32`, and the macOS target compiles with clang against POSIX and the
C++ standard library.

| Interface | Covers | Windows | Other targets |
| --- | --- | --- | --- |
| `code/platform/` | Files, directory searches, file times, free space, waits, the process (its path, the single-instance lock, the timer resolution), the debug log's console and debugger output, and the machine registry | `*_win32.cpp` | `*_posix.cpp` |
| `code/hostwindow.h` | The game window, the pointer and cursor, key state, message boxes and display modes | `code/hostwindow_win32.cpp` | None in this tree |
| `code/crtcompat.h`, `code/crtcompat.cpp` | The MSVC runtime spellings the tree is written against | Inert under MSVC | Defined here |

Each implementation file guards itself on the platform it serves and compiles
to nothing elsewhere.

## 1 Layout and ABI

macOS is LP64, and this tree builds only the platform library and the harnesses
there. Nothing pins a Windows width on it, so the engine's layout-sensitive
structures are not established at those widths. The build settles the
differences that do not depend on width:

| Property | MSVC Win32 x86 | clang on macOS | Settled by |
| --- | --- | --- | --- |
| `void *`, `long`, `size_t` | 4 bytes | 8 bytes | nothing |
| `wchar_t` | 2 bytes | 4 bytes | `-fshort-wchar` |
| Strict aliasing | not assumed | assumed | `-fno-strict-aliasing` |
| Null test on `this` in a `dynamic_cast` | kept | deleted | `-fno-delete-null-pointer-checks` |

`size_t` is `unsigned int` under MSVC Win32 and `unsigned long` on macOS, so a
`std::min(unsigned, size_t)` that deduced one type under MSVC deduces two
there; the fix is an explicit template argument at the call. The two-byte
`wchar_t` is compiled against a library that assumes four, so no C library
wide-string routine may be called, and the compiler can turn a hand-written
length loop back into that library's `wcslen`; `-fno-builtin-wcslen` and
`-fno-builtin-wcsnlen` are passed to stop it.

`__declspec` is resolved by `-fdeclspec`, including the `__declspec(property)`
accessors. Clang accepts the calling-convention keywords on every target and
ignores the conventions a target lacks; their single-underscore spellings are
erased in `code/crtcompat.h`. Clang's default floating-point model keeps single
precision single and reassociates nothing, which is what `/arch:SSE2
/fp:precise` buy under MSVC, so the simulation's floating-point model carries
over as long as no fast-math option is introduced; `-fno-fast-math` is passed
explicitly.

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
describes the POSIX one.

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

**Free space** is asked at startup, before a save, and as one input to the
session's unique identifier. `Platform_Free_Space` in `disk_posix.cpp` answers
it with `statvfs` on the user directory.

## 3 Waits and the process

`Platform_Sleep` in `platform/wait.h` is `::Sleep` on Windows and
`std::this_thread::sleep_for` on POSIX, where a wait of zero yields the
timeslice as `::Sleep(0)` does.

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
window procedure. No other implementation is in this tree, so a POSIX target
leaves this header's functions for a host to supply, and until one does the
executable is not built there ([Building OpenTS](BUILDING.md#other-toolchains)).

A host feeds input into shared code. Keys go to
`Keyboard->Post_Key_Event`. Mouse buttons go to `Game_Window_Mouse_Button` in
`code/gamewindow.cpp`, which offers each to the tactical map and then puts it
in the keyboard buffer; the same file takes double clicks, wheel notches, lost
capture, focus loss and return (`Focus_Loss`, `Focus_Restore`) and the window's
creation and destruction. The Windows window procedure translates its messages
into these calls, and the keyboard has no window-message handler of its own.
Tooltips time themselves from the message pump rather than from a window
timer.

`code/keyname.cpp` spells a hotkey for the keyboard screen. Windows names each
key with `GetKeyNameText` from the player's layout; elsewhere the names come
from a US-layout table. `tests/keyname` checks both.

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
format, none of which exists off Windows. The file is compiled rather than
excluded, with the reporter behind `_WIN32` and empty stubs of its entry points
for every other target, so a fault there is reported by the operating system
alone. The engine throws no C++ exceptions of its own; the macOS build passes
`-fexceptions` to match `/EHsc`.

Off MSVC, `CPU_Id` in `code/getcpu.cpp` does not run CPUID and reports family 4
with the vendor `Not available`.

## 7 Keeping Windows code apart

Code that calls the Windows API lives mostly in files of its own: the
`*_win32.cpp` files in `code/platform/`, `code/netsocket_win32.cpp` and
`code/hostwindow_win32.cpp`. Each guards itself with `#if defined(_WIN32)`, and
its POSIX counterpart, where one exists, with `#if !defined(_WIN32)`, so every
file is compiled on every target and the one that does not apply compiles to
nothing. A shared file that needs a Windows call for one step keeps it in a
`#if defined(_WIN32)` block beside the portable path, as `code/keyname.cpp`
does for key names, `code/startup.cpp` for `WinMain`, and `code/except.cpp`
for the whole crash reporter. `_WIN32` is the compiler's own marker for a
Windows target; `WIN32` and `_WINDOWS` are defined by the build only for a
Windows build. `code/ui/uiwin32.cpp`, the shell's window-message hook, is the
one Windows file that is not guarded: `code/CMakeLists.txt` leaves it out of a
POSIX build.

Engine code outside those files reaches portable interfaces instead of the
Windows API:

- **Time.** `mstimer.cpp` and `milsectmr.cpp` read
  `std::chrono::steady_clock` and hand out milliseconds since the first
  reading. Every caller compares readings against one another, so the origin
  is not observable.
- **The logging lock.** `dbgprint.cpp` holds a `std::mutex` and identifies the
  owning thread with `std::thread::id`.
- **Byte order and address text.** `netsocket.h` supplies the conversions and
  the broadcast address, and `netsocket.cpp` reads a dotted quad, so no caller
  outside the socket implementations includes a socket header.
- **Files and file times** ([section 2](#2-the-file-layer)). No signature
  carries a `HANDLE`, `FILETIME`, `SYSTEMTIME` or `WIN32_FIND_DATA`. A time is
  a `FileTimeType`, the 100 ns ticks since 1601 a `FILETIME` holds, with its
  halves named, so saves store the same eight bytes. The load and save screens
  format a date with the standard library as `MM/DD/YY` and a time as `HH:MM`,
  rather than in the user locale's short forms.
- **The string table** ([section 5](#5-strings-and-version-text)), converted
  once from the `STRINGTABLE` blocks of the resource script.
- **Debug output and process start-up** ([section 3](#3-waits-and-the-process)).
- **Waits.** The engine sleeps through `Platform_Sleep`. `::Sleep` is left
  only in the Windows crash reporter and `process_win32.cpp`.
- **Glyph code pages.** `UTF8::OEM_437_Glyph` and `UTF8::Windows_1252_Glyph`
  read static tables in `code/codepage.cpp` on every target. The tables hold
  what `WideCharToMultiByte` writes with no flags, best-fit substitutes
  included, and `tests/utf8` compares them with it when it runs on Windows.
- **Dialogs and window messages.** The interface screens are RmlUi documents
  ([UI system design](UI_DESIGN.md)), which answer with `DialogResultType` from
  `code/dialogresult.h`. The network lobby uses message names of its own
  (`code/lobbymsg.h`), which keep the Win32 numbers, and reads the Westwood
  serial through `platform/registry.h`.

The Windows side of these interfaces was syntax-checked with clang against
MinGW-w64 headers. It has not been compiled with MSVC.
