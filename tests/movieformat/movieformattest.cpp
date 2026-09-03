/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "movieformat.h"

#include <cstdio>
#include <cstring>


namespace {

int Failures = 0;


void Check(char const * name, bool condition)
{
	if (!condition) {
		std::printf("FAIL %s\n", name);
		Failures++;
	}
}


void Check_Name(char const * label, char const * input, char const * expected)
{
	char output[64];
	bool const resolved = Movie_Resolve_Name(input, output, sizeof(output));
	Check(label, resolved && std::strcmp(output, expected) == 0);
}

}


int main(int argc, char ** argv)
{
	if (argc != 2) {
		std::printf("usage: MovieFormatTest EXTENSION\n");
		return(2);
	}

	char expected[64];
	std::snprintf(expected, sizeof(expected), "INTR0%s", argv[1]);
	Check("selected extension", std::strcmp(Movie_Extension(), argv[1]) == 0);
	Check_Name("base name", "INTR0", expected);
	Check_Name("VQA input", "INTR0.VQA", expected);
	Check_Name("lower-case VQA input", "INTR0.vqa", expected);
	Check_Name("MP4 input", "INTR0.MP4", expected);
	Check_Name("lower-case MP4 input", "INTR0.mp4", expected);

	char output[9] = "changed";
	Check("short destination", !Movie_Resolve_Name("INTR0", output, sizeof(output)) && output[0] == '\0');
	Check("null input", !Movie_Resolve_Name(nullptr, output, sizeof(output)));

	if (Failures != 0) {
		std::printf("%d movie filename checks failed\n", Failures);
		return(1);
	}

	std::printf("movie filename checks passed for %s\n", argv[1]);
	return(0);
}
