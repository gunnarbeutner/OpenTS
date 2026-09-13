/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The bgfx side of the presenter. This is the only translation unit that includes bgfx,
// which keeps the library's headers and build settings away from the rest of the engine.

#include "bgfxbackend.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#else
#include "except.h"
#include "platform/diagnostics.h"
#include "win.h"
#endif

#include "bgfxviews.hh"

#include <algorithm>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fs_ocornut_imgui.bin.h>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <vs_ocornut_imgui.bin.h>


static const bgfx::EmbeddedShader _EmbeddedShaders[] = {
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER_END()
};




static bool _Initialized = false;

static bgfx::TextureHandle _FrameTexture = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _Program = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _TextureSampler = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _PrescaleTarget = BGFX_INVALID_HANDLE;
static bgfx::VertexLayout _VertexLayout;

static int _FrameWidth = 0;
static int _FrameHeight = 0;

static bool _FrameUploaded = false;

static bool _FramePending = false;
static bool _FramePointSampled = false;

static int _PrescaleWidth = 0;
static int _PrescaleHeight = 0;
static int _DrawableWidth = 0;
static int _DrawableHeight = 0;
static unsigned int _ResetFlags = BGFX_RESET_FLIP_AFTER_RENDER;

// True while the frame texture holds the game's own 565 layout. When the hardware cannot
// sample that format the frame is widened to 32 bits on the way in instead.
static bool _FrameIs565 = false;
static unsigned int * _ConvertBuffer = NULL;
static unsigned int _ConvertTable[65536];

// The queued movie frame, drawn as its own quad over the frame texture at a
// rect already in window pixels.
static bgfx::TextureHandle _VideoTexture = BGFX_INVALID_HANDLE;
static int _VideoTextureWidth = 0;
static int _VideoTextureHeight = 0;
static bool _VideoPending = false;
static int _VideoFrameX = 0;
static int _VideoFrameY = 0;
static int _VideoFrameWidth = 0;
static int _VideoFrameHeight = 0;


// The CRT filter. Everything here is skipped while it is off, so the ordinary present path
// submits the quads it always did.
static bool _CRTEnabled = false;
static bgfx::TextureHandle _MaskTexture = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle _VignetteTexture = BGFX_INVALID_HANDLE;
static int _MaskPeriod = 0;

// How far a scanline dips, how far a stripe of the aperture grille dims the two channels
// beside it, and how dark the corners go.
static float const CRT_SCAN_DEPTH = 0.58f;
static float const CRT_GRILLE_DEPTH = 0.34f;
static float const CRT_VIGNETTE_DEPTH = 0.55f;

// How far the glass bulges, as a fraction of half the picture. The corners stay where they
// are and the middle of each edge is drawn in by about this much.
static float const CRT_BARREL = 0.018f;

// The additive passes are spaced in pixels of a 720 line picture and scaled from there, so
// the bleed and the halation keep their proportions at any size.
static float const CRT_TAP_REFERENCE = 720.0f;

// The picture is drawn as a grid, because the glass it is drawn on curves.
static int const CRT_COLUMNS = 24;
static int const CRT_ROWS = 18;


struct BackendVertex
{
	float X;
	float Y;
	float U;
	float V;
	unsigned int Color;
};


// How far a point of the picture is moved off the flat rectangle the engine thinks it draws
// into by the curve of the glass.
struct CRTWarp
{
	float Barrel;
};


// One more copy of the picture, offset by this many pixels of a 720 line picture and added
// at this strength.
struct CRTTap
{
	float X;
	float Y;
	float Alpha;
};


static void Report_Fatal(char const * text)
{
#ifdef __EMSCRIPTEN__
	emscripten_console_error(text);
	abort();
#else
	Fatal("%s", text);
#endif
}


static void Report_Trace(char const * text)
{
#ifdef __EMSCRIPTEN__
	emscripten_console_log(text);
#else
	Debug_Output_Write(text);
#endif
}


// bgfx reports lost devices and shader failures through this rather than a return code,
// so the engine would otherwise present to a black window with no explanation.
class BackendCallback : public bgfx::CallbackI
{
	public:
		virtual ~BackendCallback(void) override {}

		virtual void fatal(const char * filepath, uint16_t line, bgfx::Fatal::Enum code, const char * str) override
		{
			char message[1024];

			// A debug check is the library's own assertion, not a renderer failure. The ones it
			// runs while shutting down compare reference counts on interfaces that an overlay
			// or the Direct3D debug layer is free to hold, so ending the process over one would
			// report somebody else's reference as a crash.
			if (code == bgfx::Fatal::DebugCheck) {
				snprintf(message, sizeof(message), "Renderer check failed at %s(%u): %s",
							filepath != NULL ? filepath : "", (unsigned)line, str != NULL ? str : "");
				Report_Trace(message);
				return;
			}

			snprintf(message, sizeof(message), "Renderer error %d at %s(%u): %s", (int)code,
						filepath != NULL ? filepath : "", (unsigned)line, str != NULL ? str : "");
			Report_Fatal(message);
		}

		virtual void traceVargs(const char * filepath, uint16_t line, const char * format, va_list argList) override
		{
			char message[1024];
			vsnprintf(message, sizeof(message), format, argList);
			Report_Trace(message);
		}

		virtual void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
		virtual void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
		virtual void profilerEnd(void) override {}
		virtual uint32_t cacheReadSize(uint64_t) override { return(0); }
		virtual bool cacheRead(uint64_t, void *, uint32_t) override { return(false); }
		virtual void cacheWrite(uint64_t, const void *, uint32_t) override {}
		virtual void screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *, uint32_t, bool) override {}
		virtual void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
		virtual void captureEnd(void) override {}
		virtual void captureFrame(const void *, uint32_t) override {}
};

static BackendCallback _Callback;


#if defined(_WIN32)
// bgfx contains cache-line-aligned render records but requests their backing arrays with
// the allocator's default alignment. The Win32 CRT only guarantees eight-byte alignment,
// which is insufficient when clang-cl copies those records with aligned SSE instructions.
class BackendAllocator : public bx::AllocatorI
{
	public:
		virtual ~BackendAllocator(void) override {}

		virtual void * realloc(void * ptr, size_t size, size_t alignment, const char *, uint32_t) override
		{
			if (size == 0) {
				_aligned_free(ptr);
				return(NULL);
			}

			const size_t cachelinealignment = BX_CACHE_LINE_SIZE;
			alignment = std::max(alignment, cachelinealignment);
			return(_aligned_realloc(ptr, size, alignment));
		}
};

static BackendAllocator _Allocator;
#endif


/// <summary>
/// Builds the table that widens a 565 pixel to the 32 bit color the fallback path uploads.
/// </summary>
static void Build_Convert_Table(void)
{
	for (int pixel = 0; pixel < 65536; pixel++) {
		unsigned int red = (unsigned int)(((pixel >> 11) & 0x1F) * 255 / 31);
		unsigned int green = (unsigned int)(((pixel >> 5) & 0x3F) * 255 / 63);
		unsigned int blue = (unsigned int)((pixel & 0x1F) * 255 / 31);

		_ConvertTable[pixel] = 0xFF000000 | (red << 16) | (green << 8) | blue;
	}
}


// Where a point of the picture lands on the glass. The coordinates are measured from the
// middle of the picture and run to one at its edges, and they come back in the same
// measure. The corners are held in place, so the curve keeps the picture inside the
// rectangle it was given rather than spilling over the moulding.
static void Warp_Point(CRTWarp const & warp, float nx, float ny, float & warpedx, float & warpedy)
{
	// Convex glass: the edges bow out and the corners draw in, within the picture's bounds.
	float const radius = nx * nx + ny * ny;
	float const bulge = 1.0f + warp.Barrel * (1.0f - radius);

	warpedx = nx * bulge;
	warpedy = ny * bulge;
}


/// <summary>
/// Submits one textured rectangle covering the given destination, tiled to the requested
/// number of texture repeats and tinted by the given colour. A warp subdivides it into a
/// grid whose points are moved onto the curve of the glass.
/// </summary>
static bool Submit_Rect(bgfx::ViewId view, bgfx::TextureHandle texture, float x, float y, float width, float height,
	float urepeat, float vrepeat, unsigned int color, uint64_t state, unsigned int samplerflags, bool flipv,
	CRTWarp const * warp)
{
	int const columns = (warp != nullptr) ? CRT_COLUMNS : 1;
	int const rows = (warp != nullptr) ? CRT_ROWS : 1;
	uint32_t const count = (uint32_t)(columns * rows * 6);

	bgfx::TransientVertexBuffer buffer;

	if (bgfx::getAvailTransientVertexBuffer(count, _VertexLayout) < count) {
		return(false);
	}

	bgfx::allocTransientVertexBuffer(&buffer, count, _VertexLayout);

	BackendVertex * vertex = (BackendVertex *)buffer.data;

	for (int row = 0; row < rows; row++) {
		for (int column = 0; column < columns; column++) {
			float const u0 = urepeat * (float)column / (float)columns;
			float const u1 = urepeat * (float)(column + 1) / (float)columns;
			float const t0 = (float)row / (float)rows;
			float const t1 = (float)(row + 1) / (float)rows;
			float const v0 = flipv ? vrepeat * (1.0f - t0) : vrepeat * t0;
			float const v1 = flipv ? vrepeat * (1.0f - t1) : vrepeat * t1;

			float corner[4][2] = {
				{ x + width * (float)column / (float)columns, y + height * t0 },
				{ x + width * (float)(column + 1) / (float)columns, y + height * t0 },
				{ x + width * (float)(column + 1) / (float)columns, y + height * t1 },
				{ x + width * (float)column / (float)columns, y + height * t1 },
			};

			if (warp != nullptr) {
				for (int point = 0; point < 4; point++) {
					float const nx = (corner[point][0] - (x + width * 0.5f)) / (width * 0.5f);
					float const ny = (corner[point][1] - (y + height * 0.5f)) / (height * 0.5f);
					float warpedx = nx;
					float warpedy = ny;

					Warp_Point(*warp, nx, ny, warpedx, warpedy);
					corner[point][0] = x + width * 0.5f + warpedx * width * 0.5f;
					corner[point][1] = y + height * 0.5f + warpedy * height * 0.5f;
				}
			}

			vertex[0] = { corner[0][0], corner[0][1], u0, v0, color };
			vertex[1] = { corner[1][0], corner[1][1], u1, v0, color };
			vertex[2] = { corner[2][0], corner[2][1], u1, v1, color };
			vertex[3] = { corner[0][0], corner[0][1], u0, v0, color };
			vertex[4] = { corner[2][0], corner[2][1], u1, v1, color };
			vertex[5] = { corner[3][0], corner[3][1], u0, v1, color };
			vertex += 6;
		}
	}

	bgfx::setVertexBuffer(0, &buffer);
	bgfx::setTexture(0, _TextureSampler, texture, samplerflags);
	bgfx::setState(state);
	bgfx::submit(view, _Program);
	return(true);
}


/// <summary>
/// Submits one textured rectangle covering the given destination. False means the
/// transient vertex memory ran out and nothing was submitted.
/// </summary>
static bool Submit_Quad(bgfx::ViewId view, bgfx::TextureHandle texture, float x, float y, float width, float height, unsigned int samplerflags, bool flipv = false)
{
	return(Submit_Rect(view, texture, x, y, width, height, 1.0f, 1.0f, 0xFFFFFFFF,
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, samplerflags, flipv, nullptr));
}


/// <summary>
/// Builds an orthographic projection over a target measured in pixels, with the origin in
/// its top left corner.
/// </summary>
void Backend_Build_Ortho_Projection(float * result, int width, int height)
{
	const float depthnear = 0.0f;
	const float depthfar = 1000.0f;
	const bool homogeneous = bgfx::getCaps()->homogeneousDepth;

	memset(result, 0, sizeof(float) * 16);

	result[0] = 2.0f / (float)width;
	result[5] = -2.0f / (float)height;
	result[10] = homogeneous ? 2.0f / (depthfar - depthnear) : 1.0f / (depthfar - depthnear);
	result[12] = -1.0f;
	result[13] = 1.0f;
	result[14] = homogeneous ? -(depthfar + depthnear) / (depthfar - depthnear) : -depthnear / (depthfar - depthnear);
	result[15] = 1.0f;
}


/// <summary>
/// Sets a view to draw into a target of the given size using pixel coordinates.
/// </summary>
static void Set_View_Transform(bgfx::ViewId view, int width, int height)
{
	float projection[16];
	bgfx::setViewRect(view, 0, 0, (uint16_t)width, (uint16_t)height);
	Backend_Build_Ortho_Projection(projection, width, height);
	bgfx::setViewTransform(view, NULL, projection);
}


/// <summary>
/// Discards the intermediate target the pixel art filter magnifies through.
/// </summary>
static void Destroy_Prescale_Target(void)
{
	if (bgfx::isValid(_PrescaleTarget)) {
		bgfx::destroy(_PrescaleTarget);
		_PrescaleTarget = BGFX_INVALID_HANDLE;
	}
	_PrescaleWidth = 0;
	_PrescaleHeight = 0;
}


/// <summary>
/// Makes sure the pixel art filter has an intermediate target of the requested size.
/// </summary>
/// <returns>bool; Is a target of that size ready to render into?</returns>
static bool Ensure_Prescale_Target(int width, int height)
{
	if (bgfx::isValid(_PrescaleTarget) && _PrescaleWidth == width && _PrescaleHeight == height) {
		return(true);
	}

	Destroy_Prescale_Target();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (width <= 0 || height <= 0 || width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize) {
		return(false);
	}

	_PrescaleTarget = bgfx::createFrameBuffer((uint16_t)width, (uint16_t)height, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	if (!bgfx::isValid(_PrescaleTarget)) {
		return(false);
	}

	_PrescaleWidth = width;
	_PrescaleHeight = height;
	return(true);
}


// One texel of the phosphor mask: a scanline profile down the rows and an aperture grille
// across the columns, one column to a channel.
static void Build_Mask_Texel(int column, int row, int height, float depth, unsigned char * texel)
{
	// The row is sampled at the texel rather than between two, or a two row cell would give
	// both rows the same value and the scanlines would be a flat dimming.
	float const scan = 1.0f - CRT_SCAN_DEPTH * depth * (0.5f - 0.5f * cosf(6.2831853f * (float)row / (float)height));

	for (int channel = 0; channel < 3; channel++) {
		float const grille = (channel == column) ? 1.0f : (1.0f - CRT_GRILLE_DEPTH * depth);
		texel[channel] = (unsigned char)(255.0f * scan * grille + 0.5f);
	}

	texel[3] = 255;
}


// The mask is tiled over the picture rather than stretched, so its texture only has to
// hold one cell: three columns for the grille and one row per destination pixel of a
// scanline period.
static bool Ensure_Mask_Texture(int period)
{
	if (bgfx::isValid(_MaskTexture) && _MaskPeriod == period) {
		return(true);
	}

	if (bgfx::isValid(_MaskTexture)) {
		bgfx::destroy(_MaskTexture);
		_MaskTexture = BGFX_INVALID_HANDLE;
	}
	_MaskPeriod = 0;

	int const height = std::min(std::max(period, 2), 8);
	unsigned char texels[3 * 8 * 4];

	// A picture the page draws at its own resolution has a cell no larger than the detail
	// it covers, so the mask is eased off there rather than competing with the picture.
	float const depth = (period >= 3) ? 1.0f : 0.8f;

	for (int row = 0; row < height; row++) {
		for (int column = 0; column < 3; column++) {
			Build_Mask_Texel(column, row, height, depth, texels + (row * 3 + column) * 4);
		}
	}

	_MaskTexture = bgfx::createTexture2D(3, (uint16_t)height, false, 1, bgfx::TextureFormat::RGBA8,
		BGFX_TEXTURE_NONE, bgfx::copy(texels, (uint32_t)(3 * height * 4)));

	if (!bgfx::isValid(_MaskTexture)) {
		return(false);
	}

	_MaskPeriod = period;
	return(true);
}


static bool Ensure_Vignette_Texture(void)
{
	if (bgfx::isValid(_VignetteTexture)) {
		return(true);
	}

	int const size = 64;
	unsigned char texels[size * size * 4];

	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			float const nx = ((float)x + 0.5f) / (float)size * 2.0f - 1.0f;
			float const ny = ((float)y + 0.5f) / (float)size * 2.0f - 1.0f;
			float falloff = std::min(std::max((nx * nx + ny * ny) * 0.58f, 0.0f), 1.0f);

			falloff = falloff * falloff * (3.0f - 2.0f * falloff);
			falloff = falloff * (0.45f + 0.55f * falloff);

			unsigned char const level = (unsigned char)(255.0f * (1.0f - CRT_VIGNETTE_DEPTH * falloff) + 0.5f);
			unsigned char * texel = texels + (y * size + x) * 4;

			texel[0] = level;
			texel[1] = level;
			texel[2] = level;
			texel[3] = 255;
		}
	}

	_VignetteTexture = bgfx::createTexture2D((uint16_t)size, (uint16_t)size, false, 1, bgfx::TextureFormat::RGBA8,
		BGFX_TEXTURE_NONE, bgfx::copy(texels, (uint32_t)sizeof(texels)));

	return(bgfx::isValid(_VignetteTexture));
}


// Adds one more copy of the picture, offset and faint. An additive copy carries the
// brightness of what it copies, so a dark ground stays dark and only what is already
// bright spreads.
static void Submit_CRT_Tap(bgfx::TextureHandle source, float x, float y, float width, float height,
	float offsetx, float offsety, float alpha, bool flipv, CRTWarp const & warp)
{
	unsigned int const color = 0x00FFFFFF | ((unsigned int)(255.0f * std::min(alpha, 1.0f) + 0.5f) << 24);

	Submit_Rect(VIEW_PRESENT, source, x + offsetx, y + offsety, width, height, 1.0f, 1.0f, color,
		BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE),
		BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, flipv, &warp);
}


// The picture on the curve of the glass.
static bool Submit_CRT_Picture(bgfx::TextureHandle source, int destx, int desty, int destwidth, int destheight,
	unsigned int samplerflags, bool flipv)
{
	CRTWarp const glass = { CRT_BARREL };

	return(Submit_Rect(VIEW_PRESENT, source, (float)destx, (float)desty, (float)destwidth, (float)destheight,
		1.0f, 1.0f, 0xFFFFFFFF, BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A, samplerflags, flipv, &glass));
}


// The picture bleeding sideways, then the mask and the corners over it, then the halation
// the glass spreads around anything bright. The UI shell submits its own view after this
// one, so the overlay is drawn over the filter and stays as legible as it is without it.
static void Submit_CRT_Overlays(bgfx::TextureHandle source, int destx, int desty, int destwidth, int destheight,
	bool flipv)
{
	// Sideways only, and about a pixel: a beam has a width, so no pixel of a tube has a
	// razor edge along the line it is drawn on.
	static CRTTap const bleed[] = {
		{ -1.2f, 0.0f, 0.14f },
		{ 1.2f, 0.0f, 0.14f },
	};

	// Light spread through the glass. The near taps are the glow around lettering and the
	// far ones the wider halo a bright field throws.
	static CRTTap const halation[] = {
		{ 0.0f, 0.0f, 0.07f },
		{ -3.0f, 0.0f, 0.045f },
		{ 3.0f, 0.0f, 0.045f },
		{ 0.0f, -3.0f, 0.035f },
		{ 0.0f, 3.0f, 0.035f },
		{ -7.0f, -7.0f, 0.025f },
		{ 7.0f, 7.0f, 0.025f },
	};

	float const x = (float)destx;
	float const y = (float)desty;
	float const width = (float)destwidth;
	float const height = (float)destheight;

	// A tap is spaced for a 720 line picture and scales from there, with a floor so the
	// spread does not collapse into the pixel it came from in a small window.
	float const spacing = std::max(std::min(width, height) / CRT_TAP_REFERENCE, 0.5f);
	CRTWarp const glass = { CRT_BARREL };

	for (CRTTap const & tap : bleed) {
		Submit_CRT_Tap(source, x, y, width, height, tap.X * spacing, tap.Y * spacing, tap.Alpha, flipv, glass);
	}

	// One scanline to a frame row where the picture is magnified, and a two pixel period
	// where it is not, which is the line count a tube of this size would have had. Both
	// counts are whole so the pattern keeps its phase against the destination pixels.
	int const period = std::max(2, (_FrameHeight > 0) ? (destheight / _FrameHeight) : 2);
	int const grille = 3 * std::max(1, period / 2);
	float const vrepeat = (float)std::max(1, (int)((float)destheight / (float)period + 0.5f));
	float const urepeat = (float)std::max(1, (int)((float)destwidth / (float)grille + 0.5f));

	if (Ensure_Mask_Texture(period)) {
		Submit_Rect(VIEW_PRESENT, _MaskTexture, x, y, width, height, urepeat, vrepeat, 0xFFFFFFFF,
			BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO),
			BGFX_SAMPLER_POINT, false, &glass);
	}

	if (Ensure_Vignette_Texture()) {
		Submit_Rect(VIEW_PRESENT, _VignetteTexture, x, y, width, height, 1.0f, 1.0f, 0xFFFFFFFF,
			BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO),
			BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, false, &glass);
	}

	for (CRTTap const & tap : halation) {
		Submit_CRT_Tap(source, x, y, width, height, tap.X * spacing, tap.Y * spacing, tap.Alpha, flipv, glass);
	}
}


/// <summary>
/// Starts the renderer on an existing presentation target.
/// </summary>
/// <param name="window">The window the frame is presented into.</param>
/// <param name="drawablewidth">The drawable area's width in physical pixels.</param>
/// <param name="drawableheight">The drawable area's height in physical pixels.</param>
/// <param name="renderer">Which graphics API to ask for, or auto to let bgfx decide.</param>
/// <param name="vsync">Should presents wait for the display's refresh?</param>
/// <returns>bool; Did the renderer start?</returns>
bool Backend_Init(NativeWindow const & window, int drawablewidth, int drawableheight, BackendRenderer renderer, bool vsync)
{
	if (_Initialized) {
		return(true);
	}

#if !defined(__EMSCRIPTEN__)
	bgfx::renderFrame();
#endif

	_DrawableWidth = drawablewidth;
	_DrawableHeight = drawableheight;
	_ResetFlags = BGFX_RESET_FLIP_AFTER_RENDER | (vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);

	bgfx::Init init;
	init.platformData.ndt = window.Display;
	init.platformData.nwh = window.Handle;
	init.platformData.type = window.Type == NATIVE_WINDOW_WAYLAND
		? bgfx::NativeWindowHandleType::Wayland
		: bgfx::NativeWindowHandleType::Default;
	init.resolution.width = (uint32_t)drawablewidth;
	init.resolution.height = (uint32_t)drawableheight;
	init.resolution.reset = _ResetFlags;
	init.callback = &_Callback;
#if defined(_WIN32)
	init.allocator = &_Allocator;
#endif

	switch (renderer) {
		case BACKEND_RENDERER_D3D11:
			init.type = bgfx::RendererType::Direct3D11;
			break;

		case BACKEND_RENDERER_D3D12:
			init.type = bgfx::RendererType::Direct3D12;
			break;

		case BACKEND_RENDERER_VULKAN:
			init.type = bgfx::RendererType::Vulkan;
			break;

		case BACKEND_RENDERER_OPENGL:
			init.type = bgfx::RendererType::OpenGL;
			break;

		case BACKEND_RENDERER_OPENGLES:
			init.type = bgfx::RendererType::OpenGLES;
			break;

		default:
			init.type = bgfx::RendererType::Count;
			break;
	}

	if (!bgfx::init(init)) {
		return(false);
	}

	_VertexLayout.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	bgfx::RendererType::Enum type = bgfx::getRendererType();
	bgfx::ShaderHandle vertexshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_ocornut_imgui");
	bgfx::ShaderHandle fragmentshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_ocornut_imgui");

	if (!bgfx::isValid(vertexshader) || !bgfx::isValid(fragmentshader)) {
		bgfx::shutdown();
		return(false);
	}

	_Program = bgfx::createProgram(vertexshader, fragmentshader, true);
	_TextureSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

	if (!bgfx::isValid(_Program) || !bgfx::isValid(_TextureSampler)) {
		bgfx::shutdown();
		return(false);
	}

	if (_CRTEnabled) {
		bgfx::setViewMode(VIEW_PRESENT, bgfx::ViewMode::Sequential);
	}

	_Initialized = true;
	return(true);
}


/// <summary>
/// Shuts the renderer down and releases everything it created.
/// </summary>
void Backend_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	Destroy_Prescale_Target();

	if (bgfx::isValid(_FrameTexture)) {
		bgfx::destroy(_FrameTexture);
		_FrameTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_VideoTexture)) {
		bgfx::destroy(_VideoTexture);
		_VideoTexture = BGFX_INVALID_HANDLE;
	}
	_VideoTextureWidth = 0;
	_VideoTextureHeight = 0;
	_VideoPending = false;
	if (bgfx::isValid(_MaskTexture)) {
		bgfx::destroy(_MaskTexture);
		_MaskTexture = BGFX_INVALID_HANDLE;
	}
	_MaskPeriod = 0;
	if (bgfx::isValid(_VignetteTexture)) {
		bgfx::destroy(_VignetteTexture);
		_VignetteTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_TextureSampler)) {
		bgfx::destroy(_TextureSampler);
		_TextureSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_Program)) {
		bgfx::destroy(_Program);
		_Program = BGFX_INVALID_HANDLE;
	}

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	bgfx::shutdown();

	_FrameWidth = 0;
	_FrameHeight = 0;
	_FrameUploaded = false;
	_FramePending = false;
	_Initialized = false;
}


/// <summary>
/// Points the renderer at a frame of the given size, replacing any earlier one.
/// </summary>
/// <returns>bool; Is a texture of that size ready to receive frames?</returns>
bool Backend_Set_Frame_Size(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return(false);
	}

	if (bgfx::isValid(_FrameTexture) && _FrameWidth == width && _FrameHeight == height) {
		return(true);
	}

	if (bgfx::isValid(_FrameTexture)) {
		bgfx::destroy(_FrameTexture);
		_FrameTexture = BGFX_INVALID_HANDLE;
	}

	// bgfx names packed formats from their low bits up, so its B5G6R5 is the layout the
	// game already draws in. Emulated support would convert every upload on the way
	// through, which is what the fallback below does more cheaply.
	const bgfx::Caps * caps = bgfx::getCaps();
	_FrameIs565 = (caps->formats[bgfx::TextureFormat::B5G6R5] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;

	_FrameTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, _FrameIs565 ? bgfx::TextureFormat::B5G6R5 : bgfx::TextureFormat::BGRA8);
	_FrameUploaded = false;
	if (!bgfx::isValid(_FrameTexture)) {
		return(false);
	}

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	if (!_FrameIs565) {
		if (_ConvertTable[0xFFFF] == 0) {
			Build_Convert_Table();
		}
		_ConvertBuffer = new unsigned int[width * height];
	}

	_FrameWidth = width;
	_FrameHeight = height;
	return(true);
}


/// <summary>
/// Tells the renderer the drawable area changed size.
/// </summary>
void Backend_On_Resize(int drawablewidth, int drawableheight)
{
	if (!_Initialized || drawablewidth <= 0 || drawableheight <= 0) {
		return;
	}

	if (_DrawableWidth == drawablewidth && _DrawableHeight == drawableheight) {
		return;
	}

	_DrawableWidth = drawablewidth;
	_DrawableHeight = drawableheight;
	bgfx::reset((uint32_t)drawablewidth, (uint32_t)drawableheight, _ResetFlags);
}


/// <summary>
/// Queues a 32 bit RGBA movie frame to draw over every following present until
/// replaced or cleared. The rect is in window pixels; the pixels are copied
/// before this returns.
/// </summary>
/// <param name="pitch">The bytes between one row and the next.</param>
void Backend_Queue_Video_Frame(void const * pixels, int pitch, int width, int height,
	int dest_x, int dest_y, int dest_width, int dest_height)
{
	if (!_Initialized || pixels == NULL || width <= 0 || height <= 0) {
		return;
	}

	if (!bgfx::isValid(_VideoTexture) || _VideoTextureWidth != width || _VideoTextureHeight != height) {
		if (bgfx::isValid(_VideoTexture)) {
			bgfx::destroy(_VideoTexture);
		}
		_VideoTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::RGBA8);
		if (!bgfx::isValid(_VideoTexture)) {
			_VideoTextureWidth = 0;
			_VideoTextureHeight = 0;
			return;
		}
		_VideoTextureWidth = width;
		_VideoTextureHeight = height;
	}

	bgfx::updateTexture2D(_VideoTexture, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height, bgfx::copy(pixels, (uint32_t)(height * pitch)), (uint16_t)pitch);

	_VideoFrameX = dest_x;
	_VideoFrameY = dest_y;
	_VideoFrameWidth = dest_width;
	_VideoFrameHeight = dest_height;
	_VideoPending = true;
}


void Backend_Clear_Video_Frame(void)
{
	_VideoPending = false;
}


/// <summary>
/// Submits the frame to the window, uploading new pixels first when given any.
/// </summary>
/// <param name="pixels">The frame's top left pixel, in 16 bit 565, or NULL to present the
/// frame uploaded last.</param>
/// <param name="pitch">The bytes between one row of that frame and the next.</param>
/// <param name="destx">Where the left edge of the frame lands in the window.</param>
/// <param name="desty">Where the top edge of the frame lands in the window.</param>
/// <param name="destwidth">How wide the frame is drawn.</param>
/// <param name="destheight">How tall the frame is drawn.</param>
/// <param name="mode">How the frame is filtered when it is drawn larger than it is.</param>
/// <returns>bool; Was a frame submitted? When not, the window is unchanged and nothing should
/// be drawn over it.</returns>
bool Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode)
{
	if (!_Initialized || !bgfx::isValid(_FrameTexture)) {
		return(false);
	}

	if (pixels == NULL && !_FrameUploaded) {
		return(false);
	}

	// A minimized window has no client area to present into.
	if (_DrawableWidth <= 0 || _DrawableHeight <= 0) {
		return(false);
	}

	_FramePending = true;

	if (pixels != NULL) {
		if (_FrameIs565) {
			bgfx::updateTexture2D(_FrameTexture, 0, 0, 0, 0, (uint16_t)_FrameWidth, (uint16_t)_FrameHeight, bgfx::copy(pixels, (uint32_t)(_FrameHeight * pitch)), (uint16_t)pitch);
			_FrameUploaded = true;
		} else if (_ConvertBuffer != NULL) {
			for (int y = 0; y < _FrameHeight; y++) {
				unsigned short const * source = (unsigned short const *)((char const *)pixels + y * pitch);
				unsigned int * dest = _ConvertBuffer + y * _FrameWidth;
				for (int x = 0; x < _FrameWidth; x++) {
					dest[x] = _ConvertTable[source[x]];
				}
			}
			bgfx::updateTexture2D(_FrameTexture, 0, 0, 0, 0, (uint16_t)_FrameWidth, (uint16_t)_FrameHeight, bgfx::copy(_ConvertBuffer, (uint32_t)(_FrameWidth * _FrameHeight * 4)), (uint16_t)(_FrameWidth * 4));
			_FrameUploaded = true;
		}
	}

	bgfx::TextureHandle source = _FrameTexture;
	unsigned int samplerflags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	bool from_prescale = false;

	if (mode == BACKEND_SCALE_NEAREST) {
		samplerflags |= BGFX_SAMPLER_POINT;
	}

	// The pixel art filter keeps whole pixels whole. An exact multiple needs nothing but
	// point sampling; anything else is magnified to the next whole multiple with point
	// sampling and then shrunk to the window smoothly, which keeps edges sharp without
	// the uneven pixel sizes that point sampling alone would give.
	if (mode == BACKEND_SCALE_PIXELART && destwidth > _FrameWidth && destheight > _FrameHeight) {
		if ((destwidth % _FrameWidth) == 0 && (destheight % _FrameHeight) == 0) {
			samplerflags |= BGFX_SAMPLER_POINT;
		} else {
			int scale = (destwidth + _FrameWidth - 1) / _FrameWidth;
			int scaley = (destheight + _FrameHeight - 1) / _FrameHeight;
			if (scaley > scale) {
				scale = scaley;
			}

			if (Ensure_Prescale_Target(_FrameWidth * scale, _FrameHeight * scale)) {
				bgfx::setViewFrameBuffer(VIEW_PRESCALE, _PrescaleTarget);
				bgfx::setViewClear(VIEW_PRESCALE, BGFX_CLEAR_COLOR, 0x000000FF);
				Set_View_Transform(VIEW_PRESCALE, _PrescaleWidth, _PrescaleHeight);
				if (Submit_Quad(VIEW_PRESCALE, _FrameTexture, 0.0f, 0.0f, (float)_PrescaleWidth, (float)_PrescaleHeight, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT)) {
					source = bgfx::getTexture(_PrescaleTarget);
					from_prescale = true;
				}
			}
		}
	}

	// Clearing the whole window is what paints the bars beside a frame that does not
	// share the window's shape.
	bgfx::setViewFrameBuffer(VIEW_PRESENT, BGFX_INVALID_HANDLE);
	bgfx::setViewClear(VIEW_PRESENT, BGFX_CLEAR_COLOR, 0x000000FF);
	Set_View_Transform(VIEW_PRESENT, _DrawableWidth, _DrawableHeight);

	_FramePointSampled = (samplerflags & BGFX_SAMPLER_POINT) != 0;

	bool flipv = from_prescale && bgfx::getCaps()->originBottomLeft;
	bool submitted = _CRTEnabled
		? Submit_CRT_Picture(source, destx, desty, destwidth, destheight, samplerflags, flipv)
		: Submit_Quad(VIEW_PRESENT, source, (float)destx, (float)desty, (float)destwidth, (float)destheight, samplerflags, flipv);
	if (_VideoPending && bgfx::isValid(_VideoTexture)) {
		Submit_Quad(VIEW_PRESENT, _VideoTexture,
			(float)_VideoFrameX, (float)_VideoFrameY,
			(float)_VideoFrameWidth, (float)_VideoFrameHeight,
			BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	}
	if (_CRTEnabled) {
		Submit_CRT_Overlays(source, destx, desty, destwidth, destheight, flipv);
	}
	return(submitted);
}


void Backend_Set_CRT_Filter(bool enabled)
{
	_CRTEnabled = enabled;
	if (_Initialized && enabled) {
		bgfx::setViewMode(VIEW_PRESENT, bgfx::ViewMode::Sequential);
	}
}


bool Backend_Frame_Is_Point_Sampled(void)
{
	return(_FramePointSampled);
}


void Backend_End_Frame(void)
{
	if (!_Initialized || !_FramePending) {
		return;
	}

	_FramePending = false;
	bgfx::frame();
}


/// <summary>
/// Names the graphics API the renderer settled on.
/// </summary>
char const * Backend_Renderer_Name(void)
{
	if (!_Initialized) {
		return("none");
	}
	return(bgfx::getRendererName(bgfx::getRendererType()));
}
