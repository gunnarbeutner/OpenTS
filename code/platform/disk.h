/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

// The bytes free to this process on the volume holding a directory; null or empty names the
// current one.
bool Platform_Free_Space(char const * directory, std::uint64_t & bytes);
