/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if !defined(_WIN32)

#include "platform/diagnostics.h"

#include "dbgprint.h"

#include <cstdio>
#include <cstring>

#include <sys/utsname.h>


// Standard error already reaches whatever terminal or page console started the program, so
// there is no window to open.
bool Debug_Console_Open(void)
{
	return(false);
}


void Debug_Console_Write(char const *, std::size_t)
{
}


void Debug_Console_Wait_For_Key(void)
{
}


void Debug_Output_Write(char const * text)
{
	std::fputs(text, stderr);
}


std::string Operating_System_Name(void)
{
	struct utsname name;
	if (uname(&name) != 0) {
		return(std::string("unknown"));
	}

	return(std::string(name.sysname) + " " + name.release);
}


std::string Code_Page_Description(void)
{
	return(std::string());
}


unsigned long Platform_Last_Error(void)
{
	return(0);
}


void Platform_Restore_Last_Error(unsigned long)
{
}


/// <summary>
/// Returns the system message text for an errno value, in a buffer owned by the calling
/// thread.
/// </summary>
char const * Last_Error_Text(unsigned long error)
{
	static thread_local char message_buffer[256];

	std::snprintf(message_buffer, sizeof(message_buffer), "%s", std::strerror(int(error)));
	return(message_buffer);
}

#endif	// !_WIN32
