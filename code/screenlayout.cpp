/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "screenlayout.h"

#include "surface.hh"

#include "_rect.h"
#include "_surface.h"
#include "bsurface.h"
#include "globals.h"
#include "goptions.h"
#include "sidebar.h"
#include "surface.h"
#include "xsurface.h"

#include "color.hh"

#include <algorithm>
#include <cstring>


// The room the tactical view gives up to the tab strip. The strip is not
// magnified with the interface because it is drawn into the composite, sidebar
// and tile surfaces alike and only the sidebar is scaled.
static int const TAB_HEIGHT = 16;


// The frame height one step of magnification is worth.
static int const UI_SCALE_STEP = 540;


// The shortest sidebar surface the radar pane and build strips fit in.
static int const UI_SCALE_MIN_HEIGHT = 400;


// Shell pages are pre-rendered artwork with clean edges, which the sharp
// filter magnifies without reading as blocks or blur.
static SurfaceFilterType const SHELL_FILTER = SURFACE_FILTER_SHARP;


ScreenLayout Compute_Screen_Layout(Rect const & visible)
{
	ScreenLayout layout;

	int const scale = UI_Scale(visible.Width, visible.Height);
	int const sidewidth = SidebarClass::SIDE_WIDTH * scale;

	layout.Tactical = visible;
	layout.Tactical.X = ((Options.IsSidebarOnRight || Debug_Map) ? 0 : sidewidth);
	layout.Tactical.Y = TAB_HEIGHT;
	layout.Tactical.Width -= sidewidth;
	layout.Tactical.Height -= TAB_HEIGHT;

	layout.Hidden = visible;
	layout.Composite = Rect(0, 0, layout.Tactical.Width, visible.Height);
	layout.Tile = layout.Composite;
	layout.Sidebar = Rect(0, 0, SidebarClass::SIDE_WIDTH, visible.Height / scale);

	return(layout);
}


int UI_Scale(int framewidth, int frameheight)
{
	int scale = Options.UIScale;

	if (scale <= 0) {
		scale = frameheight / UI_SCALE_STEP;
	}

	scale = std::clamp(scale, 1, UI_SCALE_MAX);

	// The sidebar must leave the world at least half the width and keep the
	// height its own artwork needs.
	while (scale > 1
			&& (frameheight / scale < UI_SCALE_MIN_HEIGHT
				|| SidebarClass::SIDE_WIDTH * scale * 2 > framewidth)) {
		scale--;
	}

	return(scale);
}


int UI_Scale(void)
{
	return(UI_Scale(VisibleRect.Width, VisibleRect.Height));
}


Rect Sidebar_To_Screen(Rect const & rect)
{
	int const scale = UI_Scale();

	return(Rect(SidebarRect.X + rect.X * scale, rect.Y * scale, rect.Width * scale, rect.Height * scale));
}


Point2D Screen_To_Sidebar(Point2D const & point)
{
	int const scale = UI_Scale();

	return(Point2D((point.X - SidebarRect.X) / scale, point.Y / scale));
}


// A size rather than a rectangle, because a mode change moves the design
// space without resizing the artwork.
static Point2D _ShellSize(0, 0);


static void Discard_Shell_Page(void);


void Set_Shell_Size(Point2D const & size)
{
	_ShellSize = size;
	Discard_Shell_Page();
}


Rect Fit_Centered(Point2D const & size, Rect const & frame)
{
	if (size.X <= 0 || size.Y <= 0) {
		return(frame);
	}

	int width = frame.Width;
	int height = size.Y * width / size.X;

	if (height > frame.Height) {
		height = frame.Height;
		width = size.X * height / size.Y;
	}

	return(Rect(frame.X + (frame.Width - width) / 2, frame.Y + (frame.Height - height) / 2, width, height));
}


// Artwork larger than the surface keeps the surface's size.
static Rect Shell_Rect_In(Rect const & surface)
{
	if (_ShellSize.X <= 0 || _ShellSize.Y <= 0) {
		return(surface);
	}

	int const width = std::min(_ShellSize.X, surface.Width);
	int const height = std::min(_ShellSize.Y, surface.Height);

	return(Rect((surface.Width - width) / 2, (surface.Height - height) / 2, width, height));
}


// The design rectangle is placed against the surface the shell draws into,
// which is not always the size of the screen; a mode change rebuilds it, so it
// is looked up rather than kept.
Rect Shell_Rect(void)
{
	return(Shell_Rect_In((HiddenSurface != NULL) ? HiddenSurface->Get_Rect() : VisibleRect));
}


// A screen that has claimed no design space gets the design rectangle back
// unchanged, so every mapping is the identity and every copy is plain.
static Rect Shell_Screen_Rect(void)
{
	Rect const design = Shell_Rect();

	if (_ShellSize.X <= 0 || _ShellSize.Y <= 0) {
		return(design);
	}

	return(Fit_Centered(Point2D(design.Width, design.Height), VisibleRect));
}


// Both edges are mapped rather than the corner and the size, so that
// neighboring rectangles meet exactly and a rectangle within the design space
// maps to one within the screen rectangle, which Blit_Shell relies on.
Rect Shell_To_Screen(Rect const & rect)
{
	Rect const design = Shell_Rect();
	Rect const screen = Shell_Screen_Rect();

	int const left = screen.X + (rect.X - design.X) * screen.Width / design.Width;
	int const right = screen.X + (rect.X + rect.Width - design.X) * screen.Width / design.Width;
	int const top = screen.Y + (rect.Y - design.Y) * screen.Height / design.Height;
	int const bottom = screen.Y + (rect.Y + rect.Height - design.Y) * screen.Height / design.Height;

	return(Rect(left, top, right - left, bottom - top));
}


// A point in the black beside the picture lands outside the design rectangle.
Point2D Screen_To_Shell(Point2D const & point)
{
	Rect const design = Shell_Rect();
	Rect const screen = Shell_Screen_Rect();

	return(Point2D(
		design.X + (point.X - screen.X) * design.Width / screen.Width,
		design.Y + (point.Y - screen.Y) * design.Height / screen.Height
	));
}


static void Fill_Shell_Surround(void)
{
	Rect const screen = Shell_Screen_Rect();

	Rect const bands[] = {
		Rect(0, 0, VisibleRect.Width, screen.Y),
		Rect(0, screen.Y + screen.Height, VisibleRect.Width, VisibleRect.Height - (screen.Y + screen.Height)),
		Rect(0, screen.Y, screen.X, screen.Height),
		Rect(screen.X + screen.Width, screen.Y, VisibleRect.Width - (screen.X + screen.Width), screen.Height)
	};

	for (Rect const & band : bands) {
		if (band.Is_Valid()) {
			VisibleSurface->Fill_Rect(VisibleSurface->Get_Rect(), band, TBLACK);
		}
	}
}


// The magnified page and the design space picture it was resampled from,
// always built together, with the geometry they were built for kept beside
// them because a mode change moves both rectangles.
static BSurface * _ShellPage = NULL;
static BSurface * _ShellDrawn = NULL;
static Rect _ShellPageDesign(0, 0, 0, 0);
static Rect _ShellPageScreen(0, 0, 0, 0);


static void Discard_Shell_Page(void)
{
	delete _ShellPage;
	_ShellPage = NULL;

	delete _ShellDrawn;
	_ShellDrawn = NULL;

	_ShellPageDesign = Rect(0, 0, 0, 0);
	_ShellPageScreen = Rect(0, 0, 0, 0);
}


// Returns the bounding rectangle, in design coordinates, of what the surface
// holds that the page was not built from, and brings the copy up to date.
static Rect Refresh_Shell_Drawn(Surface const & surface, Rect const & design)
{
	unsigned short const * from = (unsigned short const *)surface.Lock(design.Top_Left());
	unsigned short * to = (unsigned short *)_ShellDrawn->Lock();

	if (from == NULL || to == NULL) {
		if (from != NULL) surface.Unlock();
		if (to != NULL) _ShellDrawn->Unlock();
		return(design);
	}

	int top = 0;
	int bottom = -1;
	int left = design.Width;
	int right = -1;

	size_t const span = (size_t)design.Width * sizeof(unsigned short);

	for (int y = 0; y < design.Height; y++) {

		if (std::memcmp(from, to, span) != 0) {
			int first = 0;
			while (from[first] == to[first]) {
				first++;
			}
			int last = design.Width - 1;
			while (from[last] == to[last]) {
				last--;
			}

			left = std::min(left, first);
			right = std::max(right, last);
			if (bottom < 0) {
				top = y;
			}
			bottom = y;

			std::memcpy(to, from, span);
		}

		from = (unsigned short const *)(((char const *)from) + surface.Stride());
		to = (unsigned short *)(((char *)to) + _ShellDrawn->Stride());
	}

	surface.Unlock();
	_ShellDrawn->Unlock();

	if (bottom < 0) {
		return(Rect(0, 0, 0, 0));
	}

	return(Rect(design.X + left, design.Y + top, right - left + 1, bottom - top + 1));
}


// Returns whether the page is there to copy from. A page pixel reads at most
// RESAMPLE_MAX_TAPS design pixels around its position, so a change grown by
// that much covers every page pixel it reached, and two more cover the
// rounding of the mapping.
static bool Update_Shell_Page(Surface const & surface, Rect const & design, Rect const & screen)
{
	if (surface.Bytes_Per_Pixel() != 2) {
		Discard_Shell_Page();
		return(false);
	}

	Rect const page(0, 0, screen.Width, screen.Height);

	if (_ShellPage == NULL || _ShellPageDesign != design || _ShellPageScreen != screen) {

		Discard_Shell_Page();

		_ShellPage = new BSurface(screen.Width, screen.Height, 2);
		_ShellDrawn = new BSurface(design.Width, design.Height, 2);

		if (!_ShellDrawn->Blit_From(_ShellDrawn->Get_Rect(), surface, design)
				|| !_ShellPage->Blit_Scaled_Region(page, surface, design, page, SHELL_FILTER)) {
			Discard_Shell_Page();
			return(false);
		}

		_ShellPageDesign = design;
		_ShellPageScreen = screen;

		return(true);
	}

	Rect const changed = Refresh_Shell_Drawn(surface, design);

	if (changed.Is_Valid()) {
		int const reach = XSurface::RESAMPLE_MAX_TAPS;
		Rect const grown = Intersect(design, Rect(changed.X - reach, changed.Y - reach,
									changed.Width + reach * 2, changed.Height + reach * 2));
		Rect const mapped = Shell_To_Screen(grown) - screen.Top_Left();
		Rect const window = Intersect(page, Rect(mapped.X - 2, mapped.Y - 2, mapped.Width + 4, mapped.Height + 4));

		_ShellPage->Blit_Scaled_Region(page, surface, design, window, SHELL_FILTER);
	}

	return(true);
}


// The region is trimmed to the design space before it is mapped, because
// Blit_Clip trims a scaled blit's two rectangles independently and would
// change the magnification. The scaled blit resamples the region from the
// whole design space so that piecewise repaints carry no outlines.
void Blit_Shell(Surface & surface, Rect const & rect)
{
	Rect const design = Shell_Rect();
	Rect const source = Intersect(design, rect);

	if (_ShellSize.X > 0 && _ShellSize.Y > 0 && rect.Is_Valid() && Intersect(rect, design) != rect) {
		Fill_Shell_Surround();
	}

	if (!source.Is_Valid()) {
		return;
	}

	Rect const screen = Shell_Screen_Rect();
	Rect const region = Shell_To_Screen(source);

	if (screen.Width == design.Width && screen.Height == design.Height) {
		VisibleSurface->Blit_From(region, surface, source);
		return;
	}

	// Point sampling costs less than finding out how little of it is owed.
	if (SHELL_FILTER != SURFACE_FILTER_POINT && Update_Shell_Page(surface, design, screen)) {
		VisibleSurface->Blit_From(region, *_ShellPage, region - screen.Top_Left());
		return;
	}

	VisibleSurface->Blit_Scaled_Region(screen, surface, design, region, SHELL_FILTER);
}


static void Fill_Out_Shell_Surface(Surface * surface)
{
	if (surface == NULL) {
		return;
	}

	Rect const frame = surface->Get_Rect();
	Rect const design = Shell_Rect_In(frame);
	Rect const filled = Fit_Centered(Point2D(design.Width, design.Height), frame);

	if (filled == design) {
		return;
	}

	// The magnification is out of the surface and back into it.
	BSurface picture(design.Width, design.Height, surface->Bytes_Per_Pixel());
	picture.Blit_From(picture.Get_Rect(), *surface, design);

	surface->Fill(TBLACK);

	// A dialog behind the screen repaints itself from this copy, so it is
	// resampled the same way the screen is.
	surface->Blit_From(filled, picture, picture.Get_Rect(), false, true, SHELL_FILTER);
}


void Fill_Out_Shell(void)
{
	if (_ShellSize.X <= 0 || _ShellSize.Y <= 0) {
		return;
	}

	Fill_Out_Shell_Surface(HiddenSurface);
	Fill_Out_Shell_Surface(AlternateSurface);

	_ShellSize = Point2D(0, 0);

	Discard_Shell_Page();
}


Point2D Fit_Point(Point2D const & point, Point2D const & size, Rect const & frame)
{
	if (size.X <= 0 || size.Y <= 0) {
		return(point);
	}

	Rect const centered(frame.X + (frame.Width - size.X) / 2, frame.Y + (frame.Height - size.Y) / 2, size.X, size.Y);
	Rect const filled = Fit_Centered(size, frame);

	return(Point2D(
		filled.X + (point.X - centered.X) * filled.Width / size.X,
		filled.Y + (point.Y - centered.Y) * filled.Height / size.Y
	));
}
