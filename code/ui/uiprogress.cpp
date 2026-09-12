/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uiprogress.h"

#include "dbgprint.h"
#include "uicontext.h"
#include "uishell.h"
#include "uisystem.h"
#include "video.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>


// The width of PROGBAR2.SHP, which is how much of it a finished job shows.
static int const BAR_WIDTH = 147;

static Rml::ElementDocument * _Document = nullptr;
static Rml::DataModelHandle _Model;
static int _Width = 0;


// Nothing else updates the context or presents a frame while the job runs, so the box is
// drawn here each time it changes.
static void Redraw_Now(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return;
	}

	UI_Update_Context(context);
	UI_Mark_Overlay_Dirty();
	Video_Present();
}


bool UI_Progress_Open(void)
{
	if (!UI_Is_Initialized() || _Document != nullptr) {
		return(false);
	}

	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	_Width = 0;

	Rml::DataModelConstructor constructor = context->CreateDataModel("progress");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("Width", &_Width);
	_Model = constructor.GetModelHandle();

	_Document = context->LoadDocument("progress.rml");
	if (_Document == nullptr) {
		DebugString("UI: document 'progress.rml' would not load\n");
		context->RemoveDataModel("progress");
		_Model = Rml::DataModelHandle();
		return(false);
	}

	_Document->Show();
	Redraw_Now();
	return(true);
}


void UI_Progress_Set(double fraction)
{
	if (_Document == nullptr) {
		return;
	}

	if (fraction < 0.0) {
		fraction = 0.0;
	} else if (fraction > 1.0) {
		fraction = 1.0;
	}

	// The painter cut the shape at the product of its width and the fraction, truncated.
	int const width = (int)(BAR_WIDTH * fraction);
	if (width == _Width) {
		return;
	}

	_Width = width;
	_Model.DirtyVariable("Width");
	Redraw_Now();
}


void UI_Progress_Close(void)
{
	if (_Document == nullptr) {
		return;
	}

	Rml::Context * context = UI_Context();

	_Document->Close();
	_Document = nullptr;

	if (context != nullptr) {
		context->RemoveDataModel("progress");
	}
	_Model = Rml::DataModelHandle();

	UI_Mark_Overlay_Dirty();
}


bool UI_Progress_Is_Open(void)
{
	return(_Document != nullptr);
}
