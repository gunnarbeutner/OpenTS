/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The plain menus, each a column of buttons whose press ends it. The drivers say what each
// button answers.

#include "always.h"

#include "uimenus.h"

#include "dialogresult.h"
#include "uirmlview.h"
#include "uirunner.h"

#include <RmlUi/Core/ElementDocument.h>

#include <cstring>


class UIMenuPresenter : public UIPresenterClass
{
	public:
		UIMenuPresenter(UIMenuRequest const & request) : Request(request) {}

		std::function<void(bool)> Show_View;

		void Service(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		UIMenuRequest const & Request;
};


void UIMenuPresenter::Service(void)
{
	if (!Request.Each_Pass || Has_Result()) {
		return;
	}

	int const answer = Request.Each_Pass(Show_View);
	if (answer != Request.NoAnswer) {
		Finish(UI_RESULT_ACCEPTED, answer);
	}
}


void UIMenuPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_ACTION_ACCEPT:
			if (Request.KeysAnswer) {
				Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			}
			return;

		case UI_ACTION_CANCEL:
			if (Request.KeysAnswer) {
				Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			}
			return;

		default:
			break;
	}

	int const index = intent.Action - UI_ACTION_SCREEN;
	if (index < 0 || index >= (int)Request.Choices.size()) {
		return;
	}

	UIMenuChoice const & choice = Request.Choices[index];
	if (choice.Enabled) {
		Finish(UI_RESULT_ACCEPTED, choice.Answer);
	}
}


class UIMenuView : public UIRmlViewClass
{
	public:
		UIMenuView(UIMenuPresenter & presenter, UIMenuRequest const & request) :
			UIRmlViewClass(presenter, request.Document), Request(request) {}

	protected:
		bool Bind(void) override;
		int Action_For(char const * name) const override;

	private:
		UIMenuRequest const & Request;
};


bool UIMenuView::Bind(void)
{
	if (Request.Variant != nullptr) {
		if (Rml::Element * menu = Document->GetElementById("menu")) {
			menu->SetClass(Request.Variant, true);
		}
	}

	// Whether a button takes a press is settled before the menu opens and never changes.
	for (UIMenuChoice const & choice : Request.Choices) {
		Rml::Element * button = Document->GetElementById(choice.Name);

		if (button == nullptr) {
			continue;
		}

		if (choice.Enabled) {
			button->RemoveAttribute("disabled");
		} else {
			button->SetAttribute("disabled", "");
		}
	}

	Attach_Actions();
	return(true);
}


int UIMenuView::Action_For(char const * name) const
{
	int const action = UIRmlViewClass::Action_For(name);
	if (action != UI_ACTION_NONE || name == nullptr) {
		return(action);
	}

	for (std::size_t index = 0; index < Request.Choices.size(); index++) {
		if (std::strcmp(Request.Choices[index].Name, name) == 0) {
			return(UI_ACTION_SCREEN + (int)index);
		}
	}

	return(UI_ACTION_NONE);
}


bool UI_Menu_Screen(UIMenuRequest const & request, int & answer)
{
	UIMenuPresenter presenter(request);
	UIMenuView view(presenter, request);

	presenter.Show_View = [&view](bool show) {
		if (show) {
			view.Show(true);
		} else {
			view.Hide();
		}
	};

	UIResult const result = UI_Run_Modal(presenter, view);

	switch (result.Type) {
		case UI_RESULT_FAILED:
			return(false);

		case UI_RESULT_ACCEPTED:
		case UI_RESULT_CANCELLED:
			answer = result.Code;
			break;

		default:
			break;
	}

	return(true);
}
