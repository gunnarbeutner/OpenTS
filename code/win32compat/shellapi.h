/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The shell calls of <shellapi.h>.

#pragma once

#include "windef.h"

/* ShellExecute, from shellapi.h. */
HINSTANCE ShellExecuteA(HWND window, LPCSTR operation, LPCSTR file, LPCSTR parameters, LPCSTR directory, int showcommand);
#define ShellExecute	ShellExecuteA
LPWSTR * CommandLineToArgvW(LPCWSTR commandline, int * count);
