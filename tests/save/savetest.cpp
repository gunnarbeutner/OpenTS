// Exercises the file a saved game is kept in: the field table the load dialog lists from,
// the compressed content block, and every way the reader refuses a file that is not a
// whole, intact save of a version it knows.
//
// Every file it touches it creates itself, in a scratch directory named by the first
// argument or the working directory, so it reads no game data and leaves nothing behind.

#include "savefile.h"

#include "platform/file.h"

#include <lzo/lzo1x.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int Failures = 0;
static int Checks = 0;
static std::string Scratch;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;
	Failures++;
	printf("FAIL %s\n", name);
}


static void Check_Result(char const * name, SaveFileClass::ResultType actual, SaveFileClass::ResultType expected)
{
	Checks++;
	if (actual == expected) return;
	Failures++;
	printf("FAIL %s: got \"%s\", expected \"%s\"\n", name,
		SaveFileClass::Result_Text(actual), SaveFileClass::Result_Text(expected));
}


static std::string Scratch_Path(char const * name)
{
	return(Scratch + "\\" + name);
}


static std::vector<unsigned char> Noise(std::size_t length, unsigned int seed)
{
	std::vector<unsigned char> data(length);
	unsigned int state = seed * 2654435761u + 1u;
	for (std::size_t index = 0; index < length; index++) {
		state = state * 1103515245u + 12345u;
		data[index] = (unsigned char)((state >> 16) & 0xFF);
	}
	return(data);
}


static std::vector<unsigned char> Prose(std::size_t length)
{
	static char const text[] = "The quick brown fox jumps over the lazy dog. ";
	std::vector<unsigned char> data;
	while (data.size() < length) {
		data.push_back((unsigned char)text[data.size() % (sizeof(text) - 1)]);
	}
	return(data);
}


static std::vector<unsigned char> Read_Whole_File(char const * path)
{
	std::vector<unsigned char> data;
	PlatformFileClass file;
	if (!file.Open(path, PlatformOpenType::READ)) return(data);
	std::int64_t const size = file.Size();
	if (size > 0) {
		data.resize((std::size_t)size);
		std::uint32_t got = 0;
		if (!file.Read(data.data(), (std::uint32_t)size, got) || got != (std::uint32_t)size) data.clear();
	}
	return(data);
}


static bool Write_Whole_File(char const * path, std::vector<unsigned char> const & data)
{
	PlatformFileClass file;
	if (!file.Open(path, PlatformOpenType::WRITE)) return(false);
	std::uint32_t written = 0;
	bool ok = true;
	if (!data.empty()) {
		ok = file.Write(data.data(), (std::uint32_t)data.size(), written) && written == data.size();
	}
	return(file.Close() && ok);
}


static bool File_Exists(char const * path)
{
	PlatformFileInfoType info;
	return(Platform_File_Info(path, info));
}


enum {
	FIELD_TITLE = 2,
	FIELD_HOUSE = 3,
	FIELD_VERSION = 16,
	FIELD_WHEN = 13,
	FIELD_MISSING = 77,
};


// The content of a save is a section now, so the checks below put one in and take it out.
enum { SECTION_ONE = 1 };


static SaveFileClass::ResultType Write_One(SaveFileClass & save, char const * path,
	std::vector<unsigned char> const & content)
{
	SaveFileClass::ResultType result = save.Begin_Write(path);
	if (result != SaveFileClass::RESULT_OK) return(result);

	if (!content.empty()) {
		result = save.Write_Section(SECTION_ONE, content.data(), (std::uint32_t)content.size());
		if (result != SaveFileClass::RESULT_OK) {
			save.Abandon_Write();
			return(result);
		}
	}

	unsigned char const names[2] = {0, 0};
	return(save.End_Write(names, sizeof(names)));
}


static std::vector<unsigned char> Read_One(SaveFileClass const & save)
{
	std::vector<unsigned char> out;
	save.Get_Section(SECTION_ONE, out);
	return(out);
}


static void Fill(SaveFileClass & save, std::vector<unsigned char> const & content)
{
	FileTimeType const when = FileTimeType::From_Parts(0x12345678u, 0x01D2C3B4u);

	save.Set_String(FIELD_TITLE, "GDI 04: Eviction Notice");
	save.Set_String(FIELD_HOUSE, "GDI");
	save.Set_Int(FIELD_VERSION, 0x00010203);
	save.Set_Time(FIELD_WHEN, when);
	(void)content;
}


static void Check_Fields(char const * prefix, SaveFileClass const & save)
{
	char text[64];
	int value = 0;
	FileTimeType when;

	Check((std::string(prefix) + ": title present").c_str(), save.Get_String(FIELD_TITLE, text, sizeof(text)));
	Check((std::string(prefix) + ": title text").c_str(), strcmp(text, "GDI 04: Eviction Notice") == 0);
	Check((std::string(prefix) + ": house present").c_str(), save.Get_String(FIELD_HOUSE, text, sizeof(text)));
	Check((std::string(prefix) + ": house text").c_str(), strcmp(text, "GDI") == 0);
	Check((std::string(prefix) + ": version present").c_str(), save.Get_Int(FIELD_VERSION, &value));
	Check((std::string(prefix) + ": version value").c_str(), value == 0x00010203);
	Check((std::string(prefix) + ": time present").c_str(), save.Get_Time(FIELD_WHEN, &when));
	Check((std::string(prefix) + ": time value").c_str(),
		when.Low() == 0x12345678u && when.High() == 0x01D2C3B4u && when.Ticks == 0x01D2C3B412345678ull);
	Check((std::string(prefix) + ": a missing field is absent").c_str(), !save.Get_String(FIELD_MISSING, text, sizeof(text)));
	Check((std::string(prefix) + ": a field is not found under another kind").c_str(), !save.Get_Int(FIELD_TITLE, &value));

	Check((std::string(prefix) + ": a short buffer is clipped").c_str(),
		save.Get_String(FIELD_TITLE, text, 4) && strcmp(text, "GDI") == 0);
}


static void Test_Round_Trip(void)
{
	std::string const path = Scratch_Path("ROUNDTRIP.SAV");
	std::vector<unsigned char> const content = Prose(300000);

	SaveFileClass written;
	Fill(written, content);
	Check_Result("round trip: write", Write_One(written, path.c_str(), content), SaveFileClass::RESULT_OK);
	Check("round trip: no temporary file is left behind", !File_Exists((path + ".tmp").c_str()));

	std::vector<unsigned char> const image = Read_Whole_File(path.c_str());
	Check("round trip: the prose was compressed", !image.empty() && image.size() < content.size() / 4);

	SaveFileClass read;
	Check_Result("round trip: read", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	Check_Fields("round trip", read);
	Check("round trip: content reads back whole", Read_One(read) == content);

	SaveFileClass listed;
	Check_Result("round trip: fields alone", listed.Read_Fields(path.c_str()), SaveFileClass::RESULT_OK);
	Check_Fields("fields alone", listed);
	Check("fields alone: no content is read", Read_One(listed).empty());
}


static void Test_Cuts(void)
{
	SaveFileClass save;
	save.Set_String(FIELD_TITLE, "ab\xC3\xA9" "cd");

	char text[8];
	Check("cuts: a cut never splits a character", save.Get_String(FIELD_TITLE, text, 4) && strcmp(text, "ab") == 0);
	Check("cuts: a cut after a character keeps it whole", save.Get_String(FIELD_TITLE, text, 5) && strcmp(text, "ab\xC3\xA9") == 0);
	Check("cuts: a buffer that fits keeps everything", save.Get_String(FIELD_TITLE, text, 8) && strcmp(text, "ab\xC3\xA9" "cd") == 0);
}


static void Test_Incompressible(void)
{
	std::string const path = Scratch_Path("NOISE.SAV");
	std::vector<unsigned char> const content = Noise(70000, 7);

	SaveFileClass written;
	Fill(written, content);
	Check_Result("noise: write", Write_One(written, path.c_str(), content), SaveFileClass::RESULT_OK);

	std::vector<unsigned char> const image = Read_Whole_File(path.c_str());
	Check("noise: stored as it is when compression does not pay", image.size() >= content.size() + SaveFileClass::HEADER_SIZE);

	SaveFileClass read;
	Check_Result("noise: read", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	Check("noise: content reads back whole", Read_One(read) == content);
}


static void Test_Empty(void)
{
	std::string const path = Scratch_Path("EMPTY.SAV");

	SaveFileClass written;
	Check_Result("empty: write", Write_One(written, path.c_str(), {}), SaveFileClass::RESULT_OK);

	std::vector<unsigned char> const image = Read_Whole_File(path.c_str());
	Check("empty: a header and the shortest name table",
		image.size() == SaveFileClass::HEADER_SIZE + 2);

	SaveFileClass read;
	Check_Result("empty: read", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	Check("empty: no content", Read_One(read).empty());
	char text[8];
	Check("empty: no fields", !read.Get_String(FIELD_TITLE, text, sizeof(text)));
}


static void Test_Overwrite(void)
{
	std::string const path = Scratch_Path("REPLACE.SAV");

	std::vector<unsigned char> const earlier = Prose(5000);
	std::vector<unsigned char> const later = Noise(20000, 11);

	SaveFileClass first;
	Fill(first, earlier);
	first.Set_String(FIELD_TITLE, "the earlier save");
	Check_Result("replace: first write", Write_One(first, path.c_str(), earlier), SaveFileClass::RESULT_OK);

	Check("replace: a stale temporary is planted", Write_Whole_File((path + ".tmp").c_str(), Noise(100, 3)));

	SaveFileClass second;
	Fill(second, later);
	second.Set_String(FIELD_TITLE, "the later save");
	Check_Result("replace: second write", Write_One(second, path.c_str(), later), SaveFileClass::RESULT_OK);
	Check("replace: the stale temporary is gone", !File_Exists((path + ".tmp").c_str()));

	SaveFileClass read;
	Check_Result("replace: read", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	char text[64];
	Check("replace: the later save is the one on disk",
		read.Get_String(FIELD_TITLE, text, sizeof(text)) && strcmp(text, "the later save") == 0);
	Check("replace: the later content is the one on disk", Read_One(read) == later);

	SaveFileClass rewritten;
	rewritten.Set_String(FIELD_TITLE, "overwritten field");
	rewritten.Set_String(FIELD_TITLE, "final field");
	Check_Result("replace: field rewrite", Write_One(rewritten, path.c_str(), {}), SaveFileClass::RESULT_OK);
	Check_Result("replace: field rewrite read", read.Read_Fields(path.c_str()), SaveFileClass::RESULT_OK);
	Check("replace: a field set twice keeps the last value",
		read.Get_String(FIELD_TITLE, text, sizeof(text)) && strcmp(text, "final field") == 0);
}


/*
** A save with fixed fields and content must come out as the same bytes the format has always
** produced. The size and checksum were recorded from the writer that stored Win32 FILETIMEs;
** the time field is spelled out byte for byte: eight bytes, the low word first, each
** little-endian.
*/
static void Test_Fixed_Image(void)
{
	std::string const path = Scratch_Path("FIXED.SAV");

	std::vector<unsigned char> content = Prose(5000);
	for (unsigned int index = 0; index < 700; index++) {
		content.push_back((unsigned char)((index * 2654435761u) >> 24));
	}

	SaveFileClass written;
	written.Set_String(FIELD_TITLE, "GDI 04: Eviction Notice");
	written.Set_String(FIELD_HOUSE, "GDI");
	written.Set_Int(FIELD_VERSION, 0x00010203);
	written.Set_Time(FIELD_WHEN, FileTimeType::From_Parts(0x12345678u, 0x01D2C3B4u));
	written.Set_Time(12, FileTimeType{0x01DC2F0A9B8C7D6Eull});

	unsigned char const names[4] = {1, 0, 4, 0};
	Check_Result("fixed image: begin", written.Begin_Write(path.c_str()), SaveFileClass::RESULT_OK);
	Check_Result("fixed image: section",
		written.Write_Section(SECTION_ONE, content.data(), (std::uint32_t)content.size()), SaveFileClass::RESULT_OK);
	Check_Result("fixed image: end", written.End_Write(names, sizeof(names)), SaveFileClass::RESULT_OK);

	std::vector<unsigned char> const image = Read_Whole_File(path.c_str());
	Check("fixed image: the length it has always had", image.size() == 922);
	Check("fixed image: the bytes it has always had",
		SaveFileClass::Checksum(image.data(), (std::uint32_t)image.size()) == 0x8FF1F0CCu);

	unsigned char const field[16] = {
		0x0D, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00,
		0x78, 0x56, 0x34, 0x12, 0xB4, 0xC3, 0xD2, 0x01,
	};
	bool found = false;
	for (std::size_t at = SaveFileClass::HEADER_SIZE; at + sizeof(field) <= image.size(); at++) {
		if (memcmp(image.data() + at, field, sizeof(field)) == 0) found = true;
	}
	Check("fixed image: a time is stored low word first", found);

	SaveFileClass read;
	FileTimeType when;
	Check_Result("fixed image: read", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	Check("fixed image: a time reads back whole", read.Get_Time(12, &when) && when.Ticks == 0x01DC2F0A9B8C7D6Eull);
}


static void Put_U32(std::vector<unsigned char> & image, std::size_t at, unsigned int value)
{
	image[at] = (unsigned char)(value & 0xFF);
	image[at + 1] = (unsigned char)((value >> 8) & 0xFF);
	image[at + 2] = (unsigned char)((value >> 16) & 0xFF);
	image[at + 3] = (unsigned char)((value >> 24) & 0xFF);
}


static void Test_Limits(void)
{
	std::string const path = Scratch_Path("LIMITS.SAV");

	std::vector<unsigned char> const small = Prose(3000);

	SaveFileClass kept;
	Fill(kept, small);
	kept.Set_String(FIELD_TITLE, "the save that stays");
	Check_Result("limits: the save that stays", Write_One(kept, path.c_str(), small), SaveFileClass::RESULT_OK);

	SaveFileClass wide;
	Fill(wide, small);
	wide.Set_String(FIELD_TITLE, std::string(0x10001, 'x').c_str());
	Check_Result("limits: a field beyond its limit is refused",
		wide.Begin_Write(path.c_str()), SaveFileClass::RESULT_TOO_LARGE);

	SaveFileClass many;
	Fill(many, small);
	for (int id = 100; id < 117; id++) {
		many.Set_String(id, std::string(0x10000, 'y').c_str());
	}
	Check_Result("limits: a table beyond its limit is refused",
		many.Begin_Write(path.c_str()), SaveFileClass::RESULT_TOO_LARGE);

	SaveFileClass huge;
	Check_Result("limits: a section beyond its limit is refused",
		Write_One(huge, path.c_str(), std::vector<unsigned char>(0x10000001)), SaveFileClass::RESULT_TOO_LARGE);

	Check("limits: no temporary is left behind", !File_Exists((path + ".tmp").c_str()));
	SaveFileClass read;
	Check_Result("limits: the earlier save still reads", read.Read(path.c_str()), SaveFileClass::RESULT_OK);
	char text[64];
	Check("limits: the earlier save is the one on disk",
		read.Get_String(FIELD_TITLE, text, sizeof(text)) && strcmp(text, "the save that stays") == 0);
}


// Recomputes the payload checksum after a test has changed a byte inside it on purpose,
// so that the check under test is the one that answers rather than the checksum.
static void Reseal_Payload(std::vector<unsigned char> & image)
{
	unsigned int const payload = (unsigned int)image[12] | ((unsigned int)image[13] << 8)
		| ((unsigned int)image[14] << 16) | ((unsigned int)image[15] << 24);
	unsigned int const length = (unsigned int)image[16] | ((unsigned int)image[17] << 8)
		| ((unsigned int)image[18] << 16) | ((unsigned int)image[19] << 24);

	image[32] = 0; image[33] = 0; image[34] = 0; image[35] = 0;
	unsigned int const crc = SaveFileClass::Checksum(image.data() + payload, length);
	image[32] = (unsigned char)(crc & 0xFF);
	image[33] = (unsigned char)((crc >> 8) & 0xFF);
	image[34] = (unsigned char)((crc >> 16) & 0xFF);
	image[35] = (unsigned char)((crc >> 24) & 0xFF);
}


// Recomputes the header checksum after a test has changed a header byte on purpose.
static void Reseal_Header(std::vector<unsigned char> & image, unsigned int table)
{
	unsigned int crc = SaveFileClass::Checksum(image.data(), SaveFileClass::HEADER_SIZE - 4);
	crc = SaveFileClass::Checksum(image.data() + SaveFileClass::HEADER_SIZE, table, crc);
	Put_U32(image, 36, crc);
}


// Rebuilds a save image around a field table of the test's own making, with the content
// kept and every checksum made good.
static std::vector<unsigned char> Forge_Table(std::vector<unsigned char> const & image, unsigned int table,
	std::vector<unsigned char> const & newtable)
{
	std::vector<unsigned char> forged(image.begin(), image.begin() + SaveFileClass::HEADER_SIZE);
	forged.insert(forged.end(), newtable.begin(), newtable.end());
	forged.insert(forged.end(), image.begin() + SaveFileClass::HEADER_SIZE + table, image.end());
	Put_U32(forged, 8, (unsigned int)newtable.size());
	Put_U32(forged, 12, SaveFileClass::HEADER_SIZE + (unsigned int)newtable.size());
	Reseal_Header(forged, (unsigned int)newtable.size());
	return(forged);
}


// Replaces the content of a save image with a compressed block of the test's own making,
// declared as expanding to the length given, with every checksum made good.
static std::vector<unsigned char> Forge_Content(std::vector<unsigned char> const & image, unsigned int table,
	std::vector<unsigned char> const & stored, unsigned int expands_to)
{
	std::vector<unsigned char> forged(image.begin(), image.begin() + SaveFileClass::HEADER_SIZE + table);
	forged.insert(forged.end(), stored.begin(), stored.end());
	forged[6] |= 0x01;
	Put_U32(forged, 12, SaveFileClass::HEADER_SIZE + table);
	Put_U32(forged, 16, (unsigned int)stored.size());
	Put_U32(forged, 20, expands_to);
	Put_U32(forged, 24, SaveFileClass::Checksum(stored.data(), (unsigned int)stored.size()));
	Reseal_Header(forged, table);
	return(forged);
}


static void Test_Refusals(void)
{
	SaveFileClass read;

	std::string const missing = Scratch_Path("MISSING.SAV");
	Check_Result("refuse: a missing file", read.Read(missing.c_str()), SaveFileClass::RESULT_MISSING);
	Check_Result("refuse: a missing file's fields", read.Read_Fields(missing.c_str()), SaveFileClass::RESULT_MISSING);

	std::string const plain = Scratch_Path("PLAIN.SAV");
	std::vector<unsigned char> hello = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
	Write_Whole_File(plain.c_str(), hello);
	Check_Result("refuse: a file that is not a save", read.Read(plain.c_str()), SaveFileClass::RESULT_NOT_A_SAVE);
	Check_Result("refuse: its fields", read.Read_Fields(plain.c_str()), SaveFileClass::RESULT_NOT_A_SAVE);

	std::string const good = Scratch_Path("GOOD.SAV");
	SaveFileClass written;
	Fill(written, Prose(40000));
	Check_Result("refuse: the reference save", Write_One(written, good.c_str(), Prose(40000)), SaveFileClass::RESULT_OK);
	std::vector<unsigned char> const image = Read_Whole_File(good.c_str());
	Check("refuse: the reference save is readable", !image.empty());

	std::string const damaged = Scratch_Path("DAMAGED.SAV");

	unsigned int const table = (unsigned int)image[8] | ((unsigned int)image[9] << 8)
		| ((unsigned int)image[10] << 16) | ((unsigned int)image[11] << 24);

	std::vector<unsigned char> future = image;
	future[4] = 99;
	future[5] = 0;
	Reseal_Header(future, table);
	Write_Whole_File(damaged.c_str(), future);
	Check_Result("refuse: a later format version", read.Read(damaged.c_str()), SaveFileClass::RESULT_UNSUPPORTED_VERSION);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_UNSUPPORTED_VERSION);

	std::vector<unsigned char> flagged = image;
	flagged[6] |= 0x02;
	Reseal_Header(flagged, table);
	Write_Whole_File(damaged.c_str(), flagged);
	Check_Result("refuse: a header flag this build does not know", read.Read(damaged.c_str()), SaveFileClass::RESULT_UNSUPPORTED_VERSION);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_UNSUPPORTED_VERSION);

	unsigned int const payload = (unsigned int)image[12] | ((unsigned int)image[13] << 8)
		| ((unsigned int)image[14] << 16) | ((unsigned int)image[15] << 24);
	unsigned int const names_at = (unsigned int)image[20] | ((unsigned int)image[21] << 8)
		| ((unsigned int)image[22] << 16) | ((unsigned int)image[23] << 24);

	// The one section this save carries, and the length it says it unpacks to.
	unsigned int const unpacked = (unsigned int)image[payload + 6] | ((unsigned int)image[payload + 7] << 8)
		| ((unsigned int)image[payload + 8] << 16) | ((unsigned int)image[payload + 9] << 24);
	Check("refuse: the reference save has one section", payload + 10 < names_at);

	std::vector<unsigned char> short_block = image;
	Put_U32(short_block, payload + 6, unpacked + 1);
	Reseal_Payload(short_block);
	Reseal_Header(short_block, table);
	Write_Whole_File(damaged.c_str(), short_block);
	SaveFileClass damaged_read;
	Check_Result("refuse: a section that ends before its declared length",
		damaged_read.Read(damaged.c_str()), SaveFileClass::RESULT_OK);
	std::vector<unsigned char> out;
	Check("refuse: and its section will not unpack", !damaged_read.Get_Section(SECTION_ONE, out));

	// The reader sizes the output buffer from the declared length, so this block runs past
	// the end of it. The bounds-checked decompressor stops there rather than writing on.
	std::vector<unsigned char> long_block = image;
	Put_U32(long_block, payload + 6, unpacked - 1);
	Reseal_Payload(long_block);
	Reseal_Header(long_block, table);
	Write_Whole_File(damaged.c_str(), long_block);
	Check_Result("refuse: a section that expands past its declared length",
		damaged_read.Read(damaged.c_str()), SaveFileClass::RESULT_OK);
	Check("refuse: and that section will not unpack either", !damaged_read.Get_Section(SECTION_ONE, out));

	std::vector<unsigned char> vast = image;
	Put_U32(vast, payload + 6, 0x10000001);
	Reseal_Payload(vast);
	Reseal_Header(vast, table);
	Write_Whole_File(damaged.c_str(), vast);
	Check_Result("refuse: a section declared larger than any save",
		damaged_read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> stretched = image;
	Put_U32(stretched, payload + 2, (unsigned int)image.size());
	Reseal_Payload(stretched);
	Reseal_Header(stretched, table);
	Write_Whole_File(damaged.c_str(), stretched);
	Check_Result("refuse: a section claiming more than the payload holds",
		damaged_read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> const oversized(0x100001, 0);
	Write_Whole_File(damaged.c_str(), Forge_Table(image, table, oversized));
	Check_Result("refuse: a field table longer than any listing", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> gapped = image;
	gapped.insert(gapped.begin() + payload, 8, 0);
	Put_U32(gapped, 12, payload + 8);
	Reseal_Header(gapped, table);
	Check("refuse: the gapped image is longer", gapped.size() == image.size() + 8);
	Write_Whole_File(damaged.c_str(), gapped);
	Check_Result("refuse: a gap between the table and the content", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> header_hit = image;
	header_hit[9] ^= 0x01;
	Write_Whole_File(damaged.c_str(), header_hit);
	Check_Result("refuse: a header byte flipped", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> table_hit = image;
	table_hit[SaveFileClass::HEADER_SIZE + 10] ^= 0x20;
	Write_Whole_File(damaged.c_str(), table_hit);
	Check_Result("refuse: a field byte flipped", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
	Check_Result("refuse: its fields", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);

	std::vector<unsigned char> content_hit = image;
	content_hit[image.size() - 40] ^= 0x80;
	Write_Whole_File(damaged.c_str(), content_hit);
	Check_Result("refuse: a content byte flipped", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
	Check_Result("refuse: a flipped content byte still lists", read.Read_Fields(damaged.c_str()), SaveFileClass::RESULT_OK);

	std::size_t const cuts[] = { 3, 12, SaveFileClass::HEADER_SIZE - 1, SaveFileClass::HEADER_SIZE + 5,
		SaveFileClass::HEADER_SIZE + table, image.size() / 2, image.size() - 1 };
	for (std::size_t cut : cuts) {
		std::vector<unsigned char> truncated(image.begin(), image.begin() + cut);
		Write_Whole_File(damaged.c_str(), truncated);
		char name[80];
		snprintf(name, sizeof(name), "refuse: a file cut at %u bytes", (unsigned int)cut);
		SaveFileClass::ResultType const result = read.Read(damaged.c_str());
		Check(name, result == SaveFileClass::RESULT_CORRUPT || (cut < 4 && result == SaveFileClass::RESULT_NOT_A_SAVE));
	}

	std::vector<unsigned char> appended = image;
	appended.push_back(0);
	Write_Whole_File(damaged.c_str(), appended);
	Check_Result("refuse: a file with a trailing byte", read.Read(damaged.c_str()), SaveFileClass::RESULT_CORRUPT);
}


int main(int argc, char ** argv)
{
	if (lzo_init() != LZO_E_OK) {
		printf("lzo_init failed\n");
		return(2);
	}

	Scratch = (argc > 1) ? argv[1] : ".";
	Platform_Create_Directory(Scratch.c_str());

	Test_Round_Trip();
	Test_Cuts();
	Test_Incompressible();
	Test_Empty();
	Test_Overwrite();
	Test_Fixed_Image();
	Test_Limits();
	Test_Refusals();

	char const * const names[] = { "ROUNDTRIP.SAV", "NOISE.SAV", "EMPTY.SAV", "REPLACE.SAV",
		"LIMITS.SAV", "PLAIN.SAV", "GOOD.SAV", "DAMAGED.SAV", "FIXED.SAV" };
	for (char const * name : names) {
		Platform_Remove_File(Scratch_Path(name).c_str());
	}

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
