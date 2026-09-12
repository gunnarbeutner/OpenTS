/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The operating system's part of the debug log: the console window, the
// debugger's output channel, the facts the log's banner reports about the
// system, and the thread's last error. diagnostics_win32.cpp answers these on
// Windows; diagnostics_posix.cpp sends what Windows shows in a debugger or a
// console to standard error instead. dbgprint.cpp owns the log itself. The
// same two files define Last_Error_Text, which dbgprint.h declares.

#pragma once

#include <cstddef>
#include <string>


// False where the platform has no console to open, or it could not be opened.
bool Debug_Console_Open(void);
void Debug_Console_Write(char const * text, std::size_t length);
void Debug_Console_Wait_For_Key(void);

// Cheap when nothing is listening, so every message can be offered.
void Debug_Output_Write(char const * text);

// Such as "Windows 10.0.26200", or "unknown".
std::string Operating_System_Name(void);

// Empty where the platform has no code pages to report.
std::string Code_Page_Description(void);

// The thread's Win32 last-error value, which logging must leave as it found it. Zero and
// ignored off Windows, where errno is the only such state.
unsigned long Platform_Last_Error(void);
void Platform_Restore_Last_Error(unsigned long error);
