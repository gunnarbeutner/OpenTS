/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises the LCW pipe and straw against blocks whose headers are plausible and whose
// payloads are damaged, which reach the engine from downloaded maps and saved games. Block
// sizing, round trips and headers the compressor could not have written are tests/lcwblock's.

#include "lcw.h"
#include "lcwpipe.h"
#include "lcwstraw.h"

#include <cstdio>
#include <cstring>
#include <vector>


static int Failures = 0;
static int Checks = 0;

static int const BLOCK_SIZE = 1024 * 8;


static void Check(char const * name, bool condition, char const * detail)
{
	Checks++;
	if (!condition) {
		Failures++;
		printf("FAIL %s: %s\n", name, detail);
	}
}


/*
** Collects everything the pipe chain produces so a test can compare it with what it fed in.
*/
class Capture_Pipe : public Pipe
{
	public:
		virtual int Put(void const * source, int slen) override
		{
			if (source != NULL && slen > 0) {
				unsigned char const * bytes = (unsigned char const *)source;
				Data.insert(Data.end(), bytes, bytes + slen);
			}
			return(slen);
		}

		std::vector<unsigned char> Data;
};


/*
** Serves a fixed block of bytes to the straw chain, running dry at the end the way a
** truncated file would.
*/
class Buffer_Straw : public Straw
{
	public:
		Buffer_Straw(std::vector<unsigned char> const & data) : Data(data), Position(0) {}

		virtual int Get(void * buffer, int slen) override
		{
			int const left = (int)Data.size() - Position;
			int const len = (slen < left) ? slen : left;

			if (len > 0) {
				memmove(buffer, &Data[(size_t)Position], (size_t)len);
				Position += len;
			}
			return(len > 0 ? len : 0);
		}

	private:
		std::vector<unsigned char> Data;
		int Position;
};


static void Append_Block(std::vector<unsigned char> & stream, unsigned compcount, unsigned uncompcount,
	std::vector<unsigned char> const & payload)
{
	stream.push_back((unsigned char)(compcount & 0xff));
	stream.push_back((unsigned char)((compcount >> 8) & 0xff));
	stream.push_back((unsigned char)(uncompcount & 0xff));
	stream.push_back((unsigned char)((uncompcount >> 8) & 0xff));
	stream.insert(stream.end(), payload.begin(), payload.end());
}


static std::vector<unsigned char> Compress_Through_Pipe(std::vector<unsigned char> const & data)
{
	Capture_Pipe out;
	LCWPipe comp(LCWPipe::COMPRESS);

	comp.Put_To(&out);
	comp.Put(data.data(), (int)data.size());
	comp.End();

	return(out.Data);
}


static std::vector<unsigned char> Decompress_Through_Pipe(std::vector<unsigned char> const & stream)
{
	Capture_Pipe out;
	LCWPipe decomp(LCWPipe::DECOMPRESS);

	decomp.Put_To(&out);
	decomp.Put(stream.data(), (int)stream.size());
	decomp.End();

	return(out.Data);
}


static std::vector<unsigned char> Decompress_Through_Straw(std::vector<unsigned char> const & stream, int wanted)
{
	Buffer_Straw source(stream);
	LCWStraw decomp(LCWStraw::DECOMPRESS);

	decomp.Get_From(&source);

	std::vector<unsigned char> got((size_t)wanted, 0);
	int const len = decomp.Get(got.data(), wanted);

	got.resize((size_t)(len > 0 ? len : 0));
	return(got);
}


/*
** A small xorshift generator keeps the corpora identical on every platform.
*/
static unsigned int Random_State = 0;


static unsigned char Next_Random(void)
{
	Random_State ^= Random_State << 13;
	Random_State ^= Random_State >> 17;
	Random_State ^= Random_State << 5;
	return((unsigned char)(Random_State >> 16));
}


static std::vector<unsigned char> Sample_Data(size_t length)
{
	Random_State = 0x5eed1234u;

	std::vector<unsigned char> data;
	while (data.size() < length) {
		unsigned char const control = Next_Random();

		if ((control & 3) == 0) {
			size_t const run = (size_t)Next_Random() + 1;
			unsigned char const value = Next_Random();
			for (size_t i = 0; i < run; i++) data.push_back(value);
		} else {
			size_t const chunk = (size_t)(Next_Random() & 63) + 1;
			for (size_t i = 0; i < chunk; i++) data.push_back(Next_Random());
		}
	}

	data.resize(length);
	return(data);
}


/*
** A header the compressor could not have written is tests/lcwblock's subject. What is left
** to check here is a block whose header is plausible and whose payload is not: it must stop
** at the damage rather than write past the size it claims.
*/
static void Test_Damaged_Payloads(void)
{
	struct {
		char const * Name;
		unsigned CompCount;
		unsigned UncompCount;
		std::vector<unsigned char> Payload;
	} const cases[] = {
		{"run reaching past the block", 7, BLOCK_SIZE, {0x81, 'A', 0xfe, 0x00, 0xf0, 0x55, 0x80}},
		{"back reference before the block", 5, BLOCK_SIZE, {0x81, 'A', 0x01, 0x00, 0x80}},
	};

	for (auto const & test : cases) {
		std::vector<unsigned char> stream;
		Append_Block(stream, test.CompCount, test.UncompCount, test.Payload);

		char name[128];

		snprintf(name, sizeof(name), "pipe stops at a %s", test.Name);
		Check(name, Decompress_Through_Pipe(stream).empty(), "the pipe produced output from a damaged block");

		snprintf(name, sizeof(name), "straw stops at a %s", test.Name);
		Check(name, Decompress_Through_Straw(stream, BLOCK_SIZE).empty(), "the straw produced output from a damaged block");
	}

	/*
	** A block that simply runs out early is the truncation case the container has always
	** handled: the straw reports the source exhausted, and the pipe hands the bytes it
	** accumulated on untouched rather than decompressing a block that has no end code.
	*/
	{
		std::vector<unsigned char> stream;
		Append_Block(stream, 200, BLOCK_SIZE, std::vector<unsigned char>(50, 0x80));

		Check("the straw reports a truncated block exhausted", Decompress_Through_Straw(stream, BLOCK_SIZE).empty(),
			"the straw produced output from a truncated block");
		Check("the pipe passes a truncated block through", Decompress_Through_Pipe(stream) == stream,
			"the pipe did not pass the truncated block through untouched");
	}
}


/*
** A bad block in the middle of a stream must not cost the blocks before it, and must not
** be followed by output from anything after it.
*/
static void Test_Damaged_Stream_Stops(void)
{
	std::vector<unsigned char> const data = Sample_Data(BLOCK_SIZE);
	std::vector<unsigned char> const good = Compress_Through_Pipe(data);

	std::vector<unsigned char> stream = good;
	Append_Block(stream, 7, BLOCK_SIZE, {0x81, 'A', 0xfe, 0x00, 0xf0, 0x55, 0x80});
	stream.insert(stream.end(), good.begin(), good.end());

	Check("the pipe keeps the blocks before the damage", Decompress_Through_Pipe(stream) == data,
		"the pipe did not stop at the damaged block");
	Check("the straw keeps the blocks before the damage", Decompress_Through_Straw(stream, BLOCK_SIZE * 4) == data,
		"the straw did not stop at the damaged block");
}


int main(void)
{
	Test_Damaged_Payloads();
	Test_Damaged_Stream_Stops();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
