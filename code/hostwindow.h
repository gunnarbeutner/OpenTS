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

#include <cstdint>

struct NativeWindow;

// Positions are in the game window's client pixels, before the frame's scaling.

struct HostCursor;


Point2D Host_Pointer_Position(void);
void Host_Move_Pointer(Point2D const & position);

// Raises or lowers the pointer's display count, as ShowCursor does; the pointer shows while
// the count is not negative. Returns the count after the change.
int Host_Show_Pointer(bool show);

// Keeps the pointer inside the window's client area, or lets it go again.
void Host_Confine_Pointer(bool confine);

// Delivers every pointer event to the game window, even off it, until released.
void Host_Capture_Pointer(void);
void Host_Release_Pointer(void);
bool Host_Pointer_Is_Captured(void);

// How far the pointer travels, in client pixels, before a press becomes a drag.
Point2D Host_Drag_Threshold(void);

// Builds a pointer image from 0xAARRGGBB pixels in rows from the top; null if the host
// cannot. The host keeps it until Host_Destroy_Cursor.
HostCursor * Host_Create_Cursor(std::uint32_t const * pixels, int width, int height, int hotx, int hoty);
void Host_Destroy_Cursor(HostCursor * cursor);

// Shows the image over the window; null gives the pointer back to the host.
void Host_Set_Cursor(HostCursor * cursor);
void Host_Hide_Cursor(void);

// The largest image, in pixels a side, that the host still draws.
int Host_Max_Cursor_Size(void);

// Whether the host has a pointer of its own to show while the game does not hold the mouse.
// Without one, the game's image stands in for it.
bool Host_Has_Class_Cursor(void);

// The modifier keys held now, as the WWKEY_ bits a key code carries.
unsigned short Host_Key_Modifiers(void);

// Whether a key or mouse button, named by its VK_ code, is held now.
bool Host_Key_Is_Down(unsigned short key);

// The character a key code, with its modifier bits, types in the player's layout; zero when it
// types none.
int Host_Key_To_Character(unsigned short key);

// Opens the game window, sized for the frame when it is a window rather than the whole
// screen; Has_Main_Window turns true. Host_Close_Window ends it and returns once it has gone.
void Host_Create_Window(int width, int height);
void Host_Close_Window(void);

// What the renderer draws into, and its size in pixels; false while it has no size.
NativeWindow Host_Native_Window(void);
bool Host_Window_Drawable_Size(int & width, int & height);

// The display's refresh rate in hertz, or zero when the host cannot tell.
int Host_Window_Refresh_Rate(void);

// Asks for the window to be painted again from the game's surfaces.
void Host_Invalidate_Window(void);

void Host_Focus_Window(void);

// Resizes a window that tracks the frame to a frame of this size, about its middle.
void Host_Fit_Window_To_Frame(int width, int height);

// The display's modes by index from zero, repeats included; false past the last.
bool Host_Display_Mode(int index, int & width, int & height);

// The buttons, and the icon beside the text, of a message box; the values are Windows's.
enum HostMessageBoxStyle : unsigned int {
	HOST_BOX_OK = 0x00,
	HOST_BOX_YES_NO = 0x04,
	HOST_BOX_ERROR = 0x10,
	HOST_BOX_QUESTION = 0x20,
	HOST_BOX_WARNING = 0x30,
};

enum HostMessageBoxAnswer {
	HOST_ANSWER_OK = 1,
	HOST_ANSWER_CANCEL = 2,
	HOST_ANSWER_YES = 6,
	HOST_ANSWER_NO = 7,
};

// Asks the player, and waits for the answer. A host that cannot ask answers as a dismissed
// box does: OK for a plain box, no for a yes-or-no question.
HostMessageBoxAnswer Host_Message_Box(char const * caption, char const * text, unsigned int style);
