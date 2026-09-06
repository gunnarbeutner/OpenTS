/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The process snapshot of <tlhelp32.h>.

#pragma once

#include "windef.h"

// The process snapshot, from tlhelp32.h.
#define TH32CS_SNAPHEAPLIST	0x00000001
#define TH32CS_SNAPPROCESS	0x00000002
#define TH32CS_SNAPTHREAD	0x00000004
#define TH32CS_SNAPMODULE	0x00000008
#define TH32CS_SNAPALL		(TH32CS_SNAPHEAPLIST | TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD | TH32CS_SNAPMODULE)

typedef struct tagMODULEENTRY32 {
	DWORD dwSize;
	DWORD th32ModuleID;
	DWORD th32ProcessID;
	DWORD GlblcntUsage;
	DWORD ProccntUsage;
	BYTE * modBaseAddr;
	DWORD modBaseSize;
	HMODULE hModule;
	char szModule[256];
	char szExePath[260];
} MODULEENTRY32, * LPMODULEENTRY32;

HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD processid);
BOOL Module32First(HANDLE snapshot, LPMODULEENTRY32 entry);
BOOL Module32Next(HANDLE snapshot, LPMODULEENTRY32 entry);
