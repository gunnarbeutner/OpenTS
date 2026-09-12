/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The campaign choice screen. It preserves what Campaign_Choice_Dialog_Proc wrote: the
// campaign picked in the list and the difficulty, and those only on OK.

#include "always.h"

#include "uicampaign.h"

#include "_rules.h"
#include "campaign.h"
#include "data.h"
#include "dialogresult.h"
#include "gamedlg.h"
#include "globals.h"
#include "language/language.h"
#include "options.h"
#include "rules.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "voc.h"
#include "win.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <string>
#include <vector>


enum
{
	UI_CAMPAIGN_ACCEPT = UI_ACTION_ACCEPT,
	UI_CAMPAIGN_CANCEL = UI_ACTION_CANCEL,

	UI_CAMPAIGN_SELECT = UI_ACTION_SCREEN,
	UI_CAMPAIGN_DIFFICULTY,
};


class UICampaignPresenter : public UIPresenterClass
{
	public:
		UICampaignPresenter(std::vector<int> const & campaigns) : Campaigns(campaigns) {}

		std::vector<Rml::String> Rows;
		int Selected = -1;
		int Difficulty = 0;
		Rml::String DifficultyName;

		int Chosen = CAMPAIGN_NONE;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Name_Difficulty(void);

		std::vector<int> const & Campaigns;
};


void UICampaignPresenter::Name_Difficulty(void)
{
	DifficultyName = Fetch_String(GameDifficultyNames[Difficulty]);
}


void UICampaignPresenter::Refresh(void)
{
	Rows.clear();
	for (int index : Campaigns) {
		Rows.push_back(::Campaigns[index]->Description);
	}

	Selected = Rows.empty() ? -1 : 0;

	Difficulty = Options.Difficulty;
	if (Difficulty < 0) {
		Difficulty = 0;
	}
	if (Difficulty > 2) {
		Difficulty = 2;
	}
	Name_Difficulty();
}


void UICampaignPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_CAMPAIGN_SELECT:
			// The list clicked on a press, whether or not the row changed.
			Sound_Effect(Rule->GenericClick);
			if (intent.Identity >= 0 && intent.Identity < (int)Rows.size()) {
				Selected = intent.Identity;
			}
			break;

		case UI_CAMPAIGN_DIFFICULTY:
			if (intent.Identity >= 0 && intent.Identity <= 2) {
				Difficulty = intent.Identity;
				Name_Difficulty();
			}
			break;

		// An empty list answered no campaign, as the list box's item data for no selection
		// did.
		case UI_CAMPAIGN_ACCEPT:
			if (Selected >= 0) {
				Chosen = Campaigns[Selected];
			}
			Options.Difficulty = Difficulty;
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		case UI_CAMPAIGN_CANCEL:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UICampaignView : public UIRmlViewClass
{
	public:
		UICampaignView(UICampaignPresenter & presenter) :
			UIRmlViewClass(presenter, "campaign.rml"), Campaign(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UICampaignPresenter & Campaign;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


void UICampaignView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	if (name == "select") {
		intent.Action = UI_CAMPAIGN_SELECT;
		intent.Identity = arguments.size() > 1 ? arguments[1].Get<int>() : -1;
	} else if (name == "difficulty") {
		// The new value is read from the event rather than from the binding; see UISoundView.
		intent.Action = UI_CAMPAIGN_DIFFICULTY;
		intent.Identity = (int)(event.GetParameter<float>("value", (float)Campaign.Difficulty) + 0.5f);
	} else if (name == "accept") {
		intent.Action = UI_CAMPAIGN_ACCEPT;
	} else if (name == "cancel") {
		intent.Action = UI_CAMPAIGN_CANCEL;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UICampaignView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("campaign");
	if (!constructor) {
		return(false);
	}

	UI_Register_Array<std::vector<Rml::String>>(constructor);

	constructor.Bind("Rows", &Campaign.Rows);
	constructor.Bind("Selected", &Campaign.Selected);
	constructor.Bind("Difficulty", &Campaign.Difficulty);
	constructor.Bind("DifficultyName", &Campaign.DifficultyName);

	constructor.BindEventCallback("act", &UICampaignView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UICampaignView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UICampaignView::Sync(void)
{
	if (!Model) {
		return;
	}

	// The slider is pushed once; after that only what it drives is.
	if (!Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("Selected");
	Model.DirtyVariable("DifficultyName");
}


void UICampaignView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("campaign");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Campaign_Screen(std::vector<int> const & campaigns, int & chosen)
{
	UICampaignPresenter presenter(campaigns);
	UICampaignView view(presenter);

	UIResult const result = UI_Run_Modal(presenter, view);

	if (result.Type == UI_RESULT_FAILED) {
		return(false);
	}

	if (result.Type == UI_RESULT_ACCEPTED) {
		chosen = presenter.Chosen;
	}

	return(true);
}
