/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Advice for a file that a mounted image serves. Where no image answers for a name the calls
// do nothing, which is always the case on Windows. PlatformFileClass::Hint does the same for
// an open file.

#pragma once

#include "blocksource.hh"

#include <cstdint>

// Advises that a run of a named file is about to be used; a length of 0 means the rest of
// the file. True when an image answers for the name.
bool Platform_Hint_File(char const * filename, BlockHintType kind, std::uint32_t offset, std::uint32_t length);

// Fetches a run of a named file into the image's store so a later read is answered locally.
// It waits, and is legal only where the engine may suspend.
bool Platform_Prefetch_File(char const * filename, std::uint32_t offset, std::uint32_t length);

// How many of a named file's bytes a persistent store already holds.
std::uint64_t Platform_Stored_Bytes(char const * filename);
