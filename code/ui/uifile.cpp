/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// RmlUi's file interface over the game's file system. Documents, styles, images, and fonts
// therefore load from a loose directory, from the search paths, or from a mix file, in the
// order CDFileClass already applies, which is what lets a mod override one of them.

#include "always.h"

#include "uifile.h"

#include "ccfile.h"
#include "dbgprint.h"

#include <RmlUi/Core/FileInterface.h>

#include <cstring>


// RmlUi resolves a relative reference against the document that made it, and the engine's
// lookup takes a bare name rather than a path. Every reference is therefore reduced to its
// basename, so `ui/` on disk is a packaging convenience and never part of the lookup key.
static Rml::String Base_Name(Rml::String const & path)
{
	size_t mark = path.find_last_of("\\/:");
	if (mark == Rml::String::npos) {
		return(path);
	}

	return(path.substr(mark + 1));
}


class UIFileInterface : public Rml::FileInterface
{
	public:
		Rml::FileHandle Open(Rml::String const & path) override;
		void Close(Rml::FileHandle file) override;
		size_t Read(void * buffer, size_t size, Rml::FileHandle file) override;
		bool Seek(Rml::FileHandle file, long offset, int origin) override;
		size_t Tell(Rml::FileHandle file) override;
		size_t Length(Rml::FileHandle file) override;
};


static UIFileInterface _Interface;


Rml::FileHandle UIFileInterface::Open(Rml::String const & path)
{
	Rml::String name = Base_Name(path);

	CCFileClass * file = new CCFileClass(name.c_str());
	if (file->Open(CCFileClass::READ) == 0) {
		delete file;
		return(0);
	}

	return((Rml::FileHandle)file);
}


void UIFileInterface::Close(Rml::FileHandle handle)
{
	CCFileClass * file = (CCFileClass *)handle;
	if (file == nullptr) {
		return;
	}

	file->Close();
	delete file;
}


size_t UIFileInterface::Read(void * buffer, size_t size, Rml::FileHandle handle)
{
	CCFileClass * file = (CCFileClass *)handle;
	if (file == nullptr || buffer == nullptr) {
		return(0);
	}

	// The engine counts bytes in an int where RmlUi counts them in a size_t, so a request
	// past what the engine can express is refused rather than silently truncated.
	if (size > (size_t)INT_MAX) {
		return(0);
	}

	int read = file->Read(buffer, (int)size);
	return(read > 0 ? (size_t)read : 0);
}


bool UIFileInterface::Seek(Rml::FileHandle handle, long offset, int origin)
{
	CCFileClass * file = (CCFileClass *)handle;
	if (file == nullptr) {
		return(false);
	}

	// FileClass::Seek clamps to the file rather than failing, so the landing position is
	// compared against the request. A clamped seek must not look like a success.
	int wanted = 0;
	switch (origin) {
		case SEEK_SET:
			wanted = (int)offset;
			break;

		case SEEK_CUR:
			wanted = file->Seek(0, SEEK_CUR) + (int)offset;
			break;

		case SEEK_END:
			wanted = file->Size() + (int)offset;
			break;

		default:
			return(false);
	}

	if (wanted < 0) {
		return(false);
	}

	return(file->Seek(wanted, SEEK_SET) == wanted);
}


size_t UIFileInterface::Tell(Rml::FileHandle handle)
{
	CCFileClass * file = (CCFileClass *)handle;
	if (file == nullptr) {
		return(0);
	}

	int position = file->Seek(0, SEEK_CUR);
	return(position > 0 ? (size_t)position : 0);
}


size_t UIFileInterface::Length(Rml::FileHandle handle)
{
	CCFileClass * file = (CCFileClass *)handle;
	if (file == nullptr) {
		return(0);
	}

	int size = file->Size();
	return(size > 0 ? (size_t)size : 0);
}


Rml::FileInterface * UI_File_Interface(void)
{
	return(&_Interface);
}
