/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "always.h"

#include "dbgprint.h"

#include "opents_build.h"
#include "platform/diagnostics.h"
#include "platform/localtime.h"
#include "platform/process.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _DEBUG
static char const BuildType[] = "debug";
#else
static char const BuildType[] = "release";
#endif

static char const DebugTruncationNotice[] = "\n*** Log size limit reached. Nothing further will be written to this file. ***\n";

static constexpr size_t DEBUG_MESSAGE_MAX = 4096;
static constexpr size_t DEBUG_PATH_MAX = 1024;
static constexpr unsigned DEBUG_LOG_MAX_AGE_DAYS = 14;
static constexpr std::uint64_t DEBUG_LOG_MAX_BYTES = 64ULL * 1024ULL * 1024ULL;
static constexpr std::uint64_t DEBUG_LOG_NOTICE_RESERVE = sizeof(DebugTruncationNotice) - 1;
static constexpr std::uint64_t DEBUG_LOG_BUDGET = DEBUG_LOG_MAX_BYTES - DEBUG_LOG_NOTICE_RESERVE;

static std::mutex DebugLock;
static std::thread::id DebugLockOwner;
static bool DebugInitDone = false;
static bool AtLineStart = true;
static bool ConsoleActive = false;
static std::FILE * DebugFile = nullptr;
static char DebugDirectory[DEBUG_PATH_MAX];
static char DebugFileName[DEBUG_PATH_MAX];
static std::uint64_t DebugBytesWritten = 0;

static bool ConsoleRequested = false;


// A time of day as the log reports it, in local time to the millisecond.
struct LogTimeType {
	std::tm Parts;
	unsigned Milliseconds;
};


static LogTimeType Log_Time_Now(void)
{
	std::chrono::system_clock::time_point const now = std::chrono::system_clock::now();
	auto const since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

	LogTimeType result;
	result.Parts = Local_Calendar_Time(std::chrono::system_clock::to_time_t(now));
	result.Milliseconds = unsigned(since_epoch.count() % 1000);
	return(result);
}


// The rule the Windows file search applies to a pattern: '*' matches any run of characters,
// '?' any one character, and letters match without regard to case.
static bool Matches_Wildcard(char const * pattern, char const * name)
{
	char const * star = nullptr;
	char const * resume = nullptr;

	while (*name != '\0') {
		if (*pattern == '*') {
			star = pattern++;
			resume = name;
			continue;
		}

		if (*pattern != '\0' && (*pattern == '?'
			|| std::tolower((unsigned char)*pattern) == std::tolower((unsigned char)*name))) {
			pattern++;
			name++;
			continue;
		}

		if (star == nullptr) {
			return(false);
		}

		pattern = star + 1;
		name = ++resume;
	}

	while (*pattern == '*') {
		pattern++;
	}
	return(*pattern == '\0');
}


/// <summary>
/// Deletes files matching a pattern that were last written more than the given number of days
/// ago. Directories are never removed.
/// </summary>
/// <param name="directory">Directory to search, without a trailing separator.</param>
/// <param name="pattern">File name pattern, such as "DEBUG_*.LOG", matched without regard to
/// case.</param>
/// <param name="days">Age threshold in days. Values above 90 are rejected.</param>
/// <returns>True if the directory was searched.</returns>
bool Delete_Files_Older_Than(char const * directory, char const * pattern, unsigned days)
{
	if (directory == nullptr || pattern == nullptr || days > 90) {
		return(false);
	}

	namespace fs = std::filesystem;

	std::error_code error;
	fs::directory_iterator entry(fs::path(directory), error);
	if (error) {
		return(false);
	}

	fs::file_time_type const cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * int(days));

	// Collected first, because removing an entry while iterating leaves it unspecified whether
	// the iteration sees the change.
	std::vector<fs::path> victims;

	for (; entry != fs::directory_iterator(); entry.increment(error)) {
		if (error) {
			break;
		}

		std::error_code status;
		if (entry->is_directory(status)) {
			continue;
		}

		// UTF-8, because converting a name to the active code page can fail.
		std::u8string const name = entry->path().filename().u8string();
		if (!Matches_Wildcard(pattern, (char const *)name.c_str())) {
			continue;
		}

		fs::file_time_type const written = entry->last_write_time(status);
		if (status || written >= cutoff) {
			continue;
		}

		victims.push_back(entry->path());
	}

	for (fs::path const & victim : victims) {
		std::error_code ignored;
		fs::remove(victim, ignored);
	}

	return(true);
}


/// <summary>
/// Opens the console where the platform has one. The caller holds the logging lock.
/// </summary>
static void Init_Console_Locked(void)
{
	if (ConsoleActive) {
		return;
	}

	ConsoleActive = Debug_Console_Open();
}


static void Write_Banner_Locked(LogTimeType const & started, int argc, char const * const * argv);
static void Write_Text_Locked(char const * text, size_t length);
static void Write_Message_Locked(char const * buffer, bool with_prefix);


static bool Requests_Debug_Console(int argc, char const * const * argv)
{
	for (int index = 1; index < argc; index++) {
		char const * const token = argv[index];
		if (token[0] != '-' || (token[1] != 'X' && token[1] != 'x')) continue;
		if (strchr(token + 2, 'C') != nullptr || strchr(token + 2, 'c') != nullptr) return(true);
	}
	return(false);
}


// Creates the log exclusively, so a file another process already has is never reopened.
// Writes go straight to the operating system, as the crash reporter reads the file back.
static std::FILE * Create_Log_File(char const * path)
{
	std::FILE * const file = std::fopen(path, "wbx");
	if (file != nullptr) {
		std::setvbuf(file, nullptr, _IONBF, 0);
	}
	return(file);
}


/// <summary>
/// Prepares the log directory and this run's log file, then opens the console if this build
/// or the command line asks for it. The caller holds the logging lock. A log that cannot be
/// opened leaves the debugger and console sinks working.
/// </summary>
static void Init_Locked(int argc, char const * const * argv, char const * directory)
{
	if (DebugInitDone) {
		return;
	}
	DebugInitDone = true;

	// The log belongs beside the executable, which is not yet the current directory, unless
	// the caller named somewhere else.
	std::string const executable_directory = Executable_Directory();
	if (directory != nullptr && directory[0] != '\0') {
		std::snprintf(DebugDirectory, sizeof(DebugDirectory), "%s", directory);
	} else if (!executable_directory.empty()) {
		std::snprintf(DebugDirectory, sizeof(DebugDirectory), "%sDebug", executable_directory.c_str());
	}

	LogTimeType const now = Log_Time_Now();

	char timestamp[32];
	std::snprintf(timestamp, sizeof(timestamp), "%02d-%02d-%04d_%02d-%02d-%02d",
				now.Parts.tm_mday, now.Parts.tm_mon + 1, now.Parts.tm_year + 1900,
				now.Parts.tm_hour, now.Parts.tm_min, now.Parts.tm_sec);

	std::error_code error;
	if (DebugDirectory[0] != '\0') {
		std::filesystem::create_directory(std::filesystem::path(DebugDirectory), error);
	}

	if (DebugDirectory[0] != '\0' && std::filesystem::is_directory(std::filesystem::path(DebugDirectory), error)) {

		Delete_Files_Older_Than(DebugDirectory, "DEBUG_*.LOG", DEBUG_LOG_MAX_AGE_DAYS);

		char const separator = char(std::filesystem::path::preferred_separator);

		std::snprintf(DebugFileName, sizeof(DebugFileName), "%s%cDEBUG_%s.LOG",
					DebugDirectory, separator, timestamp);
		DebugFile = Create_Log_File(DebugFileName);

		// A second process started in the same second must not disturb the first one's log.
		if (DebugFile == nullptr) {
			std::snprintf(DebugFileName, sizeof(DebugFileName), "%s%cDEBUG_%s_%lu.LOG",
						DebugDirectory, separator, timestamp, (unsigned long)Process_Id());
			DebugFile = Create_Log_File(DebugFileName);
		}

		if (DebugFile == nullptr) {
			DebugFileName[0] = '\0';
		}
	}

	ConsoleRequested = ConsoleRequested || Requests_Debug_Console(argc, argv);

#ifdef _DEBUG
	Init_Console_Locked();
#else
	if (ConsoleRequested) {
		Init_Console_Locked();
	}
#endif

	// Last, so that the banner heads the log and also reaches a console that has just opened.
	AtLineStart = true;
	Write_Banner_Locked(now, argc, argv);
}


/// <summary>
/// Writes raw text to every enabled sink. The caller holds the logging lock.
/// </summary>
static void Write_Text_Locked(char const * text, size_t length)
{
	if (length == 0) return;

	if (DebugFile != nullptr) {

		// The notice is paid for out of the reserve, so the file never passes its limit.
		if (DebugBytesWritten + length > DEBUG_LOG_BUDGET) {
			std::fwrite(DebugTruncationNotice, 1, size_t(DEBUG_LOG_NOTICE_RESERVE), DebugFile);
			std::fclose(DebugFile);
			DebugFile = nullptr;
		} else {
			std::fwrite(text, 1, length, DebugFile);
			DebugBytesWritten += length;
		}
	}

	Debug_Output_Write(text);

	if (ConsoleActive) {
		Debug_Console_Write(text, length);
	}
}


/// <summary>
/// Writes one finished message, stamping the record prefix when one is due. The caller holds
/// the logging lock.
/// </summary>
static void Write_Message_Locked(char const * buffer, bool with_prefix)
{
	size_t const length = strlen(buffer);
	if (length == 0) {
		return;
	}

	// The prefix identifies a record rather than a call, so it is written once per line: a
	// line assembled from several calls is stamped where it starts. Prefix and message go out
	// together to keep this to one write per call.
	if (with_prefix && AtLineStart) {
		LogTimeType const now = Log_Time_Now();

		char stamped[DEBUG_MESSAGE_MAX + 32];
		int const written = std::snprintf(stamped, sizeof(stamped), "[%02d:%02d:%02d.%03u] %s",
												now.Parts.tm_hour, now.Parts.tm_min, now.Parts.tm_sec,
												now.Milliseconds, buffer);
		if (written > 0) {
			// snprintf reports the length it wanted, which is not what was stored.
			size_t const kept = std::min(size_t(written), sizeof(stamped) - 1);
			Write_Text_Locked(stamped, kept);
			AtLineStart = buffer[length - 1] == '\n';
			return;
		}
	}

	Write_Text_Locked(buffer, length);
	AtLineStart = buffer[length - 1] == '\n';
}


/// <summary>
/// Opens the log with the wordmark and the facts that identify the build and the run. The
/// caller holds the logging lock, so the text goes through the unlocked sink directly;
/// DebugStringNoPrefix would meet the re-entrancy guard and reach the debugger only.
/// </summary>
/// <param name="started">The time this run's log was opened.</param>
static void Write_Banner_Locked(LogTimeType const & started, int argc, char const * const * argv)
{
	// A raw literal keeps the lettering readable, and keeps its backslashes out of the reach of
	// escape processing. It opens on its own line so the rows line up here, which costs a
	// leading newline that the write below steps over.
	static char const Wordmark[] =
R"ART(
  ___                  _____ ____
 / _ \ _ __   ___ _ __|_   _/ ___|
| | | | '_ \ / _ \ '_ \ | | \___ \
| |_| | |_) |  __/ | | || |  ___) |
 \___/| .__/ \___|_| |_||_| |____/
      |_|

)ART";

	Write_Message_Locked(Wordmark + 1, false);

	char line[512];

	snprintf(line, sizeof(line), "Version  : OpenTS %s (%s %s build)\n", OPENTS_VERSION, OPENTS_ARCH, BuildType);
	Write_Message_Locked(line, false);

	snprintf(line, sizeof(line), "Commit   : %s on %s%s\n", OPENTS_COMMIT, OPENTS_BRANCH,
				OPENTS_COMMIT_DIRTY ? " (modified)" : "");
	Write_Message_Locked(line, false);

	snprintf(line, sizeof(line), "Committed: %s\n", OPENTS_COMMIT_DATE);
	Write_Message_Locked(line, false);

	snprintf(line, sizeof(line), "Started  : %04d-%02d-%02d %02d:%02d:%02d\n",
				started.Parts.tm_year + 1900, started.Parts.tm_mon + 1, started.Parts.tm_mday,
				started.Parts.tm_hour, started.Parts.tm_min, started.Parts.tm_sec);
	Write_Message_Locked(line, false);

	snprintf(line, sizeof(line), "System   : %s\n", Operating_System_Name().c_str());
	Write_Message_Locked(line, false);

	std::string const code_pages = Code_Page_Description();
	if (!code_pages.empty()) {
		snprintf(line, sizeof(line), "Codepage : %s\n", code_pages.c_str());
		Write_Message_Locked(line, false);
	}

	// The arguments only. The executable path usually carries the account name, and re-joining
	// the arguments loses the shell's original quoting, which a diagnostic can live without.
	Write_Message_Locked("Options  : ", false);
	if (argv != nullptr && argc > 1) {
		for (int index = 1; index < argc; index++) {
			if (index > 1) Write_Message_Locked(" ", false);
			Write_Message_Locked(argv[index], false);
		}
	} else {
		Write_Message_Locked("(none)", false);
	}
	Write_Message_Locked("\n", false);

	Write_Message_Locked("--------------------------------------------------------------------------------\n", false);
}


/// <summary>
/// Takes the logging lock and reports one finished message.
/// </summary>
static void Emit(char const * buffer, bool with_prefix)
{
	std::thread::id const self = std::this_thread::get_id();

	// A fault raised inside a logging call brings the handler back here on the same thread,
	// where taking the lock again would deadlock. Such a message reaches only the debugger on
	// Windows, and standard error elsewhere.
	if (DebugLockOwner == self) {
		Debug_Output_Write(buffer);
		return;
	}

	DebugLock.lock();
	DebugLockOwner = self;

	Write_Message_Locked(buffer, with_prefix);

	DebugLockOwner = std::thread::id();
	DebugLock.unlock();
}


/// <summary>
/// Opens this run's log in the directory given, or beside the executable when none is.
/// Messages reported before this call reach the debugger and the console but no file.
/// Repeated initialization keeps the first setup, including a failed file open.
/// </summary>
void Debug_Init(int argc, char const * const * argv, char const * directory)
{
	DebugLock.lock();
	DebugLockOwner = std::this_thread::get_id();
	Init_Locked(argc, argv, directory);
	DebugLockOwner = std::thread::id();
	DebugLock.unlock();
}


/// <summary>
/// Opens the console after logging is initialized, or requests it for Debug_Init.
/// </summary>
void Debug_Init_Console(void)
{
	DebugLock.lock();
	DebugLockOwner = std::this_thread::get_id();
	ConsoleRequested = true;
	if (DebugInitDone) Init_Console_Locked();
	DebugLockOwner = std::thread::id();
	DebugLock.unlock();
}


/// <summary>
/// Waits for a keypress when the debug console is open, so that text written just before the
/// process exits stays readable. Does nothing when there is no console.
/// </summary>
void Debug_Console_Hold(void)
{
	if (!ConsoleActive) {
		return;
	}

	DebugString("Press any key to close this window.\n");
	Debug_Console_Wait_For_Key();
}


/// <summary>
/// Returns the full path of this run's debug log, or an empty string when no log could be
/// opened or initialization has not run. Call after Debug_Init on the startup thread.
/// </summary>
char const * Debug_Log_File_Name(void)
{
	return(DebugFileName);
}


/// <summary>
/// Returns the folder where per-run diagnostic files belong, or an empty string when it could
/// not be selected. Call after Debug_Init on the startup thread.
/// </summary>
char const * Debug_Directory(void)
{
	return(DebugDirectory);
}


/// <summary>
/// Reports a formatted message to the debug log, the debugger, and the debug console. A
/// message that starts a line is stamped with the time it was reported.
/// </summary>
/// <param name="string">The printf style format string to report.</param>
void __cdecl DebugString(char const * string, ...)
{
	// Callers report an error and then branch on it, so logging must not disturb it.
	unsigned long const last_error = Platform_Last_Error();
	int const last_errno = errno;

	char buffer[DEBUG_MESSAGE_MAX];

	va_list va;
	va_start(va, string);
	vsnprintf(buffer, sizeof(buffer), string, va);
	va_end(va);

	Emit(buffer, true);

	errno = last_errno;
	Platform_Restore_Last_Error(last_error);
}


/// <summary>
/// Reports a formatted message with no identifying prefix, so the text appears exactly as
/// given. Callers use it to continue a line another call began, and for text such as the
/// startup banner that reads better unstamped. It reaches the same places DebugString does.
/// </summary>
/// <param name="string">The printf style format string to report.</param>
void __cdecl DebugStringNoPrefix(char const * string, ...)
{
	unsigned long const last_error = Platform_Last_Error();
	int const last_errno = errno;

	char buffer[DEBUG_MESSAGE_MAX];

	va_list va;
	va_start(va, string);
	vsnprintf(buffer, sizeof(buffer), string, va);
	va_end(va);

	Emit(buffer, false);

	errno = last_errno;
	Platform_Restore_Last_Error(last_error);
}
