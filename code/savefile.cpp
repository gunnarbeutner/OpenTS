/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "savefile.h"

#include "crc.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <lzo/lzo1x.h>
#include <new>
#include <string>

namespace {

unsigned char const Signature[4] = { 'O', 'T', 'S', 'V' };

constexpr std::uint32_t FLAG_LZO = 0x0001;
constexpr std::uint32_t FIELD_HEADER_SIZE = 8;
constexpr std::uint32_t MAX_FIELD_LENGTH = 0x10000;
// No game state comes near this, and a header asking for more is asking for memory.
constexpr std::uint32_t MAX_CONTENT_LENGTH = 0x10000000;
// A listing is a dozen short fields; a table beyond this is not one.
constexpr std::uint32_t MAX_TABLE_LENGTH = 0x100000;


std::uint32_t Get_U16(unsigned char const * from)
{
	return((std::uint32_t)from[0] | ((std::uint32_t)from[1] << 8));
}


std::uint32_t Get_U32(unsigned char const * from)
{
	return((std::uint32_t)from[0] | ((std::uint32_t)from[1] << 8)
		| ((std::uint32_t)from[2] << 16) | ((std::uint32_t)from[3] << 24));
}


void Put_U16(unsigned char * into, std::uint32_t value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
}


void Put_U32(unsigned char * into, std::uint32_t value)
{
	into[0] = (unsigned char)(value & 0xFF);
	into[1] = (unsigned char)((value >> 8) & 0xFF);
	into[2] = (unsigned char)((value >> 16) & 0xFF);
	into[3] = (unsigned char)((value >> 24) & 0xFF);
}


void Append(std::vector<unsigned char> & into, void const * data, std::size_t length)
{
	unsigned char const * bytes = (unsigned char const *)data;
	into.insert(into.end(), bytes, bytes + length);
}


// Sizes a buffer the header asked for, and says so rather than throw when the process
// cannot hold it.
bool Reserve(std::vector<unsigned char> & buffer, std::size_t length)
{
	try {
		buffer.resize(length);
	} catch (std::bad_alloc const &) {
		buffer.clear();
		return(false);
	}
	return(true);
}


bool Read_Range(PlatformFileClass & file, void * into, std::uint32_t length)
{
	unsigned char * cursor = (unsigned char *)into;

	while (length > 0) {
		std::uint32_t got = 0;
		if (!file.Read(cursor, length, got) || got == 0) return(false);
		cursor += got;
		length -= got;
	}

	return(true);
}


bool Write_Range(PlatformFileClass & file, void const * data, std::uint32_t length)
{
	unsigned char const * cursor = (unsigned char const *)data;

	while (length > 0) {
		std::uint32_t const block = (length > 0x100000) ? 0x100000 : length;
		std::uint32_t written = 0;
		if (!file.Write(cursor, block, written) || written != block) return(false);
		cursor += written;
		length -= written;
	}

	return(true);
}


struct HeaderType {
	std::uint32_t Version;
	std::uint32_t Flags;
	std::uint32_t TableLength;
	std::uint32_t PayloadOffset;
	std::uint32_t PayloadLength;
	std::uint32_t NamesOffset;
	std::uint32_t NamesStored;
	std::uint32_t NamesUnpacked;
	std::uint32_t PayloadCRC;
	std::uint32_t HeaderCRC;
};


// The header checksum continues over the field table, so a listing can verify what it
// reads without touching the content.
std::uint32_t Header_CRC(unsigned char const * header, unsigned char const * table, std::uint32_t length)
{
	return(SaveFileClass::Checksum(table, length, SaveFileClass::Checksum(header, SaveFileClass::HEADER_SIZE - 4)));
}


// Decides everything the first 32 bytes can decide, in the order a caller wants to
// hear about it: not ours, a version we do not read, or damage.
SaveFileClass::ResultType Parse_Header(unsigned char const * bytes, std::uint32_t available, HeaderType & header)
{
	if (available < sizeof(Signature) || memcmp(bytes, Signature, sizeof(Signature)) != 0) {
		return(SaveFileClass::RESULT_NOT_A_SAVE);
	}
	if (available < SaveFileClass::HEADER_SIZE) {
		return(SaveFileClass::RESULT_CORRUPT);
	}

	header.Version = Get_U16(bytes + 4);
	header.Flags = Get_U16(bytes + 6);
	header.TableLength = Get_U32(bytes + 8);
	header.PayloadOffset = Get_U32(bytes + 12);
	header.PayloadLength = Get_U32(bytes + 16);
	header.NamesOffset = Get_U32(bytes + 20);
	header.NamesStored = Get_U32(bytes + 24);
	header.NamesUnpacked = Get_U32(bytes + 28);
	header.PayloadCRC = Get_U32(bytes + 32);
	header.HeaderCRC = Get_U32(bytes + 36);

	if (header.Version == 0 || header.Version > SaveFileClass::FORMAT_VERSION) {
		return(SaveFileClass::RESULT_UNSUPPORTED_VERSION);
	}
	if ((header.Flags & ~FLAG_LZO) != 0) {
		return(SaveFileClass::RESULT_UNSUPPORTED_VERSION);
	}
	if (header.TableLength > MAX_TABLE_LENGTH) {
		return(SaveFileClass::RESULT_CORRUPT);
	}
	if (header.PayloadOffset != SaveFileClass::HEADER_SIZE + header.TableLength) {
		return(SaveFileClass::RESULT_CORRUPT);
	}
	if (header.PayloadLength > MAX_CONTENT_LENGTH || header.NamesUnpacked > MAX_CONTENT_LENGTH) {
		return(SaveFileClass::RESULT_CORRUPT);
	}
	if (header.NamesOffset < header.PayloadOffset
			|| header.NamesStored > header.PayloadLength
			|| header.NamesOffset - header.PayloadOffset > header.PayloadLength - header.NamesStored) {
		return(SaveFileClass::RESULT_CORRUPT);
	}

	return(SaveFileClass::RESULT_OK);
}

}	// namespace


SaveFileClass::SaveFileClass(void)
{
}


std::uint32_t SaveFileClass::Checksum(unsigned char const * data, std::uint32_t length, std::uint32_t seed)
{
	return(CRC::Memory(data, length, seed));
}


char const * SaveFileClass::Result_Text(ResultType result)
{
	switch (result) {
		case RESULT_OK: return("ok");
		case RESULT_MISSING: return("the file is missing");
		case RESULT_NOT_A_SAVE: return("the file is not a saved game");
		case RESULT_UNSUPPORTED_VERSION: return("the file uses a format version this build does not read");
		case RESULT_CORRUPT: return("the file is damaged");
		case RESULT_WRITE_FAILED: return("the file could not be written");
		case RESULT_NO_MEMORY: return("there is not enough memory to read the file");
		case RESULT_TOO_LARGE: return("the game state is larger than a saved game can hold");
	}
	return("unknown");
}


SaveFileClass::FieldType const * SaveFileClass::Find(int id, int kind) const
{
	for (FieldType const & field : Fields) {
		if (field.ID == id && field.Kind == kind) return(&field);
	}
	return(nullptr);
}


void SaveFileClass::Set(int id, int kind, void const * data, std::size_t length)
{
	for (FieldType & field : Fields) {
		if (field.ID == id && field.Kind == kind) {
			field.Bytes.assign((unsigned char const *)data, (unsigned char const *)data + length);
			return;
		}
	}

	FieldType field;
	field.ID = id;
	field.Kind = kind;
	field.Bytes.assign((unsigned char const *)data, (unsigned char const *)data + length);
	Fields.push_back(field);
}


void SaveFileClass::Set_String(int id, char const * text)
{
	if (text == nullptr) text = "";
	Set(id, FIELD_STRING, text, strlen(text));
}


void SaveFileClass::Set_Int(int id, int value)
{
	unsigned char bytes[4];
	Put_U32(bytes, (std::uint32_t)value);
	Set(id, FIELD_INT, bytes, sizeof(bytes));
}


void SaveFileClass::Set_Time(int id, FileTimeType time)
{
	unsigned char bytes[8];
	Put_U32(bytes, time.Low());
	Put_U32(bytes + 4, time.High());
	Set(id, FIELD_TIME, bytes, sizeof(bytes));
}


// A string that does not fit is truncated to what does; the result is always terminated.
bool SaveFileClass::Get_String(int id, char * text, int size) const
{
	if (text == nullptr || size <= 0) return(false);

	FieldType const * const field = Find(id, FIELD_STRING);
	if (field == nullptr) {
		text[0] = '\0';
		return(false);
	}

	std::size_t length = field->Bytes.size();
	if (length > (std::size_t)(size - 1)) {
		// A cut never splits a UTF-8 sequence, so a shortened description stays text.
		length = (std::size_t)(size - 1);
		while (length > 0 && (field->Bytes[length] & 0xC0) == 0x80) length--;
	}
	memcpy(text, field->Bytes.data(), length);
	text[length] = '\0';

	return(true);
}


bool SaveFileClass::Get_Int(int id, int * value) const
{
	FieldType const * const field = Find(id, FIELD_INT);
	if (field == nullptr || field->Bytes.size() != 4) return(false);

	if (value != nullptr) *value = (int)Get_U32(field->Bytes.data());
	return(true);
}


bool SaveFileClass::Get_Time(int id, FileTimeType * time) const
{
	FieldType const * const field = Find(id, FIELD_TIME);
	if (field == nullptr || field->Bytes.size() != 8) return(false);

	if (time != nullptr) {
		*time = FileTimeType::From_Parts(Get_U32(field->Bytes.data()), Get_U32(field->Bytes.data() + 4));
	}
	return(true);
}


void SaveFileClass::Clear_Fields(void)
{
	Fields.clear();
}


void SaveFileClass::Serialize_Fields(std::vector<unsigned char> & table) const
{
	table.clear();

	for (FieldType const & field : Fields) {
		unsigned char head[FIELD_HEADER_SIZE];
		Put_U16(head, (std::uint32_t)field.ID);
		Put_U16(head + 2, (std::uint32_t)field.Kind);
		Put_U32(head + 4, (std::uint32_t)field.Bytes.size());
		Append(table, head, sizeof(head));
		Append(table, field.Bytes.data(), field.Bytes.size());
	}
}


SaveFileClass::ResultType SaveFileClass::Parse_Fields(unsigned char const * table, std::uint32_t length)
{
	Fields.clear();

	std::uint32_t offset = 0;
	while (offset < length) {
		if (length - offset < FIELD_HEADER_SIZE) return(RESULT_CORRUPT);

		FieldType field;
		field.ID = (int)Get_U16(table + offset);
		field.Kind = (int)Get_U16(table + offset + 2);
		std::uint32_t const bytes = Get_U32(table + offset + 4);
		offset += FIELD_HEADER_SIZE;

		if (bytes > MAX_FIELD_LENGTH || bytes > length - offset) return(RESULT_CORRUPT);
		field.Bytes.assign(table + offset, table + offset + bytes);
		offset += bytes;

		Fields.push_back(field);
	}

	return(RESULT_OK);
}


// The file lands under its final name only once every byte is on disk, so a save
// interrupted at any point leaves the previous file untouched.
SaveFileClass::~SaveFileClass(void)
{
	Abandon_Write();
}


// Every byte of the payload passes here, since its checksum is accumulated as it goes.
SaveFileClass::ResultType SaveFileClass::Append_Payload(unsigned char const * data, std::uint32_t length)
{
	if (!Writing.Is_Open()) {
		return(RESULT_WRITE_FAILED);
	}
	if (length > MAX_CONTENT_LENGTH - PayloadLength) {
		return(RESULT_TOO_LARGE);
	}
	if (!Write_Range(Writing, data, length)) {
		return(RESULT_WRITE_FAILED);
	}

	PayloadCRC = Checksum(data, length, PayloadCRC);
	PayloadLength += length;
	return(RESULT_OK);
}


/// <summary>
/// Opens a save for writing and puts the listing fields in it. The header is left as room
/// to fill in, since what belongs in it is known only once everything else is written.
/// </summary>
SaveFileClass::ResultType SaveFileClass::Begin_Write(char const * path)
{
	Abandon_Write();

	if (path == nullptr) {
		return(RESULT_WRITE_FAILED);
	}

	for (FieldType const & field : Fields) {
		if (field.Bytes.size() > MAX_FIELD_LENGTH) return(RESULT_TOO_LARGE);
	}

	std::vector<unsigned char> table;
	Serialize_Fields(table);
	if (table.size() > MAX_TABLE_LENGTH) return(RESULT_TOO_LARGE);

	Target = path;
	Temporary = Target + ".tmp";

	if (!Writing.Open(Temporary.c_str(), PlatformOpenType::WRITE)) {
		return(RESULT_WRITE_FAILED);
	}

	TableLength = (std::uint32_t)table.size();
	PayloadAt = HEADER_SIZE + TableLength;
	PayloadLength = 0;
	PayloadCRC = 0;
	NamesAt = 0;
	NamesStored = 0;
	NamesUnpacked = 0;

	std::vector<unsigned char> const room(HEADER_SIZE, 0);
	if (!Write_Range(Writing, room.data(), HEADER_SIZE)
			|| !Write_Range(Writing, table.data(), TableLength)) {
		Abandon_Write();
		return(RESULT_WRITE_FAILED);
	}

	return(RESULT_OK);
}


// Compressed only where that makes it smaller, which the caller's two lengths tell apart.
static bool Pack(unsigned char const * data, std::uint32_t length, std::vector<unsigned char> & out)
{
	out.clear();
	if (length == 0) {
		return(true);
	}

	// Sized rather than thrown for, as everything else the writer asks memory for is.
	std::vector<unsigned char> work;
	if (!Reserve(work, LZO1X_MEM_COMPRESS)
			|| !Reserve(out, (std::size_t)length + length / 16 + 64 + 3)) {
		return(false);
	}

	lzo_uint packed = 0;
	int const status = lzo1x_1_compress(data, (lzo_uint)length, out.data(), &packed, work.data());

	if (status == LZO_E_OK && packed < length) {
		out.resize((std::size_t)packed);
		return(true);
	}

	out.assign(data, data + length);
	return(true);
}


/// <summary>
/// Puts one section in the file under the identifier its name has in the table.
/// </summary>
SaveFileClass::ResultType SaveFileClass::Write_Section(unsigned short id, unsigned char const * data,
	std::uint32_t length)
{
	if (!Writing.Is_Open()) return(RESULT_WRITE_FAILED);
	if (length > MAX_CONTENT_LENGTH) return(RESULT_TOO_LARGE);

	std::vector<unsigned char> stored;
	if (!Pack(data, length, stored)) {
		return(RESULT_NO_MEMORY);
	}

	unsigned char entry[10];
	Put_U16(entry, id);
	Put_U32(entry + 2, (std::uint32_t)stored.size());
	Put_U32(entry + 6, length);

	ResultType result = Append_Payload(entry, sizeof(entry));
	if (result != RESULT_OK) return(result);

	return(Append_Payload(stored.data(), (std::uint32_t)stored.size()));
}


/// <summary>
/// Writes the name table, fills the header in, and moves the file into place.
/// </summary>
SaveFileClass::ResultType SaveFileClass::End_Write(unsigned char const * names, std::uint32_t length)
{
	if (!Writing.Is_Open()) return(RESULT_WRITE_FAILED);

	std::vector<unsigned char> stored;
	if (!Pack(names, length, stored)) {
		Abandon_Write();
		return(RESULT_NO_MEMORY);
	}

	NamesAt = PayloadAt + PayloadLength;
	NamesStored = (std::uint32_t)stored.size();
	NamesUnpacked = length;

	ResultType result = Append_Payload(stored.data(), NamesStored);
	if (result != RESULT_OK) {
		Abandon_Write();
		return(result);
	}

	std::vector<unsigned char> table;
	Serialize_Fields(table);

	unsigned char header[HEADER_SIZE];
	memset(header, 0, sizeof(header));
	memcpy(header, Signature, sizeof(Signature));
	Put_U16(header + 4, FORMAT_VERSION);
	Put_U16(header + 6, 0);
	Put_U32(header + 8, TableLength);
	Put_U32(header + 12, PayloadAt);
	Put_U32(header + 16, PayloadLength);
	Put_U32(header + 20, NamesAt);
	Put_U32(header + 24, NamesStored);
	Put_U32(header + 28, NamesUnpacked);
	Put_U32(header + 32, PayloadCRC);
	Put_U32(header + 36, Header_CRC(header, table.data(), TableLength));

	bool ok = (Writing.Seek(0, SEEK_SET) == 0);
	if (ok) ok = Write_Range(Writing, header, HEADER_SIZE);
	if (ok) ok = Writing.Flush();
	if (!Writing.Close()) ok = false;

	if (ok) ok = Platform_Replace_File(Temporary.c_str(), Target.c_str());

	if (!ok) {
		Platform_Remove_File(Temporary.c_str());
		return(RESULT_WRITE_FAILED);
	}

	return(RESULT_OK);
}


/// <summary>
/// Gives up on a save being written, leaving whatever was under the name untouched.
/// </summary>
void SaveFileClass::Abandon_Write(void)
{
	if (!Writing.Is_Open()) {
		return;
	}

	Writing.Close();
	Platform_Remove_File(Temporary.c_str());
}


// A run whose two lengths agree was stored as it is; anything else went through LZO.
bool SaveFileClass::Unpack(std::uint32_t offset, std::uint32_t stored, std::uint32_t unpacked,
	std::vector<unsigned char> & out) const
{
	if (offset > Image.size() || stored > Image.size() - offset) {
		return(false);
	}

	try {
		out.resize(unpacked);
	} catch (std::bad_alloc const &) {
		return(false);
	}

	if (stored == unpacked) {
		memcpy(out.data(), Image.data() + offset, unpacked);
		return(true);
	}

	lzo_uint length = unpacked;
	int const status = lzo1x_decompress_safe(Image.data() + offset, (lzo_uint)stored,
		out.data(), &length, nullptr);

	return(status == LZO_E_OK && length == unpacked);
}


bool SaveFileClass::Get_Names(std::vector<unsigned char> & out) const
{
	return(Unpack(NamesAt, NamesStored, NamesUnpacked, out));
}


bool SaveFileClass::Get_Section(unsigned short id, std::vector<unsigned char> & out) const
{
	for (SectionType const & section : Sections) {
		if (section.ID == id) {
			return(Unpack(section.Offset, section.Stored, section.Unpacked, out));
		}
	}
	return(false);
}


std::vector<unsigned short> SaveFileClass::Section_Ids(void) const
{
	std::vector<unsigned short> ids;
	ids.reserve(Sections.size());
	for (SectionType const & section : Sections) {
		ids.push_back(section.ID);
	}
	return(ids);
}


/// <summary>
/// Reads the header, the listing fields and where every section sits. A section is
/// unpacked only when it is asked for.
/// </summary>
SaveFileClass::ResultType SaveFileClass::Read(char const * path)
{
	Fields.clear();
	Sections.clear();
	Image.clear();

	if (path == nullptr) return(RESULT_MISSING);

	PlatformFileClass file;
	if (!file.Open(path, PlatformOpenType::READ)) return(RESULT_MISSING);

	std::int64_t const whole = file.Size();
	bool ok = (whole >= 0 && whole <= (std::int64_t)0xFFFFFFFE);
	std::uint32_t const size = ok ? (std::uint32_t)whole : 0;

	if (ok) {
		try {
			Image.resize(size);
		} catch (std::bad_alloc const &) {
			return(RESULT_NO_MEMORY);
		}
		ok = Read_Range(file, Image.data(), size);
	}

	file.Close();

	if (!ok) {
		Image.clear();
		return(RESULT_CORRUPT);
	}

	HeaderType header;
	ResultType result = Parse_Header(Image.data(), size, header);
	if (result != RESULT_OK) {
		Image.clear();
		return(result);
	}

	if (size != header.PayloadOffset + header.PayloadLength) {
		Image.clear();
		return(RESULT_CORRUPT);
	}

	if (Header_CRC(Image.data(), Image.data() + HEADER_SIZE, header.TableLength) != header.HeaderCRC) {
		Image.clear();
		return(RESULT_CORRUPT);
	}

	result = Parse_Fields(Image.data() + HEADER_SIZE, header.TableLength);
	if (result != RESULT_OK) {
		Image.clear();
		return(result);
	}

	if (Checksum(Image.data() + header.PayloadOffset, header.PayloadLength) != header.PayloadCRC) {
		Fields.clear();
		Image.clear();
		return(RESULT_CORRUPT);
	}

	// The sections run from the payload to the names, which are last.
	std::uint32_t at = header.PayloadOffset;
	while (at < header.NamesOffset) {
		if (header.NamesOffset - at < 10) {
			Fields.clear();
			Image.clear();
			return(RESULT_CORRUPT);
		}

		SectionType section;
		section.ID = (unsigned short)Get_U16(Image.data() + at);
		section.Stored = Get_U32(Image.data() + at + 2);
		section.Unpacked = Get_U32(Image.data() + at + 6);
		section.Offset = at + 10;

		if (section.Unpacked > MAX_CONTENT_LENGTH || section.Stored > header.NamesOffset - section.Offset) {
			Fields.clear();
			Image.clear();
			return(RESULT_CORRUPT);
		}

		Sections.push_back(section);
		at = section.Offset + section.Stored;
	}

	NamesAt = header.NamesOffset;
	NamesStored = header.NamesStored;
	NamesUnpacked = header.NamesUnpacked;

	return(RESULT_OK);
}


SaveFileClass::ResultType SaveFileClass::Read_Fields(char const * path)
{
	Fields.clear();

	if (path == nullptr) return(RESULT_MISSING);

	PlatformFileClass file;
	if (!file.Open(path, PlatformOpenType::READ)) return(RESULT_MISSING);

	unsigned char head[HEADER_SIZE];
	std::uint32_t got = 0;
	bool ok = file.Read(head, HEADER_SIZE, got);

	HeaderType header;
	ResultType result = ok ? Parse_Header(head, got, header) : RESULT_CORRUPT;

	std::vector<unsigned char> table;
	if (result == RESULT_OK && header.TableLength > 0) {
		std::int64_t const size = file.Size();
		if (size < 0 || header.TableLength > size - HEADER_SIZE) {
			result = RESULT_CORRUPT;
		} else if (!Reserve(table, header.TableLength)) {
			result = RESULT_NO_MEMORY;
		} else {
			if (!Read_Range(file, table.data(), header.TableLength)) result = RESULT_CORRUPT;
		}
	}
	file.Close();

	if (result != RESULT_OK) return(result);
	if (Header_CRC(head, table.data(), (std::uint32_t)table.size()) != header.HeaderCRC) return(RESULT_CORRUPT);

	return(Parse_Fields(table.data(), (std::uint32_t)table.size()));
}
