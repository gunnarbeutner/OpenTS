/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(_WIN32)

#include "platform/registry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


bool Platform_Read_Machine_Registry(char const * key, char const * value, void * buffer, unsigned int size)
{
	HKEY handle;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) {
		return(false);
	}

	DWORD type;
	DWORD length = size;
	LONG const result = RegQueryValueExA(handle, value, nullptr, &type, (BYTE *)buffer, &length);
	RegCloseKey(handle);
	return(result == ERROR_SUCCESS);
}

#endif	// _WIN32
