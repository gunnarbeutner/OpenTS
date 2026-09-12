/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The iOS app's entry point. SDL2main runs this from UIKit, and it settles the two
// directories an app has before handing the engine the arguments it would have had from a
// command line.

#include <SDL.h>

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

int OpenTS_Main(int argc, char ** argv);


int main(int argc, char ** argv)
{
	char * base = SDL_GetBasePath();
	char * user = SDL_GetPrefPath("OpenTS", "OpenTS");

	// Everything the game writes is relative to the current directory, and the bundle the app
	// runs from is read only.
	if (base == nullptr || user == nullptr || chdir(user) != 0) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenTS", "Cannot open the app's data directories.", nullptr);
		SDL_free(base);
		SDL_free(user);
		return(1);
	}

	freopen("ios-runtime.log", "w", stdout);
	freopen("ios-errors.log", "w", stderr);
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	std::vector<std::string> arguments = {
		"OpenTS", "-NOINTRO", "-NOBRIEFING",
		std::string("-DATADIR=") + base + "GameData",
		std::string("-USERDIR=") + user
	};

	// An app is launched with no arguments of its own, so the first campaign mission stands in
	// for the menus a player has no way to reach yet.
	if (argc == 1) {
		arguments.emplace_back("-SCENARIO=GDI1A.MAP");
		arguments.emplace_back("-CAMPAIGN=GDI1");
	}
	for (int index = 1; index < argc; index++) {
		arguments.emplace_back(argv[index]);
	}

	SDL_free(base);
	SDL_free(user);

	std::vector<char *> pointers;
	for (std::string & argument : arguments) {
		pointers.push_back(argument.data());
	}
	pointers.push_back(nullptr);

	return(OpenTS_Main(static_cast<int>(arguments.size()), pointers.data()));
}
