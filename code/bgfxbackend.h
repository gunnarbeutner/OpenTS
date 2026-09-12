/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The renderer's private interface. No bgfx type appears here, so a caller needs neither
// the library's headers nor its build settings. video.cpp is the engine's only caller.

#pragma once

#include "nativewindow.hh"


enum BackendRenderer {
	BACKEND_RENDERER_AUTO,
	BACKEND_RENDERER_D3D11,
	BACKEND_RENDERER_D3D12,
	BACKEND_RENDERER_VULKAN,
	BACKEND_RENDERER_OPENGL,

	// OpenGL ES 3, which is WebGL 2 in a browser. Appended so that the stored
	// Renderer configuration value keeps its meaning.
	BACKEND_RENDERER_OPENGLES,
};


enum BackendScaleMode {
	BACKEND_SCALE_NEAREST,
	BACKEND_SCALE_LINEAR,
	BACKEND_SCALE_PIXELART,
};


// Drawable sizes are physical pixel dimensions supplied by the application shell.
bool Backend_Init(NativeWindow const & window, int drawablewidth, int drawableheight, BackendRenderer renderer, bool vsync);
void Backend_Shutdown(void);

bool Backend_Set_Frame_Size(int width, int height);
void Backend_On_Resize(int drawablewidth, int drawableheight);

// Submits the frame. The pixels are 16 bit 565 and stay owned by the caller; they are
// consumed before this returns. Nothing reaches the screen until Backend_End_Frame, so the
// UI shell can submit over the frame in between.
//
// upload says whether the pixels changed since the last present. A present that only the
// overlay asked for passes false and reuses the texture already on the device, which is
// what keeps a menu over a still frame from costing a full upload per refresh.
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode, bool upload);

// Ends the bgfx frame that Backend_Present opened, putting everything submitted to it on
// the screen. No other code begins or ends a bgfx frame.
void Backend_End_Frame(void);

// Queues a 32 bit RGBA movie frame to draw over every following Backend_Present
// until replaced or cleared, at a rect in window pixels. The pixels are copied
// before this returns and stay owned by the caller.
void Backend_Queue_Video_Frame(void const * pixels, int pitch, int width, int height,
	int dest_x, int dest_y, int dest_width, int dest_height);

void Backend_Clear_Video_Frame(void);

char const * Backend_Renderer_Name(void);
