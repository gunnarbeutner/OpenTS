/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The network lobby's screen stack, answering the queries of the dialog family it replaced.

#pragma once

#include "ui/uiwsstack.h"


bool WS_Destroy_Dialog(WSScreenHandle window, int id);

WSScreenHandle WS_Find_Dialog(int id);

int WS_Wait_Dialog(WSScreenHandle window, bool (*callback)(void));

// Null, and zero, when no screen is up.
WSScreenHandle WS_Top_Window(void);
int WS_Top_Window_ID(void);

WSScreenHandle WS_Next_Lower_Dialog(WSScreenHandle window);
