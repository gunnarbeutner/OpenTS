/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "syncrechook.h"

#include "_rtti.h"
#include "abstract.h"
#include "anim.h"
#include "animtype.h"
#include "coord.h"
#include "event.h"
#include "face.h"
#include "globals.h"
#include "mission.h"
#include "object.h"
#include "random.h"
#include "scenario.h"
#include "except.h"
#include "platform/process.h"
#include "session.h"

#include <cstdio>
#include <string>


static uintptr_t ModuleBase = 0;
static uint32_t ModuleSize = 0;
static uint32_t MapImageBase = 0;


static uint32_t Sync_Caller_RVA(unsigned caller)
{
	uintptr_t const address = caller;
	if (ModuleBase != 0 && address >= ModuleBase && address < ModuleBase + ModuleSize) {
		uint32_t const rva = (uint32_t)(address - ModuleBase);
		// The flag bit is only free because the image is far smaller than two gigabytes, so an
		// offset that reached it would be read back as an address outside the image.
		if ((rva & SYNC_CALLER_EXTERN) == 0) {
			return(rva);
		}
	}
	// The image is large address aware, so an address outside it can carry the flag bit itself
	// and loses it here. Such an address only says the call came from elsewhere.
	return(SYNC_CALLER_EXTERN | ((uint32_t)address & ~SYNC_CALLER_EXTERN));
}


// One description per call site rather than per entry: a report holds thousands of entries drawn
// from a few dozen sites, and each lookup takes the symbol handler's lock.
namespace {
	struct CallerTextType {
		uint32_t Rva;
		bool Known;
		char Text[192];
	};

	CallerTextType CallerTexts[256];
	unsigned CallerTextCount = 0;
}


static bool Read_Bytes(std::FILE * file, long offset, unsigned char * buffer, size_t size)
{
	return(std::fseek(file, offset, SEEK_SET) == 0 && std::fread(buffer, 1, size, file) == size);
}


static uint32_t Little_Endian_32(unsigned char const * bytes)
{
	return(uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24));
}


/// <summary>
/// Reads the image base the linker wrote this build's map file against. The loader rewrites that
/// field in the mapped header whenever it relocates the image, which address space layout
/// randomization makes the normal case, so the answer only survives in the file itself.
/// </summary>
/// <returns>The preferred base, or zero when the executable is not a 32-bit PE image or its
/// header could not be read.</returns>
static uint32_t Sync_Preferred_Image_Base(void)
{
	// Offsets fixed by the PE format: the DOS header's pointer to the NT headers, and within
	// those the signature, the optional header's magic, and its ImageBase field.
	constexpr long NT_HEADERS_POINTER = 0x3C;
	constexpr long OPTIONAL_HEADER = 24;
	constexpr long IMAGE_BASE = OPTIONAL_HEADER + 28;
	constexpr uint32_t PE_SIGNATURE = 0x00004550;
	constexpr unsigned PE32_MAGIC = 0x10B;

	std::string const path = Executable_Path();
	if (path.empty()) {
		return(0);
	}

	std::FILE * const file = std::fopen(path.c_str(), "rb");
	if (file == nullptr) {
		return(0);
	}

	uint32_t base = 0;
	unsigned char bytes[4];

	if (Read_Bytes(file, 0, bytes, 2) && bytes[0] == 'M' && bytes[1] == 'Z'
		&& Read_Bytes(file, NT_HEADERS_POINTER, bytes, 4)) {

		long const nt = long(Little_Endian_32(bytes));

		if (Read_Bytes(file, nt, bytes, 4) && Little_Endian_32(bytes) == PE_SIGNATURE
			&& Read_Bytes(file, nt + OPTIONAL_HEADER, bytes, 2)
			&& (unsigned(bytes[0]) | (unsigned(bytes[1]) << 8)) == PE32_MAGIC
			&& Read_Bytes(file, nt + IMAGE_BASE, bytes, 4)) {

			base = Little_Endian_32(bytes);
		}
	}

	std::fclose(file);

	return(base);
}


static char const * Sync_Describe_Caller(uint32_t rva)
{
	for (unsigned i = 0; i < CallerTextCount; i++) {
		if (CallerTexts[i].Rva == rva) {
			return(CallerTexts[i].Known ? CallerTexts[i].Text : nullptr);
		}
	}

	if (CallerTextCount >= ARRAY_SIZE(CallerTexts) || ModuleBase == 0) {
		return(nullptr);
	}

	CallerTextType & entry = CallerTexts[CallerTextCount];
	entry.Rva = rva;
	entry.Text[0] = '\0';

	// The recorded address is the instruction after the call, which can belong to the next line
	// or, for a call in tail position, the next function.
	entry.Known = Describe_Code_Address((void const *)(ModuleBase + rva - 1),
					entry.Text, sizeof(entry.Text));

	CallerTextCount++;

	return(entry.Known ? entry.Text : nullptr);
}


void Sync_Record_Random_Impl(Random2Class const & gen, int value, int minval, int maxval, bool ranged, unsigned caller)
{
	SyncRandomEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.Index1 = gen.Index_1();
	entry.Index2 = gen.Index_2();
	entry.Value = value;
	entry.Min = minval;
	entry.Max = maxval;
	entry.Kind = (Scen != nullptr && &gen == &Scen->RandomNumber) ? SYNC_RANDOM_CRITICAL : SYNC_RANDOM_NONCRITICAL;
	entry.Shape = ranged ? SYNC_DRAW_RANGED : (SyncRecorder.In_Ranged_Draw() ? SYNC_DRAW_INNER : SYNC_DRAW_PLAIN);
	SyncRecorder.Add_Random(entry);
}


void Sync_Record_Facing_Impl(DirType const & facing, unsigned caller)
{
	SyncFacingEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.Facing = (int16_t)(int)facing.As_Dir256();
	entry.Kind = SYNC_FACING_SET;
	SyncRecorder.Add_Facing(entry);
}


void Sync_Record_Target_Impl(AbstractClass const & subject, AbstractClass const * target, unsigned caller)
{
	SyncTargetEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.SubjectRTTI = (int8_t)subject.Fetch_RTTI();
	entry.SubjectID = subject.Fetch_ID();
	entry.TargetRTTI = (int8_t)(target != nullptr ? target->Fetch_RTTI() : RTTI_NONE);
	entry.TargetID = (target != nullptr ? target->Fetch_ID() : 0);
	SyncRecorder.Add_Target(entry);
}


void Sync_Record_Mission_Impl(ObjectClass const & subject, int before, int after, int kind, unsigned caller)
{
	SyncMissionEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.SubjectRTTI = (int8_t)subject.Fetch_RTTI();
	entry.SubjectID = subject.Fetch_ID();
	entry.Before = (int8_t)before;
	entry.After = (int8_t)after;
	entry.Kind = (uint8_t)kind;
	SyncRecorder.Add_Mission(entry);
}


void Sync_Record_Anim_Impl(AnimClass const & anim, Coord const & coord, unsigned caller)
{
	SyncAnimEntryType entry {};
	entry.Frame = Frame;
	entry.Caller = Sync_Caller_RVA(caller);
	entry.AnimID = anim.Fetch_ID();
	entry.TypeHeapID = (int16_t)(anim.Class != nullptr ? anim.Class->Fetch_Heap_ID() : -1);
	entry.X = coord.X;
	entry.Y = coord.Y;
	entry.Z = coord.Z;
	SyncRecorder.Add_Anim(entry);
}


void Sync_Record_Event_Impl(EventClass const & event, int source)
{
	switch (event.Type) {
		case EventClass::FRAMEINFO:
		case EventClass::FRAMESYNC:
		case EventClass::RESPONSE_TIME:
		case EventClass::PROCESS_TIME:
		case EventClass::TIMING:
			return;
		default:
			break;
	}

	SyncEventEntryType entry {};
	entry.Frame = event.Frame;
	entry.SeenAt = Frame;
	entry.House = event.ID;
	entry.Type = event.Type;
	entry.Source = (uint8_t)source;

	unsigned length = 0;
	if (event.Type < EventClass::LAST_EVENT) {
		length = EventClass::EventLength[event.Type];
	}
	if (length > sizeof(entry.Bytes)) {
		length = sizeof(entry.Bytes);
	}
	entry.Length = (uint8_t)length;
	memcpy(entry.Bytes, &event.Data, length);

	if (source == SYNC_EVENT_QUEUED) {
		SyncRecorder.Add_Queued_Event(entry);
	} else {
		SyncRecorder.Add_Executed_Event(entry);
	}
}


void Sync_Recorder_Arm(void)
{
	SyncRecorder.Reset();

	bool const network = (Session.Type == GAME_IPX || Session.Type == GAME_INTERNET);
	SyncRecorder.Set_Recording(network || Session.Record || Session.Play);

	ModuleBase = 0;
	ModuleSize = 0;
	MapImageBase = 0;

	// The image's extent turns a return address into an offset a map file can be read against.
	// Where there is no image, a zero base makes every caller record as an address outside it.
	std::uintptr_t base = 0;
	std::size_t size = 0;
	if (Executable_Image_Range(base, size)) {
		ModuleBase = base;
		ModuleSize = (uint32_t)size;
	}

	MapImageBase = Sync_Preferred_Image_Base();

	SyncCallerContextType context;
	context.MapImageBase = MapImageBase;
	context.Describe = Sync_Describe_Caller;
	Sync_Set_Caller_Context(context);
}


void Sync_Recorder_Disarm(void)
{
	SyncRecorder.Set_Recording(false);
	SyncRecorder.Reset();
}


static char const * Anim_Name(int heap_id)
{
	if (heap_id >= 0 && heap_id < AnimTypes.Count()) {
		return(AnimTypes[heap_id]->Name());
	}
	return("?");
}


static char const * Mission_Name(int mission)
{
	return(MissionClass::Mission_Name((MissionType)mission));
}


static char const * Rtti_Name(int rtti)
{
	return(Name_From_RTTI((RTTIType)rtti));
}


static char const * Event_Name(int type)
{
	if (type >= 0 && type < EventClass::LAST_EVENT) {
		return(EventClass::EventNames[type]);
	}
	return("?");
}


SyncNamesType const & Sync_Engine_Names(void)
{
	static SyncNamesType const names = { Rtti_Name, Mission_Name, Event_Name, Anim_Name };
	return(names);
}
