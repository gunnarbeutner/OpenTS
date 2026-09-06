/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The version resource calls of <winver.h>.

#pragma once

#include "windef.h"

DWORD GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle);
BOOL GetFileVersionInfoA(LPCSTR filename, DWORD handle, DWORD length, LPVOID data);
BOOL VerQueryValueA(LPCVOID block, LPCSTR subblock, LPVOID * buffer, PUINT length);
#define GetFileVersionInfoSize	GetFileVersionInfoSizeA
#define GetFileVersionInfo		GetFileVersionInfoA
#define VerQueryValue			VerQueryValueA
