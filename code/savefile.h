/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "platform/file.h"
#include "platform/filetime.h"

#include <cstdint>
#include <string>
#include <vector>

// The file a saved game is kept in: a fixed header, a table of listing fields, and one
// compressed block of game state. docs/SAVE-FORMAT.md records the layout.
class SaveFileClass
{
	public:
		enum ResultType {
			RESULT_OK,
			RESULT_MISSING,				// No file under that name.
			RESULT_NOT_A_SAVE,			// The file does not begin with the signature.
			RESULT_UNSUPPORTED_VERSION,	// A format version, or a header flag, this build does not read.
			RESULT_CORRUPT,				// A length, checksum or block that does not add up.
			RESULT_WRITE_FAILED,		// The file could not be written or moved into place.
			RESULT_NO_MEMORY,			// The file is within its limits but the process cannot hold it.
			RESULT_TOO_LARGE,			// The content or a listing field is more than a save can hold.
		};

		enum {
			FORMAT_VERSION = 3,
			HEADER_SIZE = 40,
		};

		SaveFileClass(void);

		void Set_String(int id, char const * text);
		void Set_Int(int id, int value);
		void Set_Time(int id, FileTimeType time);
		bool Get_String(int id, char * text, int size) const;
		bool Get_Int(int id, int * value) const;
		bool Get_Time(int id, FileTimeType * time) const;
		void Clear_Fields(void);

		/*
		 * Writing goes forward through the file, one section at a time, so that no more
		 * than the section in hand is ever held in memory. The names the sections and
		 * their members were written under go in last, since only then are they all
		 * known, and the header is filled in once the rest is on disk.
		 */
		ResultType Begin_Write(char const * path);
		ResultType Write_Section(unsigned short id, unsigned char const * data, std::uint32_t length);
		ResultType End_Write(unsigned char const * names, std::uint32_t length);
		void Abandon_Write(void);

		ResultType Read(char const * path);
		ResultType Read_Fields(char const * path);

		// The name table this file was written through, unpacked.
		bool Get_Names(std::vector<unsigned char> & out) const;

		// One section, unpacked. False when the file carries none under that identifier.
		bool Get_Section(unsigned short id, std::vector<unsigned char> & out) const;

		// What the file holds, in the order it holds it.
		std::vector<unsigned short> Section_Ids(void) const;

		static char const * Result_Text(ResultType result);
		static std::uint32_t Checksum(unsigned char const * data, std::uint32_t length, std::uint32_t seed = 0);

		~SaveFileClass(void);

	private:
		enum FieldKind {
			FIELD_STRING = 1,
			FIELD_INT = 2,
			FIELD_TIME = 3,
		};

		// Where one section sits in the file that is open for reading.
		struct SectionType {
			unsigned short ID;
			std::uint32_t Offset;
			std::uint32_t Stored;
			std::uint32_t Unpacked;
		};

		struct FieldType {
			int ID;
			int Kind;
			std::vector<unsigned char> Bytes;
		};

		FieldType const * Find(int id, int kind) const;
		void Set(int id, int kind, void const * data, std::size_t length);
		void Serialize_Fields(std::vector<unsigned char> & table) const;
		ResultType Parse_Fields(unsigned char const * table, std::uint32_t length);
		ResultType Append_Payload(unsigned char const * data, std::uint32_t length);
		bool Unpack(std::uint32_t offset, std::uint32_t stored, std::uint32_t unpacked,
			std::vector<unsigned char> & out) const;

		std::vector<FieldType> Fields;

		// The file open for writing, and what has gone into it so far.
		PlatformFileClass Writing;
		std::string Target;
		std::string Temporary;
		std::uint32_t PayloadAt = 0;
		std::uint32_t PayloadLength = 0;
		std::uint32_t PayloadCRC = 0;
		std::uint32_t TableLength = 0;

		// The file read back, and where its sections and names are inside it.
		std::vector<unsigned char> Image;
		std::vector<SectionType> Sections;
		std::uint32_t NamesAt = 0;
		std::uint32_t NamesStored = 0;
		std::uint32_t NamesUnpacked = 0;
};
