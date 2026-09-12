/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The shell's context, for the views and the runner beside it. It is not in uishell.h
// because that header is what the engine includes and names no toolkit type.

#pragma once

namespace Rml { class Context; class ElementDocument; }


// The one context every document lives in, or null before the shell starts and after it
// shuts down.
Rml::Context * UI_Context(void);

// Opens a dialog with the wipe the dialogs it replaced made, and plays their sound. Ends by
// itself; UI_End_Reveal stops it early for a document that is going away.
void UI_Begin_Reveal(Rml::ElementDocument * document);
void UI_End_Reveal(Rml::ElementDocument * document);
