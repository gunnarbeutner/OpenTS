/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The file layer over POSIX, for the targets that have no Win32 API. It mirrors
// file_win32.cpp; where the two differ it is because the host does. On a page it also
// carries what a browser adds: the persistent directory and the manifest of archives the
// page serves.

#include "always.h"

#if !defined(_WIN32)

#include "platform/file.h"
#include "platform/filehint.h"

#include "blocksource.h"

#if defined(__EMSCRIPTEN__)
#include "manifest.h"

#include <emscripten.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>


struct PlatformFileClass::StateType
{
	int Descriptor = -1;

	// A file the manifest names is read through the volume holding it.
	std::shared_ptr<BlockFileClass> Volume;
	BlockEntryClass Image;
	std::uint32_t Cursor = 0;
};


namespace {

// Where each host's stat keeps its nanosecond stamps.
#if defined(__APPLE__)
#define HOST_STAT_WRITE(info)	((info).st_mtimespec)
#else
#define HOST_STAT_WRITE(info)	((info).st_mtim)
#endif


bool Path_Present(std::string const & path)
{
	struct stat info;

	return(::lstat(path.c_str(), &info) == 0);
}


// A path that exists as spelled is used as spelled; otherwise each missing component is
// matched without regard to case, an unmatched component keeps its spelling, and two entries
// differing only in case resolve to the first in sort order.
std::string Resolve_Case(std::string const & translated)
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


// Only this directory is mounted on IndexedDB and survives the page. It stands in front of
// the game directory, so a name that exists as game data still resolves to the game data.
#define PERSISTENT_DIRECTORY "/save"

std::string const & Persistent_Root(void)
{
	static std::string const root = []() -> std::string {
		struct stat info;

		if (::stat(PERSISTENT_DIRECTORY, &info) == 0 && S_ISDIR(info.st_mode)) {
			return(PERSISTENT_DIRECTORY "/");
		}

		return(std::string());
	}();

	return(root);
}


bool Is_Persistent(std::string const & path)
{
	std::string const & root = Persistent_Root();

	return(!root.empty() && path.compare(0, root.size(), root) == 0);
}


std::string Forward_Slashes(char const * path)
{
	std::string translated((path != nullptr) ? path : "");

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}
	return(translated);
}


// A relative path is looked for in the persistent directory, then the game directory, and
// one in neither resolves into the persistent directory, so a file about to be created lands
// where it survives the tab. The whole relative path is carried across because saves sit in
// a folder of their own.
std::string Host_Path(char const * path)
{
	std::string const translated = Forward_Slashes(path);
	std::string const & root = Persistent_Root();

	if (!root.empty() && !translated.empty() && translated.front() != '/') {
		std::string const persistent = root + translated;

		// The caller's spelling is tried in both directories before either is matched
		// without regard to case.
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


// IndexedDB is reached asynchronously, so the transfer starts here and finishes on its own;
// the page counts completions for an automated check to wait on.
bool PersistentDirty = false;


void Note_Change(std::string const & path)
{
	if (Is_Persistent(path)) PersistentDirty = true;
}


void Flush_Persistent_Storage(void)
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


// The image is asked for a path relative to the volume root, drive letter and leading
// separator removed; a path that climbs out of the volume is refused.
bool Image_Path(char const * path, std::string & inside)
{
	inside.clear();

	if (path == nullptr) return(false);

	std::string translated = Forward_Slashes(path);

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


// Only the last path component is looked up, because the manifest carries no directories and
// one name answers to exactly one archive.
std::shared_ptr<BlockFileClass> Image_Entry(char const * filename, BlockEntryClass & entry)
{
	std::string inside;
	if (!Image_Path(filename, inside) || inside.empty()) return(nullptr);

	std::size_t const slash = inside.find_last_of('/');
	std::string leaf = (slash == std::string::npos) ? inside : inside.substr(slash + 1);

	// The manifest's keys are uppercase DOS 8.3, matched without regard to case as the
	// ISO9660 reader's Find does.
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


// A record whose date the volume left unset reports no time at all.
FileTimeType File_Time_From_Image(BlockEntryClass const & image)
{
	FileTimeType time;

	if (image.DateTime == 0 || !File_Time_From_Dos_Date_Time(image.DateTime, time)) {
		return(FileTimeType{});
	}
	return(time);
}


void Info_From_Image(std::string const & name, BlockEntryClass const & image, PlatformFileInfoType & info)
{
	info = PlatformFileInfoType{};
	info.Name = name;
	info.Size = image.Size;
	info.Modified = File_Time_From_Image(image);
	info.IsReadOnly = true;
}


// A name beginning with a dot is hidden, so the engine's scans skip dot files as they skip
// hidden files on Windows.
void Info_From_Stat(std::string const & name, struct stat const & host, PlatformFileInfoType & info)
{
	info = PlatformFileInfoType{};
	info.Name = name;
	info.Size = (std::uint64_t)host.st_size;
	info.Modified = File_Time_From_Unix((std::int64_t)HOST_STAT_WRITE(host).tv_sec, (std::int64_t)HOST_STAT_WRITE(host).tv_nsec);
	info.IsDirectory = S_ISDIR(host.st_mode);
	info.IsHidden = (name.size() > 1 && name[0] == '.' && name != "..");
	info.IsReadOnly = (host.st_mode & S_IWUSR) == 0;
}


std::string Leaf_Of(std::string const & path)
{
	std::size_t const mark = path.find_last_of('/');

	return((mark == std::string::npos) ? path : path.substr(mark + 1));
}


// DOS wildcard matching, ignoring case; "*.*" means every file, including a name with no
// extension.
bool Match_Wildcard(char const * pattern, char const * name)
{
	if (std::strcmp(pattern, "*.*") == 0) pattern = "*";

	char const * patternmark = nullptr;
	char const * namemark = nullptr;

	while (*name != '\0') {
		if (*pattern == '?' || std::tolower((unsigned char)*pattern) == std::tolower((unsigned char)*name)) {
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


// The host directory and the image answer for a name's size and date differently, so each
// match remembers its side.
struct MatchType
{
	std::string Name;
	BlockEntryClass Image;
};


bool Already_Matched(std::vector<MatchType> const & matches, char const * name)
{
	for (MatchType const & already : matches) {
		if (::strcasecmp(already.Name.c_str(), name) == 0) return(true);
	}
	return(false);
}


// The persistent directory joins a search of the game directory it stands in front of; a
// name the game directory already answered is left alone, matching the order Host_Path
// resolves a bare name in.
void Persistent_Matches(std::string const & directory, std::string const & leaf, std::vector<MatchType> & matches)
{
	std::string const & root = Persistent_Root();
	if (root.empty() || !directory.empty()) return;

	DIR * const scan = ::opendir(root.c_str());
	if (scan == nullptr) return;

	for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
		if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;
		if (Already_Matched(matches, item->d_name)) continue;

		MatchType match;
		match.Name = item->d_name;
		matches.push_back(std::move(match));
	}

	::closedir(scan);
}


// The manifest joins a search of the root only, since it carries no directories, and a name
// already answered is left alone so a search reports the copy an open reaches.
void Image_Matches(std::string const & directory, std::string const & leaf, std::vector<MatchType> & matches)
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
		if (Already_Matched(matches, name.c_str())) continue;

		MatchType match;
		match.Name = name;

		if (!Manifest_Find(name.c_str(), match.Image)) continue;
		matches.push_back(std::move(match));
	}
#endif
}


std::uint32_t Image_Span(BlockEntryClass const & image, std::uint32_t offset, std::uint32_t length)
{
	return((length != 0) ? length : (image.Size - offset));
}

}	// namespace


PlatformFileClass::PlatformFileClass(void) = default;
PlatformFileClass::PlatformFileClass(PlatformFileClass && other) noexcept = default;


PlatformFileClass::~PlatformFileClass(void)
{
	Close();
}


PlatformFileClass & PlatformFileClass::operator = (PlatformFileClass && other) noexcept
{
	if (this != &other) {
		Close();
		State = std::move(other.State);
	}
	return(*this);
}


bool PlatformFileClass::Open(char const * path, PlatformOpenType mode)
{
	Close();

	if (path == nullptr) {
		errno = EINVAL;
		return(false);
	}

	int flags = O_RDONLY;

	switch (mode) {
		case PlatformOpenType::READ:		flags = O_RDONLY; break;
		case PlatformOpenType::WRITE:		flags = O_WRONLY | O_CREAT | O_TRUNC; break;
		case PlatformOpenType::UPDATE:		flags = O_RDWR | O_CREAT; break;
		case PlatformOpenType::EXCLUSIVE:	flags = O_WRONLY | O_CREAT | O_EXCL; break;

		default:
			errno = EINVAL;
			return(false);
	}

	std::string const host = Host_Path(path);
	struct stat existing;
	bool const present = (::stat(host.c_str(), &existing) == 0);

	// Windows refuses to open a directory as a file, and so does this.
	if (present && S_ISDIR(existing.st_mode)) {
		errno = EISDIR;
		return(false);
	}

	// The image is read-only, so only a read of a file the host lacks resolves there.
	if (!present && mode == PlatformOpenType::READ) {
		BlockEntryClass found;
		std::shared_ptr<BlockFileClass> volume = Image_Entry(path, found);

		if (volume) {
			State = std::make_unique<StateType>();
			State->Volume = std::move(volume);
			State->Image = found;

			// A network-backed image reads ahead from the first block on this hint and
			// never past the end of the file.
			State->Volume->Hint(State->Image, BLOCK_HINT_SEQUENTIAL, 0, State->Image.Size);
			return(true);
		}
	}

	int const descriptor = ::open(host.c_str(), flags, (mode_t)0666);
	if (descriptor < 0) {
		return(false);
	}

	State = std::make_unique<StateType>();
	State->Descriptor = descriptor;

	if (mode != PlatformOpenType::READ) {
		Note_Change(host);
	}
	return(true);
}


bool PlatformFileClass::Close(void)
{
	if (State == nullptr) {
		return(false);
	}

	bool closed = true;

	if (!State->Volume) {
		closed = (::close(State->Descriptor) == 0);
	}

	State.reset();
	Flush_Persistent_Storage();
	return(closed);
}


bool PlatformFileClass::Read(void * buffer, std::uint32_t length, std::uint32_t & got)
{
	got = 0;

	if (State == nullptr || (buffer == nullptr && length != 0)) {
		return(false);
	}

	if (State->Volume) {

		// The volume reports a transport failure the same way as the end of the file, so
		// the count the file could answer is worked out first and anything less is a
		// failure; DeferredReadClass tells a declined read from a fault.
		std::uint32_t const available = (State->Cursor < State->Image.Size) ? State->Image.Size - State->Cursor : 0;
		std::uint32_t const wanted = (length < available) ? length : available;

		int const read = State->Volume->Read(State->Image, State->Cursor, buffer, wanted);

		got = (read > 0) ? (std::uint32_t)read : 0;
		State->Cursor += got;
		return(read >= 0 && got == wanted);
	}

	// A short host read is resumed; only the end of the file stops early.
	char * const cursor = (char *)buffer;

	while (got < length) {
		ssize_t const read = ::read(State->Descriptor, cursor + got, (size_t)(length - got));

		if (read < 0) {
			if (errno == EINTR) continue;
			return(false);
		}

		if (read == 0) break;
		got += (std::uint32_t)read;
	}

	return(true);
}


bool PlatformFileClass::Write(void const * buffer, std::uint32_t length, std::uint32_t & put)
{
	put = 0;

	if (State == nullptr || State->Volume || (buffer == nullptr && length != 0)) {
		return(false);
	}

	char const * const cursor = (char const *)buffer;

	while (put < length) {
		ssize_t const written = ::write(State->Descriptor, cursor + put, (size_t)(length - put));

		if (written < 0) {
			if (errno == EINTR) continue;
			return(false);
		}

		if (written == 0) break;
		put += (std::uint32_t)written;
	}

	return(put == length);
}


std::int64_t PlatformFileClass::Seek(std::int64_t offset, int origin)
{
	if (State == nullptr || (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)) {
		return(-1);
	}

	if (State->Volume) {
		std::int64_t const base = (origin == SEEK_SET) ? 0
			: ((origin == SEEK_CUR) ? (std::int64_t)State->Cursor : (std::int64_t)State->Image.Size);
		std::int64_t const wanted = base + offset;

		// A seek past the end is allowed and one before the start is not, as on Windows.
		if (wanted < 0 || wanted > (std::int64_t)0xFFFFFFFFLL) {
			return(-1);
		}

		State->Cursor = (std::uint32_t)wanted;
		return(wanted);
	}

	off_t const position = ::lseek(State->Descriptor, (off_t)offset, origin);
	return((position < 0) ? -1 : (std::int64_t)position);
}


std::int64_t PlatformFileClass::Size(void) const
{
	if (State == nullptr) {
		return(-1);
	}

	if (State->Volume) {
		return((std::int64_t)State->Image.Size);
	}

	struct stat info;
	if (::fstat(State->Descriptor, &info) != 0) {
		return(-1);
	}
	return((std::int64_t)info.st_size);
}


bool PlatformFileClass::Flush(void)
{
	return(State != nullptr && !State->Volume && ::fsync(State->Descriptor) == 0);
}


bool PlatformFileClass::Modified_Time(FileTimeType & time) const
{
	if (State == nullptr) {
		return(false);
	}

	if (State->Volume) {
		time = File_Time_From_Image(State->Image);
		return(true);
	}

	struct stat info;
	if (::fstat(State->Descriptor, &info) != 0) {
		return(false);
	}

	time = File_Time_From_Unix((std::int64_t)HOST_STAT_WRITE(info).tv_sec, (std::int64_t)HOST_STAT_WRITE(info).tv_nsec);
	return(true);
}


// The access time moves with the write time, as the DOS-era callers set both.
bool PlatformFileClass::Set_Modified_Time(FileTimeType time)
{
	if (State == nullptr || State->Volume) {
		return(false);
	}

	std::int64_t seconds = 0;
	std::int64_t nanoseconds = 0;
	Unix_From_File_Time(time, seconds, nanoseconds);

	struct timespec times[2];
	times[0].tv_sec = (time_t)seconds;
	times[0].tv_nsec = (long)nanoseconds;
	times[1] = times[0];

	return(::futimens(State->Descriptor, times) == 0);
}


bool PlatformFileClass::Hint(BlockHintType kind, std::uint32_t offset, std::uint32_t length)
{
	if (State == nullptr || !State->Volume || offset >= State->Image.Size) {
		return(false);
	}

	State->Volume->Hint(State->Image, kind, offset, Image_Span(State->Image, offset, length));
	return(true);
}


// A name the host lacks but the manifest carries is reported, or a caller that tests before
// opening decides the file is missing.
bool Platform_File_Info(char const * path, PlatformFileInfoType & info)
{
	if (path == nullptr) {
		return(false);
	}

	std::string const host = Host_Path(path);
	struct stat status;

	if (::stat(host.c_str(), &status) == 0) {
		Info_From_Stat(Leaf_Of(host), status, info);
		return(true);
	}

	BlockEntryClass found;
	if (Image_Entry(path, found)) {
		Info_From_Image(Leaf_Of(Forward_Slashes(path)), found, info);
		return(true);
	}

	return(false);
}


bool Platform_Remove_File(char const * path)
{
	if (path == nullptr) {
		return(false);
	}

	std::string const host = Host_Path(path);

	if (::unlink(host.c_str()) != 0) {
		return(false);
	}

	Note_Change(host);
	Flush_Persistent_Storage();
	return(true);
}


// rename replaces its target in one step.
bool Platform_Replace_File(char const * source, char const * target)
{
	if (source == nullptr || target == nullptr) {
		return(false);
	}

	std::string const from = Host_Path(source);
	std::string const to = Host_Path(target);

	if (::rename(from.c_str(), to.c_str()) != 0) {
		return(false);
	}

	Note_Change(from);
	Note_Change(to);
	Flush_Persistent_Storage();
	return(true);
}


bool Platform_Copy_File(char const * source, char const * target)
{
	if (source == nullptr || target == nullptr) {
		return(false);
	}

	std::string const to = Host_Path(target);

	int const from = ::open(Host_Path(source).c_str(), O_RDONLY);
	if (from < 0) {
		return(false);
	}

	int const into = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0666);
	if (into < 0) {
		::close(from);
		return(false);
	}

	char block[64 * 1024];
	bool copied = true;

	for (;;) {
		ssize_t const got = ::read(from, block, sizeof(block));

		if (got < 0) {
			if (errno == EINTR) continue;
			copied = false;
			break;
		}
		if (got == 0) break;

		ssize_t placed = 0;
		while (placed < got) {
			ssize_t const put = ::write(into, block + placed, (size_t)(got - placed));
			if (put < 0) {
				if (errno == EINTR) continue;
				copied = false;
				break;
			}
			placed += put;
		}

		if (!copied) break;
	}

	::close(from);
	if (::close(into) != 0) copied = false;

	Note_Change(to);
	Flush_Persistent_Storage();
	return(copied);
}


bool Platform_Create_Directory(char const * path)
{
	return(path != nullptr && ::mkdir(Host_Path(path).c_str(), (mode_t)0777) == 0);
}


// The search runs whole before it returns, so a directory the engine also writes into has a
// defined answer. Sorting is a deliberate departure from what a host's directory order would
// give: the order decides which ECACHE*.MIX overrides which.
std::vector<PlatformFileInfoType> Platform_Find_Files(char const * pattern)
{
	std::vector<PlatformFileInfoType> found;

	if (pattern == nullptr) {
		return(found);
	}

	std::string const translated = Forward_Slashes(pattern);
	std::size_t const split = translated.find_last_of('/');
	std::string const requested = (split == std::string::npos) ? std::string() : translated.substr(0, split + 1);
	std::string const leaf = (split == std::string::npos) ? translated : translated.substr(split + 1);
	std::string directory = requested.empty() ? requested : Host_Path(requested.c_str());

	std::vector<MatchType> matches;

	if (leaf.find_first_of("*?") == std::string::npos) {

		// A search with no wildcard names one entry and is answered with that entry alone.
		std::string const resolved = Host_Path((directory + leaf).c_str());
		struct stat info;

		if (::stat(resolved.c_str(), &info) == 0) {
			std::size_t const mark = resolved.find_last_of('/');
			MatchType match;

			directory = (mark == std::string::npos) ? std::string() : resolved.substr(0, mark + 1);
			match.Name = (mark == std::string::npos) ? resolved : resolved.substr(mark + 1);
			matches.push_back(std::move(match));
		}

	} else {

		DIR * const scan = ::opendir(directory.empty() ? "." : directory.c_str());

		if (scan != nullptr) {
			for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
				if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;

				MatchType match;
				match.Name = item->d_name;
				matches.push_back(std::move(match));
			}
			::closedir(scan);
		}
	}

	Persistent_Matches(requested, leaf, matches);

	// The image is searched under the caller's spelling, since the two filesystems answer
	// for case separately.
	Image_Matches(requested, leaf, matches);

	std::sort(matches.begin(), matches.end(), [](MatchType const & left, MatchType const & right) {
		return(Platform_Name_Order(left.Name, right.Name));
	});

	found.reserve(matches.size());

	for (MatchType const & match : matches) {
		PlatformFileInfoType entry;
		struct stat info;

		if (match.Image.Is_Valid()) {
			Info_From_Image(match.Name, match.Image, entry);
		} else if (::stat(Host_Path((directory + match.Name).c_str()).c_str(), &info) == 0) {
			Info_From_Stat(match.Name, info, entry);
		} else {
			entry.Name = match.Name;
		}

		found.push_back(std::move(entry));
	}

	return(found);
}


std::string Platform_Host_Path(char const * path)
{
	return(Host_Path(path));
}


bool Platform_Hint_File(char const * filename, BlockHintType kind, std::uint32_t offset, std::uint32_t length)
{
	if (filename == nullptr || *filename == '\0') return(false);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume || offset >= found.Size) return(false);

	volume->Hint(found, kind, offset, Image_Span(found, offset, length));
	return(true);
}


bool Platform_Prefetch_File(char const * filename, std::uint32_t offset, std::uint32_t length)
{
	if (filename == nullptr || *filename == '\0') return(false);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume || offset >= found.Size) return(false);

	return(volume->Prefetch(found, offset, Image_Span(found, offset, length)));
}


std::uint64_t Platform_Stored_Bytes(char const * filename)
{
	if (filename == nullptr || *filename == '\0') return(0);

	BlockEntryClass found;
	std::shared_ptr<BlockFileClass> volume = Image_Entry(filename, found);

	if (!volume) return(0);

	std::uint64_t const held = volume->Stored_Bytes();
	return((held > found.Size) ? found.Size : held);
}


bool Local_Calendar_Time(FileTimeType time, CalendarTimeType & calendar)
{
	std::int64_t seconds = 0;
	std::int64_t nanoseconds = 0;
	Unix_From_File_Time(time, seconds, nanoseconds);

	time_t const when = (time_t)seconds;
	struct tm parts;
	if (::localtime_r(&when, &parts) == nullptr) {
		return(false);
	}

	calendar.Year = parts.tm_year + 1900;
	calendar.Month = parts.tm_mon + 1;
	calendar.DayOfWeek = parts.tm_wday;
	calendar.Day = parts.tm_mday;
	calendar.Hour = parts.tm_hour;
	calendar.Minute = parts.tm_min;
	calendar.Second = parts.tm_sec;
	calendar.Milliseconds = (int)(nanoseconds / 1000000);
	return(true);
}

#endif	// !_WIN32
