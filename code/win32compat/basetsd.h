/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The pointer-sized integers, at the widths <basetsd.h> gives them on the host.

#pragma once

// The pointer-sized integers follow the host's pointer, as Win64's do.
#if defined(__LP64__) || defined(_LP64)
typedef long			INT_PTR;
typedef unsigned long	UINT_PTR;
#else
typedef int				INT_PTR;
typedef unsigned int	UINT_PTR;
#endif
typedef long			LONG_PTR;
typedef unsigned long	ULONG_PTR;
typedef ULONG_PTR		DWORD_PTR;
typedef ULONG_PTR		SIZE_T;
typedef LONG_PTR		SSIZE_T;
typedef unsigned int		DWORD32;
typedef unsigned long long	DWORD64;
