/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if !defined(_WIN32)

#include "platform/process.h"

#include <climits>
#include <cstdlib>
#include <vector>

#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif


std::string Executable_Path(void)
{
#if defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);

	std::vector<char> buffer(size + 1);
	if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
		return(std::string());
	}

	// The loader reports the path it was started by, which may be relative or a link.
	char resolved[PATH_MAX];
	if (realpath(buffer.data(), resolved) == nullptr) {
		return(std::string(buffer.data()));
	}
	return(std::string(resolved));
#elif defined(__linux__)
	std::vector<char> buffer(PATH_MAX);
	for (;;) {
		ssize_t const length = readlink("/proc/self/exe", buffer.data(), buffer.size());
		if (length < 0) {
			return(std::string());
		}
		if (size_t(length) < buffer.size()) {
			return(std::string(buffer.data(), size_t(length)));
		}
		buffer.resize(buffer.size() * 2);
	}
#else
	return(std::string());
#endif
}


bool Executable_Image_Range(std::uintptr_t & base, std::size_t & size)
{
	base = 0;
	size = 0;
	return(false);
}


std::uint32_t Process_Id(void)
{
	return(std::uint32_t(getpid()));
}


bool Acquire_Single_Instance(void)
{
	return(true);
}


void Release_Single_Instance(void)
{
}


void Raise_Timer_Resolution(void)
{
}


void Restore_Timer_Resolution(void)
{
}

#endif	// !_WIN32
