/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "ui/uiunicode.h"
#include "utf8.h"

#include <climits>
#include <new>
#if defined(_WIN32)
#include <windows.h>
#endif


bool UI_UTF8_To_UTF16(std::string_view text, std::wstring & wide)
{
	wide.clear();

	if (text.size() > UI_CLIPBOARD_MAX_BYTES || text.size() > INT_MAX) {
		return(false);
	}
	if (text.empty()) {
		return(true);
	}

#if defined(_WIN32)

	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
	if (length == 0) {
		return(false);
	}

	try {
		wide.resize((std::size_t)length);
	} catch (std::bad_alloc const &) {
		return(false);
	}

	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), wide.data(), length) != length) {
		wide.clear();
		return(false);
	}
	return(true);
#else
	if (!UTF8::Is_Valid(text)) {
		return(false);
	}
	try {
		std::string terminated(text);
		char const * cursor = terminated.c_str();
		while (cursor < terminated.c_str() + terminated.size()) {
			wide.push_back((wchar_t)UTF8::Decode(cursor));
		}
	} catch (std::bad_alloc const &) {
		wide.clear();
		return(false);
	}
	return(true);
#endif
}


bool UI_UTF16_To_UTF8(std::wstring_view wide, std::string & text)
{
	text.clear();

	if (wide.size() > UI_CLIPBOARD_MAX_BYTES / sizeof(wchar_t) || wide.size() > INT_MAX) {
		return(false);
	}
	if (wide.empty()) {
		return(true);
	}

#if defined(_WIN32)

	int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	if (length == 0) {
		return(false);
	}

	try {
		text.resize((std::size_t)length);
	} catch (std::bad_alloc const &) {
		return(false);
	}

	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), text.data(), length, nullptr, nullptr) != length) {
		text.clear();
		return(false);
	}
	return(true);
#else
	try {
		for (wchar_t value : wide) {
			char32_t code = (char32_t)value;
			if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
				text.clear();
				return(false);
			}
			char encoded[UTF8::MAX_SEQUENCE];
			int count = UTF8::Encode(code, encoded);
			text.append(encoded, count);
			if (text.size() > UI_CLIPBOARD_MAX_BYTES) {
				text.clear();
				return(false);
			}
		}
	} catch (std::bad_alloc const &) {
		text.clear();
		return(false);
	}
	return(true);
#endif
}
