/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The game's own text lives in languagestrings.cpp, so every target reads the
// same strings without asking a module loader for them.

#pragma once


/// <summary>
/// Returns the string the script gives the identifier, in UTF-8, or null where it defines
/// none. The text is static, so the pointer stays good for the life of the process.
/// </summary>
char const * Language_String(unsigned int id);
