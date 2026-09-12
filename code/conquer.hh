/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// What one pass of the outer game loop did; only GAME_FRAME_FINISHED ends the
// loop.
enum GameFrameType {
	GAME_FRAME_ADVANCED,		// A frame was played.
	GAME_FRAME_SUSPENDED,		// The game is parked, so no frame was played.
	GAME_FRAME_FINISHED			// The scenario is over.
};
