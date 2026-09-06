/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The out-of-line half of the WebAssembly target's Win32 substitute;
// win32compat.h states the contract.

#include "always.h"
#include "substitute.h"
#include "iphlpapi.h"
#include "winioctl.h"
#include "mmsystem.h"
#include "shellapi.h"

#include "browser.h"
#include "crtcompat.h"
#include "httpsource.h"
#include "manifest.h"
#include "misc.h"
#include "video.h"
#include "windows.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


// Every value below is what MSVC reports for the same construct, on Win32 x86
// for an ILP32 host and on Win64 for an LP64 one, so a type that drifts fails
// the build rather than reshaping a saved game or a network packet.
constexpr bool ILP32 = sizeof(void *) == 4;
constexpr size_t PTR = sizeof(void *);
static_assert(PTR == 4 || PTR == 8, "");
static_assert(sizeof(INT_PTR) == PTR && sizeof(UINT_PTR) == PTR && sizeof(LONG_PTR) == PTR && sizeof(ULONG_PTR) == PTR, "");
static_assert(sizeof(BYTE) == 1 && sizeof(WORD) == 2 && sizeof(DWORD) == 4, "");
static_assert(sizeof(LONG) == 4 && sizeof(ULONG) == 4 && sizeof(BOOL) == 4, "");
static_assert(sizeof(LONGLONG) == 8 && alignof(LONGLONG) == 8, "");
static_assert(sizeof(WPARAM) == PTR && sizeof(LPARAM) == PTR && sizeof(LRESULT) == PTR, "");
static_assert(sizeof(HRESULT) == 4 && sizeof(HANDLE) == PTR && sizeof(HWND) == PTR, "");
static_assert(sizeof(WCHAR) == 2, "-fshort-wchar keeps WCHAR two bytes, as Windows has it");

static_assert(sizeof(POINT) == 8 && offsetof(POINT, y) == 4, "");
static_assert(sizeof(RECT) == 16 && offsetof(RECT, bottom) == 12, "");
static_assert(sizeof(SIZE) == 8, "");
static_assert(sizeof(MSG) == (ILP32 ? 28 : 48) && offsetof(MSG, pt) == (ILP32 ? 20 : 36), "");
static_assert(sizeof(FILETIME) == 8, "");
static_assert(sizeof(SYSTEMTIME) == 16, "");
static_assert(sizeof(LARGE_INTEGER) == 8 && sizeof(ULARGE_INTEGER) == 8, "");
static_assert(offsetof(LARGE_INTEGER, u.HighPart) == 4, "");
static_assert(sizeof(CRITICAL_SECTION) == (ILP32 ? 24 : 40), "");
static_assert(sizeof(WIN32_FIND_DATAA) == 320 && offsetof(WIN32_FIND_DATAA, cFileName) == 44, "");
static_assert(sizeof(WAVEFORMATEX) == 18, "mmsystem.h packs the wave formats to one byte");
static_assert(sizeof(BITMAPFILEHEADER) == 14, "wingdi.h packs the file header to two bytes");
static_assert(sizeof(BITMAPINFOHEADER) == 40, "");
static_assert(sizeof(RGBQUAD) == 4, "");
static_assert(sizeof(WNDCLASSA) == (ILP32 ? 40 : 72), "");
static_assert(sizeof(SCROLLINFO) == 28, "");
static_assert(sizeof(LOGFONTA) == 60, "");
static_assert(sizeof(EXCEPTION_RECORD) == (ILP32 ? 80 : 152), "");
static_assert(sizeof(CONTEXT) == 716, "");


// Each entry point names itself once, so a stub inside a frame loop does not
// bury the log.
static char const * ReportedFunctions[512];
static int ReportedCount = 0;


static bool Already_Reported(char const * function)
{
	for (int index = 0; index < ReportedCount; index++) {
		if (ReportedFunctions[index] == function || strcmp(ReportedFunctions[index], function) == 0) {
			return(true);
		}
	}

	if (ReportedCount < (int)(sizeof(ReportedFunctions) / sizeof(ReportedFunctions[0]))) {
		ReportedFunctions[ReportedCount++] = function;
	}
	return(false);
}


void Win32_Stub_Reached(char const * function)
{
	if (Already_Reported(function)) return;
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached; it reports failure.\n", function);
	fflush(stderr);
}


void Win32_Unsupported_Reached(char const * description)
{
	if (Already_Reported(description)) return;
	fprintf(stderr, "OpenTS: %s is not implemented on this target; the call reports failure.\n", description);
	fflush(stderr);
}


void Win32_Stub_Fatal(char const * function)
{
	fprintf(stderr, "OpenTS: unimplemented Win32 entry point %s reached, and it has no way to "
		"report failure to its caller. Stopping rather than continuing on a result that was "
		"never produced.\n", function);
	fflush(stderr);
	abort();
}


//------------------------------------------------------------------------------
// The last error and the null identifier.
//------------------------------------------------------------------------------


// Callers that set an error code expect to read it back.
static DWORD LastError = 0;


DWORD GetLastError(void)
{
	return(LastError);
}


void SetLastError(DWORD error)
{
	LastError = error;
}




//------------------------------------------------------------------------------
// The clocks.
//------------------------------------------------------------------------------


// GetTickCount and timeGetTime both count milliseconds since the first call.
static bool ClockStarted = false;
static struct timespec ClockOrigin;


static unsigned long long Monotonic_Nanoseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!ClockStarted) {
		ClockOrigin = now;
		ClockStarted = true;
	}

	return((unsigned long long)(now.tv_sec - ClockOrigin.tv_sec) * 1000000000ULL
		+ (unsigned long long)now.tv_nsec - (unsigned long long)ClockOrigin.tv_nsec);
}


DWORD GetTickCount(void)
{
	return((DWORD)(Monotonic_Nanoseconds() / 1000000ULL));
}


DWORD timeGetTime(void)
{
	return(GetTickCount());
}


BOOL QueryPerformanceCounter(LARGE_INTEGER * count)
{
	if (count == nullptr) return(FALSE);
	count->QuadPart = (LONGLONG)Monotonic_Nanoseconds();
	return(TRUE);
}


BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency)
{
	if (frequency == nullptr) return(FALSE);
	frequency->QuadPart = 1000000000LL;
	return(TRUE);
}


static void Fill_System_Time(SYSTEMTIME * result, struct tm const & parts, long milliseconds)
{
	result->wYear = (WORD)(parts.tm_year + 1900);
	result->wMonth = (WORD)(parts.tm_mon + 1);
	result->wDayOfWeek = (WORD)parts.tm_wday;
	result->wDay = (WORD)parts.tm_mday;
	result->wHour = (WORD)parts.tm_hour;
	result->wMinute = (WORD)parts.tm_min;
	result->wSecond = (WORD)parts.tm_sec;
	result->wMilliseconds = (WORD)milliseconds;
}


void GetSystemTime(SYSTEMTIME * time)
{
	if (time == nullptr) return;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm parts;
	gmtime_r(&now.tv_sec, &parts);
	Fill_System_Time(time, parts, now.tv_nsec / 1000000);
}


void GetLocalTime(SYSTEMTIME * time)
{
	if (time == nullptr) return;

	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm parts;
	localtime_r(&now.tv_sec, &parts);
	Fill_System_Time(time, parts, now.tv_nsec / 1000000);
}


void GetSystemTimeAsFileTime(FILETIME * filetime)
{
	if (filetime == nullptr) return;

	// Win32 counts 100 nanosecond intervals from the start of 1601; the host counts seconds
	// from the start of 1970.
	unsigned long long const epoch = 116444736000000000ULL;
	unsigned long long const now = epoch + (unsigned long long)::time(nullptr) * 10000000ULL;

	filetime->dwLowDateTime = (DWORD)(now & 0xFFFFFFFFULL);
	filetime->dwHighDateTime = (DWORD)(now >> 32);

}


int wsprintfA(LPSTR output, LPCSTR format, ...)
{
	va_list args;

	va_start(args, format);
	int result = vsprintf(output, format, args);
	va_end(args);
	return(result);
}


//------------------------------------------------------------------------------
// The file layer.
//------------------------------------------------------------------------------


// Only the conditions the file layer can produce are distinguished; the rest
// map to the generic Win32 failure.
static DWORD Win32_Error_From_Errno(int error)
{
	switch (error) {
		case ENOENT:	return(ERROR_FILE_NOT_FOUND);
		case ENOTDIR:	return(ERROR_PATH_NOT_FOUND);
		case EACCES:
		case EPERM:
		case EROFS:
		case EISDIR:	return(ERROR_ACCESS_DENIED);
		case EBADF:		return(ERROR_INVALID_HANDLE);
		case ENOMEM:	return(ERROR_NOT_ENOUGH_MEMORY);
		case EEXIST:	return(ERROR_FILE_EXISTS);
		case EINVAL:	return(ERROR_INVALID_PARAMETER);
		case ENOSPC:	return(ERROR_DISK_FULL);
		case EMFILE:
		case ENFILE:	return(ERROR_TOO_MANY_OPEN_FILES);
		case ENOTEMPTY:	return(ERROR_DIRECTORY);
		default:		return(ERROR_GEN_FAILURE);
	}
}


static bool Path_Present(std::string const & path)
{
	struct stat info;

	return(::lstat(path.c_str(), &info) == 0);
}


// A path that exists as spelled is used as spelled; otherwise each missing
// component is matched without regard to case, an unmatched component keeps its
// spelling, and two entries differing only in case resolve to the first in sort
// order.
static std::string Resolve_Case(std::string const & translated)
{
	if (translated.empty() || Path_Present(translated)) return(translated);

	std::string resolved;
	std::size_t cursor = 0;

	if (translated[0] == '/') {
		resolved = "/";
		cursor = 1;
	}

	while (cursor < translated.size()) {
		std::size_t separator = translated.find('/', cursor);
		if (separator == std::string::npos) separator = translated.size();

		std::string component(translated, cursor, separator - cursor);

		if (!component.empty() && component != "." && component != ".." && !Path_Present(resolved + component)) {
			DIR * const directory = ::opendir(resolved.empty() ? "." : resolved.c_str());

			if (directory != nullptr) {
				std::string match;

				for (struct dirent * item = ::readdir(directory); item != nullptr; item = ::readdir(directory)) {
					if (::strcasecmp(item->d_name, component.c_str()) != 0) continue;
					if (match.empty() || item->d_name < match) match = item->d_name;
				}

				::closedir(directory);
				if (!match.empty()) component = match;
			}
		}

		resolved += component;
		if (separator < translated.size()) resolved += '/';
		cursor = separator + 1;
	}

	return(resolved);
}


// Only this directory is mounted on IndexedDB and survives the page. It stands
// in front of the game directory, so a name that exists as game data still
// resolves to the game data.
#define OPENTS_PERSISTENT_DIRECTORY "/save"

static std::string const & Persistent_Root(void)
{
	static std::string const root = []() -> std::string {
		struct stat info;

		if (::stat(OPENTS_PERSISTENT_DIRECTORY, &info) == 0 && S_ISDIR(info.st_mode)) {
			return(OPENTS_PERSISTENT_DIRECTORY "/");
		}

		return(std::string());
	}();

	return(root);
}


static bool Is_Persistent(std::string const & path)
{
	std::string const & root = Persistent_Root();

	return(!root.empty() && path.compare(0, root.size(), root) == 0);
}


// A relative path is looked for in the persistent directory, then the game
// directory, and one in neither resolves into the persistent directory, so a
// file about to be created lands where it survives the tab. The whole relative
// path is carried across because saves sit in a folder of their own.
static std::string Host_Path(char const * path)
{
	std::string translated(path != nullptr ? path : "");

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}

	std::string const & root = Persistent_Root();

	if (!root.empty() && !translated.empty() && translated.front() != '/') {
		std::string const persistent = root + translated;

			// The caller's spelling is tried in both directories before either
			// is matched without regard to case.
		if (Path_Present(persistent)) return(persistent);
		if (Path_Present(translated)) return(translated);

		std::string const matched = Resolve_Case(persistent);
		if (Path_Present(matched)) return(matched);

		std::string const local = Resolve_Case(translated);
		if (Path_Present(local)) return(local);

		return(persistent);
	}

	return(Resolve_Case(translated));
}


// IndexedDB is reached asynchronously, so the transfer starts here and finishes
// on its own; the page counts completions for an automated check to wait on.
static bool PersistentDirty = false;

static void Flush_Persistent_Storage(void)
{
	if (!PersistentDirty) return;
	PersistentDirty = false;

#if defined(__EMSCRIPTEN__)
	MAIN_THREAD_EM_ASM({
		if (typeof FS === "undefined") return;

		var again = function () {
			FS.syncfs(false, function (error) {
				if (error) {
					console.error("OpenTS: writing persistent storage failed: " + error);
				}
				Module.OpenTS_Syncs = (Module.OpenTS_Syncs || 0) + 1;

				if (Module.OpenTS_SyncAgain) {
					Module.OpenTS_SyncAgain = false;
					again();
				} else {
					Module.OpenTS_SyncRunning = false;
				}
			});
		};

		if (Module.OpenTS_SyncRunning) {
			Module.OpenTS_SyncAgain = true;
		} else {
			Module.OpenTS_SyncRunning = true;
			again();
		}
	});
#endif
}


// A handle is a table slot's index plus one, so it collides with neither
// reserved value and can carry its kind, the path a delete-on-close open must
// remove, and a search's position; an image file and a mutex are further kinds.
enum HandleKindType
{
	HANDLE_KIND_FREE,
	HANDLE_KIND_FILE,
	HANDLE_KIND_FIND,
	HANDLE_KIND_IMAGE,
	HANDLE_KIND_MUTEX
};

// The host directory and the image answer for a name's size and date
// differently, so each match remembers its side.
struct FindMatchType
{
	std::string Name;
	BlockEntryClass Image;
};

struct HandleEntryType
{
	HandleKindType Kind = HANDLE_KIND_FREE;

	int Descriptor = -1;
	bool DeleteOnClose = false;
	std::string Path;

	std::shared_ptr<BlockFileClass> Volume;
	BlockEntryClass Image;
	std::uint32_t Cursor = 0;

	unsigned int Held = 0;

	std::string Directory;
	std::vector<FindMatchType> Matches;
	std::size_t Position = 0;
};

// Built on first call rather than during static initialization, where the
// engine's own static objects could open a file first.
static std::vector<HandleEntryType> & Handle_Table(void)
{
	static std::vector<HandleEntryType> table;

	return(table);
}


static HANDLE Handle_From_Index(std::size_t index)
{
	return((HANDLE)(ULONG_PTR)(index + 1));
}


static HandleEntryType * Entry_From_Handle(HANDLE handle)
{
	std::vector<HandleEntryType> & table = Handle_Table();
	ULONG_PTR const value = (ULONG_PTR)handle;

	if (value == 0 || value > table.size()) return(nullptr);

	HandleEntryType * const entry = &table[value - 1];
	if (entry->Kind == HANDLE_KIND_FREE) return(nullptr);
	return(entry);
}


static HandleEntryType * Entry_From_Handle(HANDLE handle, HandleKindType kind)
{
	HandleEntryType * const entry = Entry_From_Handle(handle);

	if (entry == nullptr || entry->Kind != kind) return(nullptr);
	return(entry);
}


// Accepts a handle to either kind of file.
static HandleEntryType * Entry_From_File_Handle(HANDLE handle)
{
	HandleEntryType * const entry = Entry_From_Handle(handle);

	if (entry == nullptr) return(nullptr);
	if (entry->Kind != HANDLE_KIND_FILE && entry->Kind != HANDLE_KIND_IMAGE) return(nullptr);
	return(entry);
}


static std::size_t Allocate_Handle(void)
{
	std::vector<HandleEntryType> & table = Handle_Table();

	for (std::size_t index = 0; index < table.size(); index++) {
		if (table[index].Kind == HANDLE_KIND_FREE) return(index);
	}

	table.emplace_back();
	return(table.size() - 1);
}


static void Release_Handle(HandleEntryType * entry)
{
	entry->Kind = HANDLE_KIND_FREE;
	entry->Descriptor = -1;
	entry->DeleteOnClose = false;
	entry->Path.clear();
	entry->Volume.reset();
	entry->Image.Reset();
	entry->Cursor = 0;
	entry->Held = 0;
	entry->Directory.clear();
	entry->Matches.clear();
	entry->Position = 0;
}



// The image is asked for a path relative to the volume root, drive letter and
// leading separator removed; a path that climbs out of the volume is refused.
static bool Image_Path(char const * path, std::string & inside)
{
	inside.clear();

	if (path == nullptr) return(false);

	std::string translated(path);

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}

	if (translated.size() >= 2 && translated[1] == ':') translated.erase(0, 2);

	std::size_t cursor = 0;

	while (cursor < translated.size()) {
		std::size_t separator = translated.find('/', cursor);
		if (separator == std::string::npos) separator = translated.size();

		std::string const component(translated, cursor, separator - cursor);
		cursor = separator + 1;

		if (component.empty() || component == ".") continue;
		if (component == "..") return(false);

		if (!inside.empty()) inside += '/';
		inside += component;
	}

	return(true);
}


// Only the last path component is looked up, because the manifest carries no
// directories and one name answers to exactly one archive.
static std::shared_ptr<BlockFileClass> Image_Entry(char const * filename, BlockEntryClass & entry)
{
	std::string inside;
	if (!Image_Path(filename, inside) || inside.empty()) return(nullptr);

	std::size_t const slash = inside.find_last_of('/');
	std::string leaf = (slash == std::string::npos) ? inside : inside.substr(slash + 1);

		// The manifest's keys are uppercase DOS 8.3, matched without regard to
		// case as the ISO9660 reader's Find does.
	for (char & character : leaf) {
		character = (char)::toupper((unsigned char)character);
	}

#if defined(__EMSCRIPTEN__)
	return(Manifest_Find(leaf.c_str(), entry));
#else
	// The manifest belongs to the page; a host with a filesystem has nothing beneath it.
	(void)leaf;
	(void)entry;
	return(nullptr);
#endif
}


bool Win32_Hint_Handle(HANDLE file, BlockHintType kind, unsigned int offset, unsigned int length)
{
	HandleEntryType * const entry = Entry_From_File_Handle(file);

	if (entry == nullptr || entry->Kind != HANDLE_KIND_IMAGE || !entry->Volume) return(false);
	if (offset >= entry->Image.Size) return(false);

	std::uint32_t const span = (length != 0) ? (std::uint32_t)length : (entry->Image.Size - offset);

	entry->Volume->Hint(entry->Image, kind, (std::uint32_t)offset, span);
	return(true);
}


bool Win32_Hint_File(char const * filename, BlockHintType kind, unsigned int offset, unsigned int length)
{
	if (filename == nullptr || *filename == '\0') return(false);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume) return(false);
	if (offset >= found.Size) return(false);

	std::uint32_t const span = (length != 0) ? (std::uint32_t)length : (found.Size - offset);

	volume->Hint(found, kind, (std::uint32_t)offset, span);
	return(true);
}


bool Win32_Prefetch_File(char const * filename, unsigned int offset, unsigned int length)
{
	if (filename == nullptr || *filename == '\0') return(false);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume) return(false);
	if (offset >= found.Size) return(false);

	std::uint32_t const span = (length != 0) ? (std::uint32_t)length : (found.Size - offset);

	return(volume->Prefetch(found, (std::uint32_t)offset, span));
}


unsigned long long Win32_Stored_Bytes(char const * filename)
{
	if (filename == nullptr || *filename == '\0') return(0);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume) return(0);

	unsigned long long const held = volume->Stored_Bytes();
	return((held > found.Size) ? found.Size : held);
}


// Win32 counts hundred-nanosecond intervals from 1601; the host counts seconds
// from 1970.
static long long const FiletimeEpochOffset = 116444736000000000LL;


static FILETIME Filetime_From_Host(long long seconds, long nanoseconds)
{
	unsigned long long const ticks = (unsigned long long)(seconds * 10000000LL + nanoseconds / 100 + FiletimeEpochOffset);
	FILETIME result;

	result.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
	result.dwHighDateTime = (DWORD)(ticks >> 32);
	return(result);
}


static FILETIME Filetime_From_Host(struct timespec const & stamp)
{
	return(Filetime_From_Host(stamp.tv_sec, stamp.tv_nsec));
}


// Where each host's stat keeps its nanosecond stamps.
#if defined(__APPLE__)
#define HOST_STAT_CHANGE(info)	((info).st_ctimespec)
#define HOST_STAT_ACCESS(info)	((info).st_atimespec)
#define HOST_STAT_WRITE(info)	((info).st_mtimespec)
#else
#define HOST_STAT_CHANGE(info)	((info).st_ctim)
#define HOST_STAT_ACCESS(info)	((info).st_atim)
#define HOST_STAT_WRITE(info)	((info).st_mtim)
#endif


// A record whose date the volume left unset reports no time at all.
static FILETIME Filetime_From_Image(unsigned int datetime)
{
	FILETIME result = {0, 0};

	if (datetime != 0) {
		DosDateTimeToFileTime((WORD)(datetime >> 16), (WORD)(datetime & 0xFFFF), &result);
	}

	return(result);
}


static long long Ticks_From_Filetime(FILETIME const * filetime)
{
	return((long long)(((unsigned long long)filetime->dwHighDateTime << 32) | filetime->dwLowDateTime));
}


static bool Calendar_From_Filetime(FILETIME const * filetime, struct tm * parts, long * milliseconds)
{
	long long const ticks = Ticks_From_Filetime(filetime) - FiletimeEpochOffset;
	long long seconds = ticks / 10000000LL;
	long long remainder = ticks % 10000000LL;

	if (remainder < 0) {
		remainder += 10000000LL;
		seconds--;
	}

	time_t const when = (time_t)seconds;
	if (::gmtime_r(&when, parts) == nullptr) return(false);
	if (milliseconds != nullptr) *milliseconds = (long)(remainder / 10000LL);
	return(true);
}


// A name beginning with a dot maps to the hidden attribute so the engine's
// scans skip dot files, and a plain writable file reports itself normal because
// Windows never reports an empty set.
static DWORD Attributes_From_Stat(char const * name, struct stat const & info)
{
	DWORD attributes = 0;

	if (S_ISDIR(info.st_mode)) attributes |= FILE_ATTRIBUTE_DIRECTORY;
	if ((info.st_mode & S_IWUSR) == 0) attributes |= FILE_ATTRIBUTE_READONLY;

	if (name != nullptr && name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
		attributes |= FILE_ATTRIBUTE_HIDDEN;
	}

	if (attributes == 0) attributes = FILE_ATTRIBUTE_NORMAL;
	return(attributes);
}


// DOS wildcard matching, ignoring case; "*.*" means every file, including a
// name with no extension.
static bool Match_Wildcard(char const * pattern, char const * name)
{
	if (strcmp(pattern, "*.*") == 0) pattern = "*";

	char const * patternmark = nullptr;
	char const * namemark = nullptr;

	while (*name != '\0') {
		if (*pattern == '?' || tolower((unsigned char)*pattern) == tolower((unsigned char)*name)) {
			pattern++;
			name++;
			continue;
		}

		if (*pattern == '*') {
			patternmark = ++pattern;
			namemark = name;
			continue;
		}

		if (patternmark != nullptr) {
			pattern = patternmark;
			name = ++namemark;
			continue;
		}

		return(false);
	}

	while (*pattern == '*') pattern++;
	return(*pattern == '\0');
}


HANDLE CreateFileA(LPCSTR filename, DWORD access, DWORD sharemode, LPSECURITY_ATTRIBUTES attributes,
	DWORD creation, DWORD flags, HANDLE templatefile)
{
		// POSIX has no mandatory locking, so sharing modes are not enforced.
	(void)sharemode;
	(void)attributes;

	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_HANDLE_VALUE);
	}

	if (templatefile != nullptr && templatefile != INVALID_HANDLE_VALUE) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("CreateFileA with a template file", INVALID_HANDLE_VALUE));
	}

	if ((access & GENERIC_EXECUTE) != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("CreateFileA with execute access", INVALID_HANDLE_VALUE));
	}

	bool const wantsread = (access & (GENERIC_READ | GENERIC_ALL)) != 0;
	bool const wantswrite = (access & (GENERIC_WRITE | GENERIC_ALL)) != 0;
	int openflags;

	if (wantsread && wantswrite) {
		openflags = O_RDWR;
	} else if (wantswrite) {
		openflags = O_WRONLY;
	} else {

			// Zero access is Win32's query-only open; a read-only descriptor
			// answers everything it can ask.
		openflags = O_RDONLY;
	}

	bool truncates = false;

	switch (creation) {
		case CREATE_NEW:
			openflags |= O_CREAT | O_EXCL;
			break;

		case CREATE_ALWAYS:
			openflags |= O_CREAT | O_TRUNC;
			truncates = true;
			break;

		case OPEN_EXISTING:
			break;

		case OPEN_ALWAYS:
			openflags |= O_CREAT;
			break;

		case TRUNCATE_EXISTING:
			openflags |= O_TRUNC;
			truncates = true;
			break;

		default:
			SetLastError(ERROR_INVALID_PARAMETER);
			return(WIN32_UNSUPPORTED("CreateFileA with a creation disposition it does not implement",
				INVALID_HANDLE_VALUE));
	}

	if (truncates && !wantswrite) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_HANDLE_VALUE);
	}

	std::string const path = Host_Path(filename);
	struct stat existing;
	bool const present = (::stat(path.c_str(), &existing) == 0);

		// Win32 refuses to open a directory as a file without backup semantics.
	if (present && S_ISDIR(existing.st_mode)) {
		SetLastError(ERROR_ACCESS_DENIED);
		return(INVALID_HANDLE_VALUE);
	}

		// The image is read-only, so only an open that would read an existing
		// file resolves there; anything that would create or write goes to the
		// host.
	if (!present && !wantswrite && (creation == OPEN_EXISTING || creation == OPEN_ALWAYS)) {
		BlockEntryClass found;
		std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

		if (volume) {
			std::size_t const index = Allocate_Handle();
			HandleEntryType & entry = Handle_Table()[index];

			entry.Kind = HANDLE_KIND_IMAGE;
			entry.Path = filename;
			entry.Volume = std::move(volume);
			entry.Image = found;
			entry.Cursor = 0;

				// A network-backed image reads ahead from the first block on
				// this hint and never past the end of the file.
			entry.Volume->Hint(entry.Image, BLOCK_HINT_SEQUENTIAL, 0, entry.Image.Size);

			SetLastError(NO_ERROR);
			return(Handle_From_Index(index));
		}
	}

	int const descriptor = ::open(path.c_str(), openflags,
		((flags & FILE_ATTRIBUTE_READONLY) != 0) ? (mode_t)0444 : (mode_t)0666);
	if (descriptor < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(INVALID_HANDLE_VALUE);
	}

	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_FILE;
	entry.Descriptor = descriptor;
	entry.DeleteOnClose = ((flags & FILE_FLAG_DELETE_ON_CLOSE) != 0);
	entry.Path = path;

	if (wantswrite && Is_Persistent(path)) PersistentDirty = true;

		// Both dispositions that accept an existing file report it through the
		// last-error slot on a successful open.
	SetLastError((present && (creation == CREATE_ALWAYS || creation == OPEN_ALWAYS))
		? ERROR_ALREADY_EXISTS : NO_ERROR);

	return(Handle_From_Index(index));
}


BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toread, LPDWORD read, LPOVERLAPPED overlapped)
{
	if (read != nullptr) *read = 0;

	if (overlapped != nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("ReadFile with an overlapped request", FALSE));
	}

	HandleEntryType * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (buffer == nullptr && toread != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (entry->Kind == HANDLE_KIND_IMAGE) {

			// The volume reports a transport failure the same way as the end of
			// the file, so the count the file could answer is worked out first
			// and anything less is a failure.
		DWORD const available = (entry->Cursor < entry->Image.Size)
			? (DWORD)(entry->Image.Size - entry->Cursor) : (DWORD)0;
		DWORD const wanted = (toread < available) ? toread : available;

		int const got = entry->Volume->Read(entry->Image, entry->Cursor, buffer, wanted);

		entry->Cursor += (std::uint32_t)(got > 0 ? got : 0);
		if (read != nullptr) *read = (DWORD)(got > 0 ? got : 0);

		if (got < 0 || (DWORD)got != wanted) {

				// A read the volume declined has not failed; the caller asked
				// for one that may say the bytes are not here yet.
			SetLastError(DeferredReadClass::Declined_Now() ? ERROR_IO_PENDING : ERROR_READ_FAULT);
			return(FALSE);
		}

		SetLastError(NO_ERROR);
		return(TRUE);
	}

		// Win32 fills the whole request from a file on disk, so a short host
		// read is resumed; only the end of the file stops early.
	char * const cursor = (char *)buffer;
	DWORD total = 0;

	while (total < toread) {
		ssize_t const got = ::read(entry->Descriptor, cursor + total, (size_t)(toread - total));

		if (got < 0) {
			if (errno == EINTR) continue;
			SetLastError(Win32_Error_From_Errno(errno));
			if (read != nullptr) *read = total;
			return(FALSE);
		}

		if (got == 0) break;
		total += (DWORD)got;
	}

	if (read != nullptr) *read = total;
	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD towrite, LPDWORD written, LPOVERLAPPED overlapped)
{
	if (written != nullptr) *written = 0;

	if (overlapped != nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WIN32_UNSUPPORTED("WriteFile with an overlapped request", FALSE));
	}

	if (Entry_From_Handle(file, HANDLE_KIND_IMAGE) != nullptr) {
		SetLastError(ERROR_ACCESS_DENIED);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_Handle(file, HANDLE_KIND_FILE);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (buffer == nullptr && towrite != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	char const * const cursor = (char const *)buffer;
	DWORD total = 0;

	while (total < towrite) {
		ssize_t const put = ::write(entry->Descriptor, cursor + total, (size_t)(towrite - total));

		if (put < 0) {
			if (errno == EINTR) continue;
			SetLastError(Win32_Error_From_Errno(errno));
			if (written != nullptr) *written = total;
			return(FALSE);
		}

		if (put == 0) break;
		total += (DWORD)put;
	}

	if (written != nullptr) *written = total;
	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distancehigh, DWORD method)
{
	HandleEntryType * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(INVALID_SET_FILE_POINTER);
	}

	int origin;

	switch (method) {
		case FILE_BEGIN:	origin = SEEK_SET; break;
		case FILE_CURRENT:	origin = SEEK_CUR; break;
		case FILE_END:		origin = SEEK_END; break;

		default:
			SetLastError(ERROR_INVALID_PARAMETER);
			return(INVALID_SET_FILE_POINTER);
	}

		// With a high word the pair is a signed sixty-four bit offset whose low
		// half is unsigned.
	long long const offset = (distancehigh != nullptr)
		? (long long)(((unsigned long long)(long long)*distancehigh << 32) | (unsigned long long)(DWORD)distance)
		: (long long)distance;

	if (entry->Kind == HANDLE_KIND_IMAGE) {
		long long const base = (origin == SEEK_SET) ? 0
			: ((origin == SEEK_CUR) ? (long long)entry->Cursor : (long long)entry->Image.Size);
		long long const wanted = base + offset;

			// Win32 allows a seek past the end and refuses one before the
			// start.
		if (wanted < 0) {
			SetLastError(ERROR_NEGATIVE_SEEK);
			return(INVALID_SET_FILE_POINTER);
		}

		if (wanted > (long long)0xFFFFFFFFLL) {
			SetLastError(ERROR_SEEK);
			return(INVALID_SET_FILE_POINTER);
		}

		entry->Cursor = (std::uint32_t)wanted;
		if (distancehigh != nullptr) *distancehigh = 0;

		SetLastError(NO_ERROR);
		return((DWORD)entry->Cursor);
	}

	off_t const position = ::lseek(entry->Descriptor, (off_t)offset, origin);
	if (position < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(INVALID_SET_FILE_POINTER);
	}

	if (distancehigh != nullptr) *distancehigh = (LONG)((unsigned long long)position >> 32);

		// Win32 has the caller clear the last-error slot before a seek and read
		// it back after, because a position whose low word is all ones equals
		// the failure value.
	SetLastError(NO_ERROR);
	return((DWORD)((unsigned long long)position & 0xFFFFFFFFULL));
}


DWORD GetFileSize(HANDLE file, LPDWORD sizehigh)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(INVALID_FILE_SIZE);
	}

	struct stat info;

	if (entry->Kind == HANDLE_KIND_FILE && ::fstat(entry->Descriptor, &info) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(INVALID_FILE_SIZE);
	}

	unsigned long long const size = (entry->Kind == HANDLE_KIND_IMAGE)
		? (unsigned long long)entry->Image.Size : (unsigned long long)info.st_size;

		// With no high word there is nowhere to put the upper half of a size
		// that needs one.
	if (sizehigh == nullptr && size > 0xFFFFFFFFULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_FILE_SIZE);
	}

	if (sizehigh != nullptr) *sizehigh = (DWORD)(size >> 32);

	SetLastError(NO_ERROR);
	return((DWORD)(size & 0xFFFFFFFFULL));
}


BOOL SetEndOfFile(HANDLE file)
{
	if (Entry_From_Handle(file, HANDLE_KIND_IMAGE) != nullptr) {
		SetLastError(ERROR_ACCESS_DENIED);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_Handle(file, HANDLE_KIND_FILE);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	off_t const position = ::lseek(entry->Descriptor, 0, SEEK_CUR);
	if (position < 0 || ::ftruncate(entry->Descriptor, position) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL FlushFileBuffers(HANDLE file)
{
	if (Entry_From_Handle(file, HANDLE_KIND_IMAGE) != nullptr) {
		SetLastError(ERROR_ACCESS_DENIED);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_Handle(file, HANDLE_KIND_FILE);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (::fsync(entry->Descriptor) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


// Closes a file handle or a mutex; a search closes through FindClose.
BOOL CloseHandle(HANDLE object)
{
	HandleEntryType * const mutex = Entry_From_Handle(object, HANDLE_KIND_MUTEX);

	if (mutex != nullptr) {
		Release_Handle(mutex);

		SetLastError(NO_ERROR);
		return(TRUE);
	}

	HandleEntryType * const entry = Entry_From_File_Handle(object);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Kind == HANDLE_KIND_IMAGE) {
		Release_Handle(entry);

		SetLastError(NO_ERROR);
		return(TRUE);
	}

	bool const closed = (::close(entry->Descriptor) == 0);
	int const failure = errno;

	if (entry->DeleteOnClose) {
		::unlink(entry->Path.c_str());
		if (Is_Persistent(entry->Path)) PersistentDirty = true;
	}

	Release_Handle(entry);

	Flush_Persistent_Storage();

	if (!closed) {
		SetLastError(Win32_Error_From_Errno(failure));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL DeleteFileA(LPCSTR filename)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const path = Host_Path(filename);

	if (::unlink(path.c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	if (Is_Persistent(path)) PersistentDirty = true;
	Flush_Persistent_Storage();

	SetLastError(NO_ERROR);
	return(TRUE);
}


// Win32 refuses to move onto an existing name, where rename would replace it.
BOOL MoveFileA(LPCSTR existing, LPCSTR newname)
{
	if (existing == nullptr || newname == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const target = Host_Path(newname);
	if (Path_Present(target)) {
		SetLastError(ERROR_ALREADY_EXISTS);
		return(FALSE);
	}

	std::string const source = Host_Path(existing);

	if (::rename(source.c_str(), target.c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	if (Is_Persistent(source) || Is_Persistent(target)) PersistentDirty = true;
	Flush_Persistent_Storage();

	SetLastError(NO_ERROR);
	return(TRUE);
}


// Only the replace flag is honoured; rename already replaces in one step here.
BOOL MoveFileExA(LPCSTR existing, LPCSTR newname, DWORD flags)
{
	if ((flags & MOVEFILE_REPLACE_EXISTING) == 0) {
		return(MoveFileA(existing, newname));
	}

	if (existing == nullptr || newname == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const source = Host_Path(existing);
	std::string const target = Host_Path(newname);

	if (::rename(source.c_str(), target.c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	if (Is_Persistent(source) || Is_Persistent(target)) PersistentDirty = true;
	Flush_Persistent_Storage();

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL CopyFileA(LPCSTR existing, LPCSTR newname, BOOL failifexists)
{
	if (existing == nullptr || newname == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const target = Host_Path(newname);
	if (failifexists && Path_Present(target)) {
		SetLastError(ERROR_FILE_EXISTS);
		return(FALSE);
	}

	int const source = ::open(Host_Path(existing).c_str(), O_RDONLY);
	if (source < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	int const destination = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0666);
	if (destination < 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		::close(source);
		return(FALSE);
	}

	char block[64 * 1024];
	bool copied = true;

	for (;;) {
		ssize_t const got = ::read(source, block, sizeof(block));

		if (got < 0) {
			if (errno == EINTR) continue;
			copied = false;
			break;
		}
		if (got == 0) break;

		ssize_t placed = 0;
		while (placed < got) {
			ssize_t const put = ::write(destination, block + placed, (size_t)(got - placed));
			if (put < 0) {
				if (errno == EINTR) continue;
				copied = false;
				break;
			}
			placed += put;
		}

		if (!copied) break;
	}

	int const failure = errno;

	::close(source);
	::close(destination);

	if (!copied) {
		SetLastError(Win32_Error_From_Errno(failure));
		return(FALSE);
	}

	if (Is_Persistent(target)) PersistentDirty = true;
	Flush_Persistent_Storage();

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD GetFileAttributesA(LPCSTR filename)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_FILE_ATTRIBUTES);
	}

	std::string const path = Host_Path(filename);
	struct stat info;

	if (::stat(path.c_str(), &info) != 0) {
		int const failure = errno;
		BlockEntryClass found;

			// A name CreateFileA can open must be reported here, or a caller
			// that tests before opening decides the file is missing.
		if (Image_Entry(filename, found)) {
			SetLastError(NO_ERROR);
			return(FILE_ATTRIBUTE_READONLY);
		}

		SetLastError(Win32_Error_From_Errno(failure));
		return(INVALID_FILE_ATTRIBUTES);
	}

	std::size_t const mark = path.find_last_of('/');

	SetLastError(NO_ERROR);
	return(Attributes_From_Stat((mark == std::string::npos) ? path.c_str() : path.c_str() + mark + 1, info));
}


// Only the read-only bit maps to something the host stores; the other
// attributes are accepted and dropped.
BOOL SetFileAttributesA(LPCSTR filename, DWORD attributes)
{
	if (filename == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	std::string const path = Host_Path(filename);
	struct stat info;

	if (::stat(path.c_str(), &info) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	mode_t mode = info.st_mode & (mode_t)07777;

	if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
		mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
	} else {
		mode |= (mode_t)S_IWUSR;
	}

	if (::chmod(path.c_str(), mode) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


// The host has no creation time; the inode change time stands in for it.
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Kind == HANDLE_KIND_IMAGE) {
		FILETIME const recorded = Filetime_From_Image(entry->Image.DateTime);

		if (creation != nullptr) *creation = recorded;
		if (access != nullptr) *access = recorded;
		if (write != nullptr) *write = recorded;

		SetLastError(NO_ERROR);
		return(TRUE);
	}

	struct stat info;
	if (::fstat(entry->Descriptor, &info) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	if (creation != nullptr) *creation = Filetime_From_Host(HOST_STAT_CHANGE(info));
	if (access != nullptr) *access = Filetime_From_Host(HOST_STAT_ACCESS(info));
	if (write != nullptr) *write = Filetime_From_Host(HOST_STAT_WRITE(info));

	SetLastError(NO_ERROR);
	return(TRUE);
}


// A null time leaves that one alone, as under Win32; the creation time is
// accepted and dropped because the host stores none.
BOOL SetFileTime(HANDLE file, FILETIME const * creation, FILETIME const * access, FILETIME const * write)
{
	(void)creation;

	if (Entry_From_Handle(file, HANDLE_KIND_IMAGE) != nullptr) {
		SetLastError(ERROR_ACCESS_DENIED);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_Handle(file, HANDLE_KIND_FILE);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	struct timespec times[2];

	times[0].tv_sec = 0;
	times[0].tv_nsec = UTIME_OMIT;
	times[1].tv_sec = 0;
	times[1].tv_nsec = UTIME_OMIT;

	if (access != nullptr) {
		long long const ticks = Ticks_From_Filetime(access) - FiletimeEpochOffset;
		times[0].tv_sec = (time_t)(ticks / 10000000LL);
		times[0].tv_nsec = (long)((ticks % 10000000LL) * 100LL);
	}

	if (write != nullptr) {
		long long const ticks = Ticks_From_Filetime(write) - FiletimeEpochOffset;
		times[1].tv_sec = (time_t)(ticks / 10000000LL);
		times[1].tv_nsec = (long)((ticks % 10000000LL) * 100LL);
	}

	if (::futimens(entry->Descriptor, times) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION information)
{
	if (information == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	HandleEntryType const * const entry = Entry_From_File_Handle(file);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Kind == HANDLE_KIND_IMAGE) {
		FILETIME const recorded = Filetime_From_Image(entry->Image.DateTime);

		memset(information, 0, sizeof(*information));

		information->dwFileAttributes = FILE_ATTRIBUTE_READONLY;
		information->ftCreationTime = recorded;
		information->ftLastAccessTime = recorded;
		information->ftLastWriteTime = recorded;
		information->nFileSizeLow = (DWORD)entry->Image.Size;
		information->nNumberOfLinks = 1;

			// The first logical block is the nearest thing to an index the
			// volume records.
		information->nFileIndexLow = entry->Image.Extents.empty()
			? (DWORD)0 : (DWORD)entry->Image.Extents.front().Start;

		SetLastError(NO_ERROR);
		return(TRUE);
	}

	struct stat info;
	if (::fstat(entry->Descriptor, &info) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	memset(information, 0, sizeof(*information));

	information->dwFileAttributes = Attributes_From_Stat(nullptr, info);
	information->ftCreationTime = Filetime_From_Host(HOST_STAT_CHANGE(info));
	information->ftLastAccessTime = Filetime_From_Host(HOST_STAT_ACCESS(info));
	information->ftLastWriteTime = Filetime_From_Host(HOST_STAT_WRITE(info));
	information->dwVolumeSerialNumber = (DWORD)info.st_dev;
	information->nFileSizeHigh = (DWORD)((unsigned long long)info.st_size >> 32);
	information->nFileSizeLow = (DWORD)((unsigned long long)info.st_size & 0xFFFFFFFFULL);
	information->nNumberOfLinks = (DWORD)info.st_nlink;
	information->nFileIndexHigh = (DWORD)((unsigned long long)info.st_ino >> 32);
	information->nFileIndexLow = (DWORD)((unsigned long long)info.st_ino & 0xFFFFFFFFULL);

	SetLastError(NO_ERROR);
	return(TRUE);
}


static void Fill_Find_Data(LPWIN32_FIND_DATAA data, std::string const & directory, FindMatchType const & match)
{
	memset(data, 0, sizeof(*data));

	struct stat info;

	if (match.Image.Is_Valid()) {
		FILETIME const recorded = Filetime_From_Image(match.Image.DateTime);

		data->dwFileAttributes = FILE_ATTRIBUTE_READONLY;
		data->ftCreationTime = recorded;
		data->ftLastAccessTime = recorded;
		data->ftLastWriteTime = recorded;
		data->nFileSizeLow = (DWORD)match.Image.Size;

	} else if (::stat(Host_Path((directory + match.Name).c_str()).c_str(), &info) == 0) {
		data->dwFileAttributes = Attributes_From_Stat(match.Name.c_str(), info);
		data->ftCreationTime = Filetime_From_Host(HOST_STAT_CHANGE(info));
		data->ftLastAccessTime = Filetime_From_Host(HOST_STAT_ACCESS(info));
		data->ftLastWriteTime = Filetime_From_Host(HOST_STAT_WRITE(info));
		data->nFileSizeHigh = (DWORD)((unsigned long long)info.st_size >> 32);
		data->nFileSizeLow = (DWORD)((unsigned long long)info.st_size & 0xFFFFFFFFULL);

	} else {
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	}

	strncpy(data->cFileName, match.Name.c_str(), sizeof(data->cFileName) - 1);
	data->cFileName[sizeof(data->cFileName) - 1] = '\0';
}


// The persistent directory joins a search of the game directory it stands in
// front of; a name the game directory already answered is left alone, matching
// the order Host_Path resolves a bare name in.
static void Persistent_Matches(std::string const & directory, std::string const & leaf, std::vector<FindMatchType> & matches)
{
	std::string const & root = Persistent_Root();
	if (root.empty() || !directory.empty()) return;

	DIR * const scan = ::opendir(root.c_str());
	if (scan == nullptr) return;

	for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
		if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;

		bool taken = false;

		for (FindMatchType const & already : matches) {
			if (::strcasecmp(already.Name.c_str(), item->d_name) == 0) taken = true;
		}
		if (taken) continue;

		FindMatchType match;
		match.Name = item->d_name;
		matches.push_back(std::move(match));
	}

	::closedir(scan);
}


// The manifest joins a search of the root only, since it carries no
// directories; matches use Match_Wildcard for the "*.*" rule, and a name
// already answered is left alone so a search reports the copy CreateFileA
// opens.
static void Image_Matches(std::string const & directory, std::string const & leaf, std::vector<FindMatchType> & matches)
{
#if !defined(__EMSCRIPTEN__)
	(void)directory;
	(void)leaf;
	(void)matches;
#else
	std::string inside;
	if (!Image_Path(directory.c_str(), inside) || !inside.empty()) return;

	for (std::string const & name : Manifest_List_Files()) {
		if (!Match_Wildcard(leaf.c_str(), name.c_str())) continue;

		bool taken = false;

		for (FindMatchType const & already : matches) {
			if (::strcasecmp(already.Name.c_str(), name.c_str()) == 0) taken = true;
		}
		if (taken) continue;

		FindMatchType match;
		match.Name = name;

		if (!Manifest_Find(name.c_str(), match.Image)) continue;
		matches.push_back(std::move(match));
	}
#endif
}


// The search runs whole and its matches are kept on the handle, so a directory
// the engine also writes into has a defined answer. Sorting is a deliberate
// departure from Windows: the order decides which ECACHE*.MIX overrides which.
HANDLE FindFirstFileA(LPCSTR filename, LPWIN32_FIND_DATAA data)
{
	if (filename == nullptr || data == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(INVALID_HANDLE_VALUE);
	}

	std::string pattern(filename);

	for (char & character : pattern) {
		if (character == '\\') character = '/';
	}

	std::size_t const split = pattern.find_last_of('/');
	std::string const requested = (split == std::string::npos) ? std::string() : pattern.substr(0, split + 1);
	std::string directory = requested;
	std::string const leaf = (split == std::string::npos) ? pattern : pattern.substr(split + 1);

	if (!directory.empty()) directory = Host_Path(directory.c_str());

	std::vector<FindMatchType> matches;

	if (leaf.find_first_of("*?") == std::string::npos) {

			// A search with no wildcard names one entry and Win32 answers with
			// that entry alone.
		std::string const resolved = Host_Path((directory + leaf).c_str());
		struct stat info;

		if (::stat(resolved.c_str(), &info) == 0) {
			std::size_t const mark = resolved.find_last_of('/');
			FindMatchType match;

			directory = (mark == std::string::npos) ? std::string() : resolved.substr(0, mark + 1);
			match.Name = (mark == std::string::npos) ? resolved : resolved.substr(mark + 1);
			matches.push_back(std::move(match));
		}

	} else {

		DIR * const scan = ::opendir(directory.empty() ? "." : directory.c_str());

		if (scan != nullptr) {
			for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
				if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;

				FindMatchType match;
				match.Name = item->d_name;
				matches.push_back(std::move(match));
			}
			::closedir(scan);
		}
	}

	Persistent_Matches(requested, leaf, matches);

		// The image is searched under the caller's spelling, since the two
		// filesystems answer for case separately.
	Image_Matches(requested, leaf, matches);

	std::sort(matches.begin(), matches.end(), [](FindMatchType const & left, FindMatchType const & right) {
		int const order = ::strcasecmp(left.Name.c_str(), right.Name.c_str());
		return(order != 0 ? order < 0 : left.Name < right.Name);
	});

	if (matches.empty()) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return(INVALID_HANDLE_VALUE);
	}

	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_FIND;
	entry.Directory = directory;
	entry.Matches = std::move(matches);
	entry.Position = 1;

	Fill_Find_Data(data, entry.Directory, entry.Matches[0]);

	SetLastError(NO_ERROR);
	return(Handle_From_Index(index));
}


BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data)
{
	if (data == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	HandleEntryType * const entry = Entry_From_Handle(find, HANDLE_KIND_FIND);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Position >= entry->Matches.size()) {
		SetLastError(ERROR_NO_MORE_FILES);
		return(FALSE);
	}

	Fill_Find_Data(data, entry->Directory, entry->Matches[entry->Position]);
	entry->Position++;

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL FindClose(HANDLE find)
{
	HandleEntryType * const entry = Entry_From_Handle(find, HANDLE_KIND_FIND);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	Release_Handle(entry);

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes)
{
	(void)attributes;

	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::mkdir(Host_Path(path).c_str(), (mode_t)0777) != 0) {
		SetLastError(errno == EEXIST ? (DWORD)ERROR_ALREADY_EXISTS : Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


BOOL RemoveDirectoryA(LPCSTR path)
{
	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::rmdir(Host_Path(path).c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD GetCurrentDirectoryA(DWORD length, LPSTR buffer)
{
	char working[MAX_PATH * 4];

	if (::getcwd(working, sizeof(working)) == nullptr) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(0);
	}

	DWORD const needed = (DWORD)strlen(working);

	if (buffer == nullptr || length <= needed) {
		SetLastError(NO_ERROR);
		return(needed + 1);
	}

	memcpy(buffer, working, needed + 1);

	SetLastError(NO_ERROR);
	return(needed);
}


BOOL SetCurrentDirectoryA(LPCSTR path)
{
	if (path == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	if (::chdir(Host_Path(path).c_str()) != 0) {
		SetLastError(Win32_Error_From_Errno(errno));
		return(FALSE);
	}

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD GetTempPathA(DWORD length, LPSTR buffer)
{
	char const * const temporary = "/tmp/";
	DWORD const needed = (DWORD)strlen(temporary);

	if (buffer == nullptr || length <= needed) {
		SetLastError(NO_ERROR);
		return(needed + 1);
	}

	memcpy(buffer, temporary, needed + 1);

	SetLastError(NO_ERROR);
	return(needed);
}


LONG CompareFileTime(FILETIME const * first, FILETIME const * second)
{
	unsigned long long left = ((unsigned long long)first->dwHighDateTime << 32) | first->dwLowDateTime;
	unsigned long long right = ((unsigned long long)second->dwHighDateTime << 32) | second->dwLowDateTime;

	if (left < right) return(-1);
	if (left > right) return(1);
	return(0);
}


BOOL FileTimeToSystemTime(FILETIME const * filetime, LPSYSTEMTIME systemtime)
{
	if (filetime == nullptr || systemtime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	long milliseconds = 0;

	if (!Calendar_From_Filetime(filetime, &parts, &milliseconds)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	Fill_System_Time(systemtime, parts, milliseconds);
	return(TRUE);
}


BOOL SystemTimeToFileTime(SYSTEMTIME const * systemtime, LPFILETIME filetime)
{
	if (systemtime == nullptr || filetime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	memset(&parts, 0, sizeof(parts));

	parts.tm_year = (int)systemtime->wYear - 1900;
	parts.tm_mon = (int)systemtime->wMonth - 1;
	parts.tm_mday = (int)systemtime->wDay;
	parts.tm_hour = (int)systemtime->wHour;
	parts.tm_min = (int)systemtime->wMinute;
	parts.tm_sec = (int)systemtime->wSecond;

	time_t const seconds = ::timegm(&parts);
	if (seconds == (time_t)-1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*filetime = Filetime_From_Host((long long)seconds, (long)systemtime->wMilliseconds * 1000000L);
	return(TRUE);
}


// The zone offset is taken at the instant being converted rather than the
// current one, so a time across a daylight-saving change converts correctly.
BOOL FileTimeToLocalFileTime(FILETIME const * filetime, LPFILETIME local)
{
	if (filetime == nullptr || local == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	long long const ticks = Ticks_From_Filetime(filetime) - FiletimeEpochOffset;
	time_t const when = (time_t)(ticks / 10000000LL);

	struct tm parts;
	if (::localtime_r(&when, &parts) == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	time_t const shifted = ::timegm(&parts);
	long long const offset = (long long)(shifted - when) * 10000000LL;

	unsigned long long const adjusted = (unsigned long long)(Ticks_From_Filetime(filetime) + offset);
	local->dwLowDateTime = (DWORD)(adjusted & 0xFFFFFFFFULL);
	local->dwHighDateTime = (DWORD)(adjusted >> 32);
	return(TRUE);
}


BOOL FileTimeToDosDateTime(FILETIME const * filetime, LPWORD dosdate, LPWORD dostime)
{
	if (filetime == nullptr || dosdate == nullptr || dostime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	if (!Calendar_From_Filetime(filetime, &parts, nullptr)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	int const year = parts.tm_year + 1900;
	if (year < 1980 || year > 2107) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*dosdate = (WORD)(((year - 1980) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
	*dostime = (WORD)((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));
	return(TRUE);
}


BOOL DosDateTimeToFileTime(WORD dosdate, WORD dostime, LPFILETIME filetime)
{
	if (filetime == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	struct tm parts;
	memset(&parts, 0, sizeof(parts));

	parts.tm_year = ((dosdate >> 9) & 0x7F) + 1980 - 1900;
	parts.tm_mon = ((dosdate >> 5) & 0x0F) - 1;
	parts.tm_mday = dosdate & 0x1F;
	parts.tm_hour = (dostime >> 11) & 0x1F;
	parts.tm_min = (dostime >> 5) & 0x3F;
	parts.tm_sec = (dostime & 0x1F) * 2;

	time_t const seconds = ::timegm(&parts);
	if (seconds == (time_t)-1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(FALSE);
	}

	*filetime = Filetime_From_Host((long long)seconds, 0);
	return(TRUE);
}


//------------------------------------------------------------------------------
// Events, mutexes, and the interlocked operations.
//------------------------------------------------------------------------------


// There is no other thread to signal an event, so each reports itself instead
// of keeping state.

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL SetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL ResetEvent(HANDLE) { return(WIN32_STUB(FALSE)); }


// Nothing here starts a second thread, so a mutex is never contended: every
// acquisition succeeds at once and ownership is still counted. A second thread
// would make these wrong rather than unimplemented.
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialowner, LPCSTR name)
{
	(void)attributes;

		// A named mutex asks whether another copy of the engine is running; one
		// module cannot see another's names, so it is always the first of its
		// name.
	std::size_t const index = Allocate_Handle();
	HandleEntryType & entry = Handle_Table()[index];

	entry.Kind = HANDLE_KIND_MUTEX;
	entry.Path = (name != nullptr) ? name : "";
	entry.Held = (initialowner != FALSE) ? 1u : 0u;

	SetLastError(NO_ERROR);
	return(Handle_From_Index(index));
}


BOOL ReleaseMutex(HANDLE mutex)
{
	HandleEntryType * const entry = Entry_From_Handle(mutex, HANDLE_KIND_MUTEX);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(FALSE);
	}

	if (entry->Held == 0) {
		SetLastError(ERROR_NOT_OWNER);
		return(FALSE);
	}

	entry->Held--;

	SetLastError(NO_ERROR);
	return(TRUE);
}


DWORD WaitForSingleObject(HANDLE object, DWORD milliseconds)
{
	(void)milliseconds;

	HandleEntryType * const entry = Entry_From_Handle(object, HANDLE_KIND_MUTEX);
	if (entry == nullptr) {
		SetLastError(ERROR_INVALID_HANDLE);
		return(WIN32_UNSUPPORTED("WaitForSingleObject on anything but a mutex", WAIT_FAILED));
	}

	entry->Held++;

	SetLastError(NO_ERROR);
	return(WAIT_OBJECT_0);
}


DWORD WaitForMultipleObjects(DWORD count, HANDLE const * objects, BOOL waitall, DWORD milliseconds)
{
	(void)milliseconds;

	if (objects == nullptr || count == 0 || count > MAXIMUM_WAIT_OBJECTS) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(WAIT_FAILED);
	}

	for (DWORD index = 0; index < count; index++) {
		if (Entry_From_Handle(objects[index], HANDLE_KIND_MUTEX) != nullptr) continue;

		SetLastError(ERROR_INVALID_HANDLE);
		return(WIN32_UNSUPPORTED("WaitForMultipleObjects on anything but a mutex", WAIT_FAILED));
	}

		// Waiting for any one takes the first, since none can be held.
	DWORD const taken = (waitall != FALSE) ? count : 1;

	for (DWORD index = 0; index < taken; index++) {
		Entry_From_Handle(objects[index], HANDLE_KIND_MUTEX)->Held++;
	}

	SetLastError(NO_ERROR);
	return(WAIT_OBJECT_0);
}


HANDLE OpenMutexA(DWORD, BOOL, LPCSTR) { return(WIN32_STUB((HANDLE)nullptr)); }


// Single-threaded, so the arithmetic is the whole contract.
LONG InterlockedIncrement(LONG volatile * addend) { LONG value = *addend + 1; *addend = value; return(value); }
LONG InterlockedDecrement(LONG volatile * addend) { LONG value = *addend - 1; *addend = value; return(value); }
LONG InterlockedExchange(LONG volatile * target, LONG value) { LONG old = *target; *target = value; return(old); }


//------------------------------------------------------------------------------
// The heap.
//------------------------------------------------------------------------------


// Windows' movable-memory modes are not honored; a caller that asks for one is
// reported.
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes)
{
	if ((flags & GMEM_MOVEABLE) != 0) WIN32_STUB_VOID();

	void * block = malloc(bytes);
	if (block != nullptr && (flags & GMEM_ZEROINIT) != 0) memset(block, 0, bytes);
	return((HGLOBAL)block);
}


HGLOBAL GlobalFree(HGLOBAL memory) { free(memory); return(nullptr); }
LPVOID GlobalLock(HGLOBAL memory) { return(memory); }
BOOL GlobalUnlock(HGLOBAL) { return(FALSE); }


HLOCAL LocalFree(HLOCAL memory) { free(memory); return(nullptr); }


//------------------------------------------------------------------------------
// The registry and the profiles.
//------------------------------------------------------------------------------


LONG RegOpenKeyExA(HKEY, LPCSTR, DWORD, DWORD, PHKEY result) { if (result != nullptr) *result = nullptr; return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, PHKEY result, LPDWORD) { if (result != nullptr) *result = nullptr; return(WIN32_STUB(ERROR_ACCESS_DENIED)); }
LONG RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, BYTE const *, DWORD) { return(WIN32_STUB(ERROR_ACCESS_DENIED)); }
LONG RegDeleteValueA(HKEY, LPCSTR) { return(WIN32_STUB(ERROR_FILE_NOT_FOUND)); }
LONG RegCloseKey(HKEY) { return(WIN32_STUB(ERROR_INVALID_HANDLE)); }


UINT GetPrivateProfileIntA(LPCSTR, LPCSTR, INT defaultvalue, LPCSTR) { return(WIN32_STUB((UINT)defaultvalue)); }
DWORD GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR defaultvalue, LPSTR returned, DWORD size, LPCSTR)
{
	if (returned != nullptr && size > 0) {
		strncpy(returned, defaultvalue != nullptr ? defaultvalue : "", size - 1);
		returned[size - 1] = '\0';
		return(WIN32_STUB((DWORD)strlen(returned)));
	}
	return(WIN32_STUB(0));
}
BOOL WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return(WIN32_STUB(FALSE)); }


//------------------------------------------------------------------------------
// The MSVC C runtime.
//------------------------------------------------------------------------------


static char * Convert_Unsigned(unsigned long value, char * buffer, int radix, bool negative)
{
	char digits[sizeof(unsigned long) * 8 + 1];
	int count = 0;

	if (radix < 2 || radix > 36) {
		buffer[0] = '\0';
		return(buffer);
	}

	do {
		unsigned long digit = value % (unsigned long)radix;
		digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
		value /= (unsigned long)radix;
	} while (value != 0);

	char * out = buffer;
	if (negative) *out++ = '-';
	while (count > 0) *out++ = digits[--count];
	*out = '\0';
	return(buffer);
}


int _getch(void)
{
	return(WIN32_STUB(-1));
}


char * itoa(int value, char * buffer, int radix)
{
	bool negative = (radix == 10 && value < 0);
	unsigned long magnitude = negative ? (unsigned long)(-(long)value) : (unsigned long)(unsigned int)value;

	return(Convert_Unsigned(magnitude, buffer, radix, negative));
}


char * ltoa(long value, char * buffer, int radix)
{
	bool negative = (radix == 10 && value < 0);
	unsigned long magnitude = negative ? (unsigned long)(-value) : (unsigned long)value;

	return(Convert_Unsigned(magnitude, buffer, radix, negative));
}


char * ultoa(unsigned long value, char * buffer, int radix)
{
	return(Convert_Unsigned(value, buffer, radix, false));
}


static void Copy_Component(char * destination, char const * start, char const * end)
{
	if (destination == nullptr) return;

	while (start < end) *destination++ = *start++;
	*destination = '\0';
}


void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext)
{
	char const * cursor = path;
	char const * drive_end = cursor;

	if (path[0] != '\0' && path[1] == ':') drive_end = path + 2;
	Copy_Component(drive, path, drive_end);

	char const * dir_end = drive_end;
	for (cursor = drive_end; *cursor != '\0'; cursor++) {
		if (*cursor == '\\' || *cursor == '/') dir_end = cursor + 1;
	}
	Copy_Component(dir, drive_end, dir_end);

	char const * ext_start = cursor;
	for (char const * scan = dir_end; *scan != '\0'; scan++) {
		if (*scan == '.') ext_start = scan;
	}
	Copy_Component(fname, dir_end, ext_start);
	Copy_Component(ext, ext_start, cursor);
}


void _makepath(char * path, char const * drive, char const * dir, char const * fname, char const * ext)
{
	char * out = path;

	if (drive != nullptr && drive[0] != '\0') {
		*out++ = drive[0];
		*out++ = ':';
	}

	if (dir != nullptr && dir[0] != '\0') {
		while (*dir != '\0') *out++ = *dir++;
		if (out[-1] != '\\' && out[-1] != '/') *out++ = '\\';
	}

	if (fname != nullptr) {
		while (*fname != '\0') *out++ = *fname++;
	}

	if (ext != nullptr && ext[0] != '\0') {
		if (ext[0] != '.') *out++ = '.';
		while (*ext != '\0') *out++ = *ext++;
	}

	*out = '\0';
}


//------------------------------------------------------------------------------
// Resources and version information.
//------------------------------------------------------------------------------


int LoadStringA(HINSTANCE, UINT, LPSTR buffer, int size) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }
HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) { return(WIN32_STUB((HRSRC)nullptr)); }
HGLOBAL LoadResource(HMODULE, HRSRC) { return(WIN32_STUB((HGLOBAL)nullptr)); }
LPVOID LockResource(HGLOBAL) { return(WIN32_STUB((LPVOID)nullptr)); }
DWORD SizeofResource(HMODULE, HRSRC) { return(WIN32_STUB(0)); }


// A wasm binary has no resource directory, so zero is the answer for the
// running module, as Windows gives for a file without a version resource;
// another file may carry one this target does not read, so that request is
// reported.
DWORD GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle)
{
	if (handle != nullptr) *handle = 0;

	char module[MAX_PATH];

	if (filename != nullptr && GetModuleFileNameA(nullptr, module, sizeof(module)) > 0
			&& strcmp(filename, module) == 0) {
		return(0);
	}

	return(WIN32_STUB(0));
}


BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) { return(WIN32_STUB(FALSE)); }
BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID * buffer, PUINT length) { if (buffer != nullptr) *buffer = nullptr; if (length != nullptr) *length = 0; return(WIN32_STUB(FALSE)); }


//------------------------------------------------------------------------------
// The console.
//------------------------------------------------------------------------------


BOOL AllocConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL FreeConsole(void) { return(WIN32_STUB(FALSE)); }
BOOL SetConsoleTitleA(LPCSTR) { return(WIN32_STUB(FALSE)); }
BOOL SetConsoleCP(UINT) { return(WIN32_STUB(FALSE)); }
BOOL SetConsoleOutputCP(UINT) { return(WIN32_STUB(FALSE)); }

// The substitute converts narrow text as Windows-1252 and says so, which is what tells the
// engine to re-encode the resource strings it fetches into UTF-8.
UINT GetACP(void) { return(1252); }
UINT GetOEMCP(void) { return(437); }
HANDLE GetStdHandle(DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL SetConsoleMode(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleMode(HANDLE, LPDWORD) { return(WIN32_STUB(FALSE)); }


BOOL SetStdHandle(DWORD, HANDLE) { return(WIN32_STUB(FALSE)); }
BOOL GetConsoleScreenBufferInfo(HANDLE, PCONSOLE_SCREEN_BUFFER_INFO) { return(WIN32_STUB(FALSE)); }
HWND GetConsoleWindow(void) { return(WIN32_STUB((HWND)nullptr)); }


BOOL SetConsoleScreenBufferSize(HANDLE, COORD) { return(WIN32_STUB(FALSE)); }


BOOL WriteConsoleA(HANDLE, void const *, DWORD, LPDWORD written, LPVOID) { if (written != nullptr) *written = 0; return(WIN32_STUB(FALSE)); }


//------------------------------------------------------------------------------
// Locale formatting.
//------------------------------------------------------------------------------


// The page's locale stands in for the user locale; a picture string is not
// served, and the engine passes none.
int GetTimeFormatA(LCID, DWORD flags, SYSTEMTIME const * time, LPCSTR format, LPSTR text, int count)
{
	if (time == nullptr || text == nullptr || count <= 0) return(0);

	if (format != nullptr) {
		return(WIN32_UNSUPPORTED("GetTimeFormatA: a picture string of its own", 0));
	}

	bool const seconds = (flags & (TIME_NOSECONDS | TIME_NOMINUTESORSECONDS)) == 0;
	bool const minutes = (flags & TIME_NOMINUTESORSECONDS) == 0;

#if defined(__EMSCRIPTEN__)
	return(EM_ASM_INT({
		var options = {hour: "numeric"};
		if ($4) options.minute = "2-digit";
		if ($3) options.second = "2-digit";

		var text = new Date(2000, 0, 1, $0, $1, $2).toLocaleTimeString(undefined, options);
		var size = lengthBytesUTF8(text) + 1;

		if (size > $6) return 0;
		stringToUTF8(text, $5, $6);
		return size;
	}, time->wHour, time->wMinute, time->wSecond, seconds, minutes, text, count));
#else
	// The C locale stands in for the user locale off the page.
	struct tm parts = {};
	parts.tm_hour = time->wHour;
	parts.tm_min = time->wMinute;
	parts.tm_sec = time->wSecond;
	parts.tm_year = 100;
	parts.tm_mday = 1;
	size_t const length = strftime(text, (size_t)count, seconds ? "%H:%M:%S" : (minutes ? "%H:%M" : "%H"), &parts);
	return(length > 0 ? (int)length + 1 : 0);
#endif
}


int GetDateFormatA(LCID, DWORD, SYSTEMTIME const * date, LPCSTR format, LPSTR text, int count)
{
	if (date == nullptr || text == nullptr || count <= 0) return(0);

	if (format != nullptr) {
		return(WIN32_UNSUPPORTED("GetDateFormatA: a picture string of its own", 0));
	}

#if defined(__EMSCRIPTEN__)
	return(EM_ASM_INT({
		var text = new Date($0, $1 - 1, $2).toLocaleDateString();
		var size = lengthBytesUTF8(text) + 1;

		if (size > $4) return 0;
		stringToUTF8(text, $3, $4);
		return size;
	}, date->wYear, date->wMonth, date->wDay, text, count));
#else
	struct tm parts = {};
	parts.tm_year = date->wYear - 1900;
	parts.tm_mon = date->wMonth - 1;
	parts.tm_mday = date->wDay;
	size_t const length = strftime(text, (size_t)count, "%x", &parts);
	return(length > 0 ? (int)length + 1 : 0);
#endif
}


//------------------------------------------------------------------------------
// Winsock.
//------------------------------------------------------------------------------


// The calls that have no BSD counterpart.
#include "winsock.h"

int WSAStartup(WORD, LPWSADATA data)
{
	if (data != nullptr) memset(data, 0, sizeof(*data));
	return(WIN32_STUB(WSASYSNOTREADY));
}


int WSACleanup(void) { return(WIN32_STUB(SOCKET_ERROR)); }
int WSAGetLastError(void) { return(errno != 0 ? WSABASEERR + errno : 0); }
void WSASetLastError(int error) { errno = error > WSABASEERR ? error - WSABASEERR : error; }
int ioctlsocket(SOCKET socket, long command, unsigned long * argument)
{
	if (command != FIONBIO || argument == nullptr) {
		return(WIN32_UNSUPPORTED("ioctlsocket with a command other than FIONBIO", SOCKET_ERROR));
	}

	int flags = fcntl(socket, F_GETFL, 0);
	if (flags == -1) return(SOCKET_ERROR);

	flags = (*argument != 0) ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return(fcntl(socket, F_SETFL, flags) == -1 ? SOCKET_ERROR : 0);
}


//------------------------------------------------------------------------------
// What is left.
//------------------------------------------------------------------------------


void OutputDebugStringA(LPCSTR string)
{
	if (string != nullptr) fputs(string, stderr);
}


/// <summary>Copies the run through, since the two code pages agree across
/// ASCII; source and destination may be the same buffer.</summary>
BOOL CharToOemBuffA(LPCSTR source, LPSTR destination, DWORD length)
{
	if (source == nullptr || destination == nullptr) {
		return(FALSE);
	}

	for (DWORD index = 0; index < length; index++) {
		destination[index] = source[index];
	}

	return(TRUE);
}


HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) { return(WIN32_STUB((HINSTANCE)nullptr)); }


BOOL DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD returned, LPOVERLAPPED) { if (returned != nullptr) *returned = 0; return(WIN32_STUB(FALSE)); }


DWORD FormatMessageA(DWORD, LPCVOID, DWORD, DWORD, LPSTR buffer, DWORD size, va_list *) { if (buffer != nullptr && size > 0) buffer[0] = '\0'; return(WIN32_STUB(0)); }


DWORD GetAdaptersInfo(PIP_ADAPTER_INFO, PULONG size) { if (size != nullptr) *size = 0; return(WIN32_STUB(ERROR_BUFFER_OVERFLOW)); }

#endif	// OPENTS_WIN32_SUBSTITUTE
