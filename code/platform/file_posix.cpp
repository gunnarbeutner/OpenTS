/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The file layer over POSIX, for the targets that have no Win32 API. It mirrors
// file_win32.cpp; where the two differ it is because the host does.

#include "always.h"

#if !defined(_WIN32)

#include "platform/file.h"

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


std::string Forward_Slashes(char const * path)
{
	std::string translated((path != nullptr) ? path : "");

	for (char & character : translated) {
		if (character == '\\') character = '/';
	}
	return(translated);
}


std::string Host_Path(char const * path)
{
	return(Resolve_Case(Forward_Slashes(path)));
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

	int const descriptor = ::open(host.c_str(), flags, (mode_t)0666);
	if (descriptor < 0) {
		return(false);
	}

	State = std::make_unique<StateType>();
	State->Descriptor = descriptor;
	return(true);
}


bool PlatformFileClass::Close(void)
{
	if (State == nullptr) {
		return(false);
	}

	bool const closed = (::close(State->Descriptor) == 0);

	State.reset();
	return(closed);
}


bool PlatformFileClass::Read(void * buffer, std::uint32_t length, std::uint32_t & got)
{
	got = 0;

	if (State == nullptr || (buffer == nullptr && length != 0)) {
		return(false);
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

	if (State == nullptr || (buffer == nullptr && length != 0)) {
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

	off_t const position = ::lseek(State->Descriptor, (off_t)offset, origin);
	return((position < 0) ? -1 : (std::int64_t)position);
}


std::int64_t PlatformFileClass::Size(void) const
{
	if (State == nullptr) {
		return(-1);
	}

	struct stat info;
	if (::fstat(State->Descriptor, &info) != 0) {
		return(-1);
	}
	return((std::int64_t)info.st_size);
}


bool PlatformFileClass::Flush(void)
{
	return(State != nullptr && ::fsync(State->Descriptor) == 0);
}


bool PlatformFileClass::Modified_Time(FileTimeType & time) const
{
	if (State == nullptr) {
		return(false);
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
	if (State == nullptr) {
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


bool Platform_File_Info(char const * path, PlatformFileInfoType & info)
{
	if (path == nullptr) {
		return(false);
	}

	std::string const host = Host_Path(path);
	struct stat status;

	if (::stat(host.c_str(), &status) != 0) {
		return(false);
	}

	Info_From_Stat(Leaf_Of(host), status, info);
	return(true);
}


bool Platform_Remove_File(char const * path)
{
	if (path == nullptr) {
		return(false);
	}

	return(::unlink(Host_Path(path).c_str()) == 0);
}


// rename replaces its target in one step.
bool Platform_Replace_File(char const * source, char const * target)
{
	if (source == nullptr || target == nullptr) {
		return(false);
	}

	std::string const from = Host_Path(source);
	std::string const to = Host_Path(target);

	return(::rename(from.c_str(), to.c_str()) == 0);
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

	std::vector<std::string> names;

	if (leaf.find_first_of("*?") == std::string::npos) {

		// A search with no wildcard names one entry and is answered with that entry alone.
		std::string const resolved = Host_Path((directory + leaf).c_str());
		struct stat info;

		if (::stat(resolved.c_str(), &info) == 0) {
			std::size_t const mark = resolved.find_last_of('/');

			directory = (mark == std::string::npos) ? std::string() : resolved.substr(0, mark + 1);
			names.push_back((mark == std::string::npos) ? resolved : resolved.substr(mark + 1));
		}

	} else {

		DIR * const scan = ::opendir(directory.empty() ? "." : directory.c_str());

		if (scan != nullptr) {
			for (struct dirent * item = ::readdir(scan); item != nullptr; item = ::readdir(scan)) {
				if (!Match_Wildcard(leaf.c_str(), item->d_name)) continue;
				names.push_back(item->d_name);
			}
			::closedir(scan);
		}
	}

	std::sort(names.begin(), names.end(), Platform_Name_Order);

	found.reserve(names.size());

	for (std::string const & name : names) {
		PlatformFileInfoType entry;
		struct stat info;

		if (::stat(Host_Path((directory + name).c_str()).c_str(), &info) == 0) {
			Info_From_Stat(name, info, entry);
		} else {
			entry.Name = name;
		}

		found.push_back(std::move(entry));
	}

	return(found);
}


std::string Platform_Host_Path(char const * path)
{
	return(Host_Path(path));
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
