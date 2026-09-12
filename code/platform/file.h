/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Files and directories as the engine names them. A path may use either separator, and off
// Windows a path that does not exist as spelled is matched without regard to case, so the
// upper-case names the engine asks for reach files a player installed in any case.

#pragma once

#include "platform/filetime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>


struct PlatformFileInfoType
{
	std::string Name;			// The entry's own name, without its directory.
	std::uint64_t Size = 0;
	FileTimeType Modified;
	bool IsDirectory = false;

	// Hidden, system or temporary on Windows, and a dot file elsewhere. The engine's scans
	// pass these over.
	bool IsHidden = false;
	bool IsReadOnly = false;
};


enum class PlatformOpenType
{
	READ,		// An existing file, for reading.
	WRITE,		// Created, or emptied if it exists, for writing.
	UPDATE,		// Created, or kept as it is if it exists, for reading and writing.
	EXCLUSIVE,	// Created for writing; fails if the name is taken.
};


// An open file, closed when the object is destroyed.
class PlatformFileClass
{
	public:
		PlatformFileClass(void);
		~PlatformFileClass(void);

		PlatformFileClass(PlatformFileClass && other) noexcept;
		PlatformFileClass & operator = (PlatformFileClass && other) noexcept;

		PlatformFileClass(PlatformFileClass const &) = delete;
		PlatformFileClass & operator = (PlatformFileClass const &) = delete;

		// Closes whatever was open first.
		bool Open(char const * path, PlatformOpenType mode);

		// False when nothing was open or the host reported a failure.
		bool Close(void);

		bool Is_Open(void) const {return(State != nullptr);}

		// Fills the whole request unless the file ends first, which is success with a short
		// count.
		bool Read(void * buffer, std::uint32_t length, std::uint32_t & got);
		bool Write(void const * buffer, std::uint32_t length, std::uint32_t & put);

		// The origin is SEEK_SET, SEEK_CUR or SEEK_END. Returns the new position, or -1.
		std::int64_t Seek(std::int64_t offset, int origin);

		// -1 when the size cannot be had.
		std::int64_t Size(void) const;

		// Returns once what was written is on the storage device.
		bool Flush(void);

		bool Modified_Time(FileTimeType & time) const;
		bool Set_Modified_Time(FileTimeType time);

	private:
		struct StateType;
		std::unique_ptr<StateType> State;
};


// False for a name the host has no entry for.
bool Platform_File_Info(char const * path, PlatformFileInfoType & info);

bool Platform_Remove_File(char const * path);

// Moves source over target in one step, so a reader of target sees the old file or the new
// one and never a mixture.
bool Platform_Replace_File(char const * source, char const * target);

// Overwrites a target that exists.
bool Platform_Copy_File(char const * source, char const * target);

// False when the directory could not be created, including when the name is taken.
bool Platform_Create_Directory(char const * path);

// The entries a wildcard pattern matches, sorted by Platform_Name_Order. "*" and "?" match
// in the last component only, "*.*" means every name, and a pattern without a wildcard
// names one entry, a directory included.
std::vector<PlatformFileInfoType> Platform_Find_Files(char const * pattern);

// Names compared without regard to ASCII case, and in byte order where that is all they
// differ in. The order decides which of several ECACHE*.MIX archives overrides which, so it
// must not depend on the host.
bool Platform_Name_Order(std::string const & left, std::string const & right);

// What the host calls the file the engine names: the path itself on Windows, and the
// resolved spelling elsewhere.
std::string Platform_Host_Path(char const * path);
