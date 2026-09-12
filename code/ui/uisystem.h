/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

namespace Rml { class Context; class SystemInterface; }


// Time, logging, and cursor requests for RmlUi. It stays alive for the process, so RmlUi
// may hold it past shutdown of the shell.
Rml::SystemInterface * UI_System_Interface(void);

// Updates the context. Its data views pass the text they bind, which is game and player data
// rather than document text, back through the string-name translation, so the translation is
// off while it runs: a chat line reading [[TXT_OK]] stays as typed.
void UI_Update_Context(Rml::Context * context);
