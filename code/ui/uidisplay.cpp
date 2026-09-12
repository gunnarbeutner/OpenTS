/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The display options screen. It stages a choice rather than applying it: the driver compares
// the staged options with the live ones afterwards, makes the change and puts the new mode up
// for confirmation.

#include "always.h"

#include "uidisplay.h"

#include "_rules.h"
#include "dialogresult.h"
#include "globals.h"
#include "goptions.h"
#include "rules.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "utf8.h"
#include "video.h"
#include "voc.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <cstdio>
#include <vector>


enum
{
	UI_DISPLAY_SELECT = UI_ACTION_SCREEN,
	UI_DISPLAY_STRETCH,
};


// One row of the list.
struct UIDisplayRow
{
	Rml::String Name;
};


class UIDisplayPresenter : public UIPresenterClass
{
	public:
		UIDisplayPresenter(GameOptionsClass & staged) : Staged(staged) {}

		std::vector<UIDisplayRow> Entries;

		// The row the list shows selected, and the one it showed when the screen opened. A
		// choice is staged only when the two differ.
		int Current = -1;
		int Previous = -1;

		bool Stretch = false;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Accept(void);

		GameOptionsClass & Staged;

		// Width and height of each row's mode, in the order the rows are listed.
		std::vector<int> Modes;
};


void UIDisplayPresenter::Refresh(void)
{
	Entries.clear();
	int initial = -1;

	enum {
		MIN_WIDTH = 640,
		MIN_HEIGHT = 400,
		MAX_WIDTH = 4096,
		MAX_HEIGHT = 4096,
	};

	Modes.clear();

	int * modes = EnumDisplayModes(MIN_WIDTH, MIN_HEIGHT, MAX_WIDTH, MAX_HEIGHT);
	if (modes != nullptr) {
		for (int * mode = modes; *mode != 0; mode += 2) {
			int const width = mode[0];
			int const height = mode[1];

			if (width == Staged.ScreenWidth && height == Staged.ScreenHeight) {
				initial = (int)Entries.size();
			}

			char buffer[64];
			snprintf(buffer, sizeof(buffer), "%d x %d", width, height);
			Entries.push_back(UIDisplayRow{buffer});
			Modes.push_back(width);
			Modes.push_back(height);
		}
		delete [] modes;
	}

	Current = initial;
	Previous = initial;
	Stretch = (Options.StretchMovies != false);
}


void UIDisplayPresenter::Accept(void)
{
	if (Previous != Current && Current >= 0 && Current < (int)Entries.size()) {
		Staged.ScreenWidth = Modes[2 * Current];
		Staged.ScreenHeight = Modes[2 * Current + 1];
	}

	// The dialog wrote this into the live options as it closed, not into the staged copy.
	Options.StretchMovies = Stretch;

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


void UIDisplayPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_DISPLAY_SELECT:
			// The list clicked on a press, whether or not the row changed.
			Sound_Effect(Rule->GenericClick);
			if (intent.Identity >= 0 && intent.Identity < (int)Entries.size()) {
				Current = intent.Identity;
			}
			break;

		case UI_DISPLAY_STRETCH:
			Stretch = (intent.Identity != 0);
			break;

		case UI_ACTION_ACCEPT:
			Accept();
			break;

		case UI_ACTION_CANCEL:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UIDisplayView : public UIRmlViewClass
{
	public:
		UIDisplayView(UIDisplayPresenter & presenter) :
			UIRmlViewClass(presenter, "display.rml"), Display(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIDisplayPresenter & Display;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


void UIDisplayView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	if (name == "accept") {
		intent.Action = UI_ACTION_ACCEPT;
	} else if (name == "cancel") {
		intent.Action = UI_ACTION_CANCEL;
	} else if (name == "select") {
		intent.Action = UI_DISPLAY_SELECT;
		intent.Identity = arguments.size() > 1 ? arguments[1].Get<int>() : -1;
	} else if (name == "stretch") {
		// The new state comes from the event, which RmlUi raises before the binding stores it.
		intent.Action = UI_DISPLAY_STRETCH;
		intent.Identity = event.GetParameter<bool>("checked", Display.Stretch) ? 1 : 0;

		// The box also reports the state the model pushes into it, which is no change.
		if (intent.Identity == (Display.Stretch ? 1 : 0)) {
			return;
		}
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIDisplayView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("display");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIDisplayRow>(constructor)) {
		handle.RegisterMember("Name", &UIDisplayRow::Name);
	}
	UI_Register_Array<std::vector<UIDisplayRow>>(constructor);

	constructor.Bind("Entries", &Display.Entries);
	constructor.Bind("Current", &Display.Current);
	constructor.Bind("Stretch", &Display.Stretch);

	constructor.BindEventCallback("act", &UIDisplayView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIDisplayView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIDisplayView::Sync(void)
{
	if (!Model) {
		return;
	}

	if (!Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("Current");
}


void UIDisplayView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("display");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Display_Options_Screen(GameOptionsClass & staged, int & result)
{
	UIDisplayPresenter presenter(staged);
	UIDisplayView view(presenter);

	UIResult const outcome = UI_Run_Modal(presenter, view);

	if (outcome.Type == UI_RESULT_FAILED) {
		return(false);
	}

	result = (outcome.Type == UI_RESULT_SESSION_ENDED) ? -1 : outcome.Code;
	return(true);
}
