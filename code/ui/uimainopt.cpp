/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The frontend options hub. It only reports which button was pressed: Main_Options_Dialog
// closes it and opens the sub-screen, as it did with the dialog, so the hub is never up at
// the same time as what it opens.

#include "always.h"

#include "uimainopt.h"

#include "audio/audioengine.h"
#include "dialogresult.h"
#include "goptions.h"
#include "language/language.h"
#include "mainopt.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uiscreens.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>


enum
{
	UI_MAINOPT_BUTTON = UI_ACTION_SCREEN,
};


class UIMainOptionsPresenter : public UIPresenterClass
{
	public:
		bool SoundEnabled = false;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;
};


void UIMainOptionsPresenter::Refresh(void)
{
	SoundEnabled = AudioEngine.Is_Available();
}


void UIMainOptionsPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_MAINOPT_BUTTON:
			// A disabled button took no click.
			if (intent.Identity == IDC_OPTMAIN_SOUND && !SoundEnabled) {
				break;
			}
			Finish(UI_RESULT_ACCEPTED, intent.Identity);
			break;

		// The dialog's procedure recorded whatever command reached it, so Enter and Escape
		// answered IDOK and IDCANCEL, which the driver treats as leaving.
		case UI_ACTION_ACCEPT:
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		case UI_ACTION_CANCEL:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UIMainOptionsView : public UIRmlViewClass
{
	public:
		UIMainOptionsView(UIMainOptionsPresenter & presenter) :
			UIRmlViewClass(presenter, "mainopt.rml"), Hub(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIMainOptionsPresenter & Hub;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


// The document names the button by the control it replaces, so the answer is the one the
// dialog gave.
void UIMainOptionsView::On_Action(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;
	intent.Action = UI_MAINOPT_BUTTON;

	if (name == "game") {
		intent.Identity = IDC_OPTMAIN_GAME_SETTINGS;
	} else if (name == "display") {
		intent.Identity = IDC_OPTMAIN_DISPLAY;
	} else if (name == "sound") {
		intent.Identity = IDC_OPTMAIN_SOUND;
	} else if (name == "keyboard") {
		intent.Identity = IDC_OPTMAIN_KEYBOARD;
	} else if (name == "mainmenu") {
		intent.Identity = IDC_OPTMAIN_MAINMENU;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIMainOptionsView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("mainopt");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("SoundEnabled", &Hub.SoundEnabled);
	constructor.BindEventCallback("act", &UIMainOptionsView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIMainOptionsView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIMainOptionsView::Sync(void)
{
	if (Model && !Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
	}
}


void UIMainOptionsView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("mainopt");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Main_Options_Screen(int & choice)
{
	UIMainOptionsPresenter presenter;
	UIMainOptionsView view(presenter);

	UIResult const result = UI_Run_Modal(presenter, view);

	if (result.Type == UI_RESULT_FAILED) {
		return(false);
	}

	// The dialog's loop left its answer unset when the session ended, and the driver read
	// that as leaving.
	choice = (result.Type == UI_RESULT_SESSION_ENDED) ? -1 : result.Code;
	return(true);
}


// Registered through the dialog driver rather than through the screen, so a run exercises
// the path a caller takes, the selector and the sub-screens included.
static bool Open_Main_Options_Dialog(void)
{
	Main_Options_Dialog();
	return(true);
}


static UIScreenRegistration _Register("options", Open_Main_Options_Dialog);
