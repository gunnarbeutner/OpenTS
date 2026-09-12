/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The page the WebAssembly build runs inside, seen from the engine: the canvas
// is the drawing target, the page's callbacks fill the keyboard and mouse
// queues, and Browser_Yield hands the thread back. Only browser.cpp includes
// Emscripten's headers.

#pragma once

#if defined(__EMSCRIPTEN__)

#include "nativewindow.hh"
#include "rect.h"
#include "win.h"

class Mouse;
class Surface;


// How "?display=" in the query string sizes the frame: "native", the default,
// makes the window size the resolution; "scaled" keeps the configured
// resolution and scales it into the window; "1024x768" pins one and scales it.
enum BrowserDisplayPolicy {
	BROWSER_DISPLAY_NATIVE,
	BROWSER_DISPLAY_SCALED,
};

BrowserDisplayPolicy Browser_Display_Policy(void);

// The resolution "?display=WIDTHxHEIGHT" named, or zero.
int Browser_Display_Width(void);
int Browser_Display_Height(void);


char const * Browser_Canvas_Selector(void);

bool Browser_Init(void);

// The window the renderer presents into, as the host names it.
NativeWindow Browser_Native_Window(void);

// Delivers what the page changed since the last pass: canvas size, visibility,
// and queued input. Cheap enough to call from every wait, and the touch
// gestures need it reached regularly because a resting finger reports nothing.
void Browser_Service(void);

// Sees each event before the keyboard buffer does; a hook that returns true
// has delivered it. The position is in the game's frame and means nothing for
// a key.
typedef bool (*BrowserEventHook)(unsigned short key, int x, int y, bool is_mouse, bool is_release);
void Browser_Set_Event_Hook(BrowserEventHook hook);

// The drawing buffer is in device pixels, which the renderer draws into and a
// window position means; the laid out box is in CSS pixels, which the frame
// is sized in.
int Browser_Canvas_Width(void);
int Browser_Canvas_Height(void);
int Browser_Canvas_CSS_Width(void);
int Browser_Canvas_CSS_Height(void);

// The display the page is on, in CSS pixels, or zero when the page will not
// say; bounds what the display options offer.
int Browser_Screen_Width(void);
int Browser_Screen_Height(void);

// A hidden tab is neither composited nor given animation frames.
bool Browser_Is_Hidden(void);

// Hands the thread back to the page and returns when it schedules the engine
// again. Browser_Yield always waits; Browser_Yield_If_Due waits only once an
// animation frame's worth of time has passed.
void Browser_Yield(void);
bool Browser_Yield_If_Due(void);

// Counts the waits that only the yield scaffold carries, with or without the
// scaffold built in; docs/WASM-PORT.md section 1.4 asks that it be driven down.
unsigned int Browser_Blocking_Wait_Count(void);
bool Browser_Yield_Is_Available(void);

// Rises once per animation frame, so presentation happens at most once per
// frame.
unsigned int Browser_Frame_Serial(void);

// How many page events Browser_Service has not yet delivered.
int Browser_Pending_Events(void);

// The page reports modifiers with each event, so the platform layer keeps the
// key state Windows would keep.
unsigned short Browser_Key_Modifiers(void);
bool Browser_Key_Is_Down(unsigned short vk_key);

// The modifiers an event was made under, and the means to present them as
// the keys the engine reads while that event is dispatched.
unsigned short Browser_Event_Modifiers(void);
void Browser_Apply_Modifiers(unsigned short modifiers);

// The character the event being dispatched types, zero for none; and the character a key
// last typed under the shift state its code carries, which is what a later reader gets.
char32_t Browser_Event_Character(void);
char32_t Browser_Key_To_Character(unsigned short key);

// The position of the last event over the canvas, in the game's frame.
int Browser_Mouse_X(void);
int Browser_Mouse_Y(void);

// Is a pointer resting at that position? False after a finger, whose last
// position is a leftover rather than somewhere the player is pointing.
bool Browser_Mouse_Is_Hovering(void);

// Raises and dismisses a soft keyboard while the engine waits on typed text;
// what is typed reaches the ordinary key queue.
void Browser_Begin_Text_Input(void);
void Browser_End_Text_Input(void);

// A WWMouseClass that reads its position from the canvas.
Mouse * Browser_Create_Mouse(void);

// Asks the player a question with the buttons of a MessageBox type nibble, and answers
// the ID pressed; a negative answer is a box the host took away, and zero means the host
// cannot ask at all.
int Browser_Message_Box(char const * caption, char const * text, int buttons);

// Whether a paint was asked for since the last call, and the window is open to take it.
bool Host_Take_Paint(void);

// Shows a CSS cursor value over the canvas.
void Browser_Show_Cursor(char const * css);

#endif	// !_WIN32
