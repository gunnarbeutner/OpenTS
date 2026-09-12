/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The dialog face is drawn from atlases of the sheets' own cells, one per colour, so a letter
// keeps the shading and the dark copy the sheets draw under it. The atlas is already coloured,
// and the vertex colour only carries the text's opacity.

#include "always.h"

#include "uifontengine.h"

#include "uifont.h"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>

// RmlUi installs its FreeType engine only when no other is set, so this one owns an instance.
#include "../../thirdparty/RmlUi/Source/Core/FontEngineDefault/FontEngineInterfaceDefault.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>


static char const * const DIALOG_FAMILY = "opents-dialog";


struct UIDialogFaceHandle
{
	int Size = 0;
	float Scale = 1.0f;
	int Magnify = 1;
	Rml::FontMetrics Metrics = {};
};


struct UIDialogAtlas
{
	std::vector<unsigned char> Pixels;
	int Width = 0;
	int Height = 0;
	Rml::CallbackTextureSource Texture;
};


class UIFontEngineClass : public Rml::FontEngineInterface
{
	public:
		void Initialize() override;
		void Shutdown() override;

		bool LoadFontFace(Rml::String const & file_name, int face_index, bool fallback_face, Rml::Style::FontWeight weight) override;
		bool LoadFontFace(Rml::String const & file_name, int face_index, Rml::String const & family, Rml::Style::FontStyle style,
			Rml::Style::FontWeight weight, bool fallback_face) override;
		bool LoadFontFace(Rml::Span<Rml::byte const> data, int face_index, Rml::String const & family, Rml::Style::FontStyle style,
			Rml::Style::FontWeight weight, bool fallback_face) override;

		Rml::FontFaceHandle GetFontFaceHandle(Rml::String const & family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
			int size) override;
		Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle, Rml::FontEffectList const & font_effects) override;
		Rml::FontMetrics const & GetFontMetrics(Rml::FontFaceHandle handle) override;
		int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, Rml::TextShapingContext const & text_shaping_context,
			Rml::Character prior_character) override;
		int GenerateString(Rml::RenderManager & render_manager, Rml::FontFaceHandle face_handle, Rml::FontEffectsHandle font_effects_handle,
			Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
			Rml::TextShapingContext const & text_shaping_context, Rml::TexturedMeshList & mesh_list) override;
		int GetVersion(Rml::FontFaceHandle handle) override;
		void ReleaseFontResources() override;

		bool Load_Dialog_Face(void);
		void Set_Pixel_Ratio(float ratio);

	private:
		Rml::FontEngineInterfaceDefault Default;

		UIDialogFontClass Font;
		std::map<int, std::unique_ptr<UIDialogFaceHandle>> Handles;
		std::map<std::uint64_t, std::unique_ptr<UIDialogAtlas>> Atlases;
		float PixelRatio = 1.0f;
		int Version = 1;

		UIDialogFaceHandle * Find(Rml::FontFaceHandle handle) const;
		void Measure(UIDialogFaceHandle & handle) const;
		UIDialogAtlas & Atlas(unsigned char red, unsigned char green, unsigned char blue, int magnify);
		float Line_Width(UIDialogFaceHandle const & handle, Rml::StringView string, float letterspacing) const;
};


static UIFontEngineClass _Engine;


static bool Is_Dialog_Family(Rml::String const & family)
{
	return(Rml::StringUtilities::ToLower(family) == DIALOG_FAMILY);
}


UIDialogFaceHandle * UIFontEngineClass::Find(Rml::FontFaceHandle handle) const
{
	for (auto const & entry : Handles) {
		if ((Rml::FontFaceHandle)entry.second.get() == handle) {
			return(entry.second.get());
		}
	}
	return(nullptr);
}


/// <summary>
/// Sets a handle's scale and metrics from its size and the pixel ratio. RmlUi truncates a
/// size given in dp to whole pixels, so the size is taken back to the whole dp that produced
/// it, and that is drawn at the ratio itself: 17dp at a ratio of 1.28 is the face at 1.28
/// times, not 21/17 times.
/// </summary>
void UIFontEngineClass::Measure(UIDialogFaceHandle & handle) const
{
	float const ratio = (PixelRatio > 0.0f) ? PixelRatio : 1.0f;
	int const line = std::max(1, Font.Line_Height());

	float logical = std::ceil(handle.Size / ratio - 0.001f);
	if ((int)(logical * ratio) != handle.Size) {
		logical = handle.Size / ratio;
	}

	handle.Scale = logical * ratio / line;
	handle.Magnify = std::max(1, (int)std::ceil(handle.Scale - 0.001f));

	float const scale = handle.Scale;

	handle.Metrics.size = handle.Size;
	handle.Metrics.ascent = Font.Ascent() * scale;
	handle.Metrics.descent = (line - Font.Ascent()) * scale;
	handle.Metrics.line_spacing = line * scale;
	handle.Metrics.x_height = (Font.X_Height() > 0) ? Font.X_Height() * scale : 0.5f * line * scale;
	handle.Metrics.underline_position = scale;
	handle.Metrics.underline_thickness = std::max(1.0f, scale);

	// The dialogs cut a long line short with three full stops, which is what RmlUi draws for
	// an ellipsis the face does not claim to have.
	handle.Metrics.has_ellipsis = false;
}


UIDialogAtlas & UIFontEngineClass::Atlas(unsigned char red, unsigned char green, unsigned char blue, int magnify)
{
	std::uint64_t const key = ((std::uint64_t)magnify << 24) | ((std::uint64_t)red << 16) | ((std::uint64_t)green << 8) | blue;

	auto found = Atlases.find(key);
	if (found != Atlases.end()) {
		return(*found->second);
	}

	std::unique_ptr<UIDialogAtlas> atlas = std::make_unique<UIDialogAtlas>();
	Font.Build_Atlas(red, green, blue, magnify, atlas->Pixels, atlas->Width, atlas->Height);

	UIDialogAtlas * const pointer = atlas.get();
	atlas->Texture = Rml::CallbackTextureSource([pointer](Rml::CallbackTextureInterface const & texture) -> bool {
		return(texture.GenerateTexture(Rml::Span<Rml::byte const>(pointer->Pixels.data(), pointer->Pixels.size()),
			Rml::Vector2i(pointer->Width, pointer->Height)));
	});

	return(*Atlases.emplace(key, std::move(atlas)).first->second);
}


float UIFontEngineClass::Line_Width(UIDialogFaceHandle const & handle, Rml::StringView string, float letterspacing) const
{
	int advance = 0;
	int characters = 0;

	for (Rml::StringIteratorU8 it(string); it; ++it) {
		advance += Font.Advance(UIDialogFontClass::Glyph((char32_t)*it));
		characters++;
	}

	return(advance * handle.Scale + characters * letterspacing);
}


void UIFontEngineClass::Initialize()
{
	Default.Initialize();
}


void UIFontEngineClass::Shutdown()
{
	// The atlases' textures belong to render managers that go once this returns.
	Atlases.clear();
	Handles.clear();
	Default.Shutdown();
}


bool UIFontEngineClass::LoadFontFace(Rml::String const & file_name, int face_index, bool fallback_face, Rml::Style::FontWeight weight)
{
	return(Default.LoadFontFace(file_name, face_index, fallback_face, weight));
}


bool UIFontEngineClass::LoadFontFace(Rml::String const & file_name, int face_index, Rml::String const & family,
	Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face)
{
	return(Default.LoadFontFace(file_name, face_index, family, style, weight, fallback_face));
}


bool UIFontEngineClass::LoadFontFace(Rml::Span<Rml::byte const> data, int face_index, Rml::String const & family,
	Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback_face)
{
	return(Default.LoadFontFace(data, face_index, family, style, weight, fallback_face));
}


// The sheets have one weight and one style, so every one asked for is that.
Rml::FontFaceHandle UIFontEngineClass::GetFontFaceHandle(Rml::String const & family, Rml::Style::FontStyle style,
	Rml::Style::FontWeight weight, int size)
{
	if (!Font.Is_Loaded() || !Is_Dialog_Family(family) || size <= 0) {
		return(Default.GetFontFaceHandle(family, style, weight, size));
	}

	std::unique_ptr<UIDialogFaceHandle> & handle = Handles[size];
	if (handle == nullptr) {
		handle = std::make_unique<UIDialogFaceHandle>();
		handle->Size = size;
		Measure(*handle);
	}

	return((Rml::FontFaceHandle)handle.get());
}


// The dialog face draws the sheets' own shadow and takes no effects.
Rml::FontEffectsHandle UIFontEngineClass::PrepareFontEffects(Rml::FontFaceHandle handle, Rml::FontEffectList const & font_effects)
{
	if (Find(handle) != nullptr) {
		return(0);
	}
	return(Default.PrepareFontEffects(handle, font_effects));
}


Rml::FontMetrics const & UIFontEngineClass::GetFontMetrics(Rml::FontFaceHandle handle)
{
	UIDialogFaceHandle const * const dialog = Find(handle);
	if (dialog != nullptr) {
		return(dialog->Metrics);
	}
	return(Default.GetFontMetrics(handle));
}


int UIFontEngineClass::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
	Rml::TextShapingContext const & text_shaping_context, Rml::Character prior_character)
{
	UIDialogFaceHandle const * const dialog = Find(handle);
	if (dialog == nullptr) {
		return(Default.GetStringWidth(handle, string, text_shaping_context, prior_character));
	}

	return((int)Rml::Math::Round(Line_Width(*dialog, string, text_shaping_context.letter_spacing)));
}


/// <summary>
/// Places each character as ODDrawCharRemap does: its whole cell, one pixel left of the pen
/// and with the size box's top row at the top of the line, the pen then moving on by the
/// width ODGetFontMetrics measured. A character the code page lacks draws as '?'.
/// </summary>
int UIFontEngineClass::GenerateString(Rml::RenderManager & render_manager, Rml::FontFaceHandle face_handle,
	Rml::FontEffectsHandle font_effects_handle, Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour,
	float opacity, Rml::TextShapingContext const & text_shaping_context, Rml::TexturedMeshList & mesh_list)
{
	UIDialogFaceHandle const * const dialog = Find(face_handle);
	if (dialog == nullptr) {
		return(Default.GenerateString(render_manager, face_handle, font_effects_handle, string, position, colour, opacity,
			text_shaping_context, mesh_list));
	}

	float const scale = dialog->Scale;
	float const letterspacing = text_shaping_context.letter_spacing;

	if (colour.alpha == 0) {
		return((int)Rml::Math::Round(Line_Width(*dialog, string, letterspacing)));
	}

	// The atlas is coloured by the text's own colour, and the vertex colour carries only its
	// opacity, as RmlUi's engine does for a colour glyph.
	auto straight = [&](Rml::byte channel) {
		return((unsigned char)std::min(255, (channel * 255 + colour.alpha / 2) / colour.alpha));
	};
	UIDialogAtlas & atlas = Atlas(straight(colour.red), straight(colour.green), straight(colour.blue), dialog->Magnify);
	Rml::ColourbPremultiplied const tint(colour.alpha, colour.alpha);

	// An element's lines arrive one call at a time into the one list, so a line joins the
	// mesh an earlier line started with the same atlas.
	Rml::Texture const texture = atlas.Texture.GetTexture(render_manager);
	auto found = std::find_if(mesh_list.begin(), mesh_list.end(), [&](Rml::TexturedMesh const & entry) {
		return(entry.texture == texture);
	});
	if (found == mesh_list.end()) {
		mesh_list.emplace_back();
		mesh_list.back().texture = texture;
		found = mesh_list.end() - 1;
	}
	Rml::TexturedMesh & mesh = *found;

	Rml::Vector2f const origin = position.Round();
	Rml::Vector2f const cell((float)Font.Cell_Width(), (float)Font.Cell_Height());
	Rml::Vector2f const texels = cell * (float)dialog->Magnify;
	Rml::Vector2f const size((float)atlas.Width, (float)atlas.Height);
	float const top = (float)(-Font.Ascent() - Font.Top_Margin()) * scale;

	int advance = 0;
	int characters = 0;

	for (Rml::StringIteratorU8 it(string); it; ++it) {
		int const glyph = UIDialogFontClass::Glyph((char32_t)*it);

		if (UIDialogFontClass::Is_Drawn(glyph)) {
			int atlasx;
			int atlasy;
			Font.Atlas_Cell(glyph, dialog->Magnify, atlasx, atlasy);

			Rml::Vector2f const corner((float)atlasx, (float)atlasy);
			Rml::Vector2f const at = origin + Rml::Vector2f((advance - 1) * scale + characters * letterspacing, top);

			Rml::MeshUtilities::GenerateQuad(mesh.mesh, at, cell * scale, tint, corner / size, (corner + texels) / size);
		}

		advance += Font.Advance(glyph);
		characters++;
	}

	return((int)Rml::Math::Round(advance * scale + characters * letterspacing));
}


int UIFontEngineClass::GetVersion(Rml::FontFaceHandle handle)
{
	if (Find(handle) != nullptr) {
		return(Version);
	}
	return(Default.GetVersion(handle));
}


void UIFontEngineClass::ReleaseFontResources()
{
	Atlases.clear();
	Version++;
	Default.ReleaseFontResources();
}


bool UIFontEngineClass::Load_Dialog_Face(void)
{
	Atlases.clear();
	bool const loaded = Font.Load();

	for (auto & entry : Handles) {
		Measure(*entry.second);
	}
	Version++;

	return(loaded);
}


void UIFontEngineClass::Set_Pixel_Ratio(float ratio)
{
	if (ratio == PixelRatio) {
		return;
	}

	PixelRatio = ratio;
	for (auto & entry : Handles) {
		Measure(*entry.second);
	}
	Version++;
}


Rml::FontEngineInterface * UI_Font_Engine(void)
{
	return(&_Engine);
}


/// <summary>
/// Reads the dlgsys glyph sheets and serves the family `opents-dialog` from them. Handles
/// taken for the family before this belong to whatever face RmlUi loaded under its name.
/// </summary>
/// <returns>bool; False when the sheets cannot be read, leaving the family to RmlUi.</returns>
bool UI_Font_Load_Dialog_Face(void)
{
	return(_Engine.Load_Dialog_Face());
}


void UI_Font_Set_Pixel_Ratio(float ratio)
{
	_Engine.Set_Pixel_Ratio(ratio);
}
