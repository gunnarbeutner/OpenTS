/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The engine's side of the presenter. The game draws its frame into the visible surface
// as it always has; this decides when that frame reaches the screen and where in the
// window it lands, and hands it to the renderer behind video.h.

#include "always.h"

#include "video.h"

#include "_map.h"
#include "_rect.h"
#include "_surface.h"
#include "bgfxbackend.h"
#include "browser.h"
#include "dbgprint.h"
#include "dsurface.h"
#include "globals.h"
#include "goptions.h"
#include "gscreen.h"
#include "init.h"
#include "mainopt.h"
#include "misc.h"
#include "movies.h"
#include "msengine.h"
#include "ownrdraw.h"
#include "surface.h"
#include "wincursor.h"
#include "windlg.h"

#include <cstdlib>


/*
 * The size of the frame the game renders into. It is not tied to the window, which may be
 * any size, nor to the desktop, whose mode the game no longer changes.
 */
int VideoModeWidth = 0;
int VideoModeHeight = 0;

/*
 * Is the game running in a framed, resizable window rather than in a borderless one
 * covering the whole screen? The display mode is never changed either way.
 */
bool WindowedMode = false;

static bool _Initialized = false;
static VideoScaleInfo _ScaleInfo;

// Set whenever the visible surface is written to, and cleared once that frame has been
// presented. A frame that is skipped for pacing stays marked, so the next present shows
// the newest content rather than a stale one.
static bool _FrameIsDirty = false;
static unsigned int _LastPresentTime = 0;
static unsigned int _PresentInterval = 16;

// Presents can nest, because a dialog repainting itself presents from inside the paint
// that the engine's own present provoked.
static bool _Presenting = false;

#if defined(OPENTS_WIN32_SUBSTITUTE)
// The animation frame of the last present, which paces presentation in place of
// a refresh rate.
static unsigned int _LastPresentSerial = ~0u;

// The frame size the canvas asked for, and when. A drag asks once per animation
// frame and each mode change rebuilds every drawing surface, so the request
// must settle first.
static int _RequestedWidth = 0;
static int _RequestedHeight = 0;
static unsigned int _RequestedAt = 0;
static bool _ChangingMode = false;

// True while the frame follows the window's size rather than keeping a fixed
// resolution the presenter scales.
static bool _FollowWindow = false;

// Milliseconds the canvas must hold still before the frame follows it.
static const unsigned int RESIZE_SETTLE_TIME = 250;

// Below this size the sidebar and the tab bar do not fit.
static const int FRAME_MIN_WIDTH = 640;
static const int FRAME_MIN_HEIGHT = 400;
#endif


/// <summary>
/// Works out the shortest sensible gap between presents from the display's refresh rate.
/// </summary>
static void Update_Present_Interval(int refreshrate)
{
	if (refreshrate <= 1) {
		refreshrate = 60;
	}

	_PresentInterval = (unsigned int)(1000 / refreshrate);
	if (_PresentInterval < 3) {
		_PresentInterval = 3;
	}
	if (_PresentInterval > 100) {
		_PresentInterval = 100;
	}
}


/// <summary>
/// Works out where the game's frame sits inside the window.
/// The frame keeps its shape, so it is grown by whichever of the two axes runs out first
/// and centered in what is left over.
/// </summary>
static void Update_Scale_Info(void)
{
	_ScaleInfo.GameWidth = VideoModeWidth;
	_ScaleInfo.GameHeight = VideoModeHeight;

	if (_ScaleInfo.GameWidth <= 0 || _ScaleInfo.GameHeight <= 0 || _ScaleInfo.DrawableWidth <= 0 || _ScaleInfo.DrawableHeight <= 0) {
		_ScaleInfo.DestX = 0;
		_ScaleInfo.DestY = 0;
		_ScaleInfo.DestWidth = _ScaleInfo.DrawableWidth;
		_ScaleInfo.DestHeight = _ScaleInfo.DrawableHeight;
		_ScaleInfo.ScaleX = 1.0f;
		_ScaleInfo.ScaleY = 1.0f;
		return;
	}

	double scalex = (double)_ScaleInfo.DrawableWidth / (double)_ScaleInfo.GameWidth;
	double scaley = (double)_ScaleInfo.DrawableHeight / (double)_ScaleInfo.GameHeight;
	double scale = (scalex < scaley) ? scalex : scaley;

	if (Options.IntegerScaling && scale >= 1.0) {
		scale = (double)(int)scale;
	}

	_ScaleInfo.DestWidth = (int)((double)_ScaleInfo.GameWidth * scale);
	_ScaleInfo.DestHeight = (int)((double)_ScaleInfo.GameHeight * scale);
	_ScaleInfo.DestX = (_ScaleInfo.DrawableWidth - _ScaleInfo.DestWidth) / 2;
	_ScaleInfo.DestY = (_ScaleInfo.DrawableHeight - _ScaleInfo.DestHeight) / 2;
	_ScaleInfo.ScaleX = (float)((double)_ScaleInfo.DestWidth / (double)_ScaleInfo.GameWidth);
	_ScaleInfo.ScaleY = (float)((double)_ScaleInfo.DestHeight / (double)_ScaleInfo.GameHeight);
}


#if defined(OPENTS_WIN32_SUBSTITUTE)

/// <summary>
/// Clamps a frame size into the renderable bounds and to a multiple of four.
/// </summary>
void Video_Clamp_Frame_Size(int & width, int & height)
{
	if (width > VIDEO_FOLLOW_MAX_WIDTH || height > VIDEO_FOLLOW_MAX_HEIGHT) {
		double const scalex = (double)VIDEO_FOLLOW_MAX_WIDTH / (double)width;
		double const scaley = (double)VIDEO_FOLLOW_MAX_HEIGHT / (double)height;
		double const scale = (scalex < scaley) ? scalex : scaley;

		width = (int)((double)width * scale);
		height = (int)((double)height * scale);
	}

	// Both minimums scale the frame together so that its shape survives and the
	// presenter does not letterbox it.
	if (width < FRAME_MIN_WIDTH || height < FRAME_MIN_HEIGHT) {
		double const scalex = (double)FRAME_MIN_WIDTH / (double)width;
		double const scaley = (double)FRAME_MIN_HEIGHT / (double)height;
		double const scale = (scalex > scaley) ? scalex : scaley;

		width = (int)((double)width * scale + 0.5);
		height = (int)((double)height * scale + 0.5);
	}

	width &= ~3;
	height &= ~3;

	if (width < FRAME_MIN_WIDTH) width = FRAME_MIN_WIDTH;
	if (height < FRAME_MIN_HEIGHT) height = FRAME_MIN_HEIGHT;
}


static bool Mode_Change_Is_Safe(void)
{
	if (!_Initialized || _Presenting || _ChangingMode) {
		return(false);
	}

	if (VisibleSurface == NULL || HiddenSurface == NULL) {
		return(false);
	}

	if (ScenarioInit != 0) {
		return(false);
	}

	// A movie keeps the surface it was created on until it is torn down.
	if (Movie_Holds_A_Surface()) {
		return(false);
	}

	// A shell screen lays its artwork out against the size it came up at and
	// never redraws it.
	if (MSEngine::Is_Screen_Up()) {
		return(false);
	}

	// Dialogs paint into the game's own surfaces, so a paint in progress must
	// finish first.
	if (OwnerDraw::Is_Painting()) {
		return(false);
	}

	return(true);
}


// Repaints the frame and lays the dialogs out again from the frame size they
// were placed against.
static void Rebuild_Screen_Under_Dialogs(int oldwidth, int oldheight)
{
	OwnerDraw::Relayout_Dialogs(oldwidth, oldheight);

	if (ScenarioActive) {
		Map.Flag_To_Redraw(GS_REDRAW_ALL);
		Map.Render();
	} else {
		Title_Screen_Restore(true);
	}

	Heal_Dialog_Controls();
}


/// <summary>
/// Records the canvas size, in CSS pixels, as the frame size to render at.
/// </summary>
void Video_Request_Frame_Size(int width, int height)
{
	if (!_FollowWindow || width <= 0 || height <= 0) {
		return;
	}

	Video_Clamp_Frame_Size(width, height);

	if (width == VideoModeWidth && height == VideoModeHeight) {
		_RequestedWidth = 0;
		_RequestedHeight = 0;
		return;
	}

	_RequestedWidth = width;
	_RequestedHeight = height;

	// Restarted on every report so that a drag is one mode change at the end.
	_RequestedAt = timeGetTime();
}


/// <summary>
/// Resizes the frame to the canvas if one has been asked for and the engine can
/// take it.
/// </summary>
void Video_Service_Display(void)
{
	// A scenario sizes its surfaces from the options, so while the frame
	// follows the window the options must say what it is.
	if (_FollowWindow && VideoModeWidth > 0 && VideoModeHeight > 0) {
		Options.ScreenWidth = VideoModeWidth;
		Options.ScreenHeight = VideoModeHeight;
	}

	if (_RequestedWidth <= 0 || _RequestedHeight <= 0) {
		return;
	}

	if (_RequestedWidth == VideoModeWidth && _RequestedHeight == VideoModeHeight) {
		_RequestedWidth = 0;
		_RequestedHeight = 0;
		return;
	}

	if ((timeGetTime() - _RequestedAt) < RESIZE_SETTLE_TIME) {
		return;
	}

	if (!Mode_Change_Is_Safe()) {
		return;
	}

	int width = _RequestedWidth;
	int height = _RequestedHeight;

	_RequestedWidth = 0;
	_RequestedHeight = 0;

	int oldwidth = Options.ScreenWidth;
	int oldheight = Options.ScreenHeight;

	int framewidth = VideoModeWidth;
	int frameheight = VideoModeHeight;
	bool underdialog = (WS_Top_Window() != NULL);

	Options.ScreenWidth = width;
	Options.ScreenHeight = height;

	_ChangingMode = true;
	bool changed = Change_Display_Mode(width, height);
	_ChangingMode = false;

	if (!changed) {
		Options.ScreenWidth = oldwidth;
		Options.ScreenHeight = oldheight;
		return;
	}

	if (underdialog) {
		Rebuild_Screen_Under_Dialogs(framewidth, frameheight);
	}
}

#endif	// OPENTS_WIN32_SUBSTITUTE


/// <summary>
/// Is a settled frame size waiting to be taken? This answers for the request
/// alone, so a screen that itself blocks the resize still gets true.
/// </summary>
bool Video_Frame_Size_Is_Pending(void)
{
#if defined(OPENTS_WIN32_SUBSTITUTE)
	if (_RequestedWidth <= 0 || _RequestedHeight <= 0) {
		return(false);
	}

	if (_RequestedWidth == VideoModeWidth && _RequestedHeight == VideoModeHeight) {
		return(false);
	}

	return((timeGetTime() - _RequestedAt) >= RESIZE_SETTLE_TIME);
#else
	return(false);
#endif
}


/// <summary>
/// Converts the configured filter into the one the renderer names.
/// </summary>
static BackendScaleMode Backend_Scale_Mode(void)
{
	switch (Options.ScaleMode) {
		case VIDEO_SCALE_LINEAR:
			return(BACKEND_SCALE_LINEAR);

		case VIDEO_SCALE_NEAREST:
			return(BACKEND_SCALE_NEAREST);

		default:
			return(BACKEND_SCALE_PIXELART);
	}
}


/// <summary>
/// Starts the presenter on the game's window.
/// </summary>
/// <param name="window">The native window whose drawable area receives the frame.</param>
/// <param name="drawablewidth">The drawable area's width in physical pixels.</param>
/// <param name="drawableheight">The drawable area's height in physical pixels.</param>
/// <param name="refreshrate">The display refresh rate in hertz, or zero when unknown.</param>
/// <returns>bool; Did the presenter start? A false return is fatal to the game.</returns>
bool Video_Init(NativeWindow const & window, int drawablewidth, int drawableheight, int refreshrate)
{
	if (_Initialized) {
		return(true);
	}

	if (window.Handle == nullptr || drawablewidth <= 0 || drawableheight <= 0) {
		return(false);
	}

	_ScaleInfo.DrawableWidth = drawablewidth;
	_ScaleInfo.DrawableHeight = drawableheight;

#if defined(OPENTS_WIN32_SUBSTITUTE)
	// On a page the window is the one size the player chose, so it overrides the configured
	// resolution before the first frame is sized from it.
	{
		int startwidth = 0;
		int startheight = 0;

		if (Browser_Display_Width() > 0 && Browser_Display_Height() > 0) {
			startwidth = Browser_Display_Width();
			startheight = Browser_Display_Height();
		} else if (Browser_Display_Policy() == BROWSER_DISPLAY_NATIVE) {
			startwidth = Browser_Canvas_CSS_Width();
			startheight = Browser_Canvas_CSS_Height();
			_FollowWindow = true;
		}

		if (startwidth > 0 && startheight > 0) {
			Video_Clamp_Frame_Size(startwidth, startheight);
			VideoModeWidth = startwidth;
			VideoModeHeight = startheight;
			Options.ScreenWidth = startwidth;
			Options.ScreenHeight = startheight;
			VisibleRect = Rect(0, 0, startwidth, startheight);
		}
	}
#endif

	BackendRenderer renderer = (BackendRenderer)Options.Renderer;
	if (!Backend_Init(window, drawablewidth, drawableheight, renderer, Options.VSync)) {
		return(false);
	}

	DebugString("Video: renderer is %s\n", Backend_Renderer_Name());

	_Initialized = true;

	if (!Backend_Set_Frame_Size(VideoModeWidth, VideoModeHeight)) {
		Backend_Shutdown();
		_Initialized = false;
		return(false);
	}

	Update_Scale_Info();
	Update_Present_Interval(refreshrate);
	return(true);
}


/// <summary>
/// Stops the presenter and releases the renderer.
/// </summary>
void Video_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	Win_Cursor_Shutdown();
	Backend_Shutdown();
	_Initialized = false;
	_FrameIsDirty = false;
}


/// <summary>
/// Moves the game to a different render resolution.
/// The caller replaces the surfaces afterwards; this only resizes what the frame is
/// presented from and leaves the previous mode untouched when it fails.
/// </summary>
/// <param name="width">The new frame width.</param>
/// <param name="height">The new frame height.</param>
/// <returns>bool; Was the mode changed?</returns>
bool Video_Set_Mode(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return(false);
	}

	if (!Backend_Set_Frame_Size(width, height)) {
		return(false);
	}

#if defined(OPENTS_WIN32_SUBSTITUTE)
	// A mode set from outside the resize path decides whether the frame keeps
	// following the window: the canvas's own size means yes, and any other size
	// is kept.
	if (!_ChangingMode) {
		int canvaswidth = Browser_Canvas_CSS_Width();
		int canvasheight = Browser_Canvas_CSS_Height();

		Video_Clamp_Frame_Size(canvaswidth, canvasheight);
		_FollowWindow = (width == canvaswidth && height == canvasheight);

		_RequestedWidth = 0;
		_RequestedHeight = 0;
	}
#endif

	VideoModeWidth = width;
	VideoModeHeight = height;

	Update_Scale_Info();
	Win_Cursor_Refresh();
	_FrameIsDirty = true;
	return(true);
}


/// <summary>
/// Tells the presenter the drawable area changed size.
/// </summary>
void Video_On_Resize(int drawablewidth, int drawableheight)
{
	if (!_Initialized || drawablewidth <= 0 || drawableheight <= 0) {
		return;
	}

	_ScaleInfo.DrawableWidth = drawablewidth;
	_ScaleInfo.DrawableHeight = drawableheight;
	Backend_On_Resize(drawablewidth, drawableheight);
	Update_Scale_Info();
	Win_Cursor_Refresh();
	Video_Mark_Dirty();
}


/// <summary>
/// Sets the refresh rate used to pace presentation.
/// </summary>
void Video_Set_Refresh_Rate(int refreshrate)
{
	if (!_Initialized) {
		return;
	}

	Update_Present_Interval(refreshrate);
	Video_Mark_Dirty();
}


/// <summary>
/// Records that the visible surface has been drawn to since the last present.
/// </summary>
void Video_Mark_Dirty(void)
{
	_FrameIsDirty = true;
}


/// <summary>
/// Puts the visible surface on the screen whatever its state.
/// </summary>
void Video_Present(void)
{
	if (!_Initialized || _Presenting || VisibleSurface == NULL) {
		return;
	}

	DSurface * surface = (DSurface *)VisibleSurface;
	void * pixels = surface->Get_Buffer();

	if (pixels == NULL) {
		return;
	}

	_Presenting = true;
	Backend_Present(pixels, surface->Stride(), _ScaleInfo.DestX, _ScaleInfo.DestY, _ScaleInfo.DestWidth, _ScaleInfo.DestHeight, Backend_Scale_Mode());
	_Presenting = false;

	_FrameIsDirty = false;
	_LastPresentTime = timeGetTime();
}


/// <summary>
/// Puts the visible surface on the screen if it has changed and the display is ready for
/// another frame.
/// A skipped present leaves the frame marked, so the next one shows the newest content.
/// This never waits: the game loop is not paced by presentation.
/// </summary>
void Video_Present_If_Dirty(void)
{
	if (!_FrameIsDirty) {
		return;
	}

#if defined(OPENTS_WIN32_SUBSTITUTE)
	// One present per animation frame: the engine reaches here far more often
	// than the page composites.
	if (Browser_Frame_Serial() == _LastPresentSerial) {
		return;
	}
	_LastPresentSerial = Browser_Frame_Serial();
#else

	unsigned int now = timeGetTime();
	if ((now - _LastPresentTime) < _PresentInterval) {
		return;
	}
#endif

	Video_Present();
}


/// <summary>
/// Queues a true color movie frame for the next present at a rect given in
/// window pixels.
/// </summary>
void Video_Queue_Movie_Frame(void const * pixels, int pitch, int width, int height,
	int dest_x, int dest_y, int dest_width, int dest_height)
{
	Backend_Queue_Video_Frame(pixels, pitch, width, height, dest_x, dest_y, dest_width, dest_height);
	Video_Mark_Dirty();
	Video_Present_If_Dirty();
}


void Video_Clear_Movie_Frame(void)
{
	Backend_Clear_Video_Frame();
}


/// <summary>
/// Reports where the game's frame is drawn inside the window.
/// </summary>
VideoScaleInfo const & Video_Get_Scale_Info(void)
{
	return(_ScaleInfo);
}


/// <summary>
/// Compares two display modes by width and then height.
/// </summary>
static int __cdecl Compare_Modes(void const * left, void const * right)
{
	int const * lhs = (int const *)left;
	int const * rhs = (int const *)right;

	if (lhs[0] != rhs[0]) {
		return(lhs[0] - rhs[0]);
	}
	return(lhs[1] - rhs[1]);
}


/// <summary>
/// Collects the display resolutions that fall within the given bounds.
/// Only the sizes matter; the desktop decides the color depth, and duplicates that differ
/// only by refresh rate are reported once.
/// </summary>
/// <param name="minwidth">The narrowest mode to report.</param>
/// <param name="minheight">The shortest mode to report.</param>
/// <param name="maxwidth">The widest mode to report.</param>
/// <param name="maxheight">The tallest mode to report.</param>
/// <returns>A caller owned array of width and height pairs ending in a zero pair, or NULL
/// when nothing matched.</returns>
int * EnumDisplayModes(int minwidth, int minheight, int maxwidth, int maxheight)
{
	DEVMODE devmode;
	int count = 0;
	int capacity = 0;
	int * modes = NULL;

	for (int pass = 0; pass < 2; pass++) {

		count = 0;

		for (int index = 0; ; index++) {
			memset(&devmode, 0, sizeof(devmode));
			devmode.dmSize = sizeof(devmode);

			if (!EnumDisplaySettings(NULL, index, &devmode)) {
				break;
			}

			int width = (int)devmode.dmPelsWidth;
			int height = (int)devmode.dmPelsHeight;

			if (width < minwidth || width > maxwidth || height < minheight || height > maxheight) {
				continue;
			}

			if (modes != NULL) {
				// The list is being filled from a second enumeration; should it have
				// grown since the one that sized the array, the extra modes are dropped.
				if (count >= capacity) {
					break;
				}
				modes[count * 2] = width;
				modes[count * 2 + 1] = height;
			}
			count++;
		}

		if (modes != NULL) {
			break;
		}

		if (count == 0) {
			return(NULL);
		}

		capacity = count;
		modes = new int[(count + 1) * 2];
	}

	qsort(modes, count, sizeof(int) * 2, Compare_Modes);

	// The same size is listed once per refresh rate and color depth it supports.
	int unique = 0;
	for (int index = 0; index < count; index++) {
		if (unique == 0 || modes[unique * 2 - 2] != modes[index * 2] || modes[unique * 2 - 1] != modes[index * 2 + 1]) {
			modes[unique * 2] = modes[index * 2];
			modes[unique * 2 + 1] = modes[index * 2 + 1];
			unique++;
		}
	}

	modes[unique * 2] = 0;
	modes[unique * 2 + 1] = 0;
	return(modes);
}
