/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The parts of the socket interface that need no socket library at all.

#include "always.h"

#include "netsocket.h"

#include <cstdlib>


/// <summary>
/// Reads a dotted quad such as "192.168.0.1" into an address in network order.
/// </summary>
/// <returns>False for anything that is not four decimal components in range, leaving the
/// address untouched. inet_addr also accepted shorthand forms such as "10.1" and hexadecimal
/// components; no address the game is configured with uses them.</returns>
bool Socket_Parse_Address(char const * text, uint32_t & address)
{
	if (text == nullptr) {
		return(false);
	}

	unsigned char quad[4];
	char const * scan = text;

	for (int index = 0; index < 4; index++) {
		if (*scan < '0' || *scan > '9') {
			return(false);
		}

		unsigned value = 0;
		while (*scan >= '0' && *scan <= '9') {
			value = value * 10 + unsigned(*scan - '0');
			if (value > 255) {
				return(false);
			}
			scan++;
		}
		quad[index] = (unsigned char)value;

		if (index < 3) {
			if (*scan != '.') {
				return(false);
			}
			scan++;
		}
	}

	if (*scan != '\0') {
		return(false);
	}

	std::memcpy(&address, quad, sizeof(address));
	return(true);
}
