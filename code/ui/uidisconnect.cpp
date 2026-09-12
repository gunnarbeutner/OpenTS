/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uidisconnect.h"

#include "dbgprint.h"
#include "session.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uishell.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


// Where the template put each player's button and bar, in the order of Session.Players:
// four down the left and four down the right.
static int const SLOT_LEFT[MAX_PLAYERS] = { 33, 33, 33, 33, 262, 262, 262, 262 };
static int const SLOT_TOP[MAX_PLAYERS] = { 20, 49, 78, 107, 20, 49, 78, 107 };

// The bars' group boxes are 60 pixels wide, and the painter never drew one narrower than 6.
static int const BAR_WIDTH = 60;
static int const BAR_MINIMUM = 6;

// ListBox_Trim keeps this many lines.
static int const MESSAGE_LIMIT = 50;


struct UIDisconnectSlot
{
	Rml::String Name;
	int Left = 0;
	int Top = 0;
	int Index = 0;
	int BarWidth = 0;
	Rml::String BarColour;
};


class UIDisconnectPresenter : public UIPresenterClass
{
	public:
		std::vector<UIDisconnectSlot> Slots;
		std::vector<Rml::String> Messages;
		Rml::String Time;

		void (*Handler)(UIIntent const & intent) = nullptr;

	protected:
		void Execute(UIIntent const & intent) override;
};


void UIDisconnectPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_DISCONNECT_KICK:
			// The dialog only made buttons for the players it was opened with.
			if (intent.Identity < 0 || intent.Identity >= (int)Slots.size()) {
				return;
			}
			break;

		case UI_ACTION_CANCEL:
			break;

		// Enter reached the dialog as IDOK, which it did not handle.
		default:
			return;
	}

	if (Handler != nullptr) {
		Handler(intent);
	}
}


class UIDisconnectView : public UIRmlViewClass
{
	public:
		UIDisconnectView(UIDisconnectPresenter & presenter) :
			UIRmlViewClass(presenter, "disconnect.rml"), Box(presenter) {}

		void Dirty(char const * name);
		void Scroll_To_End(void);

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;
		int Action_For(char const * name) const override;

	private:
		UIDisconnectPresenter & Box;
		Rml::DataModelHandle Model;
};


int UIDisconnectView::Action_For(char const * name) const
{
	if (name != nullptr && std::strcmp(name, "kick") == 0) {
		return(UI_DISCONNECT_KICK);
	}

	return(UIRmlViewClass::Action_For(name));
}


bool UIDisconnectView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("disconnect");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIDisconnectSlot>(constructor)) {
		handle.RegisterMember("Name", &UIDisconnectSlot::Name);
		handle.RegisterMember("Left", &UIDisconnectSlot::Left);
		handle.RegisterMember("Top", &UIDisconnectSlot::Top);
		handle.RegisterMember("Index", &UIDisconnectSlot::Index);
		handle.RegisterMember("BarWidth", &UIDisconnectSlot::BarWidth);
		handle.RegisterMember("BarColour", &UIDisconnectSlot::BarColour);
	}

	UI_Register_Array<std::vector<UIDisconnectSlot>>(constructor);
	UI_Register_Array<std::vector<Rml::String>>(constructor);

	constructor.Bind("Slots", &Box.Slots);
	constructor.Bind("Messages", &Box.Messages);
	constructor.Bind("Time", &Box.Time);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIDisconnectView::Bind(void)
{
	Attach_Actions();
	Model.DirtyAllVariables();
	return(true);
}


void UIDisconnectView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("disconnect");
	}

	Model = Rml::DataModelHandle();
}


void UIDisconnectView::Dirty(char const * name)
{
	if (Model) {
		Model.DirtyVariable(name);
	}
}


void UIDisconnectView::Scroll_To_End(void)
{
	if (Document == nullptr) {
		return;
	}

	Rml::Element * list = Document->GetElementById("messages");
	if (list != nullptr) {
		list->SetScrollTop(list->GetScrollHeight() - list->GetClientHeight());
	}
}


static std::unique_ptr<UIDisconnectPresenter> _Presenter;
static std::unique_ptr<UIDisconnectView> _View;

// Set when a trim asked for the newest line to be shown. The rows it names are laid out by
// the shell's next tick, so the scroll waits for the service after it.
static bool _ScrollPending = false;


bool UI_Disconnect_Open(void)
{
	if (!UI_Is_Initialized() || _View != nullptr) {
		return(false);
	}

	_Presenter = std::make_unique<UIDisconnectPresenter>();

	// The dialog named a button for each player the session held when it was created and
	// destroyed the rest.
	for (int index = 0; index < Session.Players.Count() && index < MAX_PLAYERS; index++) {
		UIDisconnectSlot slot;
		slot.Name = Session.Players[index]->Name;
		slot.Left = SLOT_LEFT[index];
		slot.Top = SLOT_TOP[index];
		slot.Index = index;
		slot.BarWidth = 0;
		slot.BarColour = "#00000000";
		_Presenter->Slots.push_back(slot);
	}

	_View = std::make_unique<UIDisconnectView>(*_Presenter);
	if (!_View->Prepare()) {
		_View.reset();
		_Presenter.reset();
		return(false);
	}

	_ScrollPending = false;
	_View->Show(true);
	DebugString("UI: the disconnect box is open\n");
	return(true);
}


void UI_Disconnect_Close(void)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->Discard();
	_View->Close();
	_View.reset();
	_Presenter.reset();
	_ScrollPending = false;
	DebugString("UI: the disconnect box is closed\n");
}


bool UI_Disconnect_Is_Open(void)
{
	return(_View != nullptr);
}


void UI_Disconnect_Set_Time(char const * text)
{
	if (_View == nullptr) {
		return;
	}

	Rml::String const time = (text != nullptr) ? text : "";
	if (time == _Presenter->Time) {
		return;
	}

	_Presenter->Time = time;
	_View->Dirty("Time");
}


void UI_Disconnect_Add_Message(char const * text)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->Messages.push_back((text != nullptr) ? text : "");
	_View->Dirty("Messages");
}


void UI_Disconnect_Trim_Messages(void)
{
	if (_View == nullptr) {
		return;
	}

	std::vector<Rml::String> & messages = _Presenter->Messages;
	if ((int)messages.size() > MESSAGE_LIMIT) {
		messages.erase(messages.begin());
		_View->Dirty("Messages");
	}

	_ScrollPending = true;
}


void UI_Disconnect_Set_Bar(int slot, int share, int red, int green, int blue)
{
	if (_View == nullptr || slot < 0 || slot >= (int)_Presenter->Slots.size()) {
		return;
	}

	UIDisconnectSlot & bar = _Presenter->Slots[slot];

	int width = 0;
	char colour[16] = "#00000000";

	if (share >= 0) {
		width = std::max(BAR_MINIMUM, share * BAR_WIDTH / 100);
		std::snprintf(colour, sizeof(colour), "#%02x%02x%02xff", red & 0xFF, green & 0xFF, blue & 0xFF);
	}

	if (bar.BarWidth == width && bar.BarColour == colour) {
		return;
	}

	bar.BarWidth = width;
	bar.BarColour = colour;
	_View->Dirty("Slots");
}


void UI_Disconnect_Service(void (*handler)(UIIntent const & intent))
{
	if (_View == nullptr) {
		return;
	}

	if (_ScrollPending) {
		_ScrollPending = false;
		_View->Scroll_To_End();
	}

	_Presenter->Handler = handler;
	_Presenter->Drain();
	_Presenter->Handler = nullptr;
}
