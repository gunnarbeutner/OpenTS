/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "point.h"
#include "rect.h"

class Surface;

// Beyond this the sidebar takes more of the screen than the world.
int const UI_SCALE_MAX = 4;

/// <summary>
/// How a screen of a given size is divided between the world and the
/// interface; every surface a scenario is drawn into is sized from it.
/// </summary>
struct ScreenLayout
{
	// The world viewport, in screen pixels.
	Rect Tactical;

	// The shell surface, which is the whole screen.
	Rect Hidden;

	// The in-game frame and the cached terrain layer beneath it. A pan swaps
	// the two, so they are the same size and both keep the full screen height.
	Rect Composite;
	Rect Tile;

	// The interface column in the sidebar surface's own coordinates, not
	// screen pixels; Blit_Sidebar magnifies it by the interface scale.
	Rect Sidebar;
};

ScreenLayout Compute_Screen_Layout(Rect const & visible);

/// <summary>
/// Returns the interface magnification, between one and UI_SCALE_MAX. A
/// configured scale of zero follows the frame, and one the frame cannot carry
/// is stepped back down.
/// </summary>
int UI_Scale(int framewidth, int frameheight);
int UI_Scale(void);

/// <summary>
/// Converts a sidebar surface rectangle into the screen rectangle it is
/// magnified into.
/// </summary>
Rect Sidebar_To_Screen(Rect const & rect);

/// <summary>
/// Converts a screen point into the sidebar surface pixel drawn beneath it.
/// </summary>
Point2D Screen_To_Sidebar(Point2D const & point);


/// <summary>
/// Declares the artwork size the shell screen coming up lays itself out at;
/// an empty size leaves the frame unmagnified.
/// </summary>
void Set_Shell_Size(Point2D const & size);

/// <summary>
/// Magnifies what a shell screen drew in the design space out to fill the
/// surfaces holding it and releases the design space. A screen that stays on
/// the display after it stops drawing calls this so that dialogs and repaints
/// working in frame pixels find the magnified picture.
/// </summary>
void Fill_Out_Shell(void);

/// <summary>
/// Returns the design rectangle centered in the surfaces the shell draws
/// into, or the whole surface when no screen has claimed a design space.
/// </summary>
Rect Shell_Rect(void);

/// <summary>
/// Converts a shell design rectangle into the screen rectangle it is
/// magnified into.
/// </summary>
Rect Shell_To_Screen(Rect const & rect);

/// <summary>
/// Converts a screen point into the shell design pixel drawn beneath it.
/// </summary>
Point2D Screen_To_Shell(Point2D const & point);

/// <summary>
/// Copies a region of a shell surface, given in design coordinates, onto the
/// visible screen, magnified.
/// </summary>
void Blit_Shell(Surface & surface, Rect const & rect);

/// <summary>
/// Returns the largest rectangle of the picture's shape that the frame holds,
/// centered in it, or the frame itself if the size is empty.
/// </summary>
Rect Fit_Centered(Point2D const & size, Rect const & frame);

/// <summary>
/// Carries a point placed against a picture centered at its own size over to
/// where it lands once the picture is filled out to the frame.
/// </summary>
Point2D Fit_Point(Point2D const & point, Point2D const & size, Rect const & frame);
