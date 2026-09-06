/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "movies.h"
#include "win.h"


// Returns true when the player should stop, so a film reaches the same skip vote the VQA
// player answers.
typedef bool (*MovieIdleCallback) (void);


class MP4Class
{
	public:
		MP4Class(char const * filename, int flags, MovieSurfaceLockCallback surface_lock,
			MovieSurfaceUnlockCallback surface_unlock, MovieSurfaceDrawCallback surface_draw,
			MovieIdleCallback idle = NULL, int frame_rate = -1, int draw_rate = -1);
		~MP4Class(void);

		bool Open_And_Load_Buffers(void);
		bool Set_Loop(int loop_id, int iterations);
		bool Set_Loop(int start, int end, int iterations);
		void Seek_To_Frame(int frame);
		int Play_VQA(int last_frame_to_play, bool breakout);
		bool Advance_Frame(bool &finished);
		void Pause_VQA(void);
		void Resume_VQA(void);
		void Close_And_Free_VQA(void);
		void Reset_VQA(void);
		bool Set_Draw_Buffer(void * buffer, int buffer_width, int buffer_height,
			int x_offset = 0, int y_offset = 0);

		// A fullscreen movie is drawn at the decoder's own color depth on a
		// layer fit to the window every frame, because a resize during
		// playback is not otherwise serviced; an inline movie keeps writing
		// through the draw buffer Set_Draw_Buffer names.
		void Set_Fullscreen_Video(void);

		int Get_Desired_Color_Mode(void);
		void Set_Primary_Color_Mode(int mode);
		int Get_VQA_Width(void) const;
		int Get_VQA_Height(void) const;
		int Set_VQA_Volume(int volume);
		bool Is_Paused(void) const;

	private:
		bool Draw_Frame(void);
		void Start(void);

		char Filename[MAX_PATH];
		int MovieID;
		int Width;
		int Height;
		int DrawBufferWidth;
		int DrawBufferHeight;
		int DrawOffsetX;
		int DrawOffsetY;
		bool IsFullscreenVideo;
		unsigned char * VideoFrameBuffer;
		int Volume;
		bool IsOpen;
		MovieIdleCallback IdleCallback;
		bool IsPaused;
		bool IsStarted;
		bool IsFocusPaused;
		MovieSurfaceLockCallback SurfaceLockCallback;
		MovieSurfaceUnlockCallback SurfaceUnlockCallback;
		MovieSurfaceDrawCallback SurfaceDrawCallback;
};
