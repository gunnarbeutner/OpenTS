/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The font engine RmlUi draws every face through. The family `opents-dialog` is served from
// the dialogs' own glyph sheets; every other family goes to RmlUi's FreeType engine.

#pragma once

namespace Rml { class FontEngineInterface; }


// Installed with Rml::SetFontEngineInterface before Rml::Initialise.
Rml::FontEngineInterface * UI_Font_Engine(void);

// Needs the game's mix files registered. False leaves the family to whatever face RmlUi
// loads under its name.
bool UI_Font_Load_Dialog_Face(void);

// The context's density-independent pixel ratio, so a dialog face asked for in dp is drawn at
// the frame's own scale rather than at the whole pixel size RmlUi truncates it to.
void UI_Font_Set_Pixel_Ratio(float ratio);
