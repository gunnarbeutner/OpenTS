/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The abort mission screen. What it has to preserve is the answer Ingame_Menu_Dialog acts on,
// and that the middle button is a surrender outside a campaign, dead once the outcome is
// already decided.

#include "always.h"

#include "uiabort.h"

#include "data.h"
#include "dialogresult.h"
#include "house.h"
#include "language/language.h"
#include "session.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>


enum
{
	UI_ABORT_QUIT = UI_ACTION_SCREEN,
	UI_ABORT_RESTART,
	UI_ABORT_CONTINUE,
};


class UIAbortPresenter : public UIPresenterClass
{
	public:
		Rml::String RestartText;
		bool RestartEnabled = true;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;
};


void UIAbortPresenter::Refresh(void)
{
	RestartText = "Restart";
	RestartEnabled = true;

	if (Session.Type != GAME_NORMAL) {
		RestartText = Fetch_String(TXT_SURRENDER);
		if (PlayerPtr->IsDefeated || PlayerPtr->IsToWin || PlayerPtr->IsToLose || PlayerPtr->IsToDie) {
			RestartEnabled = false;
		}
	}
}


void UIAbortPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_ABORT_QUIT:
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		case UI_ABORT_RESTART:
			if (RestartEnabled) {
				Finish(UI_RESULT_ACCEPTED, DIALOG_ABORT);
			}
			break;

		// Enter and Escape reached the dialog as IDOK and IDCANCEL, and it answered both as
		// its Cancel button, which carries on playing.
		case UI_ACTION_ACCEPT:
		case UI_ACTION_CANCEL:
		case UI_ABORT_CONTINUE:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UIAbortView : public UIRmlViewClass
{
	public:
		UIAbortView(UIAbortPresenter & presenter) :
			UIRmlViewClass(presenter, "abort.rml"), Abort(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIAbortPresenter & Abort;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


void UIAbortView::On_Action(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	if (name == "abort") {
		intent.Action = UI_ABORT_QUIT;
	} else if (name == "restart") {
		intent.Action = UI_ABORT_RESTART;
	} else if (name == "cancel") {
		intent.Action = UI_ABORT_CONTINUE;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIAbortView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("abort");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("RestartText", &Abort.RestartText);
	constructor.Bind("RestartEnabled", &Abort.RestartEnabled);
	constructor.BindEventCallback("act", &UIAbortView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIAbortView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIAbortView::Sync(void)
{
	if (Model && !Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
	}
}


void UIAbortView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("abort");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Abort_Screen(int & result)
{
	UIAbortPresenter presenter;
	UIAbortView view(presenter);

	UIResult const outcome = UI_Run_Modal(presenter, view);

	if (outcome.Type == UI_RESULT_FAILED) {
		return(false);
	}

	result = (outcome.Type == UI_RESULT_SESSION_ENDED) ? DIALOG_OK : outcome.Code;
	return(true);
}
