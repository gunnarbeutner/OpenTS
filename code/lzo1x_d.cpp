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
 *                     $Archive:: /Commando/Library/LZO1X_D.CPP                               $*
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 7/22/97 11:37a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* $Header: /Commando/Library/LZO1X_D.CPP 1     7/22/97 12:00p Greg_h $ */
/* lzo1x_d.c -- standalone LZO1X decompressor

   This file is part of the LZO real-time data compression library.

   Copyright (C) 1996 Markus Franz Xaver Johannes Oberhumer

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the LZO library; see the file COPYING.LIB.
   If not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA.

   Markus F.X.J. Oberhumer
   markus.oberhumer@jk.uni-linz.ac.at
 */


#include "always.h"

#include "lzo1x.h"
#include <assert.h>

#if !defined(LZO1X) && !defined(LZO1Y)
#  define LZO1X
#endif

#if 1
#  define TEST_IP				1
#else
#  define TEST_IP				(ip < ip_end)
#endif


/***********************************************************************
// decompress a block of data.
************************************************************************/

/// <summary>
/// Expands a block of LZO1X compressed data.
/// This is the low level decompression routine that the LZO straw, the LZO pipe, and the
/// compressed stream reader all funnel through. The compressed block carries its own end
/// marker, so the length of the expanded data is discovered as the block is unpacked rather
/// than being told to this routine.
/// </summary>
/// <param name="in">Pointer to the compressed data to expand.</param>
/// <param name="in_len">Length of the compressed data, in bytes.</param>
/// <param name="out">Buffer that the expanded data is written into.</param>
/// <param name="out_len">Set to the number of bytes that were expanded into the buffer.</param>
/// <returns>Returns with LZO_E_OK if the block expanded cleanly, otherwise one of the
/// LZO_E_ error codes.</returns>
/// <remarks>Be sure that the destination buffer is big enough to hold the expanded data,
/// since this routine performs no bounds checking upon it.</remarks>
int lzo1x_decompress     ( const lzo_byte * in, lzo_uint  in_len,
                                 lzo_byte * out, lzo_uint * out_len,
                                 lzo_voidp )
{
	lzo_byte *op;
	const lzo_byte *ip;
	lzo_uint t;
	const lzo_byte *m_pos;
	const lzo_byte * const ip_end = in + in_len;

	*out_len = 0;

	op = out;
	ip = in;

	if (*ip > 17) {
		t = *ip++ - 17;
		goto first_literal_run;
	}

	for (;;) {
//	while (TEST_IP) {
		t = *ip++;
		if (t >= 16)
			goto match;
		/* a literal run */
		if (t == 0) {
			t = 15;
			while (*ip == 0) {
				t += 255, ip++;
			}
			t += *ip++;
		}
		/* copy literals */
		*op++ = *ip++; *op++ = *ip++; *op++ = *ip++;
first_literal_run:
		do *op++ = *ip++; while (--t > 0);


		t = *ip++;

		if (t >= 16) {
			goto match;
		}
#if defined(LZO1X)
		m_pos = op - 1 - 0x800;
#elif defined(LZO1Y)
		m_pos = op - 1 - 0x400;
#endif
		m_pos -= t >> 2;
		m_pos -= *ip++ << 2;
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos;
//		*op++ = *m_pos++;
		goto match_done;


		/* handle matches */
		for (;;) {
//		while (TEST_IP) {
			if (t < 16) {						/* a M1 match */
				m_pos = op - 1;
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;
				*op++ = *m_pos++;
				*op++ = *m_pos;
//				*op++ = *m_pos++;
			} else {
match:
				if (t >= 64) {				/* a M2 match */
					m_pos = op - 1;
#if defined(LZO1X)
					m_pos -= (t >> 2) & 7;
					m_pos -= *ip++ << 3;
					t = (t >> 5) - 1;
#elif defined(LZO1Y)
					m_pos -= (t >> 2) & 3;
					m_pos -= *ip++ << 2;
					t = (t >> 4) - 3;
#endif
				} else {
					if (t >= 32) {			/* a M3 match */
						t &= 31;
						if (t == 0) {
							t = 31;
							while (*ip == 0) {
								t += 255, ip++;
							}
							t += *ip++;
						}
						m_pos = op - 1;
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
					} else {						/* a M4 match */
						m_pos = op;
						m_pos -= (t & 8) << 11;
						t &= 7;
						if (t == 0) {
							t = 7;
							while (*ip == 0) {
								t += 255, ip++;
							}
							t += *ip++;
						}
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
						if (m_pos == op) {
							goto eof_found;
						}
						m_pos -= 0x4000;
					}
				}
				*op++ = *m_pos++; *op++ = *m_pos++;
				do *op++ = *m_pos++; while (--t > 0);
			}

match_done:
			t = ip[-2] & 3;
			if (t == 0)
				break;
			/* copy literals */
			do *op++ = *ip++; while (--t > 0);
			t = *ip++;
		}
	}

	/* ip == ip_end and no EOF code was found */

	//Unreachable - ST 9/5/96 5:07PM
	//*out_len = op - out;
	//return (ip == ip_end ? LZO_E_EOF_NOT_FOUND : LZO_E_ERROR);

eof_found:
	assert(t == 1);
	*out_len = op - out;
	return (ip == ip_end ? LZO_E_OK : LZO_E_ERROR);
}


/***********************************************************************
// decompress a block of data that cannot be trusted.
************************************************************************/

// Each read of the input and each write of the output is checked first, and a match may
// only reach back into what this call has already written. A length is compared against
// the room left rather than added to anything, so no count can wrap.
#define NEED_IP(x)		if ((lzo_uint)(ip_end - ip) < (lzo_uint)(x)) goto input_overrun
#define NEED_OP(x)		if ((lzo_uint)(op_end - op) < (lzo_uint)(x)) goto output_overrun
#define NEED_OP_MORE(n, x)	if ((lzo_uint)(op_end - op) < (lzo_uint)(n) || (lzo_uint)(op_end - op) - (lzo_uint)(n) < (lzo_uint)(x)) goto output_overrun
#define NEED_IP_MORE(n, x)	if ((lzo_uint)(ip_end - ip) < (lzo_uint)(n) || (lzo_uint)(ip_end - ip) - (lzo_uint)(n) < (lzo_uint)(x)) goto input_overrun
#define TEST_LB(m_pos)	if ((m_pos) < out || (m_pos) >= op) goto lookbehind_overrun

/// <summary>
/// Expands a block of LZO1X compressed data that may have been damaged or forged.
/// The block is refused, rather than read or written past either buffer, when it runs
/// out of input, asks for more output than the buffer holds, or refers back before the
/// start of the output.
/// </summary>
/// <param name="in">Pointer to the compressed data to expand.</param>
/// <param name="in_len">Length of the compressed data, in bytes.</param>
/// <param name="out">Buffer that the expanded data is written into.</param>
/// <param name="out_len">On entry the size of the buffer; on return the number of bytes
/// written into it, which a refused block leaves partly filled.</param>
/// <returns>Returns with LZO_E_OK when the block expanded and ended exactly at the end of
/// the input, otherwise the LZO_E_ code naming what was wrong with it.</returns>
int lzo1x_decompress_x   ( const lzo_byte * in, lzo_uint  in_len,
                                 lzo_byte * out, lzo_uint * out_len,
                                 lzo_voidp )
{
	lzo_byte *op = out;
	const lzo_byte *ip = in;
	lzo_uint t;
	const lzo_byte *m_pos;
	const lzo_byte * const ip_end = in + in_len;
	lzo_byte * const op_end = out + *out_len;

	*out_len = 0;

	NEED_IP(1);
	if (*ip > 17) {
		t = *ip++ - 17;
		if (t < 4) {
			goto match_next;
		}
		NEED_OP(t); NEED_IP_MORE(1, t);
		do *op++ = *ip++; while (--t > 0);
		goto first_literal_run;
	}

	for (;;) {
		if (ip >= ip_end) {
			goto eof_not_found;
		}
		t = *ip++;
		if (t >= 16) {
			goto match;
		}
		/* a literal run */
		if (t == 0) {
			NEED_IP(1);
			while (*ip == 0) {
				t += 255, ip++;
				NEED_IP(1);
				NEED_OP(t);
			}
			t += 15 + *ip++;
		}
		/* copy literals */
		NEED_OP_MORE(3, t); NEED_IP_MORE(4, t);
		*op++ = *ip++; *op++ = *ip++; *op++ = *ip++;
		do *op++ = *ip++; while (--t > 0);

first_literal_run:
		NEED_IP(1);
		t = *ip++;
		if (t >= 16) {
			goto match;
		}
#if defined(LZO1X)
		m_pos = op - 1 - 0x800;
#elif defined(LZO1Y)
		m_pos = op - 1 - 0x400;
#endif
		m_pos -= t >> 2;
		NEED_IP(1);
		m_pos -= *ip++ << 2;
		TEST_LB(m_pos); NEED_OP(3);
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos;
		goto match_done;


		/* handle matches */
		for (;;) {
			if (t < 16) {						/* a M1 match */
				m_pos = op - 1;
				m_pos -= t >> 2;
				NEED_IP(1);
				m_pos -= *ip++ << 2;
				TEST_LB(m_pos); NEED_OP(2);
				*op++ = *m_pos++;
				*op++ = *m_pos;
			} else {
match:
				if (t >= 64) {				/* a M2 match */
					m_pos = op - 1;
#if defined(LZO1X)
					m_pos -= (t >> 2) & 7;
					NEED_IP(1);
					m_pos -= *ip++ << 3;
					t = (t >> 5) - 1;
#elif defined(LZO1Y)
					m_pos -= (t >> 2) & 3;
					NEED_IP(1);
					m_pos -= *ip++ << 2;
					t = (t >> 4) - 3;
#endif
				} else {
					if (t >= 32) {			/* a M3 match */
						t &= 31;
						if (t == 0) {
							t = 31;
							NEED_IP(1);
							while (*ip == 0) {
								t += 255, ip++;
								NEED_IP(1);
								NEED_OP(t);
							}
							t += *ip++;
						}
						NEED_IP(2);
						m_pos = op - 1;
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
					} else {						/* a M4 match */
						m_pos = op;
						m_pos -= (t & 8) << 11;
						t &= 7;
						if (t == 0) {
							t = 7;
							NEED_IP(1);
							while (*ip == 0) {
								t += 255, ip++;
								NEED_IP(1);
								NEED_OP(t);
							}
							t += *ip++;
						}
						NEED_IP(2);
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
						if (m_pos == op) {
							goto eof_found;
						}
						m_pos -= 0x4000;
					}
				}
				TEST_LB(m_pos); NEED_OP_MORE(2, t);
				*op++ = *m_pos++; *op++ = *m_pos++;
				do *op++ = *m_pos++; while (--t > 0);
			}

match_done:
			t = ip[-2] & 3;
			if (t == 0)
				break;
match_next:
			/* copy literals */
			NEED_OP(t); NEED_IP_MORE(1, t);
			do *op++ = *ip++; while (--t > 0);
			t = *ip++;
		}
	}

eof_not_found:
	*out_len = op - out;
	return (LZO_E_EOF_NOT_FOUND);

eof_found:
	*out_len = op - out;
	return (ip == ip_end ? LZO_E_OK : LZO_E_ERROR);

input_overrun:
	*out_len = op - out;
	return (LZO_E_INPUT_OVERRUN);

output_overrun:
	*out_len = op - out;
	return (LZO_E_OUTPUT_OVERRUN);

lookbehind_overrun:
	*out_len = op - out;
	return (LZO_E_LOOKBEHIND_OVERRUN);
}

#undef NEED_IP
#undef NEED_OP
#undef NEED_OP_MORE
#undef NEED_IP_MORE
#undef TEST_LB


/*
vi:ts=4
*/
