/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// What a screen closed with. The numbers are those of the Windows dialogs the screens
// replaced, which callers still compare against and pass along as they are.
enum DialogResultType {
	DIALOG_OK = 1,
	DIALOG_CANCEL = 2,
	DIALOG_ABORT = 3,
	DIALOG_RETRY = 4,
	DIALOG_IGNORE = 5,
	DIALOG_YES = 6,
	DIALOG_NO = 7,
};
