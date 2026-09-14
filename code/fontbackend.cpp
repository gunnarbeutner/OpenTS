/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "fontbackend.h"

#if defined(__EMSCRIPTEN__)

#include "_mixfile.h"
#include "manifest.h"
#include "mixfile.h"

#include <emscripten/emscripten.h>

#include <string>


namespace {

// The face is fetched whole rather than read in ranges: it is small, every
// glyph of it is wanted, and the toolkit takes it as one block.
EM_ASYNC_JS(int, Face_Element_Fetch, (char const * url, int * size), {
	HEAP32[(size >>> 2)] = 0;

	try {
		var answer = await fetch(UTF8ToString(url));
		if (!answer.ok) return 0;

		var bytes = new Uint8Array(await answer.arrayBuffer());
		Module.OpenTSFace = bytes;
		HEAP32[(size >>> 2)] = bytes.length;
		return 1;
	} catch (exception) {
		Module.OpenTSFace = null;
		return 0;
	}
});


EM_JS(void, Face_Element_Take, (void * destination), {
	var bytes = Module.OpenTSFace;
	Module.OpenTSFace = null;
	if (!bytes) return;

	HEAPU8.set(bytes, destination >>> 0);
});


// The lettering's own name with the face's extension: the archive that answers
// one answers the other, so a per side or per addon copy stays distinct.
std::string Face_Name(char const * source_name)
{
	std::string name(source_name);
	std::size_t const dot = name.find_last_of('.');

	return((dot == std::string::npos ? name : name.substr(0, dot)) + ".TTF");
}


char const * Owning_Archive(char const * source_name)
{
	MixFileClass * mixfile = nullptr;

	if (!MFCD::Offset(source_name, nullptr, &mixfile, nullptr, nullptr)) return(nullptr);
	if (mixfile == nullptr) return(nullptr);

	return(mixfile->Filename);
}

}	// namespace


bool Prepared_Face_Read(char const * source_name, std::vector<unsigned char> & bytes)
{
	bytes.clear();

	if (source_name == nullptr || *source_name == '\0') return(false);

	std::string const url = Manifest_Find_File(Face_Name(source_name).c_str(),
		Owning_Archive(source_name));

	if (url.empty()) return(false);

	int size = 0;
	if (!Face_Element_Fetch(url.c_str(), &size) || size <= 0) return(false);

	bytes.resize((std::size_t)size);
	Face_Element_Take(bytes.data());

	return(true);
}

#else

bool Prepared_Face_Read(char const *, std::vector<unsigned char> & bytes)
{
	bytes.clear();
	return(false);
}

#endif
