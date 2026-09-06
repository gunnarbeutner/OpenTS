/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The registry keys and calls of <winreg.h>; nothing is stored.

#pragma once

#include "winbase.h"

#define HKEY_CLASSES_ROOT		((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER		((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE		((HKEY)(ULONG_PTR)0x80000002)
#define HKEY_USERS				((HKEY)(ULONG_PTR)0x80000003)
/* Registry. */
LONG RegOpenKeyExA(HKEY key, LPCSTR subkey, DWORD options, DWORD desired, PHKEY result);
LONG RegCreateKeyExA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR classname, DWORD options, DWORD desired, LPSECURITY_ATTRIBUTES attributes, PHKEY result, LPDWORD disposition);
LONG RegQueryValueExA(HKEY key, LPCSTR valuename, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LONG RegSetValueExA(HKEY key, LPCSTR valuename, DWORD reserved, DWORD type, BYTE const * data, DWORD size);
LONG RegDeleteValueA(HKEY key, LPCSTR valuename);
LONG RegCloseKey(HKEY key);
#define RegOpenKeyEx		RegOpenKeyExA
#define RegCreateKeyEx		RegCreateKeyExA
#define RegQueryValueEx		RegQueryValueExA
#define RegSetValueEx		RegSetValueExA
#define RegDeleteValue		RegDeleteValueA
