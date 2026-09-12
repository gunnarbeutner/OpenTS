/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(_WIN32)

#include "platform/diagnostics.h"

#include "dbgprint.h"

#include <windows.h>

#include <conio.h>
#include <cstdio>

static char const CONSOLE_WINDOW_NAME[] = "Debug Console";

static HANDLE ConsoleOutput = INVALID_HANDLE_VALUE;


/// <summary>
/// Allocates a console window for this process and points the standard streams at it.
/// Returns false if the process could not be given a console, which includes one that
/// already has one.
/// </summary>
bool Debug_Console_Open(void)
{
	if (!AllocConsole()) {
		return(false);
	}

	SetConsoleTitleA(CONSOLE_WINDOW_NAME);

	// A new console decodes output in the OEM page, and the log lines it shows are UTF-8.
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	// Redirecting the standard streams is what lets ordinary stdio output, such as the
	// command line help, reach the console of a windowed application.
	FILE * stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	freopen_s(&stream, "CONIN$", "r", stdin);

	HANDLE const output = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
										FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	HANDLE const input = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
										FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

	if (output != INVALID_HANDLE_VALUE) {
		SetStdHandle(STD_OUTPUT_HANDLE, output);
		SetStdHandle(STD_ERROR_HANDLE, output);
	}

	if (input != INVALID_HANDLE_VALUE) {
		SetStdHandle(STD_INPUT_HANDLE, input);
	}

	ConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	if (ConsoleOutput == nullptr) {
		ConsoleOutput = INVALID_HANDLE_VALUE;
	}

	if (ConsoleOutput != INVALID_HANDLE_VALUE) {
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (GetConsoleScreenBufferInfo(ConsoleOutput, &info)) {
			COORD size;
			size.X = info.dwSize.X;
			size.Y = 4096;
			SetConsoleScreenBufferSize(ConsoleOutput, size);
		}
	}

	// Without this, closing the console window would take the game down with it.
	HWND const console_window = GetConsoleWindow();
	if (console_window != nullptr) {
		HMENU const menu = GetSystemMenu(console_window, FALSE);
		if (menu != nullptr) {
			DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
		}
	}

	return(true);
}


void Debug_Console_Write(char const * text, std::size_t length)
{
	if (ConsoleOutput != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteConsoleA(ConsoleOutput, text, DWORD(length), &written, nullptr);
	}
}


void Debug_Console_Wait_For_Key(void)
{
	_getch();
}


void Debug_Output_Write(char const * text)
{
	// Reporting to a debugger that is not there costs an exception round trip per message,
	// which is far more than the rest of the logging put together.
	if (IsDebuggerPresent()) {
		OutputDebugStringA(text);
	}
}


std::string Operating_System_Name(void)
{
	// Windows answers GetVersionEx with 6.2 for want of a compatibility manifest, so the real
	// build number has to come from RtlGetVersion.
	HMODULE const ntdll = GetModuleHandleA("ntdll.dll");
	if (ntdll != nullptr) {
		typedef LONG (WINAPI * RtlGetVersionType)(PRTL_OSVERSIONINFOW);
		RtlGetVersionType const rtl_get_version =
			(RtlGetVersionType)GetProcAddress(ntdll, "RtlGetVersion");

		if (rtl_get_version != nullptr) {
			RTL_OSVERSIONINFOW version = { 0 };
			version.dwOSVersionInfoSize = sizeof(version);
			if (rtl_get_version(&version) == 0) {
				char text[64];
				snprintf(text, sizeof(text), "Windows %lu.%lu.%lu",
							version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
				return(std::string(text));
			}
		}
	}

	return(std::string("unknown"));
}


std::string Code_Page_Description(void)
{
	// Windows before 10 version 1903 ignores the manifest's request for UTF-8.
	char text[64];
	snprintf(text, sizeof(text), "ANSI %u, OEM %u", GetACP(), GetOEMCP());
	return(std::string(text));
}


unsigned long Platform_Last_Error(void)
{
	return(GetLastError());
}


void Platform_Restore_Last_Error(unsigned long error)
{
	SetLastError(DWORD(error));
}


/// <summary>
/// Returns the system message text for a Win32 error code, in a buffer owned by the calling
/// thread.
/// </summary>
/// <param name="error">A code as returned by GetLastError.</param>
char const * Last_Error_Text(unsigned long error)
{
	static thread_local char message_buffer[256];

	if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, DWORD(error),
							MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
							message_buffer, sizeof(message_buffer), nullptr) == 0) {
		message_buffer[0] = '\0';
	}

	return(message_buffer);
}

#endif	// _WIN32
