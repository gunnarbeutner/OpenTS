/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

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

bool Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode);
void Backend_End_Frame(void);

void Backend_Build_Ortho_Projection(float * result, int width, int height);

bool Backend_Frame_Is_Point_Sampled(void);
// Queues a 32 bit RGBA movie frame to draw over every following Backend_Present
// until replaced or cleared, at a rect in window pixels. The pixels are copied
// before this returns and stay owned by the caller.
void Backend_Queue_Video_Frame(void const * pixels, int pitch, int width, int height,
	int dest_x, int dest_y, int dest_width, int dest_height);

void Backend_Clear_Video_Frame(void);

// The CRT filter, which draws the frame on the curve of a tube under scanlines, a phosphor
// mask, a vignette and the glow around what is bright. It is display only: a window
// position still names the frame pixel it named without it. While it is off the present
// path submits exactly what it did before.
void Backend_Set_CRT_Filter(bool enabled);

char const * Backend_Renderer_Name(void);
