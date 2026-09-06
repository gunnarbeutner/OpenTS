/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The basic types, handles, and rectangle records of <windef.h> and <minwindef.h>.

#pragma once

#include "winnt.h"

// Clang accepts the x86 calling convention keywords on every target and ignores
// the ones the target lacks, so nothing here needs to erase them.
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST			const
#define WINGDIAPI
typedef void *			LPVOID;
typedef void const *	LPCVOID;
typedef BYTE *			PBYTE;
typedef BYTE *			LPBYTE;
typedef WORD *			PWORD;
typedef DWORD *			PDWORD;
typedef INT *			PINT;
typedef UINT *			PUINT;
typedef BOOL *			PBOOL;
typedef float *			PFLOAT;
typedef WORD *			LPWORD;
typedef DWORD *			LPDWORD;
typedef LONG *			LPLONG;
typedef INT *			LPINT;
typedef BOOL *			LPBOOL;

typedef UINT_PTR		WPARAM;
typedef LONG_PTR		LPARAM;
typedef LONG_PTR		LRESULT;
typedef DWORD			COLORREF;
typedef DWORD *			LPCOLORREF;
typedef WORD			ATOM;
#define DECLARE_HANDLE(name)	struct name##__ { int unused; }; typedef struct name##__ * name

DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HGLOBAL__);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HRSRC);

typedef void *		HGDIOBJ;
typedef HICON		HCURSOR;
typedef HINSTANCE	HMODULE;
typedef HANDLE		HGLOBAL;
typedef HANDLE		HLOCAL;
typedef HKEY *		PHKEY;

#define HFILE			int
#define HFILE_ERROR		((HFILE)-1)
typedef INT_PTR (WINAPI * FARPROC)();
typedef INT_PTR (WINAPI * PROC)();
// Structures, each matching its Win32 x86 original field for field.
typedef struct tagPOINT {
	LONG x;
	LONG y;
} POINT, * PPOINT, * LPPOINT;

typedef struct tagSIZE {
	LONG cx;
	LONG cy;
} SIZE, * PSIZE, * LPSIZE;

typedef struct tagRECT {
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
} RECT, * PRECT, * LPRECT;
typedef RECT const * LPCRECT;
typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, * PFILETIME, * LPFILETIME;
// Constants.
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

#define MAX_PATH				260
#define MAKELONG(a, b)	((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | (((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16)))
#define MAKEWORD(a, b)	((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | (((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8)))
#define LOWORD(l)		((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l)		((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define LOBYTE(w)		((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w)		((BYTE)(((DWORD_PTR)(w) >> 8) & 0xff))
typedef struct tagPOINTS {
	SHORT x;
	SHORT y;
} POINTS, * LPPOINTS;
#define MAKEPOINTS(l)	(*((POINTS *)&(l)))
