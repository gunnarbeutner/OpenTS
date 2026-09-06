/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Reads the resource directory of a Portable Executable image from
// caller-supplied bytes without the operating system; the language library is a
// resource-only DLL.

#pragma once

#include <cstddef>
#include <cstdint>


// Names a resource type or a resource by number or by name string, as the
// directory does.
class PEResourceNameClass
{
	public:
		PEResourceNameClass(unsigned int id) : ID(id), Name(nullptr) {}
		PEResourceNameClass(char const * name) : ID(0), Name(name) {}

		bool Is_Named(void) const {return(Name != nullptr);}
		unsigned int Get_ID(void) const {return(ID);}
		char const * Get_Name(void) const {return(Name);}

	private:

		unsigned int ID;
		char const * Name;
};


enum PEResourceType {
	PE_RESOURCE_DIALOG = 5,
	PE_RESOURCE_STRING = 6,
	PE_RESOURCE_VERSION = 16
};


// A fetched resource is a pointer into the copied image and lives until Unload
// or destruction.
class PEResourceClass
{
	public:
		PEResourceClass(void);
		~PEResourceClass(void);

		PEResourceClass(PEResourceClass const &) = delete;
		PEResourceClass & operator = (PEResourceClass const &) = delete;

		/// <summary>Copies a whole image and locates its resource directory;
		/// false when the image does not parse or has none.</summary>
		bool Load(void const * image, std::size_t size);

		/// <summary>Copies a directory that stands alone, whose addresses count
		/// from its own start as they do before a linker places its
		/// section.</summary>
		bool Load_Directory(void const * directory, std::size_t size);

		void Unload(void);
		bool Is_Loaded(void) const {return(Image != nullptr);}

		/// <summary>Returns a pointer into the image to the resource, or NULL;
		/// size receives its length when supplied.</summary>
		void const * Fetch_Resource(PEResourceNameClass const & type, PEResourceNameClass const & name,
			std::size_t * size = nullptr) const;

		/// <summary>Copies one string-table entry into buffer, terminated when
		/// there is room.</summary>
		/// <returns>Characters written without the terminator; zero when
		/// absent.</returns>
		int Fetch_String(unsigned int id, char * buffer, int size) const;

		/// <summary>Copies one entry, such as "FileVersion", of the version
		/// resource's first translation; false when absent.</summary>
		bool Fetch_Version_String(char const * key, char * buffer, int size) const;

	private:

		bool Locate_Directory(void);
		std::size_t Offset_For_RVA(std::uint32_t rva) const;

		bool Is_Within(std::size_t offset, std::size_t length) const;
		std::uint16_t Fetch_Short(std::size_t offset) const;
		std::uint32_t Fetch_Long(std::size_t offset) const;

		std::size_t Find_Entry(std::size_t directory, PEResourceNameClass const & wanted) const;
		std::size_t First_Entry(std::size_t directory) const;
		std::size_t Sub_Directory(std::size_t entry) const;
		std::size_t Fetch_Leaf(std::size_t entry, std::size_t * size) const;

		// A standalone directory has no section table; its addresses resolve
		// against the directory itself.
		unsigned char * Image;
		std::size_t Size;
		std::size_t SectionOffset;
		unsigned int SectionCount;
		std::size_t DirectoryOffset;
		bool Standalone;
};
