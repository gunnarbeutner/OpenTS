/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The game settings screen. One document serves the dialog's three templates: the frontend
// one with a difficulty track bar, and the in-game ones for a Westwood Online game, which has
// no game speed bar, and for any other game. Nothing applies until the screen is left the
// accepting way, which is when the dialog read its controls back and wrote the settings file.

#include "always.h"

#include "uigamectrl.h"

#include "_map.h"
#include "_tooltip.h"
#include "audio/audioengine.h"
#include "cctooltip.h"
#include "data.h"
#include "dialogresult.h"
#include "gamedlg.h"
#include "globals.h"
#include "house.h"
#include "language/language.h"
#include "queue.h"
#include "session.h"
#include "techno.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"

#include "special.hh"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>


enum
{
	UI_GAMECTRL_SOUND = UI_ACTION_SCREEN,
	UI_GAMECTRL_KEYBOARD,
	UI_GAMECTRL_SPEED,
	UI_GAMECTRL_SCROLL,
	UI_GAMECTRL_DETAIL,
	UI_GAMECTRL_DIFFICULTY,
	UI_GAMECTRL_CAMEO_TEXT,
	UI_GAMECTRL_TARGET_LINES,
	UI_GAMECTRL_TOOLTIPS,
	UI_GAMECTRL_COASTING,
	UI_GAMECTRL_EDGE_SCROLL,
};


class UIGameControlsPresenter : public UIPresenterClass
{
	public:
		// Which of the dialog's three templates GameActive and the session chose.
		bool Frontend = false;
		bool Internet = false;
		bool Multi = false;

		// Track bar positions, in the dialog's sense: the right end of the speed and scroll
		// bars is the fastest setting, which is the lowest stored value.
		int Speed = 0;
		int Scroll = 0;
		int Detail = 0;
		int Difficulty = 0;

		// The labels beside the track bars name each bar's position. The owner-draw track bar
		// reported every change of its position to the dialog, the ones its range and position
		// were given as it opened included, so the template's own text never showed.
		Rml::String SpeedLabel;
		Rml::String ScrollLabel;
		Rml::String DetailLabel;
		Rml::String DifficultyLabel;

		bool CameoText = false;
		bool TargetLines = false;
		bool Tips = false;
		bool Coasting = false;
		bool EdgeScroll = false;

		bool SoundEnabled = true;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Apply(void);
};


static Rml::String Name_Of(int const * names, int index)
{
	return(Fetch_String(names[index]));
}


void UIGameControlsPresenter::Refresh(void)
{
	Frontend = (GameActive != true);
	Internet = (!Frontend && Session.Type == GAME_INTERNET);
	Multi = (!Frontend && !Internet);

	Speed = (OptionsClass::MAX_SPEED_SETTING - 1) - Options.GameSpeed;
	Scroll = (OptionsClass::MAX_SCROLL_SETTING - 1) - Options.ScrollRate;
	Detail = Options.DetailLevel;
	Difficulty = Options.Difficulty;

	CameoText = (Options.SidebarCameoText != false);
	TargetLines = (Options.ActionLines != false);
	Tips = (Options.ToolTips != false);
	Coasting = (Options.ScrollMethod == 0);
	EdgeScroll = (Options.AutoScroll != false);

	SoundEnabled = Frontend || AudioEngine.Is_Available();

	SpeedLabel = Name_Of(GameSpeedNames, Speed);
	ScrollLabel = Name_Of(GameScrollSpeedNames, Scroll);
	DetailLabel = Name_Of(GameDetailLevelNames, Detail);
	DifficultyLabel = Name_Of(GameDifficultyNames, Difficulty);
}


// What GameControlsClass::Set did, control by control, followed by the settings file write
// the dialog made straight after it. A control the template lacks is not read.
void UIGameControlsPresenter::Apply(void)
{
	if (!Internet) {
		int const gamespeed = (OptionsClass::MAX_SPEED_SETTING - 1) - Speed;
		if (Options.GameSpeed != gamespeed) {
			if (GameActive == true && Session.Type != GAME_NORMAL && Session.Type != GAME_SKIRMISH) {
				OutList.push_back(EventClass(PlayerPtr->HeapID, EventClass::GAMESPEED, gamespeed));
			} else {
				Options.GameSpeed = gamespeed;
			}
		}
	}

	Options.ScrollRate = (OptionsClass::MAX_SCROLL_SETTING - 1) - Scroll;

	if (Options.DetailLevel != Detail) {
		Options.DetailLevel = Detail;
		Map.Reinit_Cell_Drawers();
	}

	if (Options.SidebarCameoText != CameoText) {
		Options.SidebarCameoText = CameoText;
		Map.Toggle_Cameo_Text(CameoText);
	}

	Options.ActionLines = TargetLines;
	TechnoClass::Set_Action_Lines(Options.ActionLines);

	Options.ToolTips = Tips;
	if (ToolTips != nullptr && GameActive == true) {
		ToolTips->Activate(Options.ToolTips);
	}

	// Accepting turns a stored method of 2 into 1 when the box is left clear, as the
	// dialog's two-state box did.
	Options.ScrollMethod = Coasting ? 0 : 1;

	Options.AutoScroll = EdgeScroll;

	if (GameActive == false) {
		Options.Difficulty = Difficulty;
	}

	Options.Save_Settings();
}


void UIGameControlsPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	int const value = intent.Identity;

	switch (intent.Action) {
		case UI_ACTION_ACCEPT:
			Apply();
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		case UI_ACTION_CANCEL:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		// The two buttons that lead on to another in-game screen accepted this one first.
		case UI_GAMECTRL_KEYBOARD:
			if (GameActive == true) {
				SpecialDialog = SDLG_KEYBOARD;
				Apply();
				Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			}
			break;

		case UI_GAMECTRL_SOUND:
			if (GameActive == true && SoundEnabled) {
				SpecialDialog = SDLG_SOUND;
				Apply();
				Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			}
			break;

		case UI_GAMECTRL_SPEED:
			if (value >= 0 && value < OptionsClass::MAX_SPEED_SETTING) {
				Speed = value;
				SpeedLabel = Name_Of(GameSpeedNames, value);
			}
			break;

		case UI_GAMECTRL_SCROLL:
			if (value >= 0 && value < OptionsClass::MAX_SCROLL_SETTING) {
				Scroll = value;
				ScrollLabel = Name_Of(GameScrollSpeedNames, value);
			}
			break;

		case UI_GAMECTRL_DETAIL:
			if (value >= 0 && value < OptionsClass::MAX_DETAIL_SETTING) {
				Detail = value;
				DetailLabel = Name_Of(GameDetailLevelNames, value);
			}
			break;

		case UI_GAMECTRL_DIFFICULTY:
			if (Frontend && value >= 0 && value < OptionsClass::MAX_DIFFICULTY_SETTING) {
				Difficulty = value;
				DifficultyLabel = Name_Of(GameDifficultyNames, value);
			}
			break;

		case UI_GAMECTRL_CAMEO_TEXT:
			CameoText = (value != 0);
			break;

		case UI_GAMECTRL_TARGET_LINES:
			TargetLines = (value != 0);
			break;

		case UI_GAMECTRL_TOOLTIPS:
			Tips = (value != 0);
			break;

		case UI_GAMECTRL_COASTING:
			Coasting = (value != 0);
			break;

		case UI_GAMECTRL_EDGE_SCROLL:
			EdgeScroll = (value != 0);
			break;

		default:
			break;
	}
}


class UIGameControlsView : public UIRmlViewClass
{
	public:
		UIGameControlsView(UIGameControlsPresenter & presenter) :
			UIRmlViewClass(presenter, "gamectrl.rml"), Controls(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIGameControlsPresenter & Controls;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


// A control's new value is taken from the event, which RmlUi raises before the binding
// stores it.
static int Changed_Position(Rml::Event & event, int fallback)
{
	return((int)(event.GetParameter<float>("value", (float)fallback) + 0.5f));
}


static int Changed_Check(Rml::Event & event, bool fallback)
{
	return(event.GetParameter<bool>("checked", fallback) ? 1 : 0);
}


void UIGameControlsView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;
	int current = -1;

	if (name == "accept") {
		intent.Action = UI_ACTION_ACCEPT;
	} else if (name == "sound") {
		intent.Action = UI_GAMECTRL_SOUND;
	} else if (name == "keyboard") {
		intent.Action = UI_GAMECTRL_KEYBOARD;
	} else if (name == "speed") {
		intent.Action = UI_GAMECTRL_SPEED;
		intent.Identity = Changed_Position(event, Controls.Speed);
		current = Controls.Speed;
	} else if (name == "scroll") {
		intent.Action = UI_GAMECTRL_SCROLL;
		intent.Identity = Changed_Position(event, Controls.Scroll);
		current = Controls.Scroll;
	} else if (name == "detail") {
		intent.Action = UI_GAMECTRL_DETAIL;
		intent.Identity = Changed_Position(event, Controls.Detail);
		current = Controls.Detail;
	} else if (name == "difficulty") {
		intent.Action = UI_GAMECTRL_DIFFICULTY;
		intent.Identity = Changed_Position(event, Controls.Difficulty);
		current = Controls.Difficulty;
	} else if (name == "cameo") {
		intent.Action = UI_GAMECTRL_CAMEO_TEXT;
		intent.Identity = Changed_Check(event, Controls.CameoText);
		current = Controls.CameoText ? 1 : 0;
	} else if (name == "lines") {
		intent.Action = UI_GAMECTRL_TARGET_LINES;
		intent.Identity = Changed_Check(event, Controls.TargetLines);
		current = Controls.TargetLines ? 1 : 0;
	} else if (name == "tips") {
		intent.Action = UI_GAMECTRL_TOOLTIPS;
		intent.Identity = Changed_Check(event, Controls.Tips);
		current = Controls.Tips ? 1 : 0;
	} else if (name == "coasting") {
		intent.Action = UI_GAMECTRL_COASTING;
		intent.Identity = Changed_Check(event, Controls.Coasting);
		current = Controls.Coasting ? 1 : 0;
	} else if (name == "edge") {
		intent.Action = UI_GAMECTRL_EDGE_SCROLL;
		intent.Identity = Changed_Check(event, Controls.EdgeScroll);
		current = Controls.EdgeScroll ? 1 : 0;
	} else {
		return;
	}

	// RmlUi raises a change event for the value the model pushes into a control, too. That
	// one carries the value the presenter already holds and is not a player's change.
	if (intent.Identity == current) {
		return;
	}

	Presenter.Queue(intent);
}


bool UIGameControlsView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("gamectrl");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("Frontend", &Controls.Frontend);
	constructor.Bind("Internet", &Controls.Internet);
	constructor.Bind("Multi", &Controls.Multi);

	constructor.Bind("Speed", &Controls.Speed);
	constructor.Bind("Scroll", &Controls.Scroll);
	constructor.Bind("Detail", &Controls.Detail);
	constructor.Bind("Difficulty", &Controls.Difficulty);

	constructor.Bind("SpeedLabel", &Controls.SpeedLabel);
	constructor.Bind("ScrollLabel", &Controls.ScrollLabel);
	constructor.Bind("DetailLabel", &Controls.DetailLabel);
	constructor.Bind("DifficultyLabel", &Controls.DifficultyLabel);

	constructor.Bind("CameoText", &Controls.CameoText);
	constructor.Bind("TargetLines", &Controls.TargetLines);
	constructor.Bind("Tips", &Controls.Tips);
	constructor.Bind("Coasting", &Controls.Coasting);
	constructor.Bind("EdgeScroll", &Controls.EdgeScroll);

	constructor.Bind("SoundEnabled", &Controls.SoundEnabled);

	constructor.BindEventCallback("act", &UIGameControlsView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIGameControlsView::Bind(void)
{
	Attach_Actions();
	return(true);
}


// The controls are pushed once; after that only the labels follow them, so a bar being
// dragged or a box being clicked is never written over.
void UIGameControlsView::Sync(void)
{
	if (!Model) {
		return;
	}

	if (!Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("SpeedLabel");
	Model.DirtyVariable("ScrollLabel");
	Model.DirtyVariable("DetailLabel");
	Model.DirtyVariable("DifficultyLabel");
}


void UIGameControlsView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("gamectrl");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Game_Controls_Screen(void)
{
	UIGameControlsPresenter presenter;
	UIGameControlsView view(presenter);

	// A session that ends under the screen closes it without applying anything, which is
	// what the dialog's loop did by answering Cancel for it.
	UIResult const result = UI_Run_Modal(presenter, view);

	return(result.Type != UI_RESULT_FAILED);
}
