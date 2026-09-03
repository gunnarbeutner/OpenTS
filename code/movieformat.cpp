/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "movieformat.h"

#include <cstring>


namespace {

bool Equal_Ascii_No_Case(char const * left, char const * right)
{
	while (*left != '\0' && *right != '\0') {
		char l = *left++;
		char r = *right++;
		if (l >= 'a' && l <= 'z') l -= 'a' - 'A';
		if (r >= 'a' && r <= 'z') r -= 'a' - 'A';
		if (l != r) return(false);
	}
	return(*left == *right);
}

}


char const * Movie_Extension(void)
{
#if defined(OPENTS_MP4_MOVIES)
	return(".MP4");
#else
	return(".VQA");
#endif
}


bool Movie_Resolve_Name(char const * name, char * resolved, std::size_t resolved_size)
{
	if (name == nullptr || resolved == nullptr || resolved_size == 0) {
		return(false);
	}

	std::size_t length = std::strlen(name);
	if (length >= 4) {
		char const * suffix = name + length - 4;
		if (Equal_Ascii_No_Case(suffix, ".VQA") || Equal_Ascii_No_Case(suffix, ".MP4")) {
			length -= 4;
		}
	}

	char const * extension = Movie_Extension();
	std::size_t const extension_length = std::strlen(extension);
	if (length + extension_length + 1 > resolved_size) {
		resolved[0] = '\0';
		return(false);
	}

	std::memcpy(resolved, name, length);
	std::memcpy(resolved + length, extension, extension_length + 1);
	return(true);
}
