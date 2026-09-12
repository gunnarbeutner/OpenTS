/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "platform/process.h"

#include <filesystem>
#include <system_error>


/// <summary>
/// Returns the directory holding the executable, ending with a separator. Where the program
/// has no file of its own, as on a page, the current directory at the first call stands in
/// for it, because startup makes this directory current before the first archive is opened.
/// The answer is fixed at the first call.
/// </summary>
std::string Executable_Directory(void)
{
	static std::string const directory = []() -> std::string {
		std::string path = Executable_Path();

		std::string::size_type const separator = path.find_last_of("\\/");
		if (separator != std::string::npos) {
			path.erase(separator + 1);
			return(path);
		}

		std::error_code error;
		std::filesystem::path const working = std::filesystem::current_path(error);
		if (error) {
			return(std::string());
		}

		std::string result = working.string();
		if (result.empty() || (result.back() != '\\' && result.back() != '/')) {
			result += char(std::filesystem::path::preferred_separator);
		}
		return(result);
	}();

	return(directory);
}
