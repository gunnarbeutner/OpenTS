/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Returns after at least the given time; zero gives the processor up without waiting.
// On the page the wait is the engine's yield to the browser, so any request, zero
// included, costs at least one animation frame.
void Platform_Sleep(unsigned int milliseconds);
