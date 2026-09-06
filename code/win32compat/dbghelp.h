/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The symbol lookup of <dbghelp.h>; no symbols are read here.

#pragma once

#include "windef.h"

// The debug-symbol lookup, from dbghelp.h.
typedef struct _SYMBOL_INFO {
	ULONG SizeOfStruct;
	ULONG TypeIndex;
	ULONGLONG Reserved[2];
	ULONG Index;
	ULONG Size;
	ULONGLONG ModBase;
	ULONG Flags;
	ULONGLONG Value;
	ULONGLONG Address;
	ULONG Register;
	ULONG Scope;
	ULONG Tag;
	ULONG NameLen;
	ULONG MaxNameLen;
	CHAR Name[1];
} SYMBOL_INFO, * PSYMBOL_INFO;

typedef struct _IMAGEHLP_LINE64 {
	DWORD SizeOfStruct;
	PVOID Key;
	DWORD LineNumber;
	PCHAR FileName;
	DWORD64 Address;
} IMAGEHLP_LINE64, * PIMAGEHLP_LINE64;


BOOL SymInitialize(HANDLE process, LPCSTR searchpath, BOOL invade);
BOOL SymCleanup(HANDLE process);
DWORD SymSetOptions(DWORD options);
BOOL SymFromAddr(HANDLE process, DWORD64 address, DWORD64 * displacement, PSYMBOL_INFO symbol);
BOOL SymGetLineFromAddr64(HANDLE process, DWORD64 address, PDWORD displacement, PIMAGEHLP_LINE64 line);
