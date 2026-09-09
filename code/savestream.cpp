/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "savestream.h"

#include "saveload.h"

#include <cstring>
#include <string>


unsigned int LoadedSaveVersion = 0;


/// <summary>
/// Builds a save stream over the buffer given, appending to it when saving and reading
/// it from the start when loading.
/// </summary>
/// <param name="buffer">The bytes of the saved game, which must outlive this stream.</param>
/// <param name="mode">Is this stream saving or loading?</param>
SaveStreamClass::SaveStreamClass(std::vector<unsigned char> & buffer, ModeType mode, SaveNamesClass & names,
	unsigned int start) :
	Names(&names),
	Buffer(&buffer),
	Cursor(mode == MODE_SAVE ? (unsigned int)buffer.size() : 0),
	Limit((unsigned int)buffer.size()),
	Mode(mode),
	Failed(false),
	FormatVersion(mode == MODE_LOAD ? LoadedSaveVersion : ExpectedGameVersion),
	OwnerType(NULL),
	OwnerID(0)
{
	Cursor = (mode == MODE_LOAD) ? start : (unsigned int)buffer.size();
	Limit = (mode == MODE_LOAD) ? (unsigned int)buffer.size() : 0;
}


void SaveStreamClass::Fail(void)
{
	Failed = true;
}


/// <summary>
/// Moves the bytes of one value between the caller and the stream.
/// A load that runs out of stream in the middle of a value fails the stream rather than
/// hand back a partly read value, and every later call is ignored. A negative length is
/// a count that wrapped, and fails the same way.
/// </summary>
void SaveStreamClass::Serialize_Bytes(void * data, int length)
{
	if (Failed) {
		return;
	}
	if (length < 0) {
		Failed = true;
		return;
	}
	if (length == 0) {
		return;
	}

	unsigned char * const bytes = (unsigned char *)data;

	if (Mode == MODE_SAVE) {
		Buffer->insert(Buffer->end(), bytes, bytes + length);
		Cursor = (unsigned int)Buffer->size();
	} else {
		// The bound is the innermost field or body rather than the whole stream, so a
		// member that reads more than its own is refused instead of quietly spending the
		// bytes of the one after it.
		if ((unsigned int)length > Limit - Cursor) {
			Failed = true;
			return;
		}
		memcpy(bytes, Buffer->data() + Cursor, (std::size_t)length);
		Cursor += (unsigned int)length;
	}
}


// A saver patches a length it could not know until the record was written.
void SaveStreamClass::Overwrite_Bytes(unsigned int offset, void const * data, int length)
{
	if (Failed || Mode != MODE_SAVE || length <= 0) {
		return;
	}
	if (offset > Buffer->size() || (unsigned int)length > Buffer->size() - offset) {
		Failed = true;
		return;
	}
	memcpy(Buffer->data() + offset, data, (std::size_t)length);
}


std::size_t SaveNamesClass::Byte_Size(void) const
{
	std::size_t total = sizeof(unsigned short);
	for (NameEntry const & entry : Names) {
		total += 2 + entry.Name.size();
	}
	return(total);
}


void SaveNamesClass::Clear(void)
{
	Names.clear();
	Identifiers.clear();
}


/*
 * The identifier a name and width are known by in this file, minting one the first time
 * the name is met. Two members of different widths that share a name are two entries, so
 * a name means the same shape wherever it is read.
 */
bool SaveNamesClass::Intern(char const * name, unsigned char kind, unsigned short & id)
{
	std::string key = std::to_string((unsigned)kind) + ":" + (name != nullptr ? name : "");

	auto const found = Identifiers.find(key);
	if (found != Identifiers.end()) {
		id = found->second;
		return(true);
	}

	if (Names.size() >= MAX_NAMES || std::strlen(name != nullptr ? name : "") > MAX_NAME_LENGTH) {
		return(false);
	}

	id = (unsigned short)Names.size();
	Names.push_back(NameEntry{name != nullptr ? name : "", kind});
	Identifiers.emplace(std::move(key), id);
	return(true);
}


bool SaveNamesClass::Find(char const * name, unsigned char kind, unsigned short & id) const
{
	std::string const key = std::to_string((unsigned)kind) + ":" + (name != nullptr ? name : "");

	auto const found = Identifiers.find(key);
	if (found == Identifiers.end()) {
		return(false);
	}

	id = found->second;
	return(true);
}


/// <summary>
/// Writes the table the file's members are named through.
/// </summary>
void SaveNamesClass::Write(std::vector<unsigned char> & out) const
{
	unsigned short const count = (unsigned short)Names.size();
	out.push_back((unsigned char)(count & 0xFF));
	out.push_back((unsigned char)(count >> 8));

	for (NameEntry const & entry : Names) {
		out.push_back(entry.Kind);
		out.push_back((unsigned char)entry.Name.size());
		out.insert(out.end(), entry.Name.begin(), entry.Name.end());
	}
}


/// <summary>
/// Reads a table back.
/// </summary>
/// <returns>bool; Was a table of the shape this build writes found?</returns>
bool SaveNamesClass::Read(unsigned char const * data, std::size_t length)
{
	Clear();

	if (data == nullptr || length < sizeof(unsigned short)) {
		return(false);
	}

	std::size_t at = 0;
	unsigned short const count = (unsigned short)(data[0] | (data[1] << 8));
	at += sizeof(unsigned short);

	Names.reserve(count);

	for (unsigned short index = 0; index < count; index++) {
		if (length - at < 2) {
			Clear();
			return(false);
		}

		unsigned char const kind = data[at];
		unsigned char const size = data[at + 1];
		at += 2;

		if (kind != KIND_VARIABLE && kind != 1 && kind != 2 && kind != 4 && kind != 8) {
			Clear();
			return(false);
		}
		if (length - at < size) {
			Clear();
			return(false);
		}

		std::string name((char const *)data + at, size);
		at += size;

		std::string key = std::to_string((unsigned)kind) + ":" + name;
		if (!Identifiers.emplace(std::move(key), index).second) {
			Clear();
			return(false);
		}
		Names.push_back(NameEntry{std::move(name), kind});
	}

	return(true);
}


/*
 * Records where every field of the body reaching to the position given begins. A field
 * named twice in one body is a save no honest writer produced, and stops the pass rather
 * than letting one of the two silently win.
 */
bool SaveStreamClass::Index_Body(BodyFrame & frame, unsigned int end)
{
	unsigned int cursor = Cursor;

	while (cursor < end) {
		if (end - cursor < sizeof(unsigned short)) {
			Fail();
			return(false);
		}

		unsigned short id = (unsigned short)(Buffer->at(cursor) | (Buffer->at(cursor + 1) << 8));
		cursor += sizeof(unsigned short);

		if (!Names->Is_Known(id)) {
			Fail();
			return(false);
		}

		// A field is recorded where its payload begins, which for one the table gives no
		// width to is the length itself, since that is what the reader takes it from.
		unsigned int const start = cursor;
		unsigned int width = Names->Kind_Of(id);

		if (Names->Kind_Of(id) == KIND_VARIABLE) {
			if (end - cursor < sizeof(unsigned int)) {
				Fail();
				return(false);
			}
			width = (unsigned int)Buffer->at(cursor)
				| ((unsigned int)Buffer->at(cursor + 1) << 8)
				| ((unsigned int)Buffer->at(cursor + 2) << 16)
				| ((unsigned int)Buffer->at(cursor + 3) << 24);
			cursor += sizeof(unsigned int);
		}

		if (width > end - cursor) {
			Fail();
			return(false);
		}

		if (!frame.Fields.emplace(id, start).second) {
			Fail();
			return(false);
		}

		cursor += width;
	}

	return(true);
}


/// <summary>
/// Opens the run of named fields one object occupies.
/// A save writes the length once the body is closed; a load reads it, indexes the fields
/// inside and reads them by name from there on.
/// </summary>
void SaveStreamClass::Begin_Frame(bool indexed)
{
	// The frame is pushed whatever happens, so that the End_Body which follows always
	// has one of its own to take and a refused body does not close the one around it.
	BodyFrame frame;
	frame.End = (Mode == MODE_SAVE) ? (unsigned int)Buffer->size() : Cursor;
	frame.Limit = Limit;

	if (Mode == MODE_SAVE) {
		Bodies.push_back(std::move(frame));
		unsigned int length = 0;
		Serialize_Raw(length);
		return;
	}

	unsigned int length = 0;
	Serialize_Raw(length);
	if (Failed || length > Limit - Cursor) {
		if (!Failed) {
			Fail();
		}
		frame.End = Cursor;
		Bodies.push_back(std::move(frame));
		return;
	}

	unsigned int const end = Cursor + length;
	frame.End = end;

	if (indexed && !Index_Body(frame, end)) {
		frame.End = Cursor;
		Bodies.push_back(std::move(frame));
		return;
	}

	Bodies.push_back(std::move(frame));
	Limit = end;
}


/// <summary>
/// Closes the run of fields opened by Begin_Body and leaves the stream after it.
/// </summary>
void SaveStreamClass::End_Frame(void)
{
	if (Bodies.empty()) {
		Fail();
		return;
	}

	BodyFrame const frame = std::move(Bodies.back());
	Bodies.pop_back();

	if (Mode == MODE_SAVE) {
		if (!Failed) {
			unsigned int const length = (unsigned int)Buffer->size() - frame.End - (unsigned int)sizeof(unsigned int);
			Overwrite_Bytes(frame.End, &length, (int)sizeof(length));
		}
		return;
	}

	Cursor = frame.End;
	Limit = frame.Limit;
}


/*
 * Places the stream on one field of the open body. Saving writes the identifier and, for
 * a payload the table does not give a width, room for the length. Loading answers false
 * for a name the file does not carry, which leaves the member as its owner built it.
 */
bool SaveStreamClass::Open_Field(char const * name, unsigned char kind, unsigned int & mark, bool body)
{
	if (Failed) {
		return(false);
	}

	unsigned short id = 0;

	if (Mode == MODE_SAVE) {
		if (!Names->Intern(name, kind, id)) {
			Fail();
			return(false);
		}
	} else if (!Names->Find(name, kind, id)) {
		// A member this file was written without; its owner keeps what it was built with.
		return(false);
	}

	if (Mode == MODE_SAVE) {
		unsigned short written = id;
		Serialize_Raw(written);
		mark = (unsigned int)Buffer->size();
		if (kind == KIND_VARIABLE && !body) {
			unsigned int length = 0;
			Serialize_Raw(length);
		}
		return(!Failed);
	}

	if (Bodies.empty()) {
		Fail();
		return(false);
	}

	auto const found = Bodies.back().Fields.find(id);
	if (found == Bodies.back().Fields.end()) {
		return(false);
	}

	mark = Cursor;
	Cursor = found->second;

	unsigned int width = kind;
	if (kind == KIND_VARIABLE) {
		if (body) {
			// The body reads its own length back, so the cursor stays on it.
			Limit = Bodies.back().End;
			return(true);
		}
		Serialize_Raw(width);
		if (Failed) {
			return(false);
		}
	}

	if (width > Bodies.back().End - Cursor) {
		Fail();
		return(false);
	}

	Limit = Cursor + width;
	return(true);
}


/*
 * Leaves the field opened above, and on a save fills in the length that could not be
 * known until its payload had been written.
 */
void SaveStreamClass::Close_Field(unsigned int mark, bool patch)
{
	if (Mode == MODE_SAVE) {
		if (patch && !Failed) {
			unsigned int const length = (unsigned int)Buffer->size() - mark - (unsigned int)sizeof(unsigned int);
			Overwrite_Bytes(mark, &length, (int)sizeof(length));
		}
		return;
	}

	if (Bodies.empty()) {
		return;
	}

	Cursor = mark;
	Limit = Bodies.back().End;
}


void SaveStreamClass::Begin_Body(void)
{
	Begin_Frame(true);
}


void SaveStreamClass::End_Body(void)
{
	End_Frame();
}


void SaveStreamClass::Begin_Block(void)
{
	Begin_Frame(false);
}


void SaveStreamClass::End_Block(void)
{
	End_Frame();
}
