/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /G/wwlib/lcw.h                                              $*
 *                                                                                             *
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             *
 *                     $Modtime:: 10/04/99 10:25a                                             $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include <cstdint>

uint32_t LCW_Uncomp(void const * source, void * dest, unsigned long length=0);

int LCW_Comp(void const * source, void * dest, int length);

/// <summary>
/// Returns a destination size that holds LCW_Comp's output for any source of datasize bytes.
/// </summary>
constexpr int LCW_Comp_Bound(int datasize)
{
	// The worst case alternates a medium form three byte copy with one literal, five bytes out
	// for four in, plus a command byte per 63 literals and the terminator.
	return(datasize + datasize / 4 + datasize / 128 + 4);
}


/// <summary>
/// Decompresses an LCW encoded block without reading or writing outside the
/// buffers given and returns the number of bytes written. Decoding stops at
/// the end of data code or at anything a well formed stream cannot contain,
/// so a damaged stream returns a short count.
/// </summary>
int LCW_Uncomp_Bounded(void const * source, int srclen, void * dest, int destlen);
