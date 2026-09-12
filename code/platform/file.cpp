/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "platform/file.h"

namespace {

// ASCII only, so the order does not move with the C locale.
unsigned char Fold(char character)
{
	unsigned char const value = (unsigned char)character;

	return((value >= 'A' && value <= 'Z') ? (unsigned char)(value + ('a' - 'A')) : value);
}

}	// namespace


bool Platform_Name_Order(std::string const & left, std::string const & right)
{
	std::size_t const common = (left.size() < right.size()) ? left.size() : right.size();

	for (std::size_t index = 0; index < common; index++) {
		unsigned char const a = Fold(left[index]);
		unsigned char const b = Fold(right[index]);

		if (a != b) {
			return(a < b);
		}
	}

	if (left.size() != right.size()) {
		return(left.size() < right.size());
	}

	return(left < right);
}
