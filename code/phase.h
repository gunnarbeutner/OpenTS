/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The stack of loops the engine is waiting in, with dialogs layered on top.

#pragma once


// Enters a phase for the life of the object. The detail is a name for what the
// phase is showing, such as the menu page or the film, and may be null.
class PhaseScope
{
	public:
		PhaseScope(char const * name, char const * detail = nullptr);
		~PhaseScope(void);

		PhaseScope(PhaseScope const &) = delete;
		PhaseScope & operator = (PhaseScope const &) = delete;
};

// Registers a source of layers that stack above the phases: the count is read
// whenever the phase is described. A source registered twice is kept once.
void Phase_Register_Layers(char const * name, int (*count)(void));

// Recomputes the description and reports it if it changed. The layer sources
// are polled only here, so whoever changes a count calls this.
void Phase_Changed(void);

// The innermost phase, or "none" between phases.
char const * Phase_Top(void);

// Every phase from the outermost in, joined with "/", such as "game/dialog".
char const * Phase_Describe(void);

// The detail of the innermost explicit phase, or an empty string.
char const * Phase_Detail(void);

// Rises on every change to the description.
unsigned int Phase_Serial(void);

// Hands a named marker to the page, with an optional detail. Nothing on a
// build without a page.
void Phase_Event(char const * name, char const * detail = nullptr);
