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
 *                     $Archive:: /Commando/Code/wwlib/mpu.cpp                                $*
 *                                                                                             *
 *                      $Author:: Denzil_l                                                    $*
 *                                                                                             *
 *                     $Modtime:: 8/23/01 5:07p                                               $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Get_CPU_Rate -- Fetch the rate of CPU ticks per second.                                   *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "mpu.h"

#include <chrono>
#include <cstdint>

#if defined(_MSC_VER)

#include <intrin.h>

#else

// RDTSC is an x86 opcode reached through an MSVC intrinsic. The steady clock answers the same
// question elsewhere, at the rate Get_CPU_Rate reports, which is all the callers here compare.
static unsigned long long __rdtsc(void)
{
	return((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
}

#endif


/***********************************************************************************************
 * Get_CPU_Rate -- Fetch the rate of CPU ticks per second.                                     *
 *                                                                                             *
 *    This routine reports how many ticks per second the high resolution clock counts.         *
 *                                                                                             *
 * INPUT:   high  -- Reference to the location that will be filled with the upper 32 bits      *
 *                   of the result.                                                            *
 *                                                                                             *
 * OUTPUT:  Returns with the lower 32 bits of the result.                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/20/1997 JLB : Created.                                                                 *
 *=============================================================================================*/
unsigned int Get_CPU_Rate(unsigned int & high)
{
	using Period = std::chrono::steady_clock::period;
	std::uint64_t const rate = std::uint64_t(Period::den / Period::num);

	high = (unsigned int)(rate >> 32);
	return((unsigned int)rate);
}


/// <summary>
/// Fetches the processor's time stamp counter, which increments every clock tick. The value
/// is 64 bits wide; the low half is returned and the high half stored through the reference.
/// RDTSC is available on every processor the supported minimum hardware covers (SSE2, so a
/// Pentium 4 or Athlon 64 onward).
/// </summary>
/// <param name="high">Receives the high half of the 64 bit clock value.</param>
/// <returns>unsigned int; the low half of the clock value.</returns>
unsigned int Get_CPU_Clock(unsigned int & high)
{
	unsigned long long const stamp = __rdtsc();

	high = (unsigned int)(stamp >> 32);
	return((unsigned int)stamp);
}
