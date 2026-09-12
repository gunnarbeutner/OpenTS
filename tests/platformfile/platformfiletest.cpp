/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the engine's file layer in code/platform: PlatformFileClass and its open modes,
// reads, writes, seeks and sizes, the path operations, the directory search, and the file
// time arithmetic saves and RawFileClass::Get_Date_Time depend on.
//
// Every file it touches it creates itself, in a scratch directory named by the first
// argument, so it reads no game data and leaves nothing behind. The same checks run against
// the Win32 implementation under MSVC and the POSIX one elsewhere; a check that holds for one
// and not the other is a difference between targets.

#include "platform/file.h"
#include "platform/filetime.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif


static int Failures = 0;
static int Checks = 0;
static std::string Scratch;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	std::printf("FAIL %s\n", name);
}


static void Check_Equal(char const * name, long long actual, long long expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	std::printf("FAIL %s: got %lld, expected %lld\n", name, actual, expected);
}


static std::string Scratch_Path(char const * name)
{
	return(Scratch + "\\" + name);
}


// A byte sequence rather than a constant, so a read landing at the wrong offset shows up as
// wrong data rather than as a match.
static std::vector<unsigned char> Pattern(std::size_t length)
{
	std::vector<unsigned char> bytes(length);

	for (std::size_t index = 0; index < length; index++) {
		bytes[index] = (unsigned char)((index * 37 + (index >> 8)) & 0xFF);
	}
	return(bytes);
}


static bool Write_File(char const * name, std::size_t length)
{
	PlatformFileClass file;

	if (!file.Open(Scratch_Path(name).c_str(), PlatformOpenType::WRITE)) return(false);

	std::vector<unsigned char> const bytes = Pattern(length);
	std::uint32_t put = 0;
	bool const wrote = file.Write(bytes.empty() ? "" : (void const *)bytes.data(), (std::uint32_t)length, put);

	return(file.Close() && wrote && put == (std::uint32_t)length);
}


static bool Exists(std::string const & path)
{
	PlatformFileInfoType info;
	return(Platform_File_Info(path.c_str(), info));
}


static void Test_Write_Read_Size(void)
{
	std::size_t const length = 5000;

	Check("write a file", Write_File("basic.dat", length));

	PlatformFileClass file;
	Check("open the file just written", file.Open(Scratch_Path("basic.dat").c_str(), PlatformOpenType::READ));
	if (!file.Is_Open()) return;

	Check_Equal("size of the file just written", file.Size(), (long long)length);

	std::vector<unsigned char> read(length + 16, 0xCD);
	std::uint32_t got = 0;
	Check("read the whole file", file.Read(read.data(), (std::uint32_t)length, got));
	Check_Equal("bytes read", got, (long long)length);
	Check("contents of the file", std::memcmp(read.data(), Pattern(length).data(), length) == 0);

	// The end of a file is a successful read of nothing, which RawFileClass::Read relies on to
	// end its loop.
	got = 12345;
	Check("read past the end succeeds", file.Read(read.data(), 16, got));
	Check_Equal("read past the end returns nothing", got, 0);

	Check("a write to a file opened for reading fails", !file.Write(read.data(), 4, got));
	Check("close the file", file.Close());
}


static void Test_Seek(void)
{
	std::size_t const length = 5000;

	Check("write the file to seek in", Write_File("seek.dat", length));

	PlatformFileClass file;
	Check("open the file to seek in", file.Open(Scratch_Path("seek.dat").c_str(), PlatformOpenType::READ));
	if (!file.Is_Open()) return;

	Check_Equal("seek to an absolute position", file.Seek(1000, SEEK_SET), 1000);
	Check_Equal("seek forward from there", file.Seek(500, SEEK_CUR), 1500);
	Check_Equal("seek back from there", file.Seek(-200, SEEK_CUR), 1300);
	Check_Equal("seek to the end", file.Seek(0, SEEK_END), (long long)length);
	Check_Equal("seek back from the end", file.Seek(-100, SEEK_END), (long long)length - 100);
	Check_Equal("seek by nothing reports the position", file.Seek(0, SEEK_CUR), (long long)length - 100);

	Check_Equal("seek to a later position", file.Seek(2048, SEEK_SET), 2048);

	unsigned char read[64];
	std::uint32_t got = 0;
	Check("read after a seek", file.Read(read, sizeof(read), got));
	Check_Equal("bytes read after a seek", got, sizeof(read));
	Check("contents after a seek", std::memcmp(read, Pattern(length).data() + 2048, sizeof(read)) == 0);

	Check_Equal("seek before the start fails", file.Seek(-1, SEEK_SET), -1);
	Check_Equal("seek with an unknown origin fails", file.Seek(0, 99), -1);

	file.Close();

	Check_Equal("seek on a closed file fails", file.Seek(0, SEEK_SET), -1);
	Check_Equal("the size of a closed file cannot be had", file.Size(), -1);
}


static void Test_Open_Modes(void)
{
	Check("write the file to reopen", Write_File("mode.dat", 100));

	// READ on a name that is not there is the failure RawFileClass::Is_Available reads as "no
	// such file", and it must not create anything.
	PlatformFileClass missing;
	Check("opening a missing file fails", !missing.Open(Scratch_Path("absent.dat").c_str(), PlatformOpenType::READ));
	Check("opening a missing file creates nothing", !Exists(Scratch_Path("absent.dat")));

	// EXCLUSIVE refuses a name already taken; the debug log's writer keeps two runs apart by it.
	PlatformFileClass clash;
	Check("creating over an existing file fails",
		!clash.Open(Scratch_Path("mode.dat").c_str(), PlatformOpenType::EXCLUSIVE));

	PlatformFileClass fresh_exclusive;
	Check("creating a new file exclusively succeeds",
		fresh_exclusive.Open(Scratch_Path("exclusive.dat").c_str(), PlatformOpenType::EXCLUSIVE));
	fresh_exclusive.Close();

	// UPDATE keeps what is there; RawFileClass opens READ|WRITE this way.
	PlatformFileClass kept;
	Check("opening an existing file for update succeeds", kept.Open(Scratch_Path("mode.dat").c_str(), PlatformOpenType::UPDATE));

	if (kept.Is_Open()) {
		Check_Equal("updating keeps the length", kept.Size(), 100);
		Check_Equal("seek before writing", kept.Seek(40, SEEK_SET), 40);

		unsigned char const marker[4] = {0xDE, 0xAD, 0xBE, 0xEF};
		std::uint32_t put = 0;
		Check("write through an update", kept.Write(marker, sizeof(marker), put));
		Check_Equal("bytes written through an update", put, sizeof(marker));

		Check_Equal("seek back to the marker", kept.Seek(40, SEEK_SET), 40);

		unsigned char read[4] = {0, 0, 0, 0};
		std::uint32_t got = 0;
		Check("read through an update", kept.Read(read, sizeof(read), got));
		Check_Equal("bytes read through an update", got, sizeof(read));
		Check("the marker reads back", std::memcmp(read, marker, sizeof(marker)) == 0);
		Check_Equal("writing inside a file does not lengthen it", kept.Size(), 100);

		Check("a flush succeeds", kept.Flush());
		kept.Close();
	}

	PlatformFileClass fresh;
	Check("updating a missing file creates it", fresh.Open(Scratch_Path("fresh.dat").c_str(), PlatformOpenType::UPDATE));
	if (fresh.Is_Open()) {
		Check_Equal("a file just created is empty", fresh.Size(), 0);
		fresh.Close();
	}

	// WRITE discards what is there; RawFileClass opens WRITE this way.
	Check("truncate an existing file", Write_File("mode.dat", 8));
	PlatformFileInfoType info;
	Check("the truncated file is still there", Platform_File_Info(Scratch_Path("mode.dat").c_str(), info));
	Check_Equal("length after truncation", (long long)info.Size, 8);

	// A directory is not a file.
	PlatformFileClass folder;
	Check("opening a directory as a file fails", !folder.Open(Scratch.c_str(), PlatformOpenType::READ));
}


static void Test_Info(void)
{
	Check("write the file to inspect", Write_File("info.dat", 64));

	PlatformFileInfoType info;
	Check("a file is reported", Platform_File_Info(Scratch_Path("info.dat").c_str(), info));
	Check("the name is the file's own", info.Name == "info.dat");
	Check_Equal("the size is reported", (long long)info.Size, 64);
	Check("a file is not a directory", !info.IsDirectory);
	Check("a file is not hidden", !info.IsHidden);
	Check("a file just written is not read-only", !info.IsReadOnly);
	Check("a file just written has a time", info.Modified.Ticks != 0);

	Check("the scratch directory is reported", Platform_File_Info(Scratch.c_str(), info));
	Check("the scratch directory is a directory", info.IsDirectory);

	Check("a missing file is not reported", !Platform_File_Info(Scratch_Path("nothing-here.dat").c_str(), info));
}


// RawFileClass stamps and reads a file's time through the DOS packing, so the round trip
// checked here is the one it makes.
static void Test_File_Times(void)
{
	Check("write the file to stamp", Write_File("times.dat", 32));

	PlatformFileClass file;
	Check("open the file to stamp", file.Open(Scratch_Path("times.dat").c_str(), PlatformOpenType::UPDATE));
	if (!file.Is_Open()) return;

	unsigned int const packed = ((unsigned int)(((1999 - 1980) << 9) | (7 << 5) | 14) << 16)
		| (unsigned int)((13 << 11) | (45 << 5) | (30 / 2));

	FileTimeType stamp;
	Check("unpack a DOS date and time", File_Time_From_Dos_Date_Time(packed, stamp));
	Check_Equal("the unpacked time is 1999-07-14 13:45:30 UTC", (long long)stamp.Ticks, 125764335300000000LL);
	Check("stamp the file", file.Set_Modified_Time(stamp));

	FileTimeType written;
	Check("read the stamp back", file.Modified_Time(written));
	Check_Equal("the stamp survives the round trip", (long long)Dos_Date_Time(written), (long long)packed);
	file.Close();

	PlatformFileInfoType info;
	Check("the stamp is what the directory reports", Platform_File_Info(Scratch_Path("times.dat").c_str(), info)
		&& Dos_Date_Time(info.Modified) == packed);
}


static void Test_Time_Arithmetic(void)
{
	FileTimeType const parts = FileTimeType::From_Parts(0x12345678u, 0x01D2C3B4u);
	Check("the halves make the ticks", parts.Ticks == 0x01D2C3B412345678ull);
	Check("the low half is the low word", parts.Low() == 0x12345678u);
	Check("the high half is the high word", parts.High() == 0x01D2C3B4u);
	Check("a later time orders after an earlier one", FileTimeType{1} < FileTimeType{2} && FileTimeType{2} == FileTimeType{2});

	Check_Equal("the Unix origin", (long long)File_Time_From_Unix(0, 0).Ticks, 116444736000000000LL);

	std::int64_t seconds = 0;
	std::int64_t nanoseconds = 0;
	Unix_From_File_Time(FileTimeType{116444736000000000ull - 1}, seconds, nanoseconds);
	Check("a time before the Unix origin rounds down", seconds == -1 && nanoseconds == 999999900);

	CalendarTimeType const calendar = Calendar_Time(FileTimeType{125764335301230000ull});
	Check("the calendar year", calendar.Year == 1999);
	Check("the calendar month", calendar.Month == 7);
	Check("the calendar day", calendar.Day == 14);
	Check("the day of the week, a Wednesday", calendar.DayOfWeek == 3);
	Check("the calendar clock", calendar.Hour == 13 && calendar.Minute == 45 && calendar.Second == 30);
	Check("the calendar milliseconds", calendar.Milliseconds == 123);

	CalendarTimeType const origin = Calendar_Time(FileTimeType{});
	Check("the origin is the first of January 1601", origin.Year == 1601 && origin.Month == 1 && origin.Day == 1);

	Check_Equal("a time before 1980 packs to nothing", (long long)Dos_Date_Time(FileTimeType{}), 0);
	Check_Equal("a time after 2107 packs to nothing",
		(long long)Dos_Date_Time(File_Time_From_Unix(4354819200LL, 0)), 0);
	Check_Equal("the first DOS second", (long long)Dos_Date_Time(File_Time_From_Unix(315532800LL, 0)), 0x00210000LL);
	Check_Equal("odd seconds pack down to the even one",
		(long long)Dos_Date_Time(File_Time_From_Unix(315532801LL, 0)), 0x00210000LL);

	FileTimeType unused;
	Check("a DOS date with no month is refused", !File_Time_From_Dos_Date_Time(0, unused));
	Check("a DOS date on the thirtieth of February is refused",
		!File_Time_From_Dos_Date_Time(((unsigned int)((20 << 9) | (2 << 5) | 30)) << 16, unused));

	CalendarTimeType local;
	Check("the local calendar is available", Local_Calendar_Time(File_Time_Now(), local));
	Check("a local time is a real date", local.Month >= 1 && local.Month <= 12 && local.Day >= 1 && local.Day <= 31);
	Check("the clock reads after 2020", File_Time_Now() > File_Time_From_Unix(1577836800LL, 0));
}


static void Test_Wildcard_Search(void)
{
	Check("write the first file to search for", Write_File("scan_b.mix", 200));
	Check("write the second file to search for", Write_File("scan_a.mix", 100));
	Check("write a third with a capital", Write_File("SCAN_C.MIX", 50));
	Check("write a file the search must skip", Write_File("other.txt", 10));
	Check("write a file with no extension", Write_File("noext", 5));

	std::vector<PlatformFileInfoType> const found = Platform_Find_Files(Scratch_Path("scan_*.mix").c_str());

	// Sorted without regard to case, so the file written second comes first and the capital
	// sorts among the rest. The order decides which of a set of expansion archives overrides
	// which, and it must not depend on the host.
	Check_Equal("the search finds all three", (long long)found.size(), 3);
	if (found.size() == 3) {
		Check("the search is in name order",
			found[0].Name == "scan_a.mix" && found[1].Name == "scan_b.mix" && found[2].Name == "SCAN_C.MIX");
		Check_Equal("the length of the first match", (long long)found[0].Size, 100);
		Check_Equal("the length of the second match", (long long)found[1].Size, 200);
		Check("a match is a plain file", !found[0].IsDirectory && !found[0].IsHidden);
	}

	Check("a search with no matches finds nothing", Platform_Find_Files(Scratch_Path("nomatch_*.mix").c_str()).empty());

	// "*.*" is DOS for every name, a name without an extension included.
	bool extensionless = false;
	for (PlatformFileInfoType const & entry : Platform_Find_Files(Scratch_Path("*.*").c_str())) {
		if (entry.Name == "noext") extensionless = true;
	}
	Check("*.* reaches a name with no extension", extensionless);

	// A search without a wildcard names one entry. The map generator asks after its cache
	// directory this way, so a directory has to answer.
	std::vector<PlatformFileInfoType> const single = Platform_Find_Files(Scratch_Path("scan_a.mix").c_str());
	Check("a search without a wildcard finds the file named", single.size() == 1 && single[0].Name == "scan_a.mix");

	std::vector<PlatformFileInfoType> const folder = Platform_Find_Files(Scratch.c_str());
	Check("a search without a wildcard finds a directory", folder.size() == 1 && folder[0].IsDirectory);

	Check("names order without regard to case", Platform_Name_Order("a.mix", "B.MIX") && !Platform_Name_Order("B.MIX", "a.mix"));
	Check("names differing only in case order by byte", Platform_Name_Order("A.MIX", "a.mix"));
	Check("a name orders after its own prefix", Platform_Name_Order("scan", "scan_a"));
}


// A subdirectory reached with a backslash, and a name asked for in a case other than the one it
// was created under. The engine does both: it spells its paths with backslashes and asks for
// TIBSUN.MIX in upper case whatever case the file was installed under.
static void Test_Subdirectory_And_Case(void)
{
	std::string const folder = Scratch_Path("sub");

	Check("create a subdirectory", Platform_Create_Directory(folder.c_str()));
	Check("creating it twice fails", !Platform_Create_Directory(folder.c_str()));

	std::string const path = folder + "\\MixedCase.Dat";
	PlatformFileClass file;
	Check("create a file in the subdirectory", file.Open(path.c_str(), PlatformOpenType::WRITE));
	if (!file.Is_Open()) return;

	std::uint32_t put = 0;
	file.Write("opents", 6, put);
	file.Close();

	std::string const shouted = folder + "\\MIXEDCASE.DAT";
	PlatformFileClass reopened;
	Check("open the file under a different case", reopened.Open(shouted.c_str(), PlatformOpenType::READ));

	if (reopened.Is_Open()) {
		char read[8] = {0};
		std::uint32_t got = 0;
		reopened.Read(read, 6, got);
		Check_Equal("length read under a different case", got, 6);
		Check("contents read under a different case", std::memcmp(read, "opents", 6) == 0);
		reopened.Close();
	}

	std::vector<PlatformFileInfoType> const found = Platform_Find_Files((folder + "\\mixedcase.*").c_str());
	Check("search for a pattern in a different case", found.size() == 1 && found[0].Name == "MixedCase.Dat");

	Check("a directory in a different case is found", Exists(Scratch_Path("SUB")));

	Check("delete the file in the subdirectory under a different case", Platform_Remove_File(shouted.c_str()));
	Check("the file is gone", !Exists(path));
}


static void Test_Remove_Replace_Copy(void)
{
	Check("write the file to delete", Write_File("doomed.dat", 16));

	std::string const doomed = Scratch_Path("doomed.dat");
	Check("delete the file", Platform_Remove_File(doomed.c_str()));
	Check("the file is gone", !Exists(doomed));
	Check("deleting it again fails", !Platform_Remove_File(doomed.c_str()));

	// A save is written under a temporary name and moved over the old one in one step.
	Check("write the file to be replaced", Write_File("target.dat", 30));
	Check("write the replacement", Write_File("target.dat.tmp", 70));

	std::string const target = Scratch_Path("target.dat");
	std::string const temporary = Scratch_Path("target.dat.tmp");
	Check("replace the file", Platform_Replace_File(temporary.c_str(), target.c_str()));
	Check("the temporary name is gone", !Exists(temporary));

	PlatformFileInfoType info;
	Check("the replacement is under the target name", Platform_File_Info(target.c_str(), info) && info.Size == 70);

	Check("replacing from a missing file fails", !Platform_Replace_File(temporary.c_str(), target.c_str()));
	Check("a failed replace leaves the target alone", Platform_File_Info(target.c_str(), info) && info.Size == 70);

	std::string const copy = Scratch_Path("copy.dat");
	Check("copy the file", Platform_Copy_File(target.c_str(), copy.c_str()));
	Check("the copy has the length", Platform_File_Info(copy.c_str(), info) && info.Size == 70);
	Check("write a shorter file", Write_File("short.dat", 5));
	Check("copy over an existing file", Platform_Copy_File(Scratch_Path("short.dat").c_str(), copy.c_str()));
	Check("the copy was overwritten", Platform_File_Info(copy.c_str(), info) && info.Size == 5);
}


// A file must stay usable while another is opened and closed, and a closed one must stop
// being usable.
static void Test_Handles(void)
{
	Check("write the file to hold open", Write_File("handle.dat", 16));

	PlatformFileClass first;
	PlatformFileClass second;

	Check("open the file once", first.Open(Scratch_Path("handle.dat").c_str(), PlatformOpenType::READ));
	Check("open the file twice", second.Open(Scratch_Path("handle.dat").c_str(), PlatformOpenType::READ));

	Check("close the first", first.Close());
	Check("closing it twice fails", !first.Close());
	Check_Equal("the other still works", second.Size(), 16);

	PlatformFileClass moved(std::move(second));
	Check("a moved file is open where it went", moved.Is_Open() && !second.Is_Open());
	Check_Equal("and still works there", moved.Size(), 16);

	std::uint32_t got = 0;
	unsigned char byte = 0;
	Check("a closed file cannot be read", !first.Read(&byte, 1, got));
}


static void Clean_Up(void)
{
	char const * const leftovers[] = {
		"basic.dat", "seek.dat", "mode.dat", "exclusive.dat", "fresh.dat", "info.dat", "times.dat",
		"scan_a.mix", "scan_b.mix", "SCAN_C.MIX", "other.txt", "noext", "target.dat", "copy.dat",
		"short.dat", "handle.dat"
	};

	for (char const * name : leftovers) {
		Platform_Remove_File(Scratch_Path(name).c_str());
	}

	// The layer removes no directories, and std::filesystem::remove leaves one in place
	// under node on macOS, so POSIX asks rmdir directly.
	for (std::string const & folder : {Scratch_Path("sub"), Scratch}) {
#if defined(_WIN32)
		std::error_code ignored;
		std::filesystem::remove(folder, ignored);
#else
		::rmdir(Platform_Host_Path(folder.c_str()).c_str());
#endif
	}
}


int main(int argc, char ** argv)
{
	Scratch = (argc > 1) ? argv[1] : ".";
	Scratch += "\\platformfile-scratch";

	// What a run that stopped part way left behind.
	Clean_Up();
	Platform_Create_Directory(Scratch.c_str());

	PlatformFileInfoType scratch;
	if (!Platform_File_Info(Scratch.c_str(), scratch) || !scratch.IsDirectory) {
		std::printf("FATAL: could not create the scratch directory %s\n", Scratch.c_str());
		return(1);
	}

	Test_Write_Read_Size();
	Test_Seek();
	Test_Open_Modes();
	Test_Info();
	Test_File_Times();
	Test_Time_Arithmetic();
	Test_Wildcard_Search();
	Test_Subdirectory_And_Case();
	Test_Remove_Replace_Copy();
	Test_Handles();

	Clean_Up();

	std::printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
