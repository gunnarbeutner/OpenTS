/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Whether the game window exists. Gameplay reads it as "is anything drawn", so it must turn
// true and false at the same moments on every target.
bool Has_Main_Window(void);
