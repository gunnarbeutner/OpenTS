/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The progress box a long job puts up. Like the wait box it is modeless: the job opens it,
// reports progress while it works without pumping messages, and closes it, so every report
// redraws it before returning.

#pragma once


// False means the box could not be prepared or one is already open.
bool UI_Progress_Open(void);

// Sets how far the job has got, from 0 to 1, and redraws the box.
void UI_Progress_Set(double fraction);

void UI_Progress_Close(void);

bool UI_Progress_Is_Open(void);
