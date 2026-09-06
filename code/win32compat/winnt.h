/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The fundamental types, access rights, and machine records of <winnt.h>, each at its Win32 x86 width and layout.

#pragma once

#include "crtcompat.h"
#include "basetsd.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DECLSPEC_IMPORT
#define EXTERN_C		extern "C"
#define TEXT(quote)				quote
#define IN
#define OUT
#define OPTIONAL
#define DECLSPEC_UUID(uuid)
#define DECLSPEC_NOVTABLE
#define DECLSPEC_SELECTANY

// Scalar types, at their Win32 widths. A Windows long is four bytes on every
// target, so where the host's long is eight the four-byte int stands in.
#if defined(__LP64__) || defined(_LP64)
typedef unsigned int	DWORD;
typedef int				LONG;
typedef unsigned int	ULONG;
#else
typedef unsigned long	DWORD;
typedef long			LONG;
typedef unsigned long	ULONG;
#endif
typedef unsigned short	WORD;
typedef unsigned char	BYTE;
typedef int				BOOL;
typedef int				INT;
typedef unsigned int	UINT;
typedef float			FLOAT;
typedef short			SHORT;
typedef unsigned short	USHORT;
typedef char			CHAR;
typedef unsigned char	UCHAR;
typedef void			VOID;
typedef unsigned char	BOOLEAN;
typedef wchar_t			WCHAR;
typedef long long			LONGLONG;
typedef unsigned long long	ULONGLONG;
typedef unsigned long long	DWORDLONG;
typedef void *			PVOID;
typedef char *			LPSTR;
typedef char const *	LPCSTR;
typedef char *			PSTR;
typedef char const *	PCSTR;
typedef WCHAR *			LPWSTR;
typedef WCHAR const *	LPCWSTR;
typedef LPSTR			LPTSTR;
typedef LPCSTR			LPCTSTR;
typedef CHAR			TCHAR;
typedef LONG *			PLONG;
typedef ULONG *			PULONG;
typedef CHAR *			PCHAR;
typedef LONG			HRESULT;
typedef LONG			SCODE;
typedef WORD			LANGID;
typedef DWORD			LCID;
typedef void * HANDLE;
typedef HANDLE * PHANDLE;
typedef HANDLE * LPHANDLE;
// MSVC aligns __int64 to eight bytes on x86 and clang aligns long long the same
// way on wasm32, so the union keeps its Win32 offsets and size.
typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG HighPart;
	};
	struct {
		DWORD LowPart;
		LONG HighPart;
	} u;
	LONGLONG QuadPart;
} LARGE_INTEGER, * PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
	struct {
		DWORD LowPart;
		DWORD HighPart;
	};
	struct {
		DWORD LowPart;
		DWORD HighPart;
	} u;
	ULONGLONG QuadPart;
} ULARGE_INTEGER, * PULARGE_INTEGER;
// RTL_CRITICAL_SECTION keeps the size MSVC gives it because the engine embeds
// it in objects whose layout is fixed.
typedef struct _RTL_CRITICAL_SECTION {
	void * DebugInfo;
	LONG LockCount;
	LONG RecursionCount;
	HANDLE OwningThread;
	HANDLE LockSemaphore;
	ULONG_PTR SpinCount;
} CRITICAL_SECTION, RTL_CRITICAL_SECTION, * LPCRITICAL_SECTION, * PRTL_CRITICAL_SECTION;
#define GENERIC_READ			0x80000000L
#define GENERIC_WRITE			0x40000000L
#define GENERIC_EXECUTE			0x20000000L
#define GENERIC_ALL				0x10000000L

#define FILE_SHARE_READ			0x00000001
#define FILE_SHARE_WRITE		0x00000002
#define FILE_SHARE_DELETE		0x00000004
#define FILE_ATTRIBUTE_READONLY		0x00000001
#define FILE_ATTRIBUTE_HIDDEN		0x00000002
#define FILE_ATTRIBUTE_SYSTEM		0x00000004
#define FILE_ATTRIBUTE_DIRECTORY	0x00000010
#define FILE_ATTRIBUTE_ARCHIVE		0x00000020
#define FILE_ATTRIBUTE_NORMAL		0x00000080
#define FILE_ATTRIBUTE_TEMPORARY	0x00000100
#define FILE_ATTRIBUTE_OFFLINE		0x00001000
#define KEY_QUERY_VALUE		0x0001
#define KEY_SET_VALUE		0x0002
#define KEY_READ			0x20019
#define KEY_WRITE			0x20006
#define KEY_ALL_ACCESS		0xF003F

#define REG_NONE		0
#define REG_SZ			1
#define REG_EXPAND_SZ	2
#define REG_BINARY		3
#define REG_DWORD		4
typedef struct _RTL_SRWLOCK {
	PVOID Ptr;
} SRWLOCK, * PSRWLOCK;

#define SRWLOCK_INIT	{nullptr}
#define FILE_ANY_ACCESS		0
#define FILE_READ_ACCESS	0x0001
#define FILE_WRITE_ACCESS	0x0002
#define FILE_READ_DATA		0x0001
#define FILE_WRITE_DATA		0x0002
typedef struct _RTL_OSVERSIONINFOW {
	ULONG dwOSVersionInfoSize;
	ULONG dwMajorVersion;
	ULONG dwMinorVersion;
	ULONG dwBuildNumber;
	ULONG dwPlatformId;
	WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW, * PRTL_OSVERSIONINFOW;
#define LANG_NEUTRAL		0x00
#define SUBLANG_DEFAULT		0x01
#define MAKELANGID(primary, sub)	((WORD)((((WORD)(sub)) << 10) | (WORD)(primary)))
#define EXCEPTION_MAXIMUM_PARAMETERS			15
#define STATUS_NO_MEMORY						((DWORD)0xC0000017L)

typedef struct _EXCEPTION_RECORD {
	DWORD ExceptionCode;
	DWORD ExceptionFlags;
	struct _EXCEPTION_RECORD * ExceptionRecord;
	PVOID ExceptionAddress;
	DWORD NumberParameters;
	ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

// The x86 thread context keeps its Win32 x86 shape because the crash report
// reads named registers out of it; nothing here fills one in.
typedef struct _FLOATING_SAVE_AREA {
	DWORD ControlWord;
	DWORD StatusWord;
	DWORD TagWord;
	DWORD ErrorOffset;
	DWORD ErrorSelector;
	DWORD DataOffset;
	DWORD DataSelector;
	BYTE RegisterArea[80];
	DWORD Cr0NpxState;
} FLOATING_SAVE_AREA;

typedef struct _CONTEXT {
	DWORD ContextFlags;
	DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
	FLOATING_SAVE_AREA FloatSave;
	DWORD SegGs, SegFs, SegEs, SegDs;
	DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;
	DWORD Ebp, Eip, SegCs, EFlags, Esp, SegSs;
	BYTE ExtendedRegisters[512];
} CONTEXT, * PCONTEXT, * LPCONTEXT;

typedef struct _EXCEPTION_POINTERS {
	PEXCEPTION_RECORD ExceptionRecord;
	PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, * PEXCEPTION_POINTERS, * LPEXCEPTION_POINTERS;
#define MUTEX_ALL_ACCESS	0x1F0001
