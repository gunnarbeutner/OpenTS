/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#define INCLUDE_COM
#include "always.h"

#include "side.h"

#include "crc.h"
#include "globals.h"
#include "savestream.h"
#include "sun.h"
#include "tracker.h"


/// <summary>
/// Creates a side and adds it to the global side list.
/// This routine is used as the rules are parsed. The side is given its unique heap
/// identifier so that it can be referred to by the houses that belong to it.
/// </summary>
SideClass::SideClass(char const * ininame) :
	BASECLASS(ininame),
	Houses()
{
	Create_ID();
	Sides.Add(this);
}


/// <summary>
/// Destroys the side and removes it from the game.
/// This routine detaches anything that referred to this side before dropping it from
/// the global side list.
/// </summary>
SideClass::~SideClass(void)
{
	Detach_This_From_All(this);
	Sides.Delete(this);
}


/// <summary>
/// Converts a side name into a side index.
/// This routine is used when the rules are parsed and a side must be resolved from the
/// text name it was written under. The comparison ignores case.
/// </summary>
/// <returns>Returns with the identifier of the matching side, or SIDE_NONE if there is no
/// such side.</returns>
SideType SideClass::From_Name(char const * name)
{
	for (int classid = 0; classid < Sides.Count(); classid++) {
		if (stricmp(Sides[classid]->Name(), name) == 0) {
			return((SideType)classid);
		}
	}

	return(SIDE_NONE);
}


/// <summary>
/// Adds this side to the running game state checksum.
/// This routine is used by the multiplayer sync checking to prove that every machine
/// agrees on the side definitions it was handed.
/// </summary>
void SideClass::Compute_CRC(CRCEngine & crc) const
{
	BASECLASS::Compute_CRC(crc);
	crc(Houses.Count());
}


/// <summary>
/// Fetches the class identifier of this object.
/// This routine is part of the IPersist interface. It is used by the save and load
/// system to recognize what kind of object it is about to create.
/// </summary>
/// <param name="retval">Pointer to the identifier to fill in.</param>
/// <returns>Returns with S_OK, or E_POINTER if no destination was supplied.</returns>
HRESULT SideClass::GetClassID(CLSID * retval)
{
	if (retval == NULL) return(E_POINTER);
	*retval = CLSID_SideClass;
	return(S_OK);
}


/// <summary>
/// Lists the members this side carries.
/// </summary>
/// <param name="stream">The stream carrying the members.</param>
void SideClass::Serialize(SaveStreamClass & stream)
{
	BASECLASS::Serialize(stream);

	stream.Serialize(Houses);
}
