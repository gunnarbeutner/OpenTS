/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// rcimage.py compiles language.rc into the resource directory image declared
// here, so a target with no module loader reads the same strings and dialog
// templates the Visual Studio build loads out of Language.dll.

#pragma once


extern unsigned char const LanguageResourceImage[];
extern unsigned int const LanguageResourceImageSize;
