# The Win32 substitute

This document records how the Win32 substitute is put together: the headers
that stand in for the Windows SDK, the layout they are held to, how stubs report
themselves, and the design of each subsystem behind it. [Building
OpenTS](BUILDING.md#other-toolchains-and-the-win32-substitute) owns how it is
built and what has been run. The substitute answers the operating system; what
it needs from the machine, it asks a host for through `code/browser.h`, and each
host lives on a branch of its own. Where a section below speaks of the page, it
describes the WebAssembly host's answer to that question.

`code/win32compat/` stands in for the Windows SDK and the MSVC C runtime under
their own header names and partitions, so an include site is written once, for
MSVC, and a substitute target resolves it there through the include path with
the same transitive includes the SDK gives it; the build defines
`OPENTS_WIN32_SUBSTITUTE` for every toolchain but MSVC, and what the engine
guards by it is behaviour rather than includes. [Building OpenTS](BUILDING.md#the-win32-substitute) lists
the headers and the implementation files by subject. Two properties matter
more than completeness: layout and honesty.

## 1 Layout and ABI

Emscripten's wasm32 is ILP32 exactly as Win32 x86 is, and that equivalence is
why the engine's structures survive the move. Every substituted type carries
the width, signedness and alignment of its Windows original: `DWORD` and
`LONG` are four bytes whatever the host's `long` is, `WPARAM` and `LPARAM`
are pointer sized, `BOOL` is `int`, `RECT` is four `LONG`s,
`CRITICAL_SECTION` is twenty-four bytes on an ILP32 host and forty on an LP64
one. `win32compat.cpp` carries `static_assert`s over the sizes, alignments and
field offsets MSVC reports for the same constructs, on Win32 x86 for an ILP32
host and on Win64 for an LP64 one, including the two packed structures whose
size does not follow from their members (`WAVEFORMATEX` at eighteen bytes and
`BITMAPFILEHEADER` at fourteen). A change that moves a field fails the build
instead of reshaping a saved game or a network packet.

Not everything matches, and the build settles the differences that matter:

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
erased in `crtcompat.h`. WebAssembly arithmetic is
strict IEEE-754 with no excess precision and no reassociation, which is what
`/arch:SSE2 /fp:precise` buy under MSVC, so the simulation's floating-point
model carries over as long as no fast-math option is introduced;
`-fno-fast-math` is passed explicitly.

## 2 Stubs

Nothing in the substitute implements Windows for its own sake. A stub that
quietly returned success would be debugged for hours later, so every stubbed
entry point names itself at runtime, once per entry point, and returns what its
Win32 original returns on failure: `WIN32_STUB(value)`, `WIN32_STUB_VOID()`,
and `WIN32_STUB_ABORT()` where no return value could carry the failure.
`WIN32_UNSUPPORTED(description, value)` is for an entry point that is real for
the cases the engine uses but meets a request it has no answer for (an
overlapped read, a creation disposition nobody mapped); it names the request,
once per description, and fails rather than approximating.

An entry point returns the failure value silently only where Windows would
return it too: `GetMenu` answers null because no window here has a menu, and
`LoadCursor` answers null for a resource identifier that names no system
cursor. The test is whether Windows on the same input would answer the same
way.

## 3 The file layer

Everything from `CreateFileA` through the `FindFirstFileA` family is
implemented over POSIX. Sharing modes are accepted and ignored, because POSIX
has no mandatory locking. Backslashes are accepted as separators. A path that
exists as spelled is used as spelled; only a path that does not is walked
component by component, each missing component matched against its directory
without regard to case, so the upper-case names the engine asks for reach
assets a player supplied in either case on a case-sensitive filesystem. A
component with no match keeps its spelling, so a file is created under the
name the caller chose. Two entries differing only in case resolve to the first
in sort order.

A search runs whole in `FindFirstFileA` and its matches are kept on the
handle, because the engine scans a directory it is also writing into (the debug
log's folder is swept while a log is open in it). The matches are sorted in
case-insensitive name order, a deliberate departure from Windows: the order
decides which `ECACHE*.MIX` overrides which, and leaving it to the host would
let two machines with the same files disagree. Matching uses the DOS rule that
`*.*` means every file.

A handle is a slot in a table plus one, so it collides with neither reserved
value, and the slot remembers what a descriptor cannot: the kind of object
(file, search, an archive the manifest names, a mutex), the path a
delete-on-close open must remove, and a search's position. The table is built
on first use rather than during static initialization, where the engine's own
static objects open files.

Only three attribute bits have a host counterpart. A dot-file maps to the
hidden attribute so the engine's scans keep skipping it; a plain writable file
reports itself normal; `SetFileAttributesA` applies only the read-only bit.
The host has no creation time, so `GetFileTime` reports the inode change time
for it. A host read that comes back short is resumed rather than reported, so
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
transfer is started after a write and finishes on its own; the page counts the
transfers that complete, which is what the harness waits for before it
reloads.

**Manifest names.** A name neither directory answers is looked up in the
manifest, the WebAssembly host's catalogue of the archives it serves. The manifest sits underneath
the host, never over it, so a file the engine writes shadows the one it
shipped with. Only the last path component is looked up, since the manifest
carries no directories and one name answers to exactly one archive, and the
lookup ignores case. A wildcard search of the root adds what the manifest
holds, after what the host answered. `CreateFileA` resolving a read-only open
to an archive also tells the block source what the reads cannot say: these
bytes are one file, about to be read front to back, ending where the file
does.

## 4 The window manager

The front end in `code/ownrdraw.cpp` is written as Windows: a window class per
control, each driven from a window procedure, painting onto the engine's own
surfaces rather than through GDI. What it needs from the operating system is
windows, a hierarchy and message delivery, which `code/win32compat/win32user.cpp` provides
in process: real window objects with real dispatch, `SendMessage` and
`DispatchMessage`, the dialog-item protocol, and a message queue.

There is no desktop and no frame, so a window's client area is its window
rectangle and a top-level window sits at its own screen position. The engine
places dialog controls at frame coordinates inside a main window measured in
canvas pixels; `code/msgroute.cpp` already corrects that mismatch, so the
window manager hit-tests the way Windows would and leaves the correction where
the engine put it. The geometry calls answer out of the window registry for
any handle it knows and out of the canvas for the rest, the null handle among
them; reversing that order makes every control cover the whole frame and
swallow the clicks meant for the map.

**Input.** The mouse is tapped at `code/browser.cpp`'s drain, so each press and
release becomes a message exactly once however close together they arrive,
and the window it is delivered to puts it in the keyboard buffer, as the main
window's procedure does on Windows. The keyboard is not tapped: the page posts
keys to the keyboard buffer directly, which is how the main window reads them,
so key messages are queued only for windows the buffer does not serve (dialogs
and their controls), or every key would be doubled. The modifiers held when
the page reported an event are queued with the message, because the engine
asks for them when it acts on a message rather than when the message was made,
and on a page those are two different moments; `Browser_Apply_Modifiers`
presents them as the key state while the message is dispatched, and the live
modifiers return once the queue is empty. A player who lets go of control as
they release the button otherwise gets a move order where they asked for a
forced attack.

A software keyboard correcting or composing reports what it produced and no
keydown worth the name, so `Win32_User_Post_Character` posts the character on
its own, with no press around it (a press would be translated into a second
character), and only to a window that answers `DLGC_WANTCHARS`. What the
missing press costs is the dialog's own keys, so a return the page reports
only as a composed character does not stand in for OK. The engine's focus is
relayed to the page so a device whose only keyboard is on screen raises one;
the hall of fame asks for a keyboard directly, because it focuses no control.

**The pump.** `WM_PAINT` is generated rather than posted: it is reported once
the queue is empty and goes on being reported until the window validates
itself, which is how an `InvalidateRect` with no `UpdateWindow` reaches the
window at all. A window that never validates spins the pump; Windows spins
with it, a page stops answering, so after `PAINT_REPEAT_LIMIT` repeats the
update is dropped and the window's class is named in the debug output.
`WM_TIMER` is generated after `WM_PAINT`, as Windows ranks it, and rearmed
only where the tick is removed. A pump that finds nothing to do is about to ask
again, so it yields, paced so a peek from inside a frame does not cost a frame.
`GetMessage` without the scaffold cannot wait and reports the end of the queue
as a failure the caller can act on.

**Message boxes.** The engine asks real questions through `MessageBox` (the
low disk space prompt acts on the answer), so answering for the player is not
an option, and `confirm()` stops the page and may be suppressed outright. The
box is laid out in the page and the wait is the engine's own yield. Without the
scaffold the question is logged and the box reports cancelled, which is what
the Win32 original returns when it cannot put one up. A box already up is
replaced rather than stacked. `MessageBoxIndirect` keeps the text, caption,
owner and style and drops what names a Windows resource or help hook; a block
that does not state this build's size is refused.

**The caret.** Windows has one caret, blinked by the system over the focused
window. Nothing in the window manager draws, so the blink becomes an
invalidation of the owner, and whoever paints asks `Win32_Caret_Visible` where
the bar goes. It counts rather than toggles, starts hidden, and a caret that
has just moved is shown so it does not blink out under somebody typing.

`LoadIcon` returns a token that names the request rather than an image, distinct
per request, because a tab's icon belongs to the page.

## 5 Dialogs and controls

Every screen behind the main menu is a dialog template in the shipped language
library, so `CreateDialogIndirectParamA` builds a dialog and its controls out
of the template. The library carries both template shapes; the extended one
announces itself with a first double word of `0xFFFF0001`, which no classic
one can begin with. Name fields are an ordinal or UTF-16, item templates start
on four-byte boundaries, and each ends with creation data only its control can
read. The engine's captions and class names are ASCII, so a character outside
it becomes a question mark. A template out of the resource directory is bounded
by the length `code/data.cpp` recorded for it; one built in memory is bounded
by `DIALOG_TEMPLATE_LIMIT`. A classic template writes an unused identifier as
65535, and the front end compares against that widened form as Windows does.

Dialog base units are the classic 8 by 16, since there is no system font. The
number does not decide the layout: `code/windlg.cpp` measures its reference
template through the same units and carries every dialog by the ratio, so the
base unit only has to be one both sides agree on. `IDD_TEMPLATE` is 200 by 100
units and the scale pair 300 by 163 is what those units measured on the system
the artwork was drawn against; where the executable carries no such template,
the units are converted through `GetDialogBaseUnits` instead.

`code/win32compat/win32ctrl.cpp` supplies the stock classes `ownrdraw.cpp` subclasses and
paints over. It supplies state, not paint: each class answers the messages
that carry its state (where a trackbar's thumb belongs, whether a check box is
checked, what a list box's rows say) and draws nothing. A control still reports
its own state changes, so an owner-draw button that goes down tells its parent
with `WM_DRAWITEM`, which is what redraws it pressed; and a control that draws
nothing still owns its geometry, so `WM_NCHITTEST` lets a click through a
static caption and not through a button. The state pointer sits at window
extra offset 32 because the first thirty-two bytes are reserved for the dialog
words. The class names are the spellings `WC_BUTTON` and its neighbours
carry, because `ownrdraw.cpp` compares `GetClassName` case sensitively.

`code/win32compat/win32gdi.cpp` implements what the front end asks GDI for and never wrote
itself: how wide a string is, how tall a line is, and a device context to hang
a font and a colour on while it asks. The engine has one dialog typeface on
this target, the `dlgsys` remap sheet, so every face is measured and drawn
with it, and measuring and drawing agree, which a list row ellipsised against
its width depends on. A context obtained from a surface draws on it; one
obtained from a window measures and nothing more. The mapping mode is `MM_TEXT`
with the identity transform, so a request needing a real transform reports
itself. The edit box caret is measured against the text the control procedure
just drew, because the control's layout uses a different font; the bar is
filled into the surface and the blink arrives as a repaint.

After a resize under an open dialog, `Relayout_Dialogs` places each dialog by
its distance from the middle of the frame, ends the tooltip (it holds a copy
of surfaces that are gone) and drops each dialog's cached backdrop so the
repaint composes it again. A control's paint reaches the game's surfaces
through the enclosing dialog's paint, so `Is_Painting` counts the nesting and
surfaces are not replaced mid paint.

## 6 Process, timers and disk

A page is not a process with an image on disk, so `code/win32compat/win32process.cpp`
builds each answer out of what the target has. The module file name reports
the current directory by construction, because `startup.cpp` splits the
directory off it and makes that the current directory before the first
archive is opened; any other answer moves the engine off its game data.
`GetModuleHandle` for a system library answers not loaded, so each caller
takes the branch it keeps for an older Windows. The command line is the host's
argument list (`Module.arguments`, which the page builds from its query string
and a node shell passes through), reassembled for the engine's parser and
split again by `CommandLineToArgvW` following the shell API's quoting rules.
The locks and critical sections state that there is one thread: every
acquisition succeeds at once, and `TryEnterCriticalSection` must report
success or a caller enters a back-off that never ends.

`timeSetEvent` on Windows interrupts whatever the engine was doing. On a page a
registered callback is not delivered until the engine asks, and
`Win32_Timer_Service` is that ask: a callback runs on the engine's own stack
inside the service call that found it due, a period is a lower bound on the
gap between calls rather than a rate, and missed periods coalesce. The engine
arms two at most, the sound driver's maintenance pass and the movie player's
audio refill, serviced from the engine's waits and from the movie player's
loop. The clock wraps every forty-nine days, so due comparisons use the signed
difference. The millisecond clock deliberately lives in `win32compat.cpp`
rather than beside the timers, so `tests/timer` can substitute a
`timeGetTime` of its own. `Sleep` hands the thread back until the time has
passed, servicing the timers on the way through; a request shorter than a
frame costs a frame, and without the scaffold it reports and returns. Zero is
included in that: a page has no timeslice to hand back, so a wait that yields
only when a frame is already due leaves the caller spinning through the whole
interval, which is what an idle engine loop such as `MSEngine::Wait_Delay`
would otherwise do with the thread the page needs.

`GetDiskFreeSpaceEx` is asked twice, at startup and before the save dialog.
Under node the host answers about itself. A page has no filesystem to ask, and
Emscripten's in-memory one reports a fixed four gigabytes whatever the machine,
so `code/win32compat/win32disk.cpp` reports the origin's storage quota from
`navigator.storage.estimate`, waited on through the engine's yield with a
two-second timeout and asked once.

## 7 Language resources

`Fetch_String` and `Fetch_Resource` in `code/data.cpp` are the whole read
surface of `Language.dll`, plus version reporting. On this target the
resources are compiled from the same script the Visual Studio build turns into
the library (`code/language/rcimage.py`) and carried by the executable, because
there is no module loader; `code/win32compat/peresource.cpp` walks the PE resource
directory in place. Header fields are read one at a time rather than through
a structure, so the reader does not depend on the packing a compiler would
give the Windows declarations. Resource text is UTF-16 and the engine works in
Windows-1252 bytes; a character the code page has no byte for becomes a
question mark. The directory is held for the life of the process because a
fetched resource is a pointer into it, the lifetime a locked resource has on
Windows, and the size of each resource handed out is recorded because the
dialog template interpreter has to know where a variable-length template
stops. `Init_Language` reads nothing from the file system, because a global
constructor reaches `Fetch_String` before the mixfile and search path objects
exist. The dialog templates need no converter: `Fetch_Resource` returns
`RT_DIALOG` bytes in their shipped layout on both targets.

The running module is a wasm binary with no version resource, so
`GetFileVersionInfoSize` answers zero, which is what Windows answers for a
program built without one, and `Version_Name` leaves its placeholder standing.

## 8 Exceptions

`code/except.cpp` holds every `__try` in the tree and is a complete post-mortem
crash reporter over structured exception handling, DbgHelp and the minidump
format. None of the three exists in a browser: a wasm trap unwinds to the host,
and the program cannot read its own call stack. The file is compiled rather than
excluded, with the Windows half behind `#if !defined(__EMSCRIPTEN__)` and a
WebAssembly half that keeps the entry points and announces its own absence once,
so the same entry points are not declared twice. A fault reaches the host as a
trap and the browser or node reports it with the only stack there is. The engine
uses no C++ exceptions of its own; the exception option the build passes is
decided by the host's yield scaffold.

`code/detproc.cpp` no longer runs CPUID. The engine has no processor-specific
path left to select, so the probes describe a plain machine: family six, which
clears the gates the timer and the scenario timing estimate keep, and a vendor
string every caller reads as terminated.

