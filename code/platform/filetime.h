/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

// A point in time as saves and directory listings carry it: 100 ns ticks since the start of
// 1601 UTC, the unit and origin of a Windows FILETIME. A save stores the low word first,
// so the halves are named rather than reached through the layout.
struct FileTimeType
{
	std::uint64_t Ticks = 0;

	std::uint32_t Low(void) const {return((std::uint32_t)(Ticks & 0xFFFFFFFFu));}
	std::uint32_t High(void) const {return((std::uint32_t)(Ticks >> 32));}

	static FileTimeType From_Parts(std::uint32_t low, std::uint32_t high)
		{return(FileTimeType{((std::uint64_t)high << 32) | low});}

	friend auto operator <=> (FileTimeType const & left, FileTimeType const & right) = default;
};

static_assert(sizeof(FileTimeType) == 8, "a file time is stored as eight bytes");
static_assert(std::is_trivially_copyable_v<FileTimeType>, "");


// A time broken into calendar fields. Month runs from 1, and DayOfWeek from 0 for Sunday.
struct CalendarTimeType
{
	int Year = 1601;
	int Month = 1;
	int DayOfWeek = 1;
	int Day = 1;
	int Hour = 0;
	int Minute = 0;
	int Second = 0;
	int Milliseconds = 0;
};


FileTimeType File_Time_Now(void);
FileTimeType File_Time_From_Unix(std::int64_t seconds, std::int64_t nanoseconds);

// Seconds since the start of 1970 UTC, rounded down, and the nanoseconds past them.
void Unix_From_File_Time(FileTimeType time, std::int64_t & seconds, std::int64_t & nanoseconds);

// The calendar in UTC.
CalendarTimeType Calendar_Time(FileTimeType time);

// The calendar in the local zone, with the offset that applied at that instant.
bool Local_Calendar_Time(FileTimeType time, CalendarTimeType & calendar);

// The MS-DOS packing FileClass::Get_Date_Time returns: the date in the high word and the
// time, to two seconds, in the low one. Zero for a time outside 1980 to 2107.
unsigned int Dos_Date_Time(FileTimeType time);
bool File_Time_From_Dos_Date_Time(unsigned int packed, FileTimeType & time);
