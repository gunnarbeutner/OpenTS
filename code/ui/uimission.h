/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

class LoadOptionsClass;


// What the load, save and delete screens need from the dialog object that opens them. The
// object's protected state is copied in by its own driver, the only code that can reach it.
struct UIMissionFilesRequest
{
	LoadOptionsClass * Options = nullptr;

	// A LoadOptionsClass::LoadStyleType.
	int Style = 0;

	// The caller's description buffer, which primes the save field and receives what was
	// saved; nullptr for the load and delete styles.
	char * Description = nullptr;

	char const * Extension = "SAV";
	std::size_t ScanLimit = SIZE_MAX;

	bool (*Saved_Game_Exists)(char const * name) = nullptr;
	std::function<int(void)> Save_Confirmation;
};


// The screen could not be prepared.
constexpr int UI_MISSION_FILES_UNAVAILABLE = -2;

// Runs the screen for the request's style and answers with the state the dialog it replaced
// closed in: IDOK when a game was loaded or saved or the last save deleted, IDCANCEL otherwise.
int UI_Mission_Files_Screen(UIMissionFilesRequest const & request);
