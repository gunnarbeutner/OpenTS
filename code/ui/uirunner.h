/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "uiscreen.h"

class UIRmlViewClass;


// Runs a modal screen until it reports a result, pumping messages and servicing the game
// the way the dialog driver did. The view is prepared and shown by the runner, and
// closed before this returns, so a caller that gives up on the result still leaks nothing.
//
// A UI_RESULT_SESSION_ENDED result carries what the dialog driver reported by returning
// true, and UI_RESULT_FAILED means the screen could not be prepared.
UIResult UI_Run_Modal(UIPresenterClass & presenter, UIRmlViewClass & view);


// One pass of what a screen's loop does for the game underneath it: the messages pumped, and
// the session stepped or merely serviced. True means the session ended. For a caller that
// waits with a modeless box up, as the dialog driver's loop served one.
bool UI_Service_Game(void);
