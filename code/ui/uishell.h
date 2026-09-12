/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The engine's whole view of the UI shell. No RmlUi, bgfx, or window-system type appears
// here, so a caller needs none of those headers or their build settings. The shell owns
// the RmlUi context, the overlay pass, and the input scope; uishell.cpp is where those
// live.

#pragma once

#include "uievent.hh"


bool UI_Init(void);

// Installs the lettering built from the game's own glyph sheets. Needs the game's mix files
// registered, so startup calls it once they are; a document opened before then draws its
// text with nothing.
void UI_Load_Game_Fonts(void);
void UI_Shutdown(void);
bool UI_Is_Initialized(void);

// Follows Video_Set_Mode and Video_On_Resize. The context, its mapping, and its clipping
// change together, and every open document keeps its state and redraws.
void UI_On_Frame_Size_Changed(void);

// Advances layout and animation for documents that are not driven by a modal runner.
// Called from Main_Loop beside Map.Input.
void UI_Tick(void);

// Submits the overlay into the frame that Backend_Present opened. video.cpp calls this
// between Backend_Present and Backend_End_Frame and is the only caller.
void UI_Render_Overlay(void);

// Whether the overlay has to be redrawn even though the game's frame has not changed.
// A visible document keeps this set, so presents are paced by Video_Present_If_Dirty.
bool UI_Overlay_Is_Dirty(void);
void UI_Mark_Overlay_Dirty(void);

// Input. Each returns true when a document consumed the event, which keeps it out of the
// keyboard queue and away from gameplay. A move is always delivered and never consumed,
// so the game keeps tracking the cursor.
//
// Positions are physical pixels in the window's client area; the shell subtracts the
// frame's destination origin itself.
void UI_Handle_Mouse_Move(int clientx, int clienty, unsigned int modifiers);
bool UI_Handle_Mouse_Button(UIMouseButtonType button, bool down, int clientx, int clienty, unsigned int modifiers);
bool UI_Handle_Mouse_Wheel(float delta, unsigned int modifiers);
bool UI_Handle_Key(UIKeyType key, bool down, unsigned int modifiers);
bool UI_Handle_Text(char const * utf8);

// Drops capture, drags, and composition. Held keys are not replayed as presses when focus
// returns.
void UI_On_Focus_Lost(void);

// Whether a modal document is showing. Gameplay input is not eligible while one is.
bool UI_Modal_Is_Active(void);

// Brackets a modal screen. Each clears the keyboard queue, as every legacy dialog driver
// does around a dialog, so a key pressed before a screen opened is not read by it and one
// pressed under it does not reach the game afterwards. Nested screens are counted, so a
// message box over an options screen does not end the outer screen's scope.
void UI_Begin_Modal(void);
void UI_End_Modal(void);
