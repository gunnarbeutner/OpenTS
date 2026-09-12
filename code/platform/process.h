/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What the engine asks the operating system about its own process. Paths are
// narrow strings in the encoding the platform's narrow file API takes: the
// active code page on Windows, which the manifest makes UTF-8, and UTF-8
// elsewhere. process_win32.cpp and process_posix.cpp answer the questions that
// differ; process.cpp derives the rest.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>


// Empty where the program has no file of its own, as on a page.
std::string Executable_Path(void);

// Ends with a separator, so a file name can be appended directly.
std::string Executable_Directory(void);

// The address range the executable's image occupies, for turning a return
// address into an offset a map file can be read against. False where the
// platform has no such image.
bool Executable_Image_Range(std::uintptr_t & base, std::size_t & size);

std::uint32_t Process_Id(void);

// False when another copy of the game is already running and has been brought
// forward, in which case this one should end. Always true off Windows.
bool Acquire_Single_Instance(void);
void Release_Single_Instance(void);

// Windows waits a whole scheduler tick, about 15ms, however short a Sleep asks
// for unless the resolution is raised. Nothing elsewhere.
void Raise_Timer_Resolution(void);
void Restore_Timer_Resolution(void);
