/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The error codes and HRESULT helpers of <winerror.h>.

#pragma once

#define NO_ERROR					0L
#define ERROR_SUCCESS				0L
#define ERROR_FILE_NOT_FOUND		2L
#define ERROR_PATH_NOT_FOUND		3L
#define ERROR_TOO_MANY_OPEN_FILES	4L
#define ERROR_ACCESS_DENIED			5L
#define ERROR_INVALID_HANDLE		6L
#define ERROR_NOT_ENOUGH_MEMORY		8L
#define ERROR_READ_FAULT			30L
#define ERROR_GEN_FAILURE			31L
#define ERROR_SEEK					25L
#define ERROR_NEGATIVE_SEEK			131L
#define ERROR_FILE_EXISTS			80L
#define ERROR_DISK_FULL				112L
#define ERROR_DIRECTORY				267L
#define ERROR_NOT_SUPPORTED			50L
#define ERROR_INVALID_PARAMETER		87L
#define ERROR_CALL_NOT_IMPLEMENTED	120L
#define ERROR_INSUFFICIENT_BUFFER	122L
#define ERROR_ALREADY_EXISTS		183L
#define ERROR_NOT_OWNER				288L
#define ERROR_NO_MORE_FILES			18L
#define ERROR_HANDLE_EOF			38L
#define ERROR_IO_PENDING			997L

#define S_OK			((HRESULT)0L)
#define S_FALSE			((HRESULT)1L)
#define NOERROR			((HRESULT)0L)
#define E_UNEXPECTED	((HRESULT)0x8000FFFFL)
#define E_NOTIMPL		((HRESULT)0x80004001L)
#define E_OUTOFMEMORY	((HRESULT)0x8007000EL)
#define E_INVALIDARG	((HRESULT)0x80070057L)
#define E_NOINTERFACE	((HRESULT)0x80004002L)
#define E_POINTER		((HRESULT)0x80004003L)
#define E_HANDLE		((HRESULT)0x80070006L)
#define E_ABORT			((HRESULT)0x80004004L)
#define E_FAIL			((HRESULT)0x80004005L)
#define E_ACCESSDENIED	((HRESULT)0x80070005L)
#define E_PENDING		((HRESULT)0x8000000AL)

#define SUCCEEDED(hr)	(((HRESULT)(hr)) >= 0)
#define FAILED(hr)		(((HRESULT)(hr)) < 0)
#define HRESULT_CODE(hr)	((hr) & 0xFFFF)
#define MAKE_HRESULT(sev, fac, code) \
	((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define ERROR_BUFFER_OVERFLOW	111L
