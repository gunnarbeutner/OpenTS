/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The message box screen. What it has to preserve is the answer a caller reads: the button
// index, the default the Enter key gives, and what Escape means.

#include "always.h"

#include "uimsgbox.h"

#include "msgbox.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uiscreens.h"
#include "uirunner.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <cstring>
#include <string>
#include <vector>


enum
{
	UI_ACTION_BUTTON = UI_ACTION_SCREEN,
};


// One button as the document renders it. Index is what the caller is answered with, not
// where the button sits, so hiding a button does not renumber the rest.
struct UIMessageButton
{
	Rml::String Text;
	int Index = 0;
};


class UIMessageBoxPresenter : public UIPresenterClass
{
	public:
		Rml::String Message;
		std::vector<UIMessageButton> Buttons;
		bool Spread = false;

		int Default = 0;

	protected:
		void Execute(UIIntent const & intent) override;
};


void UIMessageBoxPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_ACTION_BUTTON:
			Finish(UI_RESULT_ACCEPTED, intent.Identity);
			break;

		case UI_ACTION_ACCEPT:
			// The dialog had no default push button, so Enter reached its handler as IDOK
			// and was answered with the caller's chosen response rather than with a button.
			Finish(UI_RESULT_ACCEPTED, Default);
			break;

		case UI_ACTION_CANCEL:
			// Escape reached the dialog as IDCANCEL, which was the second button's
			// identifier, and it answered 1 whether or not that button was shown.
			Finish(UI_RESULT_CANCELLED, 1);
			break;

		default:
			break;
	}
}


class UIMessageBoxView : public UIRmlViewClass
{
	public:
		UIMessageBoxView(UIMessageBoxPresenter & presenter) :
			UIRmlViewClass(presenter, "msgbox.rml"), Box(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;
		int Action_For(char const * name) const override;

	private:
		UIMessageBoxPresenter & Box;
		Rml::DataModelHandle Model;
};


int UIMessageBoxView::Action_For(char const * name) const
{
	if (name != nullptr && std::strcmp(name, "button") == 0) {
		return(UI_ACTION_BUTTON);
	}

	return(UIRmlViewClass::Action_For(name));
}


bool UIMessageBoxView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("msgbox");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIMessageButton>(constructor)) {
		handle.RegisterMember("Text", &UIMessageButton::Text);
		handle.RegisterMember("Index", &UIMessageButton::Index);
	}

	UI_Register_Array<std::vector<UIMessageButton>>(constructor);

	constructor.Bind("Message", &Box.Message);
	constructor.Bind("Buttons", &Box.Buttons);
	constructor.Bind("Spread", &Box.Spread);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIMessageBoxView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIMessageBoxView::Sync(void)
{
	if (!Model) {
		return;
	}

	Model.DirtyVariable("Message");
	Model.DirtyVariable("Buttons");
	Model.DirtyVariable("Spread");
}


void UIMessageBoxView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("msgbox");
	}

	Model = Rml::DataModelHandle();
}


int UI_Message_Box(char const * message, int defresponse,
	char const * button1, char const * button2, char const * button3)
{
	UIMessageBoxPresenter presenter;
	UIMessageBoxView view(presenter);

	presenter.Default = defresponse;
	presenter.Message = (message != nullptr) ? message : "";

	// The buttons are added in the order they were laid out rather than the order they are
	// answered with, so a row of two reads left to right the way the dialog's did.
	char const * const texts[] = { button1, button3, button2 };
	int const indexes[] = { 0, 2, 1 };

	for (int slot = 0; slot < 3; slot++) {
		if (texts[slot] == nullptr || texts[slot][0] == '\0') {
			continue;
		}

		UIMessageButton button;
		button.Text = texts[slot];
		button.Index = indexes[slot];
		presenter.Buttons.push_back(button);
	}

	// A box with nothing to answer with was never waited on: the dialog was put up and the
	// answer was zero at once.
	if (presenter.Buttons.empty()) {
		return(0);
	}

	// The dialog left its buttons in their slots and moved only a lone button to the middle
	// one, so a row of two keeps the outer slots and a row of one is centred.
	presenter.Spread = (presenter.Buttons.size() >= 2);

	UIResult const result = UI_Run_Modal(presenter, view);

	if (result.Type == UI_RESULT_FAILED) {
		return(UI_MESSAGE_BOX_UNAVAILABLE);
	}

	if (result.Type == UI_RESULT_SESSION_ENDED) {
		return(UI_MESSAGE_BOX_INTERRUPTED);
	}

	return(result.Code);
}


// The register's entries for exercising the answers a caller reads, without reproducing a
// game state that raises a box. Raised through WWMessageBox so the selector is exercised.
static int _LastAnswer = -99;


static bool Open_Probe_Box(int buttons)
{
	char const * const texts[] = { "First", "Second", "Third" };

	_LastAnswer = WWMessageBox()._Process("Probe message",
		0,
		buttons >= 1 ? texts[0] : nullptr,
		buttons >= 2 ? texts[1] : nullptr,
		buttons >= 3 ? texts[2] : nullptr);

	return(true);
}


static bool Open_Probe_Box_1(void) { return(Open_Probe_Box(1)); }
static bool Open_Probe_Box_2(void) { return(Open_Probe_Box(2)); }
static bool Open_Probe_Box_3(void) { return(Open_Probe_Box(3)); }


static UIScreenRegistration _Register1("msgbox1", Open_Probe_Box_1);
static UIScreenRegistration _Register2("msgbox2", Open_Probe_Box_2);
static UIScreenRegistration _Register3("msgbox3", Open_Probe_Box_3);


#if defined(__EMSCRIPTEN__)

extern "C" {

EMSCRIPTEN_KEEPALIVE int OpenTS_UI_Message_Box_Answer(void)
{
	return(_LastAnswer);
}

}

#endif
