/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

namespace Rml { class FileInterface; }


// The interface RmlUi reads every document resource through. It stays alive for the
// process, so RmlUi may hold it past shutdown of the shell.
Rml::FileInterface * UI_File_Interface(void);
