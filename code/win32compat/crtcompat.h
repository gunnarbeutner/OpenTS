/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The MSVC C runtime spellings the tree is written against: what MSVC declares
// in <string.h>, <stdlib.h>, <ctype.h>, <fcntl.h> and <sys/stat.h> beyond the
// standard. always.h includes it on every toolchain and it is inert under MSVC.
// What MSVC keeps in headers of its own (<io.h>, <conio.h>, <malloc.h>,
// <sys/timeb.h>, <sal.h>, <new.h>) lives beside this file under those names.

#pragma once

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// The open flags and descriptor helpers MSVC keeps in <io.h> and <fcntl.h>.
#include "io.h"


// wasm32 is ILP32 like Win32 x86, so these keep the widths the inherited code
// assumes.
#define __int8		char
#define __int16		short
#define __int32		int
#define __int64		long long

#define __forceinline	inline __attribute__((always_inline))

// The build erases the double-underscore spellings on the command line.
#define _cdecl
#define _stdcall
#define _fastcall

#ifndef S_IREAD
#define S_IREAD		S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE	S_IWUSR
#endif

// A header that would declare its own time_t tests this first.
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#endif

#ifndef _MAX_PATH
#define _MAX_PATH	260
#define _MAX_DRIVE	3
#define _MAX_DIR	256
#define _MAX_FNAME	256
#define _MAX_EXT	256
#endif

// MSVC's <ctype.h> character-class masks; the INI parser compares against them.
#ifndef _CONTROL
#define _UPPER		0x0001
#define _LOWER		0x0002
#define _DIGIT		0x0004
#define _SPACE		0x0008
#define _PUNCT		0x0010
#define _CONTROL	0x0020
#define _BLANK		0x0040
#define _HEX		0x0080
#endif

inline int stricmp(char const * left, char const * right)
{
	return(strcasecmp(left, right));
}


inline int strnicmp(char const * left, char const * right, size_t count)
{
	return(strncasecmp(left, right, count));
}


#ifndef _stricmp
#define _stricmp stricmp
#endif
#ifndef _strnicmp
#define _strnicmp strnicmp
#endif


inline int memicmp(void const * left, void const * right, size_t count)
{
	unsigned char const * l = (unsigned char const *)left;
	unsigned char const * r = (unsigned char const *)right;

	for (size_t index = 0; index < count; index++) {
		int diff = tolower(l[index]) - tolower(r[index]);
		if (diff != 0) return(diff);
	}
	return(0);
}


// Only a letter that changes is written, as the MSVC runtime does; the engine passes
// string literals that are already in the case asked for, and a host that maps them
// read-only faults on the write.
inline char * strupr(char * string)
{
	for (char * ptr = string; *ptr != '\0'; ptr++) {
		if (islower((unsigned char)*ptr)) {
			*ptr = (char)toupper((unsigned char)*ptr);
		}
	}
	return(string);
}


inline char * strlwr(char * string)
{
	for (char * ptr = string; *ptr != '\0'; ptr++) {
		if (isupper((unsigned char)*ptr)) {
			*ptr = (char)tolower((unsigned char)*ptr);
		}
	}
	return(string);
}


inline char * strrev(char * string)
{
	char * front = string;
	char * back = string + strlen(string);

	while (back > front) {
		back--;
		char swap = *front;
		*front = *back;
		*back = swap;
		front++;
	}
	return(string);
}


char * itoa(int value, char * buffer, int radix);
char * ltoa(long value, char * buffer, int radix);
char * ultoa(unsigned long value, char * buffer, int radix);

void _splitpath(char const * path, char * drive, char * dir, char * fname, char * ext);
void _makepath(char * path, char const * drive, char const * dir, char const * fname, char const * ext);

inline int freopen_s(FILE ** stream, char const * filename, char const * mode, FILE * old)
{
	FILE * result = freopen(filename, mode, old);

	if (stream != nullptr) *stream = result;
	return(result != nullptr ? 0 : errno);
}


// MSVC's optimizer hint, as a builtin.
#define __assume(condition)	__builtin_assume(condition)

#endif	// OPENTS_WIN32_SUBSTITUTE
