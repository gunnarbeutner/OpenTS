/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if !defined(_WIN32)

#include "platform/registry.h"


bool Platform_Read_Machine_Registry(char const *, char const *, void *, unsigned int)
{
	return(false);
}

#endif	// !_WIN32
