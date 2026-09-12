/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The file layer over the Win32 API. It mirrors file_posix.cpp; where the two differ it is
// because the host does.

#include "always.h"

#if defined(_WIN32)

#include "platform/file.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>


struct PlatformFileClass::StateType
{
	HANDLE Handle = INVALID_HANDLE_VALUE;
};


namespace {

FileTimeType File_Time_From_Windows(FILETIME const & time)
{
	return(FileTimeType::From_Parts(time.dwLowDateTime, time.dwHighDateTime));
}


FILETIME Windows_From_File_Time(FileTimeType time)
{
	FILETIME result;

	result.dwLowDateTime = time.Low();
	result.dwHighDateTime = time.High();
	return(result);
}


bool Is_Hidden(DWORD attributes)
{
	return((attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0);
}


std::string Leaf_Name(char const * path)
{
	std::string const text(path);
	std::size_t const mark = text.find_last_of("\\/:");

	return((mark == std::string::npos) ? text : text.substr(mark + 1));
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
		return(false);
	}

	DWORD access = GENERIC_READ;
	DWORD share = 0;
	DWORD creation = OPEN_EXISTING;
	DWORD flags = FILE_ATTRIBUTE_NORMAL;

	switch (mode) {
		case PlatformOpenType::READ:
			share = FILE_SHARE_READ | FILE_SHARE_WRITE;
			flags |= FILE_FLAG_SEQUENTIAL_SCAN;
			break;

		case PlatformOpenType::WRITE:
			access = GENERIC_WRITE;
			creation = CREATE_ALWAYS;
			break;

		case PlatformOpenType::UPDATE:
			access = GENERIC_READ | GENERIC_WRITE;
			creation = OPEN_ALWAYS;
			break;

		case PlatformOpenType::EXCLUSIVE:
			access = GENERIC_WRITE;
			share = FILE_SHARE_READ;
			creation = CREATE_NEW;
			break;

		default:
			return(false);
	}

	HANDLE const handle = CreateFileA(path, access, share, nullptr, creation, flags, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return(false);
	}

	State = std::make_unique<StateType>();
	State->Handle = handle;
	return(true);
}


bool PlatformFileClass::Close(void)
{
	if (State == nullptr) {
		return(false);
	}

	bool const closed = (CloseHandle(State->Handle) != FALSE);
	State.reset();
	return(closed);
}


bool PlatformFileClass::Read(void * buffer, std::uint32_t length, std::uint32_t & got)
{
	got = 0;

	if (State == nullptr) {
		return(false);
	}

	DWORD read = 0;
	BOOL const ok = ReadFile(State->Handle, buffer, (DWORD)length, &read, nullptr);

	got = (std::uint32_t)read;
	return(ok != FALSE);
}


bool PlatformFileClass::Write(void const * buffer, std::uint32_t length, std::uint32_t & put)
{
	put = 0;

	if (State == nullptr) {
		return(false);
	}

	DWORD written = 0;
	BOOL const ok = WriteFile(State->Handle, buffer, (DWORD)length, &written, nullptr);

	put = (std::uint32_t)written;
	return(ok != FALSE);
}


std::int64_t PlatformFileClass::Seek(std::int64_t offset, int origin)
{
	if (State == nullptr) {
		return(-1);
	}

	DWORD method = FILE_BEGIN;

	switch (origin) {
		case SEEK_SET:	method = FILE_BEGIN; break;
		case SEEK_CUR:	method = FILE_CURRENT; break;
		case SEEK_END:	method = FILE_END; break;
		default:		return(-1);
	}

	LARGE_INTEGER distance;
	LARGE_INTEGER position;

	distance.QuadPart = offset;
	if (!SetFilePointerEx(State->Handle, distance, &position, method)) {
		return(-1);
	}
	return(position.QuadPart);
}


std::int64_t PlatformFileClass::Size(void) const
{
	if (State == nullptr) {
		return(-1);
	}

	LARGE_INTEGER size;
	if (!GetFileSizeEx(State->Handle, &size)) {
		return(-1);
	}
	return(size.QuadPart);
}


bool PlatformFileClass::Flush(void)
{
	return(State != nullptr && FlushFileBuffers(State->Handle) != FALSE);
}


bool PlatformFileClass::Modified_Time(FileTimeType & time) const
{
	FILETIME written;

	if (State == nullptr || !GetFileTime(State->Handle, nullptr, nullptr, &written)) {
		return(false);
	}

	time = File_Time_From_Windows(written);
	return(true);
}


// The access time moves with the write time, as the DOS-era callers set both.
bool PlatformFileClass::Set_Modified_Time(FileTimeType time)
{
	if (State == nullptr) {
		return(false);
	}

	FILETIME const stamp = Windows_From_File_Time(time);
	return(SetFileTime(State->Handle, nullptr, &stamp, &stamp) != FALSE);
}


bool Platform_File_Info(char const * path, PlatformFileInfoType & info)
{
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (path == nullptr || !GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
		return(false);
	}

	info.Name = Leaf_Name(path);
	info.Size = ((std::uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
	info.Modified = File_Time_From_Windows(data.ftLastWriteTime);
	info.IsDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	info.IsHidden = Is_Hidden(data.dwFileAttributes);
	info.IsReadOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
	return(true);
}


bool Platform_Remove_File(char const * path)
{
	return(path != nullptr && DeleteFileA(path) != FALSE);
}


bool Platform_Replace_File(char const * source, char const * target)
{
	return(source != nullptr && target != nullptr
		&& MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING) != FALSE);
}


bool Platform_Copy_File(char const * source, char const * target)
{
	return(source != nullptr && target != nullptr && CopyFileA(source, target, FALSE) != FALSE);
}


bool Platform_Create_Directory(char const * path)
{
	return(path != nullptr && CreateDirectoryA(path, nullptr) != FALSE);
}


// The host already matches as DOS did, short names included, so only the order is imposed.
std::vector<PlatformFileInfoType> Platform_Find_Files(char const * pattern)
{
	std::vector<PlatformFileInfoType> found;

	if (pattern == nullptr) {
		return(found);
	}

	WIN32_FIND_DATAA data;
	HANDLE const search = FindFirstFileA(pattern, &data);

	if (search == INVALID_HANDLE_VALUE) {
		return(found);
	}

	do {
		PlatformFileInfoType entry;

		entry.Name = data.cFileName;
		entry.Size = ((std::uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
		entry.Modified = File_Time_From_Windows(data.ftLastWriteTime);
		entry.IsDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		entry.IsHidden = Is_Hidden(data.dwFileAttributes);
		entry.IsReadOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
		found.push_back(std::move(entry));
	} while (FindNextFileA(search, &data));

	FindClose(search);

	std::sort(found.begin(), found.end(), [](PlatformFileInfoType const & left, PlatformFileInfoType const & right) {
		return(Platform_Name_Order(left.Name, right.Name));
	});

	return(found);
}


std::string Platform_Host_Path(char const * path)
{
	return((path != nullptr) ? std::string(path) : std::string());
}


bool Local_Calendar_Time(FileTimeType time, CalendarTimeType & calendar)
{
	std::int64_t seconds = 0;
	std::int64_t nanoseconds = 0;
	Unix_From_File_Time(time, seconds, nanoseconds);

	std::time_t const when = (std::time_t)seconds;
	std::tm parts;
	if (localtime_s(&parts, &when) != 0) {
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

#endif	// _WIN32
