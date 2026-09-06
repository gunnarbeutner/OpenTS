/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Drains the SDL event queue into the game and the UI shell, and paints the window if a
// repaint was asked for. Windows_Message_Handler calls it wherever the engine waits, which
// is the only place an event may be delivered from.
void Host_Pump_Events(void);
