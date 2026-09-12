/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The display mode confirmation screen. What it has to preserve is the timeout: the dialog's
// loop checked a ten second system timer on every pass and, once it ran out, answered
// Cancel.

#include "always.h"

#include "uimodeconfirm.h"

#include "dialogresult.h"
#include "stimer.h"
#include "timer.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>


enum
{
	UI_CONFIRM_TIMEOUT = UI_ACTION_SCREEN,
};


class UIModeConfirmPresenter : public UIPresenterClass
{
	public:
		void Refresh(void) override;
		void Service(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		CDTimerClass<SystemTimerClass> Timer;
		bool Started = false;
		bool Posted = false;
};


// The runner refreshes once, just before the screen is shown, which is where the dialog
// started its timer.
void UIModeConfirmPresenter::Refresh(void)
{
	if (!Started) {
		Timer = 10 * TIMER_SECOND;
		Started = true;
	}
}


// The dialog's loop looked at its timer once a pass and, once it had run out, posted itself
// the command that answered Cancel. The command is queued rather than acted on here, so it
// takes its turn behind whatever the player did in the same pass.
void UIModeConfirmPresenter::Service(void)
{
	if (Started && !Posted && Timer <= 0) {
		Posted = true;

		UIIntent intent;
		intent.Action = UI_CONFIRM_TIMEOUT;
		Queue(intent);
	}
}


void UIModeConfirmPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_ACTION_ACCEPT:
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		// The timeout arrived as a command numbered IDCANCEL, so it answers as Cancel does.
		case UI_ACTION_CANCEL:
		case UI_CONFIRM_TIMEOUT:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UIModeConfirmView : public UIRmlViewClass
{
	public:
		UIModeConfirmView(UIModeConfirmPresenter & presenter) :
			UIRmlViewClass(presenter, "modeconfirm.rml") {}

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		Rml::DataModelHandle Model;
};


void UIModeConfirmView::On_Action(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const & arguments)
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
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIModeConfirmView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("modeconfirm");
	if (!constructor) {
		return(false);
	}

	constructor.BindEventCallback("act", &UIModeConfirmView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIModeConfirmView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIModeConfirmView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("modeconfirm");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Mode_Confirm_Screen(int & result)
{
	UIModeConfirmPresenter presenter;
	UIModeConfirmView view(presenter);

	UIResult const outcome = UI_Run_Modal(presenter, view);

	if (outcome.Type == UI_RESULT_FAILED) {
		return(false);
	}

	result = (outcome.Type == UI_RESULT_SESSION_ENDED) ? -1 : outcome.Code;
	return(true);
}
