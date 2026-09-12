/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


// Asks whether to abandon the mission, with the answers Abort_Dialog returns: IDOK to quit,
// IDABORT to restart or surrender, IDCANCEL to carry on. A session that ends under the
// screen answers IDOK, as the dialog did.
//
// False means the screen could not be prepared.
bool UI_Abort_Screen(int & result);
