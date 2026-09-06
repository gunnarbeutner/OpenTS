/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The substitute's own contract, which no Windows header declares: how an unimplemented entry point reports itself.

#pragma once

// WIN32_STUB(value) reports the call once per entry point and yields the Win32
// failure value; WIN32_STUB_ABORT() terminates where no return value could
// carry the failure.
void Win32_Stub_Reached(char const * function);
[[noreturn]] void Win32_Stub_Fatal(char const * function);

#define WIN32_STUB(value)	(::Win32_Stub_Reached(__func__), (value))
#define WIN32_STUB_VOID()	(::Win32_Stub_Reached(__func__))
#define WIN32_STUB_ABORT()	(::Win32_Stub_Fatal(__func__))

// WIN32_UNSUPPORTED reports a request an implemented entry point cannot serve,
// once per description, which must be a literal, and yields the Win32 failure
// value.
void Win32_Unsupported_Reached(char const * description);

#define WIN32_UNSUPPORTED(description, value)	(::Win32_Unsupported_Reached(description), (value))
