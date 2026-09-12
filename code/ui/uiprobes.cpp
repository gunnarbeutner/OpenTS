/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Developer probes for shell capabilities that no screen exercises on its own yet. Each is
// a register entry, so a run can raise it by name in any phase.

#include "always.h"

#include "_surface.h"
#include "progress.h"
#include "uicontext.h"
#include "uiscreens.h"
#include "uishell.h"
#include "uitexture.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>


// The hidden surface is where the engine composes a frame, so it is always there to show,
// which is what makes it the surface a probe reaches for.
static Surface * Hidden_Surface(void)
{
	return(HiddenSurface);
}


static char const _SurfaceProbe[] =
	"<rml><head><style>"
	"body { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }"
	"#thumb { position: absolute; left: 40dp; top: 40dp; width: 256dp; height: 160dp;"
	"         border: 2dp #70ff00ff; }"
	"</style></head>"
	"<body><img id=\"thumb\" src=\"surface:hidden\"/></body></rml>";


static Rml::ElementDocument * _SurfaceDocument = nullptr;


// Shows a thumbnail of the hidden surface, or takes it down when it is already up. Each
// raise reads the surface afresh, so two raises in a row show two frames.
static bool Toggle_Surface_Probe(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	if (_SurfaceDocument != nullptr) {
		_SurfaceDocument->Close();
		_SurfaceDocument = nullptr;
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	UI_Surface_Register("hidden", Hidden_Surface);
	UI_Surface_Invalidate("hidden");

	_SurfaceDocument = context->LoadDocumentFromMemory(_SurfaceProbe);
	if (_SurfaceDocument == nullptr) {
		return(false);
	}

	_SurfaceDocument->Show();
	UI_Mark_Overlay_Dirty();
	return(true);
}


static UIScreenRegistration _RegisterSurface("probe-surface", Toggle_Surface_Probe);


// A shape frame through a named palette: the mouse pointer through its own palette is
// distinctive enough to tell a right decode from a wrong one at a glance.
static char const _ShapeProbe[] =
	"<rml><head><style>"
	"body { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }"
	"img { position: absolute; top: 40dp; border: 1dp #70ff00ff; }"
	"#mouse { left: 40dp; }"
	"#progbar { left: 140dp; }"
	"</style></head>"
	"<body><img id=\"mouse\" src=\"mouse.shp#0@mousepal.pal\"/>"
	"<img id=\"progbar\" src=\"progbar2.shp#0@palette.pal\"/></body></rml>";


static Rml::ElementDocument * _ShapeDocument = nullptr;


static bool Toggle_Shape_Probe(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	if (_ShapeDocument != nullptr) {
		_ShapeDocument->Close();
		_ShapeDocument = nullptr;
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	_ShapeDocument = context->LoadDocumentFromMemory(_ShapeProbe);
	if (_ShapeDocument == nullptr) {
		return(false);
	}

	_ShapeDocument->Show();
	UI_Mark_Overlay_Dirty();
	return(true);
}


static UIScreenRegistration _RegisterShape("probe-shape", Toggle_Shape_Probe);


// A text field and a combo box in the dialogs' skin, to exercise typing, the caret, and the
// drop-down list before a screen depends on them.
static char const _FieldProbe[] =
	"<rml><head><link type=\"text/rcss\" href=\"opents.rcss\"/><style>"
	"#fields { left: 50%; top: 50%; margin-left: -150dp; margin-top: -60dp;"
	"          width: 300dp; height: 120dp; decorator: image(frame:300x120); }"
	"#name { position: absolute; left: 40dp; top: 24dp; width: 220dp; }"
	"#side { position: absolute; left: 40dp; top: 60dp; width: 220dp; }"
	"</style></head><body>"
	"<div id=\"fields\" class=\"dialog\">"
	"<input id=\"name\" class=\"text\" type=\"text\" maxlength=\"12\" value=\"Commander\"/>"
	"<select id=\"side\"><option value=\"0\">GDI</option><option value=\"1\" selected>Nod</option>"
	"<option value=\"2\">Random</option></select>"
	"</div></body></rml>";


static Rml::ElementDocument * _FieldDocument = nullptr;


static bool Toggle_Field_Probe(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	if (_FieldDocument != nullptr) {
		_FieldDocument->Close();
		_FieldDocument = nullptr;
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	_FieldDocument = context->LoadDocumentFromMemory(_FieldProbe);
	if (_FieldDocument == nullptr) {
		return(false);
	}

	_FieldDocument->Show();
	UI_Mark_Overlay_Dirty();
	return(true);
}


static UIScreenRegistration _RegisterField("probe-fields", Toggle_Field_Probe);


// The progress box through ProgressScreenClass, as the map generator drives it: opened,
// taken halfway, and closed by a second request.
static bool Open_Progress_Probe(void)
{
	Progress.Initialize(100, 1, true);
	Progress.Set_Graphic_Data("PROGBAR2.SHP");
	Progress.Set_Progress_Percent(0, 50);
	return(true);
}


static bool Close_Progress_Probe(void)
{
	Progress.End_Dialog();
	Progress.End();
	return(true);
}


static UIScreenRegistration _RegisterProgress("probe-progress", Open_Progress_Probe);
static UIScreenRegistration _RegisterProgressClose("probe-progress-close", Close_Progress_Probe);
