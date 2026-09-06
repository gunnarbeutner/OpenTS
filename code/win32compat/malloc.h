/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the engine takes from MSVC's <malloc.h>: alloca and the block size query.

#pragma once

#include <stdlib.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <alloca.h>
// The host's <malloc.h> is shadowed by this file, so the one call taken from
// it is declared here.
extern "C" size_t malloc_usable_size(void * block);
#endif


inline size_t _msize(void * block)
{
#if defined(__APPLE__)
	return(malloc_size(block));
#else
	return(malloc_usable_size(block));
#endif
}
