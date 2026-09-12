/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "platform/filetime.h"

#include <chrono>

namespace {

// The FILETIME origin lies 369 years before the Unix one.
constexpr std::int64_t UNIX_EPOCH_TICKS = 116444736000000000LL;
constexpr std::int64_t TICKS_PER_SECOND = 10000000LL;
constexpr std::int64_t SECONDS_PER_DAY = 86400LL;

using TickDuration = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;


std::int64_t Floor_Divide(std::int64_t value, std::int64_t divisor, std::int64_t & remainder)
{
	std::int64_t quotient = value / divisor;
	remainder = value % divisor;

	if (remainder < 0) {
		remainder += divisor;
		quotient--;
	}
	return(quotient);
}

}	// namespace


FileTimeType File_Time_Now(void)
{
	TickDuration const since = std::chrono::duration_cast<TickDuration>(
		std::chrono::system_clock::now().time_since_epoch());

	return(FileTimeType{(std::uint64_t)(since.count() + UNIX_EPOCH_TICKS)});
}


FileTimeType File_Time_From_Unix(std::int64_t seconds, std::int64_t nanoseconds)
{
	return(FileTimeType{(std::uint64_t)(seconds * TICKS_PER_SECOND + nanoseconds / 100 + UNIX_EPOCH_TICKS)});
}


void Unix_From_File_Time(FileTimeType time, std::int64_t & seconds, std::int64_t & nanoseconds)
{
	std::int64_t fraction = 0;

	seconds = Floor_Divide((std::int64_t)time.Ticks - UNIX_EPOCH_TICKS, TICKS_PER_SECOND, fraction);
	nanoseconds = fraction * 100;
}


CalendarTimeType Calendar_Time(FileTimeType time)
{
	std::int64_t seconds = 0;
	std::int64_t nanoseconds = 0;
	Unix_From_File_Time(time, seconds, nanoseconds);

	std::int64_t clock = 0;
	std::int64_t const day_count = Floor_Divide(seconds, SECONDS_PER_DAY, clock);

	std::chrono::sys_days const day{std::chrono::days{(int)day_count}};
	std::chrono::year_month_day const date{day};

	CalendarTimeType calendar;
	calendar.Year = (int)date.year();
	calendar.Month = (int)(unsigned)date.month();
	calendar.Day = (int)(unsigned)date.day();
	calendar.DayOfWeek = (int)std::chrono::weekday{day}.c_encoding();
	calendar.Hour = (int)(clock / 3600);
	calendar.Minute = (int)((clock / 60) % 60);
	calendar.Second = (int)(clock % 60);
	calendar.Milliseconds = (int)(nanoseconds / 1000000);
	return(calendar);
}


unsigned int Dos_Date_Time(FileTimeType time)
{
	CalendarTimeType const calendar = Calendar_Time(time);

	if (calendar.Year < 1980 || calendar.Year > 2107) {
		return(0);
	}

	unsigned int const date = ((unsigned int)(calendar.Year - 1980) << 9) | ((unsigned int)calendar.Month << 5)
		| (unsigned int)calendar.Day;
	unsigned int const clock = ((unsigned int)calendar.Hour << 11) | ((unsigned int)calendar.Minute << 5)
		| (unsigned int)(calendar.Second / 2);

	return((date << 16) | clock);
}


// A packing that names no real date or time is refused rather than normalized.
bool File_Time_From_Dos_Date_Time(unsigned int packed, FileTimeType & time)
{
	unsigned int const date = (packed >> 16) & 0xFFFF;
	unsigned int const clock = packed & 0xFFFF;

	std::chrono::year_month_day const day{
		std::chrono::year{(int)((date >> 9) & 0x7F) + 1980},
		std::chrono::month{(date >> 5) & 0x0F},
		std::chrono::day{date & 0x1F}};

	unsigned int const hour = (clock >> 11) & 0x1F;
	unsigned int const minute = (clock >> 5) & 0x3F;
	unsigned int const second = (clock & 0x1F) * 2;

	if (!day.ok() || hour > 23 || minute > 59 || second > 59) {
		return(false);
	}

	std::int64_t const days_since = std::chrono::sys_days{day}.time_since_epoch().count();
	time = File_Time_From_Unix(days_since * SECONDS_PER_DAY + hour * 3600 + minute * 60 + second, 0);
	return(true);
}
