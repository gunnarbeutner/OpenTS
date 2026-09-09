/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// A class describes its members to a save stream through these, and the leaf headers that
// carry a Serialize of their own take this rather than savestream.h, which takes them in
// turn.


/*
 * Carries one member under its own name. The name is what the file records, so renaming
 * the member renames the field, and a save written before the rename no longer answers
 * for it.
 */
#define SERIALIZE(stream, member) (stream).Serialize(#member, member)


/*
 * A bit field has no address to hand out, so its value makes the trip in an ordinary
 * variable. Assigning it back is harmless while saving.
 */
#define SERIALIZE_BIT(stream, field) \
	do { \
		bool serialize_bit = ((field) != 0); \
		(stream).Serialize(#field, serialize_bit); \
		(field) = serialize_bit; \
	} while (false)
