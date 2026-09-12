/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Pins the dialog font and its RmlUi engine against a synthetic pair of glyph sheets, with no
// engine and no game data: the sheets must be measured as ODGetFontMetrics measures them,
// coloured to the pixels the owner-draw dialogs leave on the frame, and drawn through RmlUi
// where ODDrawCharRemap draws them, while every other family still reaches RmlUi's own engine.

#include "ui/uifont.h"
#include "ui/uifontengine.h"
#include "ui/uitexture.h"

#include "dbgprint.h"
#include "utf8.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/TextShapingContext.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

int Failures = 0;


void Check(bool condition, char const * what)
{
	std::printf("%-84s %s\n", what, condition ? "ok" : "FAILED");

	if (!condition) {
		Failures++;
	}
}


// The sheet layout: a blank row above and a blank column left of every size box.
int const TOP = 1;
int const LEFT = 1;
int const BOX_WIDTH = 8;
int const BOX_HEIGHT = 10;
int const CELL_WIDTH = LEFT + BOX_WIDTH;
int const CELL_HEIGHT = TOP + BOX_HEIGHT;
int const CELLS_PER_ROW = 16;
int const SHEET_WIDTH = CELLS_PER_ROW * CELL_WIDTH;
int const SHEET_HEIGHT = ((257 + CELLS_PER_ROW - 1) / CELLS_PER_ROW) * CELL_HEIGHT;

// Where H sits, rows counted from the top of the box, and so where the baseline goes.
int const ASCENT = 8;

// Three of dlgsysi.pcx's palette entries: the letter's face, a bevel shade and the shadow.
unsigned char const LETTER[3] = { 203, 255, 0 };
unsigned char const BEVEL[3] = { 0, 215, 0 };
unsigned char const SHADOW[3] = { 31, 31, 31 };
unsigned char const GROUND[3] = { 0, 0, 255 };

// ODColorText, and what the legacy view showed on a 1280x800 frame for each of dlgsysi.pcx's
// entries drawn in it, read off a harness screenshot of the version dialog.
unsigned char const TEXT_GREEN[3] = { 112, 255, 0 };

struct Measured
{
	unsigned char Sheet[3];
	unsigned char Frame[3];
};

Measured const LEGACY_GREEN[] = {
	{ { 203, 255, 0 }, { 181, 251, 0 } },
	{ { 163, 255, 0 }, { 148, 251, 0 } },
	{ { 111, 255, 0 }, { 115, 251, 0 } },
	{ { 0, 255, 0 }, { 41, 251, 0 } },
	{ { 0, 239, 0 }, { 33, 235, 0 } },
	{ { 0, 227, 0 }, { 33, 223, 0 } },
	{ { 0, 215, 0 }, { 33, 210, 0 } },
	{ { 0, 203, 0 }, { 25, 198, 0 } },
	{ { 0, 191, 0 }, { 25, 186, 0 } },
	{ { 0, 179, 0 }, { 25, 174, 0 } },
	{ { 0, 167, 0 }, { 16, 162, 0 } },
	{ { 0, 155, 0 }, { 16, 150, 0 } },
	{ { 31, 31, 31 }, { 16, 24, 16 } },
};


struct Pattern
{
	int Character;
	char const * Rows[BOX_HEIGHT];
};


// '#' is the letter's face and '+' a bevel shade.
Pattern const PATTERNS[] = {
	{ 'H', {
		"........",
		"#....#..",
		"#....#..",
		"#....#..",
		"######..",
		"#....#..",
		"#....#..",
		"+....+..",
		"........",
		"........" } },
	{ 'O', {
		"........",
		".####...",
		"#....#..",
		"#....#..",
		"#.##.#..",
		"#....#..",
		"#....#..",
		".++++...",
		"........",
		"........" } },
	{ 'x', {
		"........",
		"........",
		"........",
		"........",
		"#...#...",
		".#.#....",
		"..#.....",
		".#.#....",
		"+...+...",
		"........" } },
	{ '?', {
		"........",
		".###....",
		"#...#...",
		"...#....",
		"..#.....",
		"..#.....",
		"........",
		"..#.....",
		"........",
		"........" } },
	{ 'T', {
		"........",
		"#######.",
		"...#....",
		"...#....",
		"...#....",
		"...#....",
		"...#....",
		"...+....",
		"........",
		"........" } },
	{ 0x80, {
		"........",
		"..####..",
		".#......",
		"####....",
		".#......",
		"####....",
		".#......",
		"..####..",
		"........",
		"........" } },
};


// A letter pixel in T's top margin and one in its left margin: the dialogs draw the whole
// cell, margins and all.
std::pair<int, int> const MARGIN_PIXELS[] = { { 4, 0 }, { 0, TOP + 4 } };


struct Sheet
{
	std::vector<unsigned char> Pixels;
	int Width = 0;
	int Height = 0;
};


std::map<std::string, Sheet> Sheets;


void Paint(Sheet & sheet, int x, int y, unsigned char const * rgb)
{
	unsigned char * pixel = &sheet.Pixels[((std::size_t)y * sheet.Width + x) * 4];
	pixel[0] = rgb[0];
	pixel[1] = rgb[1];
	pixel[2] = rgb[2];
	pixel[3] = 0xFF;
}


void Cell_Origin(int character, int & x, int & y)
{
	x = ((character + 1) % CELLS_PER_ROW) * CELL_WIDTH;
	y = ((character + 1) / CELLS_PER_ROW) * CELL_HEIGHT;
}


// Letter pixels of a character in cell coordinates, each with its colour sheet entry.
std::map<std::pair<int, int>, unsigned char const *> Letter_Pixels(int character)
{
	std::map<std::pair<int, int>, unsigned char const *> pixels;

	for (Pattern const & pattern : PATTERNS) {
		if (pattern.Character != character) {
			continue;
		}
		for (int row = 0; row < BOX_HEIGHT; row++) {
			for (int column = 0; column < BOX_WIDTH; column++) {
				char const mark = pattern.Rows[row][column];
				if (mark == '#' || mark == '+') {
					pixels[{ LEFT + column, TOP + row }] = (mark == '#') ? LETTER : BEVEL;
				}
			}
		}
	}

	if (character == 'T') {
		for (std::pair<int, int> const & pixel : MARGIN_PIXELS) {
			pixels[pixel] = LETTER;
		}
	}

	return(pixels);
}


// Every inked pixel of a character's cell: the letter over its shadow, one right and two down.
std::map<std::pair<int, int>, unsigned char const *> Cell_Pixels(int character)
{
	std::map<std::pair<int, int>, unsigned char const *> letter = Letter_Pixels(character);
	std::map<std::pair<int, int>, unsigned char const *> cell;

	for (auto const & pixel : letter) {
		std::pair<int, int> const cast = { pixel.first.first + 1, pixel.first.second + 2 };
		if (cast.first < CELL_WIDTH && cast.second < CELL_HEIGHT) {
			cell[cast] = SHADOW;
		}
	}
	for (auto const & pixel : letter) {
		cell[pixel.first] = pixel.second;
	}

	return(cell);
}


// The inked width ODGetFontMetrics measures: letter and shadow, inside the size box.
int Measured_Width(int character)
{
	int first = -1;
	int last = -1;
	for (auto const & pixel : Cell_Pixels(character)) {
		if (pixel.first.first < LEFT || pixel.first.second < TOP) {
			continue;
		}
		if (first == -1 || pixel.first.first < first) {
			first = pixel.first.first;
		}
		if (pixel.first.first > last) {
			last = pixel.first.first;
		}
	}

	return((first == -1) ? BOX_WIDTH / 3 + 1 : last - first + 1);
}


void Build_Sheets(void)
{
	Sheet alpha;
	alpha.Width = SHEET_WIDTH;
	alpha.Height = SHEET_HEIGHT;
	alpha.Pixels.assign((std::size_t)SHEET_WIDTH * SHEET_HEIGHT * 4, 0);

	Sheet colour = alpha;

	unsigned char const black[3] = { 0, 0, 0 };
	unsigned char const white[3] = { 0xFF, 0xFF, 0xFF };

	for (int y = 0; y < SHEET_HEIGHT; y++) {
		for (int x = 0; x < SHEET_WIDTH; x++) {
			Paint(alpha, x, y, black);
			Paint(colour, x, y, GROUND);
		}
	}

	for (int y = TOP; y < TOP + BOX_HEIGHT; y++) {
		for (int x = LEFT; x < LEFT + BOX_WIDTH; x++) {
			Paint(alpha, x, y, white);
			Paint(colour, x, y, white);
		}
	}

	for (int character = 0; character < 256; character++) {
		int cellx;
		int celly;
		Cell_Origin(character, cellx, celly);

		for (auto const & pixel : Cell_Pixels(character)) {
			Paint(alpha, cellx + pixel.first.first, celly + pixel.first.second, white);
			Paint(colour, cellx + pixel.first.first, celly + pixel.first.second, pixel.second);
		}
	}

	Sheets["dlgsysa.pcx"] = alpha;
	Sheets["dlgsysi.pcx"] = colour;
}


bool Legacy_Green(unsigned char const * sheet, unsigned char * frame)
{
	for (Measured const & entry : LEGACY_GREEN) {
		if (std::memcmp(entry.Sheet, sheet, 3) == 0) {
			std::memcpy(frame, entry.Frame, 3);
			return(true);
		}
	}
	return(false);
}


// A render interface that keeps what RmlUi hands it, so the text can be drawn in software.
class RecordingRenderInterface : public Rml::RenderInterface
{
	public:
		struct Texture
		{
			std::vector<unsigned char> Pixels;
			int Width = 0;
			int Height = 0;
		};

		std::map<Rml::TextureHandle, Texture> Textures;
		std::vector<Rml::TextureHandle> Drawn;
		Rml::TextureHandle Next = 1;

		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<Rml::Vertex const>, Rml::Span<int const>) override { return(1); }
		void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle texture) override { Drawn.push_back(texture); }
		void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
		Rml::TextureHandle LoadTexture(Rml::Vector2i &, Rml::String const &) override { return(0); }

		Rml::TextureHandle GenerateTexture(Rml::Span<Rml::byte const> source, Rml::Vector2i dimensions) override
		{
			Texture & texture = Textures[Next];
			texture.Pixels.assign(source.begin(), source.end());
			texture.Width = dimensions.x;
			texture.Height = dimensions.y;
			return(Next++);
		}

		void ReleaseTexture(Rml::TextureHandle texture) override { Textures.erase(texture); }
		void EnableScissorRegion(bool) override {}
		void SetScissorRegion(Rml::Rectanglei) override {}
};


struct Canvas
{
	int Width = 0;
	int Height = 0;
	std::vector<unsigned char> Pixels;

	Canvas(int width, int height) : Width(width), Height(height), Pixels((std::size_t)width * height * 4, 0) {}
	unsigned char * At(int x, int y) { return(&Pixels[((std::size_t)y * Width + x) * 4]); }
};


/// <summary>
/// Draws a mesh of axis-aligned quads as a GPU does with point sampling at pixel centres and
/// premultiplied blending, onto a transparent canvas.
/// </summary>
void Rasterize(Rml::Mesh const & mesh, RecordingRenderInterface::Texture const & texture, Canvas & canvas)
{
	for (std::size_t quad = 0; quad + 3 < mesh.vertices.size(); quad += 4) {
		Rml::Vertex const & first = mesh.vertices[quad];
		Rml::Vertex const & last = mesh.vertices[quad + 2];

		for (int y = 0; y < canvas.Height; y++) {
			float const cy = y + 0.5f;
			if (cy < first.position.y || cy >= last.position.y) {
				continue;
			}
			for (int x = 0; x < canvas.Width; x++) {
				float const cx = x + 0.5f;
				if (cx < first.position.x || cx >= last.position.x) {
					continue;
				}

				float const u = first.tex_coord.x + (cx - first.position.x) / (last.position.x - first.position.x) * (last.tex_coord.x - first.tex_coord.x);
				float const v = first.tex_coord.y + (cy - first.position.y) / (last.position.y - first.position.y) * (last.tex_coord.y - first.tex_coord.y);
				int const tx = (int)std::floor(u * texture.Width);
				int const ty = (int)std::floor(v * texture.Height);
				unsigned char const * texel = &texture.Pixels[((std::size_t)ty * texture.Width + tx) * 4];
				unsigned char * pixel = canvas.At(x, y);

				unsigned char const tint[4] = { first.colour.red, first.colour.green, first.colour.blue, first.colour.alpha };
				int const alpha = texel[3] * tint[3] / 255;
				for (int channel = 0; channel < 4; channel++) {
					int const source = texel[channel] * tint[channel] / 255;
					pixel[channel] = (unsigned char)(source + pixel[channel] * (255 - alpha) / 255);
				}
			}
		}
	}
}


// What ODDrawCharRemap leaves for the string in ODColorText, with the pen and the top of the
// line at the given canvas pixel, drawn magnify times as large.
Canvas Expected_Green(char const * string, int penx, int liney, int magnify, int width, int height)
{
	Canvas canvas(width, height);
	int pen = 0;

	for (char const * cursor = string; *cursor != '\0'; cursor++) {
		int const character = (unsigned char)*cursor;
		if (character > ' ') {
			for (auto const & pixel : Cell_Pixels(character)) {
				unsigned char frame[3];
				Legacy_Green(pixel.second, frame);
				for (int y = 0; y < magnify; y++) {
					for (int x = 0; x < magnify; x++) {
						int const px = (penx + pen - 1 + pixel.first.first) * magnify + x;
						int const py = (liney - TOP + pixel.first.second) * magnify + y;
						unsigned char * out = canvas.At(px, py);
						out[0] = frame[0];
						out[1] = frame[1];
						out[2] = frame[2];
						out[3] = 0xFF;
					}
				}
			}
		}
		pen += Measured_Width(character);
	}

	return(canvas);
}


bool Same(Canvas const & a, Canvas const & b)
{
	return(a.Width == b.Width && a.Height == b.Height && a.Pixels == b.Pixels);
}


void Check_Font(void)
{
	UIDialogFontClass font;

	Check(!font.Load() && !font.Is_Loaded(), "without the sheets the font does not load");

	Build_Sheets();

	{
		Sheet const saved = Sheets["dlgsysa.pcx"];
		Sheets["dlgsysa.pcx"].Pixels.assign(saved.Pixels.size(), 0);
		Check(!font.Load() && !font.Is_Loaded(), "an alpha sheet with no size box fails");

		Sheets["dlgsysa.pcx"] = saved;
		Sheets["dlgsysa.pcx"].Width = saved.Width - CELL_WIDTH;
		Sheets["dlgsysa.pcx"].Pixels.resize((std::size_t)Sheets["dlgsysa.pcx"].Width * saved.Height * 4);
		Check(!font.Load(), "sheets of different sizes fail");

		Sheets["dlgsysa.pcx"] = saved;
	}

	Check(font.Load() && font.Is_Loaded(), "the synthetic sheets load");

	Check(font.Line_Height() == BOX_HEIGHT && font.Top_Margin() == TOP
		&& font.Cell_Width() == CELL_WIDTH && font.Cell_Height() == CELL_HEIGHT, "the size box gives the line and the cell");
	Check(font.Ascent() == ASCENT, "the baseline goes under H's letter pixels");
	Check(font.X_Height() == ASCENT - 4, "the x-height is x's letter pixels above the baseline");

	bool spaced = true;
	for (int character : { (int)'H', (int)'O', (int)'x', (int)'?', (int)'T', 0x80, (int)' ', (int)'A' }) {
		spaced = font.Advance(character) == Measured_Width(character) && spaced;
	}
	Check(spaced, "every advance is the inked width, shadow included, as ODGetFontMetrics measures");

	Check(UIDialogFontClass::Glyph('H') == 'H' && UIDialogFontClass::Glyph(0x20AC) == 0x80
		&& UIDialogFontClass::Glyph(0xE9) == 0xE9, "code points take their Windows-1252 cells");
	Check(UIDialogFontClass::Glyph(0x0100) == '?' && UIDialogFontClass::Glyph(0x85) == '?',
		"a code point the code page lacks takes the '?' cell");
	Check(UIDialogFontClass::Glyph('\t') == '\t' && !UIDialogFontClass::Is_Drawn('\t') && !UIDialogFontClass::Is_Drawn(' ')
		&& UIDialogFontClass::Is_Drawn('!'), "a control or a space only advances");

	{
		bool exact = true;
		for (Measured const & entry : LEGACY_GREEN) {
			unsigned char out[4];
			UIDialogFontClass::Remap(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], entry.Sheet, 0xFF, out);
			exact = out[0] == entry.Frame[0] && out[1] == entry.Frame[1] && out[2] == entry.Frame[2] && out[3] == 0xFF && exact;
		}
		Check(exact, "every sheet colour drawn in ODColorText is the pixel the legacy view showed");
	}

	{
		unsigned char out[4];
		UIDialogFontClass::Remap(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], LETTER, 0x80, out);
		Check(out[0] == 90 && out[1] == 125 && out[2] == 0 && out[3] == 0x80,
			"a partial weight gives the colour's share of the blend, premultiplied");
	}

	for (int magnify : { 1, 2 }) {
		std::vector<unsigned char> atlas;
		int width = 0;
		int height = 0;
		font.Build_Atlas(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], magnify, atlas, width, height);

		bool exact = !atlas.empty();
		for (int character : { (int)'H', (int)'O', (int)'T', 0x80 }) {
			int ax;
			int ay;
			font.Atlas_Cell(character, magnify, ax, ay);
			std::map<std::pair<int, int>, unsigned char const *> const cell = Cell_Pixels(character);

			for (int y = -1; y <= CELL_HEIGHT * magnify; y++) {
				for (int x = -1; x <= CELL_WIDTH * magnify; x++) {
					unsigned char const * texel = &atlas[((std::size_t)(ay + y) * width + ax + x) * 4];
					auto found = cell.find({ (x < 0) ? -1 : x / magnify, (y < 0) ? -1 : y / magnify });
					bool const inside = x >= 0 && y >= 0 && x < CELL_WIDTH * magnify && y < CELL_HEIGHT * magnify;
					if (!inside || found == cell.end()) {
						exact = texel[3] == 0 && exact;
						continue;
					}
					unsigned char frame[3];
					Legacy_Green(found->second, frame);
					exact = std::memcmp(texel, frame, 3) == 0 && texel[3] == 0xFF && exact;
				}
			}
		}

		char label[96];
		std::snprintf(label, sizeof(label), "at %dx the atlas holds each whole cell, coloured, inside transparent padding", magnify);
		Check(exact, label);
	}
}


void Check_Engine(void)
{
	RecordingRenderInterface renderer;
	Rml::SetRenderInterface(&renderer);
	Rml::SetFontEngineInterface(UI_Font_Engine());

	if (!Rml::Initialise()) {
		Check(false, "RmlUi starts with the engine installed");
		return;
	}

	Rml::FontEngineInterface * engine = Rml::GetFontEngineInterface();
	Rml::String const language;
	Rml::TextShapingContext const shaping = { language };

	Check(engine->GetFontFaceHandle("opents-dialog", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, BOX_HEIGHT) == 0,
		"before the sheets arrive the family is left to RmlUi, which has no such face");

	Check(Rml::LoadFontFace(OPENTS_SHIPPED_FACE, "opents-sans", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal),
		"a shipped face loads through the engine");
	Rml::FontFaceHandle const sans = engine->GetFontFaceHandle("opents-sans", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 12);
	Check(sans != 0 && engine->GetStringWidth(sans, "Shell probe", shaping, Rml::Character::Null) > 0,
		"another family is served by RmlUi's own engine");

	Build_Sheets();
	Check(UI_Font_Load_Dialog_Face(), "the sheets arriving late serve the family");

	Rml::FontFaceHandle const dialog = engine->GetFontFaceHandle("OpenTS-Dialog", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, BOX_HEIGHT);
	Check(dialog != 0 && dialog != sans, "the family, in any case and weight, is the sheets' face");

	Rml::FontMetrics const & metrics = engine->GetFontMetrics(dialog);
	Check(metrics.ascent == ASCENT && metrics.descent == BOX_HEIGHT - ASCENT && metrics.line_spacing == BOX_HEIGHT && !metrics.has_ellipsis,
		"at the sheets' own size the line is the size box and the baseline sits under H");

	// H, O, a tab, x, a space, the euro sign and U+0100, which the code page lacks.
	int const width = Measured_Width('H') + Measured_Width('O') + Measured_Width('\t') + Measured_Width('x') + Measured_Width(' ')
		+ Measured_Width(0x80) + Measured_Width('?');
	Check(engine->GetStringWidth(dialog, "HO\tx \xE2\x82\xAC\xC4\x80", shaping, Rml::Character::Null) == width,
		"a string is as wide as the widths ODGetFontMetrics measured");

	Check(engine->PrepareFontEffects(dialog, {}) == 0, "the face takes no effects: its shadow is the sheets'");

	Rml::RenderManager manager(&renderer);

	{
		Rml::TexturedMeshList meshes;
		Rml::ColourbPremultiplied const green(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], 0xFF);
		int const drawn = engine->GenerateString(manager, dialog, 0, "HOx T?", Rml::Vector2f(3.4f, 13.5f), green, 1.0f, shaping, meshes);

		Check(drawn == Measured_Width('H') + Measured_Width('O') + Measured_Width('x') + Measured_Width(' ') + Measured_Width('T')
			+ Measured_Width('?'), "the string's width is returned");
		Check(meshes.size() == 1 && meshes[0].mesh.vertices.size() == 5 * 4, "one quad for each drawn character, none for the space");

		bool white = !meshes.empty();
		for (Rml::Vertex const & vertex : meshes.empty() ? Rml::Vector<Rml::Vertex>() : meshes[0].mesh.vertices) {
			white = vertex.colour.red == 0xFF && vertex.colour.green == 0xFF && vertex.colour.blue == 0xFF && vertex.colour.alpha == 0xFF && white;
		}
		Check(white, "the vertex colour does not tint the coloured atlas again");

		if (meshes.size() == 1) {
			Rml::Mesh const copy = meshes[0].mesh;
			Rml::Geometry geometry = manager.MakeGeometry(std::move(meshes[0].mesh));
			geometry.Render(Rml::Vector2f(0.0f, 0.0f), meshes[0].texture);

			Check(renderer.Drawn.size() == 1 && renderer.Textures.count(renderer.Drawn.back()) == 1, "the atlas reaches the renderer");

			if (renderer.Drawn.size() == 1 && renderer.Textures.count(renderer.Drawn.back()) == 1) {
				Canvas canvas(80, 32);
				Rasterize(copy, renderer.Textures[renderer.Drawn.back()], canvas);

				// The pen rounds to 3 and the baseline to 14, so the line's top is 14 - ASCENT.
				Check(Same(canvas, Expected_Green("HOx T?", 3, 14 - ASCENT, 1, 80, 32)),
					"drawn at the sheets' size, every pixel is where and what ODDrawCharRemap leaves");
			}
		}
	}

	{
		Rml::TexturedMeshList opaque;
		Rml::TexturedMeshList faded;
		Rml::ColourbPremultiplied const green(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], 0xFF);
		Rml::ColourbPremultiplied const half(56, 128, 0, 128);
		engine->GenerateString(manager, dialog, 0, "H", Rml::Vector2f(0.0f, 10.0f), green, 1.0f, shaping, opaque);
		engine->GenerateString(manager, dialog, 0, "H", Rml::Vector2f(0.0f, 10.0f), half, 0.5f, shaping, faded);

		Check(opaque.size() == 1 && faded.size() == 1 && opaque[0].texture == faded[0].texture
			&& faded[0].mesh.vertices[0].colour.red == 128 && faded[0].mesh.vertices[0].colour.alpha == 128,
			"a faded colour draws the same atlas with only the opacity in the vertex colour");
	}

	{
		int const version = engine->GetVersion(dialog);
		UI_Font_Set_Pixel_Ratio(1.28f);

		Rml::FontFaceHandle const scaled = engine->GetFontFaceHandle("opents-dialog", Rml::Style::FontStyle::Normal,
			Rml::Style::FontWeight::Normal, (int)(BOX_HEIGHT * 1.28f));
		Rml::FontMetrics const & sized = engine->GetFontMetrics(scaled);

		Check(engine->GetVersion(dialog) != version, "a new pixel ratio asks for the text to be generated again");
		Check(std::fabs(sized.line_spacing - BOX_HEIGHT * 1.28f) < 0.001f && std::fabs(sized.ascent - ASCENT * 1.28f) < 0.001f,
			"a size RmlUi truncated from dp is drawn at the pixel ratio itself");
	}

	{
		UI_Font_Set_Pixel_Ratio(2.0f);
		Rml::FontFaceHandle const doubled = engine->GetFontFaceHandle("opents-dialog", Rml::Style::FontStyle::Normal,
			Rml::Style::FontWeight::Normal, BOX_HEIGHT * 2);

		Rml::TexturedMeshList meshes;
		Rml::ColourbPremultiplied const green(TEXT_GREEN[0], TEXT_GREEN[1], TEXT_GREEN[2], 0xFF);
		renderer.Drawn.clear();
		engine->GenerateString(manager, doubled, 0, "TO", Rml::Vector2f(4.0f, 20.0f), green, 1.0f, shaping, meshes);

		if (meshes.size() == 1) {
			Rml::Mesh const copy = meshes[0].mesh;
			Rml::Geometry geometry = manager.MakeGeometry(std::move(meshes[0].mesh));
			geometry.Render(Rml::Vector2f(0.0f, 0.0f), meshes[0].texture);

			if (renderer.Drawn.size() == 1 && renderer.Textures.count(renderer.Drawn.back()) == 1) {
				Canvas canvas(80, 48);
				Rasterize(copy, renderer.Textures[renderer.Drawn.back()], canvas);
				Check(Same(canvas, Expected_Green("TO", 2, 10 - ASCENT, 2, 80, 48)), "at twice the size every sheet pixel is a square of four");
			} else {
				Check(false, "the doubled atlas reaches the renderer");
			}
		} else {
			Check(false, "the doubled string makes one mesh");
		}
	}

	Rml::Shutdown();
}

}


bool UI_Texture_Load(char const * source, std::vector<unsigned char> & rgba, int & width, int & height)
{
	auto found = Sheets.find(source);
	if (found == Sheets.end()) {
		return(false);
	}

	rgba = found->second.Pixels;
	width = found->second.Width;
	height = found->second.Height;
	return(true);
}


void __cdecl DebugString(char const *, ...)
{
}


// Windows-1252 with no best fit.
int UTF8::Windows_1252_Glyph(char32_t code)
{
	static char32_t const high[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
	};

	if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
		return((int)code);
	}
	if (code < 0xA0) {
		return(-1);
	}
	for (int index = 0; index < 32; index++) {
		if (high[index] == code) {
			return(0x80 + index);
		}
	}
	return(-1);
}


// sheettext.cpp's, which cannot be linked here without the surface cache; the measured
// colours above pin both.
float Sheet_Text_Remap_Factor(int hue)
{
	float val = 1.0f;

	int arr[3];
	arr[0] = 43;
	arr[1] = 128;
	arr[2] = 213;

	for (int i = 0; i < 3; i++) {
		int value = arr[i];

		if (hue > value - 16 && hue <= value) {
			val = float(value - hue);
			val *= (1.0f / 16);
			val *= (60.0f / 100);
			val += (40.0f / 100);
		} else if (hue > value && hue <= value + 16) {
			val = float(hue - value);
			val *= (1.0f / 16);
			val *= (60.0f / 100);
			val += (40.0f / 100);
		}
	}
	return(val);
}


int main(void)
{
	Check_Font();
	Check_Engine();

	std::printf("%d failure(s)\n", Failures);
	return(Failures == 0 ? 0 : 1);
}
