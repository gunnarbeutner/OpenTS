/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The module, process, thread, command line, and lock entry points, none with a
// host call underneath. The module file name is reported in the current
// directory by construction: startup.cpp makes the module's directory current
// before the first archive is opened, so any other answer moves the engine off
// its game data.

#include "always.h"
#include "substitute.h"
#include "tlhelp32.h"
#include "dbghelp.h"
#include "shellapi.h"

#include "windows.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>


// No name here can be opened; every caller wants the directory, and this stands
// in the file's place.
static char const PROGRAM_FILE_NAME[] = "OpenTS.wasm";

// Only its address is used; the engine keeps it in ProgramInstance.
static char ProcessModule = 0;


static HMODULE Process_Module(void)
{
	return((HMODULE)&ProcessModule);
}


// Fixed on the first call, because a module's path does not follow the current
// directory around.
static char const * Program_Path(void)
{
	static char path[MAX_PATH];

	if (path[0] == '\0') {
		char working[MAX_PATH];

		if (::getcwd(working, sizeof(working)) == nullptr) {
			return(nullptr);
		}

		size_t const length = strlen(working);
		char const * const separator = (length > 0 && working[length - 1] == '/') ? "" : "/";

		int const written = snprintf(path, sizeof(path), "%s%s%s", working, separator, PROGRAM_FILE_NAME);
		if (written < 0 || (size_t)written >= sizeof(path)) {
			path[0] = '\0';
			return(nullptr);
		}
	}

	return(path);
}


HMODULE GetModuleHandleA(LPCSTR name)
{
	if (name == nullptr) {
		SetLastError(NO_ERROR);
		return(Process_Module());
	}

	// KERNEL32.DLL and ntdll.dll are what the engine asks for; not loaded is
	// the truth, and each caller keeps a branch for it. Windows reports
	// ERROR_MOD_NOT_FOUND, which win32compat.h lacks and no caller reads.
	SetLastError(ERROR_FILE_NOT_FOUND);
	return(nullptr);
}


DWORD GetModuleFileNameA(HMODULE module, LPSTR filename, DWORD size)
{
	if (filename == nullptr || size == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(0);
	}

	// Null names the running program, which is the only module this target has.
	if (module != nullptr && module != Process_Module()) {
		filename[0] = '\0';
		SetLastError(ERROR_INVALID_HANDLE);
		return(0);
	}

	char const * const path = Program_Path();
	if (path == nullptr) {
		filename[0] = '\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(0);
	}

	DWORD const length = (DWORD)strlen(path);

	// Win32 truncates rather than failing and answers with the whole buffer.
	if (length >= size) {
		memcpy(filename, path, size - 1);
		filename[size - 1] = '\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return(size);
	}

	memcpy(filename, path, length + 1);
	SetLastError(NO_ERROR);
	return(length);
}


// The page builds Module.arguments from its query string and node passes its
// own; main reassembles them, so this says exactly what startup.cpp was given.
// The internal name is read first so that a host supplying arguments another
// way is still answered.
#if defined(__EMSCRIPTEN__)
EM_JS(int, Process_Argument_Count, (void), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	return args.length;
});

EM_JS(int, Process_Argument, (int index, char * buffer, int size), {
	var args = (typeof programArgs !== "undefined" && programArgs) ||
		(typeof Module !== "undefined" && Module["arguments"]) || [];
	var text = (index >= 0 && index < args.length) ? "" + args[index] : "";

	var count = 0;
	while (count < text.length && count + 1 < size) {
		var code = text.charCodeAt(count);
		HEAPU8[buffer + count] = (code > 127) ? 63 : code;
		count++;
	}
	HEAPU8[buffer + count] = 0;
	return count;
});
#else
static int Process_Argument_Count(void)
{
#if defined(__APPLE__)
	return(*_NSGetArgc());
#else
	return(0);
#endif
}

static int Process_Argument(int index, char * buffer, int size)
{
	char const * text = "";
#if defined(__APPLE__)
	if (index >= 0 && index < *_NSGetArgc()) {
		text = (*_NSGetArgv())[index];
	}
#endif
	int count = 0;
	while (text[count] != '\0' && count + 1 < size) {
		buffer[count] = text[count];
		count++;
	}
	buffer[count] = '\0';
	return(count);
}
#endif


// Quoted the way CommandLineToArgvW expects: the backslashes before a quote, or
// ending the token, are doubled.
static void Append_Argument(std::string & line, char const * text)
{
	if (!line.empty()) {
		line += ' ';
	}

	if (text[0] != '\0' && strpbrk(text, " \t\"") == nullptr) {
		line += text;
		return;
	}

	line += '"';

	for (char const * scan = text; *scan != '\0'; ) {
		size_t slashes = 0;
		while (*scan == '\\') {
			slashes++;
			scan++;
		}

		if (*scan == '\0') {
			line.append(slashes * 2, '\\');
			break;
		}

		if (*scan == '"') {
			line.append(slashes * 2 + 1, '\\');
		} else {
			line.append(slashes, '\\');
		}

		line += *scan++;
	}

	line += '"';
}


static char CommandLineText[2048];
static WCHAR CommandLineWide[2048];


static void Build_Command_Line(void)
{
	static bool built = false;

	if (built) {
		return;
	}
	built = true;

	std::string line;

	char const * const program = Program_Path();
	Append_Argument(line, (program != nullptr) ? program : PROGRAM_FILE_NAME);

	int const count = Process_Argument_Count();
	for (int index = 0; index < count; index++) {
		char argument[512];

		Process_Argument(index, argument, sizeof(argument));
		Append_Argument(line, argument);
	}

	size_t const length = (line.size() < sizeof(CommandLineText)) ? line.size() : sizeof(CommandLineText) - 1;

	memcpy(CommandLineText, line.c_str(), length);
	CommandLineText[length] = '\0';

	// The arguments arrive as ASCII, so the widening is exact.
	for (size_t index = 0; index <= length; index++) {
		CommandLineWide[index] = (WCHAR)(unsigned char)CommandLineText[index];
	}
}


LPSTR GetCommandLineA(void)
{
	Build_Command_Line();
	return(CommandLineText);
}


LPWSTR GetCommandLineW(void)
{
	Build_Command_Line();
	return(CommandLineWide);
}


static size_t Wide_Length(LPCWSTR text)
{
	size_t length = 0;
	while (text[length] != L'\0') {
		length++;
	}
	return(length);
}


// Splits the way the shell API does. The result is one allocation with the
// pointers ahead of the characters, so LocalFree releases both.
LPWSTR * CommandLineToArgvW(LPCWSTR commandline, int * count)
{
	if (count == nullptr || commandline == nullptr) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return(nullptr);
	}

	*count = 0;

	// An empty command line is answered with the program's own path: one
	// argument, not a line to parse.
	WCHAR program[MAX_PATH];
	if (commandline[0] == L'\0') {
		char const * const path = Program_Path();
		if (path == nullptr) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return(nullptr);
		}

		size_t index = 0;
		while (path[index] != '\0' && index + 1 < MAX_PATH) {
			program[index] = (WCHAR)(unsigned char)path[index];
			index++;
		}
		program[index] = L'\0';

		LPWSTR * const single = (LPWSTR *)malloc(2 * sizeof(LPWSTR) + (index + 1) * sizeof(WCHAR));
		if (single == nullptr) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return(nullptr);
		}

		WCHAR * const text = (WCHAR *)(single + 2);
		memcpy(text, program, (index + 1) * sizeof(WCHAR));

		single[0] = text;
		single[1] = nullptr;

		*count = 1;
		SetLastError(NO_ERROR);
		return(single);
	}

	size_t const length = Wide_Length(commandline);

	// Each argument is at most as long as its source, and its separating blank
	// pays for its terminator; the pointer count is bounded the same way.
	size_t const slots = length + 2;
	LPWSTR * const argv = (LPWSTR *)malloc(slots * sizeof(LPWSTR) + (length + 2) * sizeof(WCHAR));
	if (argv == nullptr) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return(nullptr);
	}

	WCHAR * out = (WCHAR *)(argv + slots);
	LPCWSTR scan = commandline;
	int argc = 0;

	argv[argc++] = out;
	if (*scan == L'"') {
		scan++;
		while (*scan != L'\0' && *scan != L'"') {
			*out++ = *scan++;
		}
		if (*scan == L'"') {
			scan++;
		}
	} else {
		while (*scan != L'\0' && *scan != L' ' && *scan != L'\t') {
			*out++ = *scan++;
		}
	}
	*out++ = L'\0';

	for (;;) {
		while (*scan == L' ' || *scan == L'\t') {
			scan++;
		}

		if (*scan == L'\0') {
			break;
		}

		argv[argc++] = out;

		// Odd inside a quoted run, which is what keeps a blank from ending it.
		int quotes = 0;
		size_t slashes = 0;

		for (;;) {
			WCHAR const letter = *scan;

			if (letter == L'\0') break;
			if ((letter == L' ' || letter == L'\t') && quotes == 0) break;

			if (letter == L'\\') {
				slashes++;
				scan++;
				continue;
			}

			if (letter == L'"') {
				for (size_t index = 0; index < slashes / 2; index++) {
					*out++ = L'\\';
				}

				if ((slashes & 1) != 0) {
					*out++ = L'"';
				} else {
					quotes++;
				}

				slashes = 0;
				scan++;

				while (*scan == L'"') {
					if (++quotes == 3) {
						*out++ = L'"';
						quotes = 0;
					}
					scan++;
				}

				if (quotes == 2) {
					quotes = 0;
				}
				continue;
			}

			for (size_t index = 0; index < slashes; index++) {
				*out++ = L'\\';
			}
			slashes = 0;

			*out++ = letter;
			scan++;
		}

		for (size_t index = 0; index < slashes; index++) {
			*out++ = L'\\';
		}
		*out++ = L'\0';
	}

	argv[argc] = nullptr;

	*count = argc;
	SetLastError(NO_ERROR);
	return(argv);
}


// One thread means a lock is never contended, so these are the whole of the
// lock rather than a stub, and the state has nothing to record.
void InitializeSRWLock(PSRWLOCK lock)
{
	if (lock != nullptr) {
		lock->Ptr = nullptr;
	}
}


// TryEnter must succeed: reporting contention would send a caller down a
// back-off path that never ends.
void InitializeCriticalSection(LPCRITICAL_SECTION section)
{
	if (section != nullptr) {
		memset(section, 0, sizeof(*section));
	}
}


void DeleteCriticalSection(LPCRITICAL_SECTION)
{
}


void EnterCriticalSection(LPCRITICAL_SECTION)
{
}


void LeaveCriticalSection(LPCRITICAL_SECTION)
{
}


BOOL TryEnterCriticalSection(LPCRITICAL_SECTION)
{
	return(TRUE);
}


void AcquireSRWLockExclusive(PSRWLOCK)
{
}


void ReleaseSRWLockExclusive(PSRWLOCK)
{
}


void AcquireSRWLockShared(PSRWLOCK)
{
}


void ReleaseSRWLockShared(PSRWLOCK)
{
}


//------------------------------------------------------------------------------
// Modules, threads, and the process.
//------------------------------------------------------------------------------


// A stub names itself once, then returns its Win32 original's failure result.

HMODULE LoadLibraryA(LPCSTR) { return(WIN32_STUB((HMODULE)nullptr)); }
BOOL FreeLibrary(HMODULE) { return(WIN32_STUB(FALSE)); }
FARPROC GetProcAddress(HMODULE, LPCSTR) { return(WIN32_STUB((FARPROC)nullptr)); }
void ExitProcess(UINT code) { exit((int)code); }
HANDLE GetCurrentProcess(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
HANDLE GetCurrentThread(void) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
DWORD GetCurrentThreadId(void) { return(1); }
DWORD GetCurrentProcessId(void) { return(WIN32_STUB(0)); }
BOOL SetPriorityClass(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
BOOL SetThreadPriority(HANDLE, int) { return(WIN32_STUB(FALSE)); }
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) { return(WIN32_STUB((HANDLE)nullptr)); }
BOOL TerminateThread(HANDLE, DWORD) { return(WIN32_STUB(FALSE)); }
DWORD ResumeThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
DWORD SuspendThread(HANDLE) { return(WIN32_STUB((DWORD)-1)); }
void GetSystemInfo(LPSYSTEM_INFO) { WIN32_STUB_VOID(); }
void GlobalMemoryStatus(LPMEMORYSTATUS) { WIN32_STUB_VOID(); }
BOOL TerminateProcess(HANDLE, UINT code) { exit((int)code); }
void RaiseException(DWORD, DWORD, DWORD, ULONG_PTR const *) { WIN32_STUB_ABORT(); }
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return(WIN32_STUB(INVALID_HANDLE_VALUE)); }
BOOL Module32First(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }
BOOL Module32Next(HANDLE, LPMODULEENTRY32) { return(WIN32_STUB(FALSE)); }
BOOL IsDebuggerPresent(void) { return(FALSE); }
PTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(PTOP_LEVEL_EXCEPTION_FILTER) { return(WIN32_STUB((PTOP_LEVEL_EXCEPTION_FILTER)nullptr)); }


//------------------------------------------------------------------------------
// Debug help.
//------------------------------------------------------------------------------


BOOL SymInitialize(HANDLE, LPCSTR, BOOL) { return(WIN32_STUB(FALSE)); }
BOOL SymCleanup(HANDLE) { return(WIN32_STUB(FALSE)); }
DWORD SymSetOptions(DWORD) { return(WIN32_STUB(0)); }
BOOL SymFromAddr(HANDLE, DWORD64, DWORD64 *, PSYMBOL_INFO) { return(WIN32_STUB(FALSE)); }
BOOL SymGetLineFromAddr64(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64) { return(WIN32_STUB(FALSE)); }

#endif	// OPENTS_WIN32_SUBSTITUTE
