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


// How the presented frame is filtered when the window is larger than it.
enum VideoScaleMode {
	VIDEO_SCALE_NEAREST,
	VIDEO_SCALE_LINEAR,
	VIDEO_SCALE_PIXELART,
};


// Where the game's frame lands inside the window. The frame keeps its aspect ratio, so
// the destination is centered and the window may show bars on two of its sides.
// Drawable dimensions and the destination rectangle are measured in physical pixels.
struct VideoScaleInfo
{
	int GameWidth;
	int GameHeight;
	int DrawableWidth;
	int DrawableHeight;
	int DestX;
	int DestY;
	int DestWidth;
	int DestHeight;
	float ScaleX;
	float ScaleY;
};


bool Video_Init(NativeWindow const & window, int drawablewidth, int drawableheight, int refreshrate);
void Video_Shutdown(void);

bool Video_Set_Mode(int width, int height);
void Video_On_Resize(int drawablewidth, int drawableheight);
void Video_Set_Refresh_Rate(int refreshrate);

#if defined(OPENTS_WIN32_SUBSTITUTE)

// Video_Request_Frame_Size only records a canvas size. Video_Service_Display is
// the only place the frame is resized, and it belongs at the bottom of the
// message pump, which the movie player and the dialog loops never reach while
// they hold a surface.
void Video_Request_Frame_Size(int width, int height);
void Video_Service_Display(void);

// Every frame size named anywhere must pass through here, or it will not match
// the frame the window produces.
void Video_Clamp_Frame_Size(int & width, int & height);

// The largest frame the window is followed to; beyond it the frame is scaled,
// since every frame is a full surface upload.
enum {
	VIDEO_FOLLOW_MAX_WIDTH = 2560,
	VIDEO_FOLLOW_MAX_HEIGHT = 1600,
};

#endif

void Video_Mark_Dirty(void);
void Video_Present(void);
void Video_Present_If_Dirty(void);

// Queues a true color movie frame and forces a present. The rect is in window
// pixels, because a resize during playback is not serviced and the frame must
// not wait for one.
void Video_Queue_Movie_Frame(void const * pixels, int pitch, int width, int height,
	int dest_x, int dest_y, int dest_width, int dest_height);
void Video_Clear_Movie_Frame(void);

VideoScaleInfo const & Video_Get_Scale_Info(void);

// Is a settled frame size waiting to be taken? A screen laid out against the
// current surfaces asks this and puts itself away so the resize can happen.
bool Video_Frame_Size_Is_Pending(void);

int * EnumDisplayModes(int minwidth, int minheight, int maxwidth, int maxheight);
