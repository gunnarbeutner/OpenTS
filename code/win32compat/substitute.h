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

#include "windef.h"
#include "blocksource.hh"

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


// A name the host cannot resolve is looked up in the manifest of this content
// set's archives; the host's copy wins when both answer.

/// <summary>Advises that a run of a file on a mounted image is about to be
/// used; a length of 0 means the rest of the file.</summary>
/// <returns>true when the name resolved to a file on a mounted image.</returns>
bool Win32_Hint_File(char const * filename, BlockHintType kind, unsigned int offset, unsigned int length);

/// <summary>Advises that a run of an open file is about to be used; a length of
/// 0 means the rest of the file.</summary>
/// <returns>true when an image answers for the handle.</returns>
bool Win32_Hint_Handle(HANDLE file, BlockHintType kind, unsigned int offset, unsigned int length);

// Fetches a run of a named file into the store so a later read is answered
// locally; it waits, and is legal only underneath the promising export.
bool Win32_Prefetch_File(char const * filename, unsigned int offset, unsigned int length);

// How many of a file's bytes a persistent store already holds, so a caller can tell what
// it would have to fetch from what is here already.
unsigned long long Win32_Stored_Bytes(char const * filename);
