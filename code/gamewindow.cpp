/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "gamewindow.h"

#include "_keyboar.h"
#include "_map.h"
#include "_surface.h"
#include "_tooltip.h"
#include "_xmouse.h"
#include "audio/audioengine.h"
#include "cctooltip.h"
#include "dbgprint.h"
#include "globals.h"
#include "gscreen.h"
#include "hostwindow.h"
#include "init.h"
#include "keyboard.h"
#include "movie.h"
#include "movies.h"
#include "video.h"
#include "vidscale.h"
#include "wwmouse.h"


static bool _HandlingMouseWheel = false;

// Whether the game held the mouse when the window lost the focus, so it takes it back.
static bool _MouseCaptured;


/// <summary>
/// Updates and presents the frame when the application window needs repainting.
/// </summary>
void Game_Window_On_Paint(bool update_surface)
{
	if (update_surface) {
		if (MouseCursor != NULL && VisibleSurface != NULL && HiddenSurface != NULL && CompositeSurface != NULL) {
			if (ScenarioActive == true) {
				Map.Blit_Sidebar(true);
				Update_Visible_Surface(CompositeSurface);
			} else if (Movie_Is_Playing() == true) {
				Movie_Update_Visible_Surface();
			} else {
				Update_Visible_Surface(HiddenSurface);
			}
		}
	}
	Video_Present_If_Dirty();
}


/// <summary>
/// Stops tactical scrolling from coasting after the right mouse button is released.
/// </summary>
void Game_Window_On_Right_Mouse_Up(void)
{
	Map.Set_Scroll_Coasting_Allowed(false);
}


/// <summary>
/// Applies a mouse wheel step to the sidebar.
/// </summary>
void Game_Window_On_Mouse_Wheel(int delta)
{
	if (_HandlingMouseWheel) {
		return;
	}

	_HandlingMouseWheel = true;
	Execute_Command(delta < 0 ? "SidebarDown" : "SidebarUp");
	_HandlingMouseWheel = false;
}


// The tactical map sees a button first; the keyboard buffer gets it whatever the map did.
void Game_Window_Mouse_Button(unsigned short button, Point2D const & position, bool release)
{
	if (ToolTips != nullptr) {
		ToolTips->Pointer_Button();
	}

	Map.Pointer_Button(button, position, release);

	if (button == VK_RBUTTON && release) {
		Game_Window_On_Right_Mouse_Up();
	}

	if (Keyboard != nullptr) {
		Point2D point = position;
		Clamp_To_Game(point);
		Keyboard->Post_Mouse_Event(button, point.X, point.Y, release);
	}
}


// A double click reaches the keyboard buffer as a second press and release, and the
// tactical map not at all.
void Game_Window_Mouse_Double_Click(unsigned short button, Point2D const & position)
{
	if (ToolTips != nullptr) {
		ToolTips->Pointer_Button();
	}

	if (Keyboard != nullptr) {
		Point2D point = position;
		Clamp_To_Game(point);
		Keyboard->Post_Mouse_Event(button, point.X, point.Y, false);
		Keyboard->Post_Mouse_Event(button, point.X, point.Y, true);
	}
}


void Game_Window_Pointer_Capture_Lost(void)
{
	Map.Pointer_Capture_Lost();
}


/***********************************************************************************************
 * Focus_Loss -- this function is called when a library function detects focus loss            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    2/1/96 2:10PM ST : Created                                                               *
 *=============================================================================================*/

void Focus_Loss(void)
{
	DebugString("Focus_Loss()\n");
	Pause_Ingame_Movie(true);
	AudioEngine.Focus_Loss();
	if (MouseCursor) {
		_MouseCaptured = MouseCursor->Is_Captured();
		DebugString("Focus_Loss(): _MouseCaptured = %s\n", _MouseCaptured ? "true" : "false");
		MouseCursor->Release_Mouse();
	}
}


/// <summary>
/// Restores the game when it regains the input focus.
/// This routine is the counterpart to Focus_Loss. It resumes the sound where it paused,
/// recaptures the mouse if it was captured when focus was lost, and flags the whole
/// screen for redraw.
/// </summary>
void Focus_Restore(void)
{
	DebugString("Focus_Restore()\n");
	AudioEngine.Focus_Restore();
	DebugString("Focus_Restore(): _MouseCaptured = %s\n", _MouseCaptured ? "true" : "false");
	if (MouseCursor && _MouseCaptured == true && !Debug_Map) {
		MouseCursor->Capture_Mouse();
	}
	Map.Flag_To_Redraw(GS_REDRAW_ALL);
	Host_Invalidate_Window();
	Pause_Ingame_Movie(false);
}


// Called as the host's window comes into being, before anything is drawn to it.
void Game_Window_Created(void)
{
	ToolTips = new CCToolTip();
	ToolTips->Set_Timer_Delay(500);
}


// Called once the host's window has gone. A clean shutdown learns here that it may finish.
void Game_Window_Destroyed(void)
{
	delete ToolTips;
	ToolTips = nullptr;

	if (ReadyToQuit != 0) {
		ReadyToQuit = 2;
	}
}
