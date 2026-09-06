/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

enum {
	// ECMA-119's logical sector size, which extents are addressed in.
	BLOCK_SECTOR_SIZE = 2048
};


// ECMA-119 descriptor types and record flags, kept for the test fixtures that
// build them.
enum BlockDescriptorType {
	BLOCK_DESCRIPTOR_BOOT = 0,
	BLOCK_DESCRIPTOR_PRIMARY = 1,
	BLOCK_DESCRIPTOR_SUPPLEMENTARY = 2,
	BLOCK_DESCRIPTOR_PARTITION = 3,
	BLOCK_DESCRIPTOR_TERMINATOR = 255
};


enum BlockRecordFlag {
	BLOCK_RECORD_HIDDEN = 0x01,
	BLOCK_RECORD_DIRECTORY = 0x02,
	BLOCK_RECORD_ASSOCIATED = 0x04,
	BLOCK_RECORD_EXTENDED_FORMAT = 0x08,
	BLOCK_RECORD_EXTENDED_PERMISSIONS = 0x10,
	BLOCK_RECORD_MULTI_EXTENT = 0x80
};


// What a hint says about a run of an image, in the order of how much it
// claims. A hint is advisory and free.
enum BlockHintType {
	BLOCK_HINT_SEQUENTIAL,	// Being read front to back, ending where it says.
	BLOCK_HINT_SOON,			// Worth fetching only while nothing else is.
	BLOCK_HINT_DONE			// Finished with, whatever was said before.
};
