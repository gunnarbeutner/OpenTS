/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// The byte Windows writes for code in code page 437 or Windows-1252 when it converts with no
// flags, best-fit substitutes included, or -1 where it would write the default character.
// Control and delete positions are returned like any other byte.
int Code_Page_437_Byte(char32_t code);
int Code_Page_1252_Byte(char32_t code);
