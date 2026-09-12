/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The bgfx side of the UI shell. Together with bgfxbackend.cpp this is the only place that
// includes bgfx. It submits into the frame Backend_Present opened, on its own view, so the
// game's frame is already on the target when a document draws.

#include "always.h"

#include "uirender.h"

#include "uitexture.h"

#include "dbgprint.h"
#include "viewid.hh"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>

#include <bx/allocator.h>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bimg/decode.h>

#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>


static const bgfx::EmbeddedShader _EmbeddedShaders[] = {
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER_END()
};


// One geometry compiled by RmlUi and re-submitted for as many frames as it lives.
//
// The indices are a static buffer, since RmlUi 6 compiles a mesh once and re-submits it.
// The vertices are kept on the processor instead and written into a transient buffer at
// each draw, because RmlUi supplies a mesh in its element's own coordinates and hands the
// element's position over as a translation per draw, while the program here is the
// presenter's embedded imgui shader, which transforms by the view and projection alone and
// ignores a model matrix. Offsetting the positions on the way into the buffer is what
// places the mesh; a program of the engine's own that reads a model matrix would let the
// vertices go back to a static buffer.
struct UIGeometry
{
	std::vector<Rml::Vertex> Vertices;
	bgfx::IndexBufferHandle Indices;
	unsigned int IndexCount;
};


static bool _Initialized = false;

static bgfx::ProgramHandle _Program = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _TextureSampler = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle _WhiteTexture = BGFX_INVALID_HANDLE;
static bgfx::VertexLayout _VertexLayout;

static std::unordered_map<unsigned int, UIGeometry> _Geometries;
static unsigned int _NextGeometry = 1;

static std::unordered_map<unsigned int, bgfx::TextureHandle> _Textures;
static unsigned int _NextTexture = 1;

// Where the overlay draws, in physical window pixels. The view's transform makes this
// rectangle's top left corner the origin RmlUi lays out from.
static int _DestX = 0;
static int _DestY = 0;
static int _DestWidth = 0;
static int _DestHeight = 0;

static bool _ScissorEnabled = false;
static int _ScissorX = 0;
static int _ScissorY = 0;
static int _ScissorWidth = 0;
static int _ScissorHeight = 0;

// While a dialog opens, everything the overlay draws is held inside this band, in the
// document's space.
static bool _RevealActive = false;
static int _RevealLeft = 0;
static int _RevealTop = 0;
static int _RevealRight = 0;
static int _RevealBottom = 0;

struct UIPictureTexture
{
	Rml::TextureHandle Handle = 0;
	Rml::Vector2i Size;
};

static std::unordered_map<std::string, UIPictureTexture> _Pictures;

static bx::DefaultAllocator _Allocator;


static void Build_Ortho_Projection(float * matrix, int width, int height)
{
	// A left handed orthographic projection whose origin is the top left corner, matching
	// the space RmlUi lays documents out in. bgfx's own helper is in bx/math.h, which this
	// file does not otherwise need.
	const float left = 0.0f;
	const float right = (float)width;
	const float top = 0.0f;
	const float bottom = (float)height;
	const float nearz = 0.0f;
	const float farz = 1000.0f;

	for (int index = 0; index < 16; index++) {
		matrix[index] = 0.0f;
	}

	matrix[0] = 2.0f / (right - left);
	matrix[5] = 2.0f / (top - bottom);
	matrix[10] = 1.0f / (farz - nearz);
	matrix[12] = (left + right) / (left - right);
	matrix[13] = (top + bottom) / (bottom - top);
	matrix[15] = 1.0f;

	if (bgfx::getCaps()->homogeneousDepth) {
		matrix[10] = 2.0f / (farz - nearz);
		matrix[14] = -(farz + nearz) / (farz - nearz);
	}
}


class UIRenderInterface : public Rml::RenderInterface
{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<Rml::Vertex const> vertices, Rml::Span<int const> indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

		Rml::TextureHandle LoadTexture(Rml::Vector2i & dimensions, Rml::String const & source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<Rml::byte const> source, Rml::Vector2i dimensions) override;
		void ReleaseTexture(Rml::TextureHandle texture) override;

		void EnableScissorRegion(bool enable) override;
		void SetScissorRegion(Rml::Rectanglei region) override;
};


static UIRenderInterface _Interface;


Rml::CompiledGeometryHandle UIRenderInterface::CompileGeometry(Rml::Span<Rml::Vertex const> vertices, Rml::Span<int const> indices)
{
	if (vertices.empty() || indices.empty()) {
		return(0);
	}

	UIGeometry geometry;
	geometry.Vertices.assign(vertices.begin(), vertices.end());
	geometry.IndexCount = (unsigned int)indices.size();

	// RmlUi's index is an int and bgfx's default index buffer is 16 bit, so a mesh past
	// 65535 vertices asks for the 32 bit form.
	if (vertices.size() > 0xFFFF) {
		geometry.Indices = bgfx::createIndexBuffer(bgfx::copy(indices.data(), (unsigned int)(indices.size() * sizeof(int))), BGFX_BUFFER_INDEX32);
	} else {
		std::vector<unsigned short> narrow(indices.size());
		for (size_t index = 0; index < indices.size(); index++) {
			narrow[index] = (unsigned short)indices[index];
		}
		geometry.Indices = bgfx::createIndexBuffer(bgfx::copy(narrow.data(), (unsigned int)(narrow.size() * sizeof(unsigned short))));
	}

	if (!bgfx::isValid(geometry.Indices)) {
		return(0);
	}

	unsigned int handle = _NextGeometry++;
	_Geometries[handle] = std::move(geometry);
	return((Rml::CompiledGeometryHandle)handle);
}


void UIRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	auto found = _Geometries.find((unsigned int)geometry);
	if (found == _Geometries.end()) {
		return;
	}

	// An untextured mesh samples a white pixel, because the shader multiplies the texel by
	// the vertex colour and has no untextured path.
	bgfx::TextureHandle bound = _WhiteTexture;
	if (texture != 0) {
		auto texturefound = _Textures.find((unsigned int)texture);
		if (texturefound != _Textures.end()) {
			bound = texturefound->second;
		}
	}

	UIGeometry const & geo = found->second;

	unsigned int count = (unsigned int)geo.Vertices.size();
	if (bgfx::getAvailTransientVertexBuffer(count, _VertexLayout) < count) {
		return;
	}

	bgfx::TransientVertexBuffer buffer;
	bgfx::allocTransientVertexBuffer(&buffer, count, _VertexLayout);

	Rml::Vertex * target = (Rml::Vertex *)buffer.data;
	for (unsigned int index = 0; index < count; index++) {
		target[index] = geo.Vertices[index];
		target[index].position.x += translation.x;
		target[index].position.y += translation.y;
	}

	if (_ScissorEnabled || _RevealActive) {
		// The document's space starts at the frame's destination, and a scissor rectangle
		// is given in target pixels, so it is offset and then clipped to the viewport.
		int left = _DestX;
		int top = _DestY;
		int right = _DestX + _DestWidth;
		int bottom = _DestY + _DestHeight;

		if (_ScissorEnabled) {
			left = std::max(left, _DestX + _ScissorX);
			top = std::max(top, _DestY + _ScissorY);
			right = std::min(right, _DestX + _ScissorX + _ScissorWidth);
			bottom = std::min(bottom, _DestY + _ScissorY + _ScissorHeight);
		}

		if (_RevealActive) {
			left = std::max(left, _DestX + _RevealLeft);
			top = std::max(top, _DestY + _RevealTop);
			right = std::min(right, _DestX + _RevealRight);
			bottom = std::min(bottom, _DestY + _RevealBottom);
		}

		if (right <= left || bottom <= top) {
			return;
		}

		bgfx::setScissor((unsigned short)left, (unsigned short)top, (unsigned short)(right - left), (unsigned short)(bottom - top));
	}

	bgfx::setVertexBuffer(0, &buffer);
	bgfx::setIndexBuffer(geo.Indices, 0, geo.IndexCount);
	bgfx::setTexture(0, _TextureSampler, bound, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

	// RmlUi hands over premultiplied vertex colours and premultiplied textures, so the
	// source factor is one rather than the source alpha.
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
		| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));

	bgfx::submit(VIEW_UI, _Program);
}


void UIRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	auto found = _Geometries.find((unsigned int)geometry);
	if (found == _Geometries.end()) {
		return;
	}

	bgfx::destroy(found->second.Indices);
	_Geometries.erase(found);
}


static Rml::TextureHandle Create_Texture(void const * rgba, int width, int height)
{
	if (rgba == nullptr || width <= 0 || height <= 0) {
		return(0);
	}

	bgfx::TextureHandle texture = bgfx::createTexture2D((unsigned short)width, (unsigned short)height, false, 1,
		bgfx::TextureFormat::RGBA8, 0, bgfx::copy(rgba, (unsigned int)(width * height * 4)));

	if (!bgfx::isValid(texture)) {
		return(0);
	}

	unsigned int handle = _NextTexture++;
	_Textures[handle] = texture;
	return((Rml::TextureHandle)handle);
}


Rml::TextureHandle UIRenderInterface::LoadTexture(Rml::Vector2i & dimensions, Rml::String const & source)
{
	// The game's own dialog artwork first: it is paletted, and the backdrop is composed
	// rather than read from a file at all.
	{
		std::vector<unsigned char> pixels;
		int width = 0;
		int height = 0;

		if (UI_Texture_Load(source.c_str(), pixels, width, height)) {
			dimensions.x = width;
			dimensions.y = height;
			return(Create_Texture(pixels.data(), width, height));
		}
	}

	// Every other document resource goes through the engine's file interface, so an image
	// resolves from a loose directory or from a mix file the same way.
	Rml::FileInterface * files = Rml::GetFileInterface();
	if (files == nullptr) {
		return(0);
	}

	Rml::FileHandle file = files->Open(source);
	if (file == 0) {
		DebugString("UI: image '%s' not found\n", source.c_str());
		return(0);
	}

	size_t length = files->Length(file);
	std::vector<unsigned char> encoded(length);
	size_t read = length > 0 ? files->Read(encoded.data(), length, file) : 0;
	files->Close(file);

	if (read != length || length == 0) {
		DebugString("UI: image '%s' is short or empty\n", source.c_str());
		return(0);
	}

	bimg::ImageContainer * image = bimg::imageParse(&_Allocator, encoded.data(), (unsigned int)encoded.size(), bimg::TextureFormat::RGBA8);
	if (image == nullptr) {
		DebugString("UI: image '%s' is not a format this build decodes\n", source.c_str());
		return(0);
	}

	dimensions.x = (int)image->m_width;
	dimensions.y = (int)image->m_height;

	// RmlUi's contract is premultiplied alpha, and a decoded file is straight alpha.
	unsigned char * pixels = (unsigned char *)image->m_data;
	for (unsigned int index = 0; index < image->m_width * image->m_height; index++) {
		unsigned char * pixel = pixels + index * 4;
		unsigned int alpha = pixel[3];
		pixel[0] = (unsigned char)((pixel[0] * alpha) / 255);
		pixel[1] = (unsigned char)((pixel[1] * alpha) / 255);
		pixel[2] = (unsigned char)((pixel[2] * alpha) / 255);
	}

	Rml::TextureHandle handle = Create_Texture(pixels, dimensions.x, dimensions.y);
	bimg::imageFree(image);
	return(handle);
}


Rml::TextureHandle UIRenderInterface::GenerateTexture(Rml::Span<Rml::byte const> source, Rml::Vector2i dimensions)
{
	if ((int)source.size() < dimensions.x * dimensions.y * 4) {
		return(0);
	}

	return(Create_Texture(source.data(), dimensions.x, dimensions.y));
}


void UIRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	auto found = _Textures.find((unsigned int)texture);
	if (found == _Textures.end()) {
		return;
	}

	bgfx::destroy(found->second);
	_Textures.erase(found);
}


void UIRenderInterface::EnableScissorRegion(bool enable)
{
	_ScissorEnabled = enable;
}


void UIRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	_ScissorX = region.Left();
	_ScissorY = region.Top();
	_ScissorWidth = region.Width();
	_ScissorHeight = region.Height();
}


bool UI_Render_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	bgfx::RendererType::Enum type = bgfx::getRendererType();
	bgfx::ShaderHandle vertexshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_ocornut_imgui");
	bgfx::ShaderHandle fragmentshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_ocornut_imgui");

	if (!bgfx::isValid(vertexshader) || !bgfx::isValid(fragmentshader)) {
		DebugString("UI: the renderer has no shader for this backend\n");
		return(false);
	}

	// The program is the presenter's shader, which multiplies a texel by the vertex
	// colour. That is what RmlUi's geometry wants, textured or not.
	_Program = bgfx::createProgram(vertexshader, fragmentshader, true);
	if (!bgfx::isValid(_Program)) {
		return(false);
	}

	_TextureSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

	// RmlUi's vertex is a position, an RGBA byte colour, and a texture coordinate, in that
	// order. The layout describes that struct rather than the presenter's own vertex.
	_VertexLayout.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.end();

	const unsigned int white = 0xFFFFFFFF;
	_WhiteTexture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&white, sizeof(white)));

	_Initialized = true;
	return(true);
}


void UI_Render_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	for (auto const & entry : _Geometries) {
		bgfx::destroy(entry.second.Indices);
	}
	_Geometries.clear();

	for (auto const & entry : _Textures) {
		bgfx::destroy(entry.second);
	}
	_Textures.clear();
	_Pictures.clear();

	if (bgfx::isValid(_WhiteTexture)) {
		bgfx::destroy(_WhiteTexture);
		_WhiteTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_TextureSampler)) {
		bgfx::destroy(_TextureSampler);
		_TextureSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_Program)) {
		bgfx::destroy(_Program);
		_Program = BGFX_INVALID_HANDLE;
	}

	UI_Texture_Shutdown();

	_Initialized = false;
}


Rml::RenderInterface * UI_Render_Interface(void)
{
	return(&_Interface);
}


void UI_Render_Begin(int destx, int desty, int destwidth, int destheight)
{
	if (!_Initialized) {
		return;
	}

	_DestX = destx;
	_DestY = desty;
	_DestWidth = destwidth;
	_DestHeight = destheight;

	_ScissorEnabled = false;

	// The viewport is the frame's destination, so a document lays out from the frame's top
	// left corner and the letterbox beside it is never drawn into. The overlay never
	// clears: the presented frame underneath it is the background.
	bgfx::setViewFrameBuffer(VIEW_UI, BGFX_INVALID_HANDLE);
	bgfx::setViewClear(VIEW_UI, BGFX_CLEAR_NONE);
	bgfx::setViewRect(VIEW_UI, (unsigned short)destx, (unsigned short)desty, (unsigned short)destwidth, (unsigned short)destheight);

	float projection[16];
	Build_Ortho_Projection(projection, destwidth, destheight);
	bgfx::setViewTransform(VIEW_UI, nullptr, projection);
}


void UI_Render_End(void)
{
	_ScissorEnabled = false;
}


void UI_Render_Set_Reveal(bool active, int left, int top, int right, int bottom)
{
	_RevealActive = active;
	_RevealLeft = left;
	_RevealTop = top;
	_RevealRight = right;
	_RevealBottom = bottom;
}


Rml::Vector2i UI_Render_Picture_Size(char const * source)
{
	auto found = _Pictures.find(source);
	if (found == _Pictures.end()) {
		UIPictureTexture picture;
		picture.Handle = _Interface.LoadTexture(picture.Size, source);
		found = _Pictures.emplace(source, picture).first;
	}

	return(found->second.Handle != 0 ? found->second.Size : Rml::Vector2i(0, 0));
}


void UI_Render_Picture(char const * source, float x, float y, float scale, float height)
{
	Rml::Vector2i const size = UI_Render_Picture_Size(source);
	if (size.x <= 0 || size.y <= 0 || scale <= 0.0f) {
		return;
	}

	Rml::TextureHandle const texture = _Pictures[source].Handle;
	float const width = size.x * scale;
	float const tile = size.y * scale;

	for (float drawn = 0.0f; drawn < height; drawn += tile) {
		float const rows = std::min(tile, height - drawn);
		float const v = rows / tile;

		Rml::Vertex vertices[4];
		for (Rml::Vertex & vertex : vertices) {
			vertex.colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
		}
		vertices[0].position = Rml::Vector2f(x, y + drawn);
		vertices[1].position = Rml::Vector2f(x + width, y + drawn);
		vertices[2].position = Rml::Vector2f(x + width, y + drawn + rows);
		vertices[3].position = Rml::Vector2f(x, y + drawn + rows);
		vertices[0].tex_coord = Rml::Vector2f(0.0f, 0.0f);
		vertices[1].tex_coord = Rml::Vector2f(1.0f, 0.0f);
		vertices[2].tex_coord = Rml::Vector2f(1.0f, v);
		vertices[3].tex_coord = Rml::Vector2f(0.0f, v);

		int const indices[6] = { 0, 1, 2, 0, 2, 3 };
		Rml::CompiledGeometryHandle const geometry = _Interface.CompileGeometry(Rml::Span<Rml::Vertex const>(vertices, 4), Rml::Span<int const>(indices, 6));
		_Interface.RenderGeometry(geometry, Rml::Vector2f(0.0f, 0.0f), texture);
		_Interface.ReleaseGeometry(geometry);
	}
}


void UI_Render_On_Reset(void)
{
	_ScissorEnabled = false;
}
