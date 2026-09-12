/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /G/wwlib/data.cpp                                           $*
 *                                                                                             *
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             *
 *                     $Modtime:: 9/24/99 4:52p                                               $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Load_Alloc_Data -- Allocates a buffer and loads the file into it.                         *
 *   Load_Uncompress -- Loads and uncompresses data to a buffer.                               *
 *   Hires_Load -- Allocates memory for, and loads, a resolution dependant file.               *
 *   Fetch_String -- Fetches a string resource.                                                *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "data.h"

#include "languagestrings.h"
#include "opents_version.h"
#include "utf8.h"


/***********************************************************************************************
 * Load_Alloc_Data -- Allocates a buffer and loads the file into it.                           *
 *                                                                                             *
 *    This is the C++ replacement for the Load_Alloc_Data function. It will allocate the       *
 *    memory big enough to hold the file and then read the file into it.                       *
 *                                                                                             *
 * INPUT:   file  -- The file to read.                                                         *
 *                                                                                             *
 *          mem   -- The memory system to use for allocation.                                  *
 *                                                                                             *
 * OUTPUT:  Returns with a pointer to the allocated and filled memory block.                   *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/17/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void * Load_Alloc_Data(FileClass & file)
{
	void * ptr = NULL;
	if (file.Is_Available()) {
		int size = file.Size();

		ptr = new char[size];
		if (ptr != NULL) {
			file.Read(ptr, size);
		}
	}
	return(ptr);
}


/***********************************************************************************************
 * Load_Uncompress -- Loads and uncompresses data to a buffer.                                 *
 *                                                                                             *
 *    This is the C++ counterpart to the Load_Uncompress function. It will load the file       *
 *    specified into the graphic buffer indicated and uncompress it.                           *
 *                                                                                             *
 * INPUT:   file     -- The file to load and uncompress.                                       *
 *                                                                                             *
 *          uncomp_buff -- The graphic buffer that initial loading will use.                   *
 *                                                                                             *
 *          dest_buff   -- The buffer that will hold the uncompressed data.                    *
 *                                                                                             *
 *          reserved_data  -- This is an optional pointer to a buffer that will hold any       *
 *                            reserved data the compressed file may contain. This is           *
 *                            typically a palette.                                             *
 *                                                                                             *
 * OUTPUT:  Returns with the size of the uncompressed data in the destination buffer.          *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/17/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
int Load_Uncompress(FileClass & file, Buffer & uncomp_buff, Buffer & dest_buff, void * reserved_data)
{
	unsigned short	size;
	void	* sptr = uncomp_buff.Get_Buffer();
	void	* dptr = dest_buff.Get_Buffer();
	int	opened = false;
	CompHeaderType	header;

	/*
	**	The file must be opened in order to be read from. If the file
	**	isn't opened, then open it. Record this fact so that it can be
	**	restored to its closed state at the end.
	*/
	if (!file.Is_Open()) {
		if (!file.Open()) {
			return(0);
		}
		opened = true;
	}

	/*
	**	Read in the size of the file (supposedly).
	*/
	file.Read(&size, sizeof(size));

	/*
	**	Read in the header block. This block contains the compression type
	**	and skip data (among other things).
	*/
	file.Read(&header, sizeof(header));
	size -= (unsigned short)sizeof(header);

	/*
	**	If there are skip bytes then they must be processed. Either read
	**	them into the buffer provided or skip past them. No check is made
	**	to ensure that the reserved data buffer is big enough (watch out!).
	*/
	if (header.Skip) {
		size -= header.Skip;
		if (reserved_data) {
			file.Read(reserved_data, header.Skip);
		} else {
			file.Seek(header.Skip, SEEK_CUR);
		}
		header.Skip = 0;
	}

	/*
	**	Determine where is the proper place to load the data. If both buffers
	**	specified are identical, then the data should be loaded at the end of
	**	the buffer and decompressed at the beginning.
	*/
	if (uncomp_buff.Get_Buffer() == dest_buff.Get_Buffer()) {
		sptr = (char *)sptr + uncomp_buff.Get_Size()-(size+sizeof(header));
	}

	/*
	**	Read in the bulk of the data.
	*/
	memmove(sptr, &header, sizeof(header));
//	Mem_Copy(&header, sptr, sizeof(header));
	file.Read((char *)sptr + sizeof(header), size);

	/*
	**	Decompress the data.
	*/
	size = (unsigned short) Uncompress_Data(sptr, dptr);

	/*
	**	Close the file if necessary.
	*/
	if (opened) {
		file.Close();
	}
	return((int)size);
}


/***********************************************************************************************
 * Fetch_String -- Fetches a string resource.                                                  *
 *                                                                                             *
 *    Fetches a string resource and returns a pointer to its text.                             *
 *                                                                                             *
 * INPUT:   id -- The ID number of the string resource to fetch.                               *
 *                                                                                             *
 * OUTPUT:  Returns with a pointer to the actual text of the string resource.                  *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/25/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
char const * Fetch_String(int id)
{
	if (id == -1 || id == TXT_NONE) return("");

	char const * const text = Language_String((unsigned int)id);
	return(text != nullptr ? text : "");
}


/// <summary>
/// Loads a compressed picture into the buffer specified.
/// This routine is a convenience layer over Load_Uncompress for callers that deal in whole
/// screen images and would rather think in pages than in bytes.
/// </summary>
/// <param name="palette">Buffer to fill in with the picture's palette, or NULL to discard
/// it.</param>
/// <returns>Returns with the number of pages the loaded picture occupies.</returns>
int Load_Picture(FileClass & file, Buffer & scratchbuf, Buffer & destbuf, unsigned char * palette, PicturePlaneType )
{
	return(Load_Uncompress(file, scratchbuf, destbuf,  palette ) / 8000);
}


/***********************************************************************************************
 * Hires_Load -- Allocates memory for, and loads, a resolution dependant file.                 *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Name of file to load                                                              *
 *                                                                                             *
 * OUTPUT:   Ptr to loaded file                                                                *
 *                                                                                             *
 * WARNINGS: Caller is responsible for releasing the memory allocated                          *
 *                                                                                             *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    5/13/96 3:20PM ST : Created                                                              *
 *=============================================================================================*/
void * Hires_Load(FileClass & file)
{
	int 	length;
	void * return_ptr;

	if (file.Is_Available()) {

		length = file.Size();
		return_ptr = new char[length];
		file.Read(return_ptr, length);
		return(return_ptr);

	} else {
		return(NULL);
	}
}


/// <summary>
/// Fills the buffer with a line naming the language pack and its version, for the version
/// information screen. The text is the same on every target and does not depend on whether
/// the language resources have loaded.
/// </summary>
/// <param name="version_string">Buffer to fill in with the description.</param>
/// <remarks>Be sure that the destination buffer is big enough to hold the composed text.</remarks>
void Get_Language_Version(char *version_string)
{
	if (version_string != nullptr) {
		strcpy(version_string, "Language: English Resources " OPENTS_VERSION);
	}
}
