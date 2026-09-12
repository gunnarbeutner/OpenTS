/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "point.h"


void Game_Window_On_Paint(bool update_surface);
void Game_Window_On_Right_Mouse_Up(void);
void Game_Window_On_Mouse_Wheel(int delta);

// The game window's mouse, from whichever host delivers it. Buttons are VK_LBUTTON,
// VK_RBUTTON or VK_MBUTTON, and positions are in the frame.
void Game_Window_Mouse_Button(unsigned short button, Point2D const & position, bool release);
void Game_Window_Mouse_Double_Click(unsigned short button, Point2D const & position);
void Game_Window_Pointer_Capture_Lost(void);

void Game_Window_Created(void);
void Game_Window_Destroyed(void);

// The window lost or regained the input focus: sound pauses and the mouse is let go, then
// both come back.
void Focus_Loss(void);
void Focus_Restore(void);
