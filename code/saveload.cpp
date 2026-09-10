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

/* $Header: /counterstrike/SAVELOAD.CPP 9     3/17/97 1:04a Steve_tall $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : SAVELOAD.CPP                                                 *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : August 23, 1994                                              *
 *                                                                                             *
 *                  Last Update : July 8, 1996 [JLB]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Code_All_Pointers -- Code all pointers.                                                   *
 *   Decode_All_Pointers -- Decodes all pointers.                                              *
 *   Get_Savefile_Info -- gets description, scenario #, house                                  *
 *   Load_Game -- loads a saved game                                                           *
 *   Load_MPlayer_Values -- Loads multiplayer-specific values                                  *
 *   Load_Misc_Values -- loads miscellaneous variables                                         *
 *   MPlayer_Save_Message -- pops up a "saving..." message                                     *
 *   Put_All -- Store all save game data to the pipe.                                          *
 *   Reconcile_Players -- Reconciles loaded data with the 'Players' vector                     *
 *   Save_Game -- saves a game to disk                                                         *
 *   Save_MPlayer_Values -- Saves multiplayer-specific values                                  *
 *   Save_Misc_Values -- saves miscellaneous variables                                         *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "saveload.h"

#include "_deploymentconfig.h"
#include "_logic.h"
#include "_map.h"
#include "_rect.h"
#include "_rules.h"
#include "_script.h"
#include "_tactica.h"
#include "_vanim.h"
#include "_warhead.h"
#include "_weapon.h"
#include "aircraft.h"
#include "airctype.h"
#include "aitrig.h"
#include "alphashp.h"
#include "anim.h"
#include "animtype.h"
#include "blight.h"
#include "building.h"
#include "builtype.h"
#include "bullet.h"
#include "bullettype.h"
#include "classfactory.h"
#include "data.h"
#include "dbgprint.h"
#include "deploymentconfig.h"
#include "empulse.h"
#include "enviro.h"
#include "factory.h"
#include "fog.h"
#include "gamedirs.h"
#include "globals.h"
#include "goptions.h"
#include "houstype.h"
#include "infantry.h"
#include "infatype.h"
#include "init.h"
#include "ion.h"
#include "language/language.h"
#include "loaddlg.h"
#include "light.h"
#include "logic.h"
#include "overlay.h"
#include "overtype.h"
#include "ovrlight.h"
#include "particle.h"
#include "partsys.h"
#include "persist.h"
#include "platform/filetime.h"
#include "psystype.h"
#include "ptype.h"
#include "revent.h"
#include "rules.h"
#include "savefile.h"
#include "savemgr.h"
#include "savestream.h"
#include "savever.h"
#include "scenario.h"
#include "screenlayout.h"
#include "script.h"
#include "session.h"
#include "side.h"
#include "sidebar.h"
#include "smudtype.h"
#include "stimer.h"
#include "sun.h"
#include "super.h"
#include "suprtype.h"
#include "swizzle.h"
#include "syncrechook.h"
#include "syncreport.h"
#include "tactical.h"
#include "taction.h"
#include "tag.h"
#include "tagtype.h"
#include "taskforc.h"
#include "team.h"
#include "teamtype.h"
#include "terrain.h"
#include "terrtype.h"
#include "tevent.h"
#include "tiberium.h"
#include "trigger.h"
#include "trigtype.h"
#include "tube.h"
#include "tutorial.h"
#include "unit.h"
#include "unittype.h"
#include "vanim.h"
#include "vanimtype.h"
#include "vein.h"
#include "vox.h"
#include "warhead.h"
#include "ambient.h"
#include "voc.h"
#include "wave.h"
#include "waypoint.h"
#include "weapon.h"

#include "objheaps.hh"

#include <memory>
#include <new>
#include <stdexcept>
#include <string>

//#define	SAVE_BLOCK_SIZE	512
#define	SAVE_BLOCK_SIZE	4096
//#define	SAVE_BLOCK_SIZE	1024

/*
********************************** Defines **********************************
*/
unsigned int ExpectedGameVersion = LoadOptionsClass::GAMEVER_OPENTS;


/// <summary>
/// Writes one object to the save stream as a record of its own. A reader that does not
/// consume exactly the record's length has read a record of another shape than was
/// written, and a missing object fails the stream rather than leaving a gap where the
/// reader expects one.
/// </summary>
/// <returns>bool; Was the record written whole?</returns>
bool Save_Object(SaveStreamClass & stream, IPersistent * persist)
{
	if (persist == nullptr) {
		stream.Fail();
		return(false);
	}

	ClassID classid = persist->Class_ID();
	stream.Serialize_Bytes(&classid, sizeof(classid));
	unsigned int const lengthat = stream.Offset();
	unsigned int length = 0;
	stream.Serialize_Raw(length);
	unsigned int const start = stream.Offset();

	bool result = persist->Save(stream, true);
	if (!result) {
		return(false);
	}

	length = stream.Offset() - start;
	stream.Overwrite_Bytes(lengthat, &length, sizeof(length));
	return(!stream.Was_Error());
}


bool Save_Object(SaveStreamClass & stream, ILocomotion * locomotion)
{
	IPersistent * const persist = dynamic_cast<IPersistent *>(locomotion);
	if (persist == nullptr) {
		stream.Fail();
		return(false);
	}
	return(Save_Object(stream, persist));
}


/// <summary>
/// Recreates one object from the save stream. It reattaches itself to its own heap as it
/// is constructed, so the caller is handed it only to keep or to refuse.
/// </summary>
/// <param name="accepts">Asked whether the object is of the class the caller expects, once
/// the record has been read and before the object takes its place. May be null when any
/// class will do.</param>
/// <returns>The object, owned by the caller, or nothing with the stream failed when the
/// identifier names no registered class, the object could not read its record, the record's
/// length does not match what the object consumed, or the class is not the one asked
/// for.</returns>
std::unique_ptr<IPersistent> Load_Object(SaveStreamClass & stream, bool (*accepts)(IPersistent const * object))
{
	ClassID classid;
	unsigned int length = 0;
	stream.Serialize_Bytes(&classid, sizeof(classid));
	stream.Serialize_Raw(length);
	if (stream.Was_Error()) {
		return(nullptr);
	}

	unsigned int const start = stream.Offset();
	if (length > stream.Size() - start) {
		DebugString("Save record at %u claims %u bytes, past the end of the save\n", start, length);
		stream.Fail();
		return(nullptr);
	}

	SwizzleManagerClass::MarkType const mark = Swizzler.Mark();
	std::unique_ptr<IPersistent> persist = Create_Object(classid);
	if (persist == nullptr) {
		DebugString("Save record at %u names a class this build does not register\n", start);
		stream.Fail();
		return(nullptr);
	}

	bool ok;
	{
		SaveStreamClass::BoundScope const bound(stream, start + length);
		ok = persist->Load(stream);
	}
	if (ok && stream.Offset() != start + length) {
		DebugString("Save record of %s at %u is %u bytes but %u were read\n",
			typeid(*persist).name(), start, length, stream.Offset() - start);
		ok = false;
	}
	if (ok && accepts != nullptr && !accepts(persist.get())) {
		DebugString("Save record of %s at %u is not the class expected there\n",
			typeid(*persist).name(), start);
		ok = false;
	}
	if (!ok) {
		Swizzler.Abandon(mark);
		stream.Fail();
		return(nullptr);
	}

	persist->Post_Load();
	return(persist);
}


/// <summary>
/// Loads a vector of persistent objects from the save game stream.
/// The objects are not handed back -- each one reattaches itself to its own heap as it is
/// constructed, which is what refills the game's vectors. A record naming any class other
/// than the heap's fails the load, since nothing else belongs in that heap.
/// </summary>
/// <returns>bool; Was the record read whole?</returns>
template<class T>
static bool Load_Vector(SaveStreamClass & stream)
{
	bool whole = true;

	// The heap travels inside a body of its own, so a reader steps over the whole of it
	// rather than losing its place in whatever follows.
	stream.Block([&]{
		int count = 0;
		stream.Serialize_Raw(count);
		if (stream.Was_Error()) {
			whole = false;
			return;
		}
		if (count < 0) {
			stream.Fail();
			whole = false;
			return;
		}

		for (int index = 0; index < count; index++) {
			std::unique_ptr<T> object = Load_Object_As<T>(stream);
			if (object == nullptr) {
				whole = false;
				return;
			}
			// The object attached itself to its own heap as it was constructed, and the heap
			// is what deletes it from here on.
			object.release();
		}
	});

	return(whole);
}


/// <summary>
/// Saves a vector of persistent objects to the save game stream.
/// </summary>
/// <returns>bool; Was the record read whole?</returns>
template<class T>
static bool Save_Vector(SaveStreamClass & stream, const DynamicVectorClass<T> &list)
{
	bool whole = true;

	stream.Block([&]{
		int count = list.Count();
		stream.Serialize_Raw(count);

		for (int index = 0; index < count; index++) {
			bool const result = Save_Object(stream, list[index]);
			if (!result) {
				whole = false;
				return;
			}
		}
	});

	return(whole && !stream.Was_Error());
}


/// <summary>
/// Builds a checksum over the whole of the game object state.
/// This routine walks the scenario and every object and type heap, folding each one's own
/// contribution into a single engine. It is used to compare the state held by two
/// machines in a networked game, so that a desynchronization can be spotted.
/// </summary>
/// <returns>Returns with the checksum engine holding the accumulated state.</returns>
CRCEngine Object_CRCs(void)
{
	int i;
	CRCEngine crc;
	Scen->Compute_CRC(crc);

#define DO_OBJ_CRC(VECTOR) \
	for (i = 0; i < VECTOR.Count(); i++) { \
		VECTOR[i]->Compute_CRC(crc); \
	}

	OBJECT_HEAP_LIST(DO_OBJ_CRC)

#undef DO_OBJ_CRC

	if (PlayerPtr != NULL) {
		crc(PlayerPtr->HeapID);
	}
	crc((int)Frame);
	crc(CurrentObject.Count());
	return(crc);
}


/// <summary>
/// Writes a per-heap checksum table to the out-of-sync report: one summary line per heap, then
/// a row per live object keyed by its stable identifier. Unlike Object_CRCs this folds in no
/// per-machine state, so two peers' tables are directly comparable.
/// </summary>
void Print_Heap_CRCs(FILE * fp)
{
	int i;

	fprintf(fp, "\n----- Heap checksums -----\n");

#define HEAP_SUMMARY(VECTOR) \
	{ \
		CRCEngine heap_crc; \
		for (i = 0; i < VECTOR.Count(); i++) { \
			VECTOR[i]->Compute_CRC(heap_crc); \
		} \
		fprintf(fp, "%-28s count=%-5d crc=%08x\n", #VECTOR, VECTOR.Count(), heap_crc()); \
	}

	OBJECT_HEAP_LIST(HEAP_SUMMARY)

#undef HEAP_SUMMARY

#define HEAP_ROWS(VECTOR) \
	{ \
		CRCEngine heap_crc; \
		fprintf(fp, "\n--- %s ---\n", #VECTOR); \
		for (i = 0; i < VECTOR.Count(); i++) { \
			VECTOR[i]->Compute_CRC(heap_crc); \
			fprintf(fp, "%05d  ID:%-8d  %08x\n", i, VECTOR[i]->Fetch_ID(), heap_crc()); \
		} \
	}

	OBJECT_HEAP_LIST_INSTANCES(HEAP_ROWS)

#undef HEAP_ROWS
}


/*
 * A section body says whether it worked, or says nothing and is taken at its word.
 */
template<typename F>
static bool Ran(F && body, SaveStreamClass & stream)
{
	if constexpr (std::is_void_v<decltype(body(stream))>) {
		body(stream);
		return(true);
	} else {
		return(body(stream));
	}
}


/*
 * One section of a save, under the name the file records it by. Each is written through a
 * stream of its own, so no more than the section in hand is ever held in memory, and the
 * order the sections are written in is not what a load finds them by.
 */
template<typename F>
static bool Save_Section(SaveFileClass & file, SaveNamesClass & names, char const * name, F && body)
{
	unsigned short id = 0;
	if (!names.Intern(name, SaveNamesClass::KIND_VARIABLE, id)) {
		return(false);
	}

	std::vector<unsigned char> bytes;
	SaveStreamClass stream(bytes, SaveStreamClass::MODE_SAVE, names);

	if (!Ran(body, stream) || stream.Was_Error()) {
		return(false);
	}

	return(file.Write_Section(id, bytes.data(), (std::uint32_t)bytes.size()) == SaveFileClass::RESULT_OK);
}


/*
 * The counterpart, which asks for a section by name. A save that carries none under that
 * name fails the load rather than leaving the subsystem at whatever it was built with.
 */
template<typename F>
static bool Load_Section(SaveFileClass const & file, SaveNamesClass & names, char const * name, F && body)
{
	unsigned short id = 0;
	std::vector<unsigned char> bytes;

	if (!names.Find(name, SaveNamesClass::KIND_VARIABLE, id) || !file.Get_Section(id, bytes)) {
		DebugString("The save carries no %s\n", name);
		return(false);
	}

	SaveStreamClass stream(bytes, SaveStreamClass::MODE_LOAD, names);

	return(Ran(body, stream) && !stream.Was_Error());
}


/*
 * A section is written and read under one name, and its body is written as though it had
 * a stream to itself, because it has: each of these opens one for the section named.
 */
#define SAVE_SECTION(section, body)	Save_Section(file, names, section, \
	[&](SaveStreamClass & stream) { return(body); })

#define LOAD_SECTION(section, body)	Load_Section(file, names, section, \
	[&](SaveStreamClass & stream) { return(body); })


/***********************************************************************************************
 * Put_All -- Store all save game data to the pipe.                                            *
 *                                                                                             *
 *    This is the bulk processor of the game related save game data. All the game object       *
 *    and state data is stored to the pipe specified.                                          *
 *                                                                                             *
 * INPUT:   pipe  -- Reference to the pipe that will receive the save game data.               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/08/1996 JLB : Created.                                                                 *
 *=============================================================================================*/
static bool Put_All(SaveFileClass & file, SaveNamesClass & names, int save_net)
{
	/*
	**	Save the scenario global information.
	*/
	if (!SAVE_SECTION("Scenario", Scen->Save(stream))
			|| !SAVE_SECTION("Environment", Environment.Save(stream))
			|| !SAVE_SECTION("Rules", Rule->Save(stream))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	DebugString("Saving AnimTypes\n");
	if (!SAVE_SECTION("AnimTypes", Save_Vector(stream, AnimTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	/*
	**	Save the map.  The map must be saved first, since it saves the Theater.
	*/
	DebugString("Saving Map\n");
	if (!SAVE_SECTION("Map", Map.Save(stream))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	DebugString("Saving Tunnels\n");
	if (!SAVE_SECTION("Tubes", Save_Vector(stream, Tubes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	/*
	**	Save miscellaneous variables.
	*/
	DebugString("Saving Misc. Values\n");
	if (!SAVE_SECTION("MiscValues", Save_Misc_Values(stream) != 0)) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	/*
	**	Save the Logic & Map layers
	*/
	DebugString("Saving Logic\n");
	if (!SAVE_SECTION("Logic", Logic.Save(stream))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	DebugString("Saving TacticalMap\n");
	if (!SAVE_SECTION("TacticalMap", Save_Object(stream, TacticalMap))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	/*
	**	Save all game objects.  This code saves every object that's stored in a
	**	TFixedIHeap class.
	*/
	DebugString("Saving HouseTypes\n");
	if (!SAVE_SECTION("HouseTypes", Save_Vector(stream, HouseTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Houses\n");
	if (!SAVE_SECTION("Houses", Save_Vector(stream, Houses))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Units\n");
	if (!SAVE_SECTION("Units", Save_Vector(stream, Units))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving UnitTypes\n");
	if (!SAVE_SECTION("UnitTypes", Save_Vector(stream, UnitTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving InfantryTypes\n");
	if (!SAVE_SECTION("InfantryTypes", Save_Vector(stream, InfantryTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Infantry\n");
	if (!SAVE_SECTION("Infantry", Save_Vector(stream, Infantry))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving BuildingTypes\n");
	if (!SAVE_SECTION("BuildingTypes", Save_Vector(stream, BuildingTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Buildings\n");
	if (!SAVE_SECTION("Buildings", Save_Vector(stream, Buildings))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving AircraftTypes\n");
	if (!SAVE_SECTION("AircraftTypes", Save_Vector(stream, AircraftTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Aircraft\n");
	if (!SAVE_SECTION("Aircraft", Save_Vector(stream, Aircraft))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Anims\n");
	if (!SAVE_SECTION("Anims", Save_Vector(stream, Anims))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving TaskForces\n");
	if (!SAVE_SECTION("TaskForces", Save_Vector(stream, TaskForces))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving TeamTypes\n");
	if (!SAVE_SECTION("TeamTypes", Save_Vector(stream, TeamTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Teams\n");
	if (!SAVE_SECTION("Teams", Save_Vector(stream, Teams))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving ScriptTypes\n");
	if (!SAVE_SECTION("ScriptTypes", Save_Vector(stream, ScriptTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Scripts\n");
	if (!SAVE_SECTION("Scripts", Save_Vector(stream, Scripts))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving TagTypes\n");
	if (!SAVE_SECTION("TagTypes", Save_Vector(stream, TagTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Tags\n");
	if (!SAVE_SECTION("Tags", Save_Vector(stream, Tags))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving TriggerTypes\n");
	if (!SAVE_SECTION("TriggerTypes", Save_Vector(stream, TriggerTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Triggers\n");
	if (!SAVE_SECTION("Triggers", Save_Vector(stream, Triggers))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving AITriggerTypes\n");
	if (!SAVE_SECTION("AITriggerTypes", Save_Vector(stream, AITriggerTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	DebugString("Saving Actions\n");
	if (!SAVE_SECTION("Actions", Save_Vector(stream, Actions))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Events\n");
	if (!SAVE_SECTION("Events", Save_Vector(stream, Events))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Factories\n");
	if (!SAVE_SECTION("Factories", Save_Vector(stream, Factories))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving VoxelAnimTypes\n");
	if (!SAVE_SECTION("VoxelAnimTypes", Save_Vector(stream, VoxelAnimTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving VoxelAnims\n");
	if (!SAVE_SECTION("VoxelAnims", Save_Vector(stream, VoxelAnims))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Warheads\n");
	if (!SAVE_SECTION("Warheads", Save_Vector(stream, Warheads))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Weapons\n");
	if (!SAVE_SECTION("Weapons", Save_Vector(stream, Weapons))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving ParticleTypes\n");
	if (!SAVE_SECTION("ParticleTypes", Save_Vector(stream, ParticleTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Particles\n");
	if (!SAVE_SECTION("Particles", Save_Vector(stream, Particles))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving ParticleSystemTypes\n");
	if (!SAVE_SECTION("ParticleSystemTypes", Save_Vector(stream, ParticleSystemTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving ParticleSystems\n");
	if (!SAVE_SECTION("ParticleSystems", Save_Vector(stream, ParticleSystems))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving BulletTypes\n");
	if (!SAVE_SECTION("BulletTypes", Save_Vector(stream, BulletTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Bullets\n");
	if (!SAVE_SECTION("Bullets", Save_Vector(stream, Bullets))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving WaypointPaths\n");
	if (!SAVE_SECTION("WaypointPaths", Save_Vector(stream, WaypointPaths))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving SmudgeTypes\n");
	if (!SAVE_SECTION("SmudgeTypes", Save_Vector(stream, SmudgeTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving OverlayTypes\n");
	if (!SAVE_SECTION("OverlayTypes", Save_Vector(stream, OverlayTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving LightSources\n");
	if (!SAVE_SECTION("LightSources", Save_Vector(stream, LightSources))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving BuildingLights\n");
	if (!SAVE_SECTION("BuildingLights", Save_Vector(stream, BuildingLights))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Sides\n");
	if (!SAVE_SECTION("Sides", Save_Vector(stream, Sides))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Tiberiums\n");
	if (!SAVE_SECTION("Tiberiums", Save_Vector(stream, Tiberiums))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Empulses\n");
	if (!SAVE_SECTION("EMPulses", Save_Vector(stream, EMPulseClass::EMPulses))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving SuperWeaponTypes\n");
	if (!SAVE_SECTION("SuperWeaponTypes", Save_Vector(stream, SuperWeaponTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving SuperWeapons\n");
	if (!SAVE_SECTION("SuperWeapons", Save_Vector(stream, SuperWeapons))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving TerrianTypes\n");
	if (!SAVE_SECTION("TerrainTypes", Save_Vector(stream, TerrainTypes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Terrains\n");
	if (!SAVE_SECTION("Terrains", Save_Vector(stream, Terrains))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving FoggedObjects\n");
	if (!SAVE_SECTION("FoggyObjects", Save_Vector(stream, FoggedObjectClass::FoggyObjects))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving AlphaShapes\n");
	if (!SAVE_SECTION("AlphaShapes", Save_Vector(stream, AlphaShapes))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving Waves\n");
	if (!SAVE_SECTION("Waves", Save_Vector(stream, Waves))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving VeinholeMonster\n");
	if (!SAVE_SECTION("VeinholeMonsters", VeinholeMonsterClass::Save_All(stream))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}
	DebugString("Saving RadarEvents\n");
	if (!SAVE_SECTION("RadarEvents", RadarEventClass::Save(stream))) {
		DebugString("\t***** FAILED!\n");
		return(false);
	}

	/*
	 * A campaign takes its options from the mission. Every other kind is given them at setup, so
	 * the save is the only place a resume can find them.
	 */
	if (Session.Type != GAME_NORMAL) {
		DebugString("Writing Session.Options\n");
		if (!SAVE_SECTION("SessionOptions", Session.Options.Save(stream))) {
			DebugString("\t***** FAILED!\n");
			return(false);
		}
	}

	return(true);
}


/// <summary>
/// Restores all of the save game data from the stream.
/// This routine is the counterpart of Put_All. It tears down the current scenario, puts
/// back the addon, theater and rules that the game was saved under, rebuilds the display
/// surfaces to suit the saved options, and then recreates every object heap in the same
/// order they were written out.
/// </summary>
/// <returns>bool; Was the game state restored?</returns>
static bool Get_All(SaveFileClass const & file, SaveNamesClass & names, bool save_net)
{
	Clear_Scenario();
	if (!LOAD_SECTION("Scenario", Scen->Load(stream))) {
		return(false);
	}
	Disable_Addon(ADDON_ANY);
	Set_Required_Addon(Scen->RequiredAddOn);
	if (!Addon_Installed(Scen->RequiredAddOn)) {
		return(false);
	}
	Enable_Addon(Scen->RequiredAddOn);

	if (Prep_For_Side_Or_First(Scen->PlayerSide) == SIDE_NONE) {
		return(false);
	}

	ScreenLayout const layout = Compute_Screen_Layout(VisibleRect);
	Rect temp = layout.Tactical;

	Allocate_Surfaces(layout.Hidden, layout.Composite, layout.Tile, layout.Sidebar);

	Map.Set_View_Dimensions(temp);

	if (!LOAD_SECTION("Environment", Environment.Load(stream))) {
		return(false);
	}

	Init_Theater(Scen->Theater);

	RulesClass::Load_Art_INI();

	if (Addon_Enabled(ADDON_FIRESTORM) == true) {
		CCFileClass artfs(DeploymentConfig.ArtExpansionFile.c_str());
		if (artfs.Is_Available() == true) {
			ArtINI.Load(artfs, false);
		}
	}

	if (!LOAD_SECTION("Rules", Rule->Load(stream))) {
		return(false);
	}

	SideType speech = Scen->SpeechSide != SIDE_NONE ? Scen->SpeechSide : Scen->PlayerSide;
	if (Prep_Speech_For_Side_Or_First(speech) == SIDE_NONE) {
		return(false);
	}

	if (!LOAD_SECTION("AnimTypes", Load_Vector<AnimTypeClass>(stream))) {
		return(false);
	}

	if (!LOAD_SECTION("Map", Map.Load(stream))) {
		return(false);
	}

	if (!LOAD_SECTION("Tubes", Load_Vector<TubeClass>(stream))) {
		return(false);
	}

	if (!LOAD_SECTION("MiscValues", Load_Misc_Values(stream) != 0)) {
		return(false);
	}

	Map.Reset_All_Subzones();
	if (!LOAD_SECTION("Logic", Logic.Load(stream))) {
		return(false);
	}

	if (TacticalMap != NULL) {
		delete TacticalMap;
		TacticalMap = NULL;
	}
	std::unique_ptr<Tactical> tactical;
	if (!Load_Section(file, names, "TacticalMap", [&](SaveStreamClass & stream){
			tactical = Load_Object_As<Tactical>(stream);
			return(tactical != nullptr);
		})) {
		return(false);
	}
	if (tactical == nullptr) {
		return(false);
	}
	// The map installed itself in TacticalMap as it was constructed, and that global is
	// what deletes it from here on.
	tactical.release();

	if (!LOAD_SECTION("HouseTypes", Load_Vector<HouseTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Houses", Load_Vector<HouseClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Units", Load_Vector<UnitClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("UnitTypes", Load_Vector<UnitTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("InfantryTypes", Load_Vector<InfantryTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Infantry", Load_Vector<InfantryClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("BuildingTypes", Load_Vector<BuildingTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Buildings", Load_Vector<BuildingClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("AircraftTypes", Load_Vector<AircraftTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Aircraft", Load_Vector<AircraftClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Anims", Load_Vector<AnimClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("TaskForces", Load_Vector<TaskForceClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("TeamTypes", Load_Vector<TeamTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Teams", Load_Vector<TeamClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("ScriptTypes", Load_Vector<ScriptTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Scripts", Load_Vector<ScriptClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("TagTypes", Load_Vector<TagTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Tags", Load_Vector<TagClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("TriggerTypes", Load_Vector<TriggerTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Triggers", Load_Vector<TriggerClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("AITriggerTypes", Load_Vector<AITriggerTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Actions", Load_Vector<TActionClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Events", Load_Vector<TEventClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Factories", Load_Vector<FactoryClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("VoxelAnimTypes", Load_Vector<VoxelAnimTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("VoxelAnims", Load_Vector<VoxelAnimClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Warheads", Load_Vector<WarheadTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Weapons", Load_Vector<WeaponTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("ParticleTypes", Load_Vector<ParticleTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Particles", Load_Vector<ParticleClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("ParticleSystemTypes", Load_Vector<ParticleSystemTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("ParticleSystems", Load_Vector<ParticleSystemClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("BulletTypes", Load_Vector<BulletTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Bullets", Load_Vector<BulletClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("WaypointPaths", Load_Vector<WaypointPathClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("SmudgeTypes", Load_Vector<SmudgeTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("OverlayTypes", Load_Vector<OverlayTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("LightSources", Load_Vector<LightSourceClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("BuildingLights", Load_Vector<BuildingLightClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Sides", Load_Vector<SideClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Tiberiums", Load_Vector<TiberiumClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("EMPulses", Load_Vector<EMPulseClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("SuperWeaponTypes", Load_Vector<SuperWeaponTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("SuperWeapons", Load_Vector<SuperClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("TerrainTypes", Load_Vector<TerrainTypeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Terrains", Load_Vector<TerrainClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("FoggyObjects", Load_Vector<FoggedObjectClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("AlphaShapes", Load_Vector<AlphaShapeClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("Waves", Load_Vector<WaveClass>(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("VeinholeMonsters", VeinholeMonsterClass::Load_All(stream))) {
		return(false);
	}
	if (!LOAD_SECTION("RadarEvents", RadarEventClass::Load(stream))) {
		return(false);
	}

	if (Session.Type != GAME_NORMAL) {
		DebugString("Reading Session.Options\n");
		if (!LOAD_SECTION("SessionOptions", Session.Options.Load(stream))) {
			DebugString("\t***** FAILED!\n");
			return(false);
		}
	}

	Map.Flag_To_Redraw(GS_REDRAW_ALL);

	return(true);
}

/***************************************************************************
 * Save_Game -- saves a game to disk                                       *
 *                                                                         *
 * Saving the Map:                                                         *
 *     DisplayClass::Save() invokes CellClass's Write() for every cell     *
 *     that needs to be saved.  A cell needs to be saved if it contains    *
 *     any special data at all, such as a TIcon, or an Occupier.           *
 *   The cell saves its own CellTrigger pointer, converted to a TARGET.    *
 *                                                                         *
 * Saving game objects:                                                    *
 *   - Any object stored in an ArrayOf class needs to be saved.  The ArrayOf*
 *     Save() routine invokes each object's Write() routine, if that       *
 *     object's IsActive is set.                                           *
 *                                                                         *
 * Saving the layers:                                                      *
 *   The Map's Layers (Ground, Air, etc) of things that are on the map,    *
 *     and the Logic's Layer of things to process both need to be saved.   *
 *     LayerClass::Save() writes the entire layer array to disk            *
 *                                                                         *
 * Saving the houses:                                                      *
 *   Each house needs to be saved, to record its Credits, Power, etc.      *
 *                                                                         *
 * Saving miscellaneous data:                                              *
 *   There are a lot of miscellaneous variables to save, such as the       *
 *     map's dimensions, the player's house, etc.                          *
 *                                                                         *
 * INPUT:                                                                  *
 *      id      numerical ID, for the file extension                       *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      true = OK, false = error                                           *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   12/28/1994 BR : Created.                                              *
 *   02/27/1996 JLB : Uses simpler game control value save operation.      *
 *=========================================================================*/
bool Save_Game(const char *file_name, char const * descr)
{
	DebugString("\nSAVING GAME [%s - %s]\n", file_name, descr);

	Swizzler.Begin_Save();

	SaveVersionInfo info;
	info.Set_Internal_Version(ExpectedGameVersion);
	info.Set_Scenario_Description(descr);
	info.Set_Version(1);
	info.Set_Player_House(PlayerPtr->Class->GivenName);
	info.Set_Campaign_Number(Scen->Campaign);
	info.Set_Scenario_Number(Scen->Scenario);
	info.Set_Executable_Name("SUN.EXE");
	info.Set_Game_Type(Session.Type);

	FileTimeType const now = File_Time_Now();
	info.Set_Last_Time(now);
	info.Set_Start_Time(now);
	info.Set_Play_Time(now);

	SaveFileClass file;
	info.Save(file);

	DebugString("Writing %s\n", file_name);
	SaveFileClass::ResultType result = file.Begin_Write(Saved_Game_Name(file_name).c_str());
	if (result != SaveFileClass::RESULT_OK) {
		DebugString("\t***** FAILED! (%s)\n", SaveFileClass::Result_Text(result));
		return(false);
	}

	DebugString("Calling Put_All()\n");
	SaveNamesClass names;
	bool res = Put_All(file, names, 0);

	// The names the sections and their members were written under are known only once
	// they have been written, so the table goes in last.
	if (res) {
		std::vector<unsigned char> table;
		names.Write(table);
		result = file.End_Write(table.data(), (std::uint32_t)table.size());
		if (result != SaveFileClass::RESULT_OK) {
			DebugString("\t***** FAILED! (%s)\n", SaveFileClass::Result_Text(result));
			res = false;
		}
	} else {
		DebugString("\t***** FAILED!\n");
		file.Abandon_Write();
	}

	DebugString("SAVING GAME [%s - %s] - %s\n\n", file_name, descr, res ? "Complete" : "Failed");

	if (res) {
		SaveManager.Autosave.Schedule(Frame);
	}
	return(res);
}


/***************************************************************************
 * Load_Game -- loads a saved game                                         *
 *                                                                         *
 * This routine loads the data in the same way it was saved out.           *
 *                                                                         *
 * Loading the Map:                                                        *
 *   - DisplayClass::Load() invokes CellClass's Load() for every cell      *
 *     that was saved.                                                     *
 * - The cell loads its own CellTrigger pointer.                           *
 *                                                                         *
 * Loading game objects:                                                   *
 * - IHeap's Load() routine loads the # of objects stored, and loads       *
 *   each object.                                                          *
 * - Triggers: Add themselves to the HouseTriggers if they're associated   *
 *   with a house                                                          *
 *                                                                         *
 * Loading the layers:                                                     *
 *     LayerClass::Load() reads the entire layer array to disk             *
 *                                                                         *
 * Loading the houses:                                                     *
 *   Each house is loaded in its entirety.                                 *
 *                                                                         *
 * Loading miscellaneous data:                                             *
 *   There are a lot of miscellaneous variables to load, such as the       *
 *     map's dimensions, the player's house, etc.                          *
 *                                                                         *
 * INPUT:                                                                  *
 *      id         numerical ID, for the file extension                    *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      true = OK, false = error                                           *
 *                                                                         *
 * WARNINGS:                                                               *
 *      If this routine returns false, the entire game will be in an       *
 *      unknown state, so the scenario will have to be re-initialized.     *
 *                                                                         *
 * HISTORY:                                                                *
 *   12/28/1994 BR : Created.                                              *
 *   1/20/97  V.Grippi Added expansion CD check                            *
 *=========================================================================*/
bool Load_Game(const char *file_name)
{
	DebugString("\nLOADING GAME [%s]\n", file_name);

	// The whole file is checked before the running game is torn down, so a damaged
	// save costs nothing. The listing fields come back with it, so the version this
	// build will not read is judged on the same read rather than on a second one.
	SaveFileClass file;
	SaveFileClass::ResultType const result = file.Read(Saved_Game_Name(file_name).c_str());
	if (result != SaveFileClass::RESULT_OK) {
		DebugString("\t***** FAILED! (%s)\n", SaveFileClass::Result_Text(result));
		return(false);
	}

	SaveVersionInfo info;
	if (!info.Load(file)) {
		return(false);
	}
	if (info.Get_Internal_Version() != ExpectedGameVersion) {
		return(false);
	}

	LoadedSaveVersion = info.Get_Internal_Version();
	Session.Type = (GameType)info.Get_Game_Type();

	Swizzler.Discard();

	std::vector<unsigned char> table;
	SaveNamesClass names;
	if (!file.Get_Names(table) || !names.Read(table.data(), table.size())) {
		DebugString("\t***** FAILED! (the name table is not one this build reads)\n");
		return(false);
	}

	bool res = false;
	// The catch sits here rather than around the whole routine because what was already
	// loaded still has to be abandoned below. Both of the ways a count read from the file
	// can end an allocation are refused here; anything else still raises.
	try {
		res = Get_All(file, names, false);
	} catch (std::bad_alloc const &) {
		DebugString("\t***** FAILED! (out of memory)\n");
	}
	if (!res) {
		DebugString("\t***** FAILED!\n");
		// What was loaded stays in the heaps until the next teardown, so the requests it
		// registered must not be answered into it once the game that follows has moved on.
		Swizzler.Discard();
		return(false);
	}

	Swizzler.Resolve();

	/*
	**	Fixup any expediency data that can be inferred from the physical
	**	data loaded.
	*/
	Post_Load_Game();

	// The next mission of a resumed campaign is played at the pair the save carries.
	Session.CampaignDifficulty = Scen->Difficulty;
	Session.CampaignCDifficulty = Scen->CDifficulty;

	Map.Init_IO();
	Map.Activate(1);
	Map.Reposition_Sidebar();
	TiberiumClass::Init_Tiberium_Growth_System();
	TiberiumClass::Init_Tiberium_Spread_System();
	Map.Complete_Radar_Refresh();
	ScenarioActive = true;
	TacticalActive = true;
	Sync_Recorder_Arm();
	Sync_Report_Reset();
	SaveManager.Autosave.Schedule(Frame);
	DebugString("LOADING GAME [%s] - Complete\n\n", file_name);
	return(true);
}


/***************************************************************************
 * Save_Misc_Values -- saves miscellaneous variables                       *
 *                                                                         *
 * INPUT:                                                                  *
 *      file      file to use for writing                                  *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      true = success, false = failure                                    *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   12/29/1994 BR : Created.                                              *
 *   03/12/1996 JLB : Simplified.                                          *
 *=========================================================================*/
static void Serialize_Misc_Values(SaveStreamClass & stream)
{
	stream.Body([&]{
		SERIALIZE(stream, GasSystem);
		SERIALIZE(stream, PlayerPtr);
		SERIALIZE(stream, Frame);
		SERIALIZE(stream, CurrentObject);
		SERIALIZE(stream, Ground);

		IonStormClass::Serialize(stream);

		SERIALIZE(stream, LogicTags);
		SERIALIZE(stream, MapTags);
		SERIALIZE(stream, CrateShares);
		SERIALIZE(stream, CrateAnims);
		SERIALIZE(stream, CrateData);
		SERIALIZE(stream, MissionControl);
		SERIALIZE(stream, Session.ObiWan);
		SERIALIZE(stream, Session.AIOnly);

		/*
		 * Speech is reached through a pair of accessors rather than a variable of its own,
		 * so it travels through a local either way.
		 */
		int state = Get_Speech_State();
		SERIALIZE(stream, state);
		if (stream.Is_Loading()) {
			Set_Speech_State(state != 0);
		}

		// The ring positions travel with every save, so a load continues where the save left off.
		int campaign_slot = SaveManager.Autosave.Campaign_Slot();
		int skirmish_slot = SaveManager.Autosave.Skirmish_Slot();
		SERIALIZE(stream, campaign_slot);
		SERIALIZE(stream, skirmish_slot);
		if (stream.Is_Loading()) {
			SaveManager.Autosave.Seed_Slots(campaign_slot, skirmish_slot);
		}

		// The scenario's own tutorial lines travel here, since a load never re-reads the map.
		SERIALIZE(stream, TutorialText);

		// Placed sounds and the sounds attached to objects come back on the next
		// sound tick; the playing sounds themselves are not saved.
		stream.Field("StaticSounds", [&]{ Static_Sounds_Serialize(stream); });
		SERIALIZE(stream, AmbientSounds);
	});
}


int Save_Misc_Values(SaveStreamClass & stream)
{
	Serialize_Misc_Values(stream);
	return(!stream.Was_Error());
}


/***********************************************************************************************
 * Load_Misc_Values -- Loads miscellaneous variables.                                          *
 *                                                                                             *
 * INPUT:   file  -- The file to load the misc values from.                                    *
 *                                                                                             *
 * OUTPUT:  Was the misc load process successful?                                              *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/24/1995 BRR : Created.                                                                 *
 *   03/12/1996 JLB : Simplified.                                                              *
 *=============================================================================================*/
int Load_Misc_Values(SaveStreamClass & stream)
{
	stream.Set_Context("Load_Misc_Values");
	Serialize_Misc_Values(stream);
	return(!stream.Was_Error());
}


/***************************************************************************
 * Get_Savefile_Info -- gets description, scenario #, house                *
 *                                                                         *
 * INPUT:                                                                  *
 *      id         numerical ID, for the file extension                    *
 *      buf      buffer to store description in                            *
 *      scenp      ptr to variable to hold scenario                        *
 *      housep   ptr to variable to hold house                             *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      true = OK, false = error (save-game file invalid)                  *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   01/12/1995 BR : Created.                                              *
 *=========================================================================*/
bool Get_Savefile_Info(char const * name, SaveVersionInfo * info)
{
	if (name == nullptr || info == nullptr) {
		return(false);
	}

	SaveFileClass file;
	SaveFileClass::ResultType const result = file.Read_Fields(Saved_Game_Name(name).c_str());
	if (result != SaveFileClass::RESULT_OK) {
		if (result != SaveFileClass::RESULT_MISSING) {
			DebugString("Saved game %s: %s\n", name, SaveFileClass::Result_Text(result));
		}
		return(false);
	}

	return(info->Load(file));
}


/// <summary>
/// Gives each seated player the restored house carrying its name, so the connections formed
/// afterwards reach the right houses.
/// </summary>
/// <returns>bool; Do the seats and the saved houses agree?</returns>
bool Reconcile_Players(void)
{
	if (Session.Players.Count() == 0) {
		return(true);
	}

	for (int i = 0; i < Session.Players.Count(); i++) {
		HouseClass * found = NULL;

		for (int house = 0; house < Houses.Count(); house++) {
			if (Houses[house]->IsHuman && stricmp(Session.Players[i]->Name, Houses[house]->IniName) == 0) {
				found = Houses[house];
				break;
			}
		}

		if (found == NULL || found->IsObserver != Session.Players[i]->Player.IsObserver) {
			return(false);
		}

		Session.Players[i]->Player.ID = found->HeapID;
	}

	// The first seat is this machine, and PlayerPtr the house that wrote the save.
	if (Houses[Session.Players[0]->Player.ID] != PlayerPtr) {
		return(false);
	}

	for (int house = 0; house < Houses.Count(); house++) {
		HouseClass * housep = Houses[house];
		if (!housep->IsHuman) {
			continue;
		}

		bool seated = false;
		for (int i = 0; i < Session.Players.Count(); i++) {
			if (Session.Players[i]->Player.ID == housep->HeapID) {
				seated = true;
				break;
			}
		}

		// A player who did not return leaves their house fighting on under the computer. An
		// observer's house has nothing to hand over.
		if (!seated && !housep->IsObserver) {
			housep->IsHuman = false;
			housep->IsStarted = true;
			housep->IQ = Rule->MaxIQ;
		}
	}

	return(true);
}


/***************************************************************************
 * MPlayer_Save_Message -- pops up a "saving..." message                   *
 *                                                                         *
 * INPUT:                                                                  *
 *      none.                                                              *
 *                                                                         *
 * OUTPUT:                                                                 *
 *      none.                                                              *
 *                                                                         *
 * WARNINGS:                                                               *
 *      none.                                                              *
 *                                                                         *
 * HISTORY:                                                                *
 *   10/30/1995 BRR : Created.                                             *
 *=========================================================================*/
void MPlayer_Save_Message(void)
{
	//char *txt = Text_String(
}
