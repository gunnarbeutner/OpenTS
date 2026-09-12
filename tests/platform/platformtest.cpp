/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Holds the process and diagnostics platform files to what the engine takes from them: where
// the executable lives, the log it writes beside it, the pruning of old logs by name and age,
// and the error state logging must leave alone. Needs no game data and no engine.

#include "dbgprint.h"
#include "platform/diagnostics.h"
#include "platform/process.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-60s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition) {
		Failures++;
	}
}


bool Same_File(fs::path const & left, fs::path const & right)
{
	std::error_code error;
	return(fs::equivalent(left, right, error) && !error);
}


std::string Read_Text(std::string const & path)
{
	std::ifstream stream(path, std::ios::binary);
	return(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
}


void Make_File(fs::path const & path, int age_days)
{
	std::ofstream(path, std::ios::binary) << "x";

	std::error_code error;
	fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(24 * age_days), error);
}


void Check_Executable(char const * argv0)
{
	std::string const directory = Executable_Directory();
	Check(!directory.empty() && (directory.back() == '/' || directory.back() == '\\'),
			"executable directory ends with a separator");

#if defined(__EMSCRIPTEN__)
	(void)argv0;
	Check(Executable_Path().empty(), "a page has no executable file");
	Check(Same_File(directory, fs::current_path()), "a page's executable directory is the current one");
#else
	fs::path const self = fs::absolute(argv0);
	Check(Same_File(Executable_Path(), self), "executable path names this program");
	Check(Same_File(directory, self.parent_path()), "executable directory holds this program");
#endif

	// The answer is fixed at the first call, so a later change of directory leaves it alone.
	std::error_code error;
	fs::path const working = fs::current_path(error);
	fs::current_path(fs::temp_directory_path(error), error);
	Check(Executable_Directory() == directory, "executable directory ignores the current directory");
	fs::current_path(working, error);

	std::uintptr_t base = 0;
	std::size_t size = 0;
	bool const has_image = Executable_Image_Range(base, size);
#if defined(_WIN32)
	std::uintptr_t const code = (std::uintptr_t)&Check_Executable;
	Check(has_image && code >= base && code < base + size, "image range covers this program's code");
#else
	Check(!has_image && base == 0 && size == 0, "no image range off Windows");
	Check(Acquire_Single_Instance(), "single instance is always granted off Windows");
	Release_Single_Instance();
#endif

	Check(Process_Id() != 0, "process id is known");
}


void Check_Log(int argc, char ** argv)
{
	Debug_Init(argc, argv);

	std::string const directory = Executable_Directory() + "Debug";
	Check(std::string(Debug_Directory()) == directory, "log directory sits beside the executable");
	Check(fs::is_directory(directory), "log directory exists");

	DebugString("platform-test: %d\n", 42);

#if defined(__EMSCRIPTEN__)
	Check(Debug_Log_File_Name()[0] == '\0', "a page writes no log file");
#else
	std::string const log = Debug_Log_File_Name();
	Check(!log.empty() && Same_File(fs::path(log).parent_path(), directory), "log file opened in the log directory");

	std::string const text = Read_Text(log);
	Check(text.find("Version  : OpenTS ") != std::string::npos, "banner names the version");
	Check(text.find("System   : ") != std::string::npos, "banner names the system");
	Check(text.find("Options  : ") != std::string::npos, "banner lists the options");
	Check(text.find("] platform-test: 42\n") != std::string::npos, "a message reaches the log stamped");
#endif

	Check(!Operating_System_Name().empty(), "system has a name");
#if defined(_WIN32)
	Check(!Code_Page_Description().empty(), "code pages are reported on Windows");
#else
	Check(Code_Page_Description().empty(), "no code pages off Windows");
#endif
}


void Check_Error_State(void)
{
	errno = EDOM;
	Platform_Restore_Last_Error(42);
	DebugString("platform-test: error state\n");
	Check(errno == EDOM, "logging leaves errno alone");
#if defined(_WIN32)
	Check(Platform_Last_Error() == 42, "logging leaves the last error alone");
#else
	Check(Platform_Last_Error() == 0, "no last error off Windows");
#endif

	// ENOENT elsewhere and ERROR_FILE_NOT_FOUND on Windows are both 2.
	Check(Last_Error_Text(2)[0] != '\0', "error text for a missing file");
}


void Check_Pruning(void)
{
	std::error_code error;
	fs::path const root = fs::temp_directory_path(error) / ("opents-platformtest-" + std::to_string(Process_Id()));
	fs::remove_all(root, error);
	fs::create_directories(root, error);
	Check(!error, "scratch directory created");

	Make_File(root / "DEBUG_OLD.LOG", 20);
	Make_File(root / "debug_lower.log", 20);
	Make_File(root / "DEBUG_.LOG", 20);
	Make_File(root / "DEBUG_NEW.LOG", 1);
	Make_File(root / "OTHER_OLD.LOG", 20);
	Make_File(root / "DEBUG_OLD.LOG.BAK", 20);
	Make_File(root / "SYNC_A.LOG", 20);
	Make_File(root / "SYNC_AB.LOG", 20);
	fs::create_directory(root / "DEBUG_DIR.LOG", error);
	fs::last_write_time(root / "DEBUG_DIR.LOG", fs::file_time_type::clock::now() - std::chrono::hours(24 * 20), error);

	std::string const directory = root.string();
	Check(Delete_Files_Older_Than(directory.c_str(), "DEBUG_*.LOG", 14), "pruning searches the directory");
	Check(Delete_Files_Older_Than(directory.c_str(), "SYNC_?.LOG", 14), "pruning takes a single-character wildcard");

	Check(!fs::exists(root / "DEBUG_OLD.LOG"), "an old match is deleted");
	Check(!fs::exists(root / "debug_lower.log"), "a match is found without regard to case");
	Check(!fs::exists(root / "DEBUG_.LOG"), "a star matches nothing at all");
	Check(fs::exists(root / "DEBUG_NEW.LOG"), "a recent match is kept");
	Check(fs::exists(root / "OTHER_OLD.LOG"), "an old file of another name is kept");
	Check(fs::exists(root / "DEBUG_OLD.LOG.BAK"), "the pattern has to match the whole name");
	Check(fs::is_directory(root / "DEBUG_DIR.LOG"), "a directory is never removed");
	Check(!fs::exists(root / "SYNC_A.LOG"), "a question mark matches one character");
	Check(fs::exists(root / "SYNC_AB.LOG"), "a question mark matches no more than one");

	Check(!Delete_Files_Older_Than(directory.c_str(), "DEBUG_*.LOG", 91), "pruning refuses an absurd age");
	Check(!Delete_Files_Older_Than(nullptr, "DEBUG_*.LOG", 14), "pruning refuses a null directory");
	Check(!Delete_Files_Older_Than((root / "missing").string().c_str(), "DEBUG_*.LOG", 14),
			"pruning reports a directory it cannot search");

	fs::remove_all(root, error);
}

}	// namespace


int main(int argc, char ** argv)
{
	Check_Executable(argv[0]);
	Check_Log(argc, argv);
	Check_Error_State();
	Check_Pruning();

	std::printf(Failures == 0 ? "\nPASSED\n" : "\nFAILED\n");
	return(Failures == 0 ? 0 : 1);
}
