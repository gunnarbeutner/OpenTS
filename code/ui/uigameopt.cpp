/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The in-game options hub. One document serves the dialog's three templates: the single
// player one with the save list buttons, the Westwood Online one with the connection and
// game speed track bars, and the plain multiplayer one. What it has to preserve is what the
// dialog procedure did with each button: which it acted on itself, which it left to
// Ingame_Menu_Dialog through SpecialDialog, and which events it queued for the other players.

#include "always.h"

#include "uigameopt.h"

#include "_keyboar.h"
#include "_map.h"
#include "_xmouse.h"
#include "data.h"
#include "dbgprint.h"
#include "dialogresult.h"
#include "gamedlg.h"
#include "globals.h"
#include "goptions.h"
#include "house.h"
#include "language/language.h"
#include "loaddlg.h"
#include "nettiming.h"
#include "queue.h"
#include "restate.h"
#include "savemgr.h"
#include "scenario.h"
#include "session.h"
#include "stats.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uiscreens.h"
#include "utf8.h"

#include "special.hh"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <cstdio>


enum
{
	UI_GAMEOPT_BUTTON = UI_ACTION_SCREEN,
	UI_GAMEOPT_SPEED,
};


// What the hub asks its driver to do with the save list before it opens the hub again.
enum UIGameOptionsLocalType
{
	UI_GAMEOPT_LOCAL_NONE,
	UI_GAMEOPT_LOCAL_SAVE,
	UI_GAMEOPT_LOCAL_LOAD,
	UI_GAMEOPT_LOCAL_DELETE,
};


class UIGameOptionsPresenter : public UIPresenterClass
{
	public:
		// Which of the dialog's three templates the session chose.
		bool Single = false;
		bool Internet = false;
		bool Multi = false;

		bool BriefingEnabled = true;
		bool LoadEnabled = true;
		bool SaveEnabled = true;
		bool DeleteEnabled = true;

		// Track bar positions, in the dialog's inverted sense: the right end is the best
		// connection and the fastest speed. The connection bar only shows the timing rung
		// the session runs at, and cannot be moved.
		int Connection = 0;
		int Speed = 0;

		// The same positions for the number each bar showed in its trough. They are kept apart
		// from the bars' own values so that showing a number never writes a bar's position
		// back over one the player is dragging it to.
		int ConnectionNumber = 0;
		int SpeedNumber = 0;

		// The labels beside the track bars name each bar's position. The owner-draw track bar
		// reported every change of its position to the dialog, the ones its range and position
		// were given as it opened included, so the template's own text never showed.
		Rml::String ConnectionLabel;
		Rml::String SpeedLabel;

		UIGameOptionsLocalType Local = UI_GAMEOPT_LOCAL_NONE;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Press(int control);
};


// What Game_Options_On_INITDIALOG decided, which the dialog also redid after a save or a
// delete changed what was on disk.
void UIGameOptionsPresenter::Refresh(void)
{
	bool const single = (Session.Type == GAME_NORMAL || Session.Type == GAME_SKIRMISH);

	Single = single;
	Internet = (!single && Session.Type == GAME_INTERNET);
	Multi = (!single && !Internet);

	BriefingEnabled = true;
	LoadEnabled = true;
	SaveEnabled = true;
	DeleteEnabled = true;

	if (single) {
		bool const present = LoadOptionsClass().Files_Present();
		LoadEnabled = present;
		DeleteEnabled = present;
	} else {
		SaveEnabled = SaveManager.Is_Multiplayer_Saving_Allowed();
		LoadEnabled = SaveManager.Multiplayer_Load_Is_Allowed() && MultiplayerLoadOptionsClass().Files_Present();
	}

	if (Internet) {
		NetTiming::TimingSettings const settings{Session.FrameSendRate, Session.MaxAhead};
		NetTiming::ConnectionQuality const quality = NetTiming::Connection_Quality_For_Settings(settings);
		unsigned int const rung = (settings.FrameSendRate >= NetTiming::MINIMUM_TIMING_RUNG
			&& settings.FrameSendRate <= NetTiming::MAXIMUM_TIMING_RUNG) ? settings.FrameSendRate : NetTiming::MAXIMUM_TIMING_RUNG;

		char label[64];
		snprintf(label, sizeof(label), Fetch_String(TXT_CONNECTION_QUALITY_RUNG), Fetch_String(Network_Quality_Text_ID(quality)), settings.FrameSendRate);

		Connection = (int)(NetTiming::MINIMUM_TIMING_RUNG + NetTiming::MAXIMUM_TIMING_RUNG - rung);
		Speed = (OptionsClass::MAX_SPEED_SETTING - 1) - Options.GameSpeed;
		ConnectionNumber = Connection;
		SpeedNumber = Speed;
		ConnectionLabel = label;
		SpeedLabel = Fetch_String(GameSpeedNames[Speed]);
	}

	if (Session.Type == GAME_SKIRMISH) {
		BriefingEnabled = false;
	}
}


void UIGameOptionsPresenter::Press(int control)
{
	switch (control) {
		case IDC_SAVE_GAME:
			if (!SaveEnabled) {
				break;
			}
			if (Single) {
				Local = UI_GAMEOPT_LOCAL_SAVE;
				Finish(UI_RESULT_ACCEPTED, 0);
			} else if (SaveManager.Is_Multiplayer_Saving_Allowed()) {
				OutList.push_back(EventClass(PlayerPtr->HeapID, EventClass::SAVEGAME));
				Finish(UI_RESULT_ACCEPTED, IDC_SAVE_GAME);
			}
			break;

		case IDC_LOAD_GAME:
			if (!LoadEnabled) {
				break;
			}
			if (Single) {
				Local = UI_GAMEOPT_LOCAL_LOAD;
				Finish(UI_RESULT_ACCEPTED, 0);
			} else if (SaveManager.Multiplayer_Load_Is_Allowed()) {
				// A list opened from in here would sit inside the main loop and stall the
				// match; the menu loop opens it between frames instead.
				SpecialDialog = SDLG_LOAD;
				Finish(UI_RESULT_ACCEPTED, IDC_LOAD_GAME);
			}
			break;

		case IDC_BRIEFING:
			if (BriefingEnabled) {
				Finish(UI_RESULT_ACCEPTED, IDC_BRIEFING);
			}
			break;

		case IDC_DELETE_GAME:
			if (DeleteEnabled) {
				Local = UI_GAMEOPT_LOCAL_DELETE;
				Finish(UI_RESULT_ACCEPTED, 0);
			}
			break;

		case IDC_RESUME_MISSION:
			if (Internet) {
				int const speed = (OptionsClass::MAX_SPEED_SETTING - 1) - Speed;
				if (Options.GameSpeed != speed) {
					OutList.push_back(EventClass(PlayerPtr->HeapID, EventClass::GAMESPEED, speed));
				}
			}
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		case IDC_ABORT_MISSION:
			if (Session.Type == GAME_INTERNET) {
				SpecialDialog = SDLG_SURRENDER;
				if (!WestwoodOnline_Tournament) {
					SpecialDialog = SDLG_ABORT;
				}
			} else {
				SpecialDialog = SDLG_ABORT;
			}
			Finish(UI_RESULT_ACCEPTED, DIALOG_CANCEL);
			break;

		case IDC_GAME_CONTROLS:
			SpecialDialog = SDLG_SETTINGS;
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		default:
			break;
	}
}


void UIGameOptionsPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_GAMEOPT_BUTTON:
			Press(intent.Identity);
			break;

		case UI_GAMEOPT_SPEED:
			if (intent.Identity >= 0 && intent.Identity < OptionsClass::MAX_SPEED_SETTING) {
				Speed = intent.Identity;
				SpeedNumber = Speed;
				SpeedLabel = Fetch_String(GameSpeedNames[Speed]);
			}
			break;

		// Enter and Escape reached the dialog as IDOK and IDCANCEL, which none of its
		// buttons are, so neither did anything.
		default:
			break;
	}
}


class UIGameOptionsView : public UIRmlViewClass
{
	public:
		UIGameOptionsView(UIGameOptionsPresenter & presenter) :
			UIRmlViewClass(presenter, "gameopt.rml"), Hub(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIGameOptionsPresenter & Hub;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


void UIGameOptionsView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;
	intent.Action = UI_GAMEOPT_BUTTON;

	if (name == "controls") {
		intent.Identity = IDC_GAME_CONTROLS;
	} else if (name == "briefing") {
		intent.Identity = IDC_BRIEFING;
	} else if (name == "load") {
		intent.Identity = IDC_LOAD_GAME;
	} else if (name == "save") {
		intent.Identity = IDC_SAVE_GAME;
	} else if (name == "delete") {
		intent.Identity = IDC_DELETE_GAME;
	} else if (name == "abort") {
		intent.Identity = IDC_ABORT_MISSION;
	} else if (name == "resume") {
		intent.Identity = IDC_RESUME_MISSION;
	} else if (name == "speed") {
		// The new position comes from the event, which RmlUi raises before the binding
		// stores it.
		int const current = Hub.Speed;
		intent.Action = UI_GAMEOPT_SPEED;
		intent.Identity = (int)(event.GetParameter<float>("value", (float)current) + 0.5f);

		// RmlUi raises a change event for the value the model pushes into a control, too.
		// That one carries the position the presenter already holds.
		if (intent.Identity == current) {
			return;
		}
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIGameOptionsView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("gameopt");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("Single", &Hub.Single);
	constructor.Bind("Internet", &Hub.Internet);
	constructor.Bind("Multi", &Hub.Multi);
	constructor.Bind("BriefingEnabled", &Hub.BriefingEnabled);
	constructor.Bind("LoadEnabled", &Hub.LoadEnabled);
	constructor.Bind("SaveEnabled", &Hub.SaveEnabled);
	constructor.Bind("DeleteEnabled", &Hub.DeleteEnabled);
	constructor.Bind("Connection", &Hub.Connection);
	constructor.Bind("Speed", &Hub.Speed);
	constructor.Bind("ConnectionNumber", &Hub.ConnectionNumber);
	constructor.Bind("SpeedNumber", &Hub.SpeedNumber);
	constructor.Bind("ConnectionLabel", &Hub.ConnectionLabel);
	constructor.Bind("SpeedLabel", &Hub.SpeedLabel);

	constructor.BindEventCallback("act", &UIGameOptionsView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIGameOptionsView::Bind(void)
{
	Attach_Actions();
	return(true);
}


// The track bar positions are pushed once; after that only the numbers and labels follow
// them, so a bar being dragged is never written over.
void UIGameOptionsView::Sync(void)
{
	if (!Model) {
		return;
	}

	if (!Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("ConnectionNumber");
	Model.DirtyVariable("SpeedNumber");
	Model.DirtyVariable("ConnectionLabel");
	Model.DirtyVariable("SpeedLabel");
}


void UIGameOptionsView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("gameopt");
	}

	Model = Rml::DataModelHandle();
}


/// <summary>
/// Runs the in-game options hub with the effects Game_Options_Dialog has around its dialog.
/// </summary>
/// <returns>False when the hub could not be prepared the first time, before anything but the
/// input lock the legacy driver takes as well.</returns>
bool UI_Game_Options_Screen(void)
{
	int rc = 0;

	IgnoreInput = true;
	Keyboard->Clear();

	bool opened = false;

	while (true) {
		UIGameOptionsPresenter presenter;
		UIGameOptionsView view(presenter);

		UIResult const result = UI_Run_Modal(presenter, view);

		if (result.Type == UI_RESULT_FAILED) {
			if (!opened) {
				return(false);
			}

			// The hub opened before, so the save list it was closed for has run, and the
			// player is returned to the game as a resume would.
			rc = DIALOG_OK;
			break;
		}

		opened = true;

		// The dialog's loop read a session that ended under it as a resume.
		if (result.Type == UI_RESULT_SESSION_ENDED) {
			rc = DIALOG_OK;
			break;
		}

		if (presenter.Local == UI_GAMEOPT_LOCAL_SAVE) {
			char description[512];
			UTF8::Copy(description, Scen->Description);
			LoadOptionsClass().Save(description);
			continue;
		}

		if (presenter.Local == UI_GAMEOPT_LOCAL_LOAD) {
			if (LoadOptionsClass().Load()) {
				rc = IDC_LOAD_GAME;
				break;
			}
			continue;
		}

		if (presenter.Local == UI_GAMEOPT_LOCAL_DELETE) {
			LoadOptionsClass().Delete();
			continue;
		}

		rc = result.Code;
		break;
	}

	Keyboard->Clear();

	if (rc == IDC_BRIEFING) {
		Restate_Mission(Scen);
	}

	IgnoreInput = Scen->IsInputLocked;

	if (rc == IDC_LOAD_GAME) {
		if (MouseCursor->Is_Hidden() == false && Scen->IsInputLocked == 1) {
			Hide_Mouse();
		} else if (MouseCursor->Is_Hidden() == true && Scen->IsInputLocked == 0) {
			Show_Mouse();
		}
	}

	Map.Flag_To_Redraw(GS_REDRAW_ALL);
	return(true);
}


// Registered through the dialog driver rather than through the screen, so a run exercises
// the path a caller takes. It is only meaningful over a game in progress.
static bool Open_Game_Options_Dialog(void)
{
	Game_Options_Dialog();
	return(true);
}


static UIScreenRegistration _Register("game-options", Open_Game_Options_Dialog);
