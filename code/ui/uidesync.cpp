/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uidesync.h"

#include "misc.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uishell.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>


// The template is 540 by 429 logical pixels. When the frame is shorter, the dialog gave up to
// two thirds of its 98 pixel chat list and moved everything below the list up with it.
static int const BOX_HEIGHT = 429;
static int const CHAT_HEIGHT = 98;

// The countdown's group box is 224 pixels wide, and the painter never drew the bar narrower
// than 6.
static int const BAR_WIDTH = 224;
static int const BAR_MINIMUM = 6;


struct UIDesyncRow
{
	Rml::String Name;
	bool Host = false;
	Rml::String Status;
	Rml::String StatusColour;
};


static Rml::String Colour(int red, int green, int blue)
{
	char text[16];
	std::snprintf(text, sizeof(text), "#%02x%02x%02xff", red & 0xFF, green & 0xFF, blue & 0xFF);
	return(text);
}


class UIDesyncPresenter : public UIPresenterClass
{
	public:
		bool Host = false;
		int Shrink = 0;
		std::vector<UIDesyncRow> Players;
		std::vector<Rml::String> Chat;
		bool LoadEnabled = true;
		bool ContinueEnabled = true;
		bool QuitEnabled = true;
		bool Countdown = false;
		Rml::String CountdownText;
		int BarWidth = 0;
		Rml::String BarColour;
		bool Suspended = false;

		void (*Handler)(UIIntent const & intent) = nullptr;

	protected:
		void Execute(UIIntent const & intent) override;
};


void UIDesyncPresenter::Execute(UIIntent const & intent)
{
	// A disabled dialog took no input, but its chat field still reported losing the focus.
	if (Suspended && intent.Action != UI_DESYNC_CHAT_FOCUS) {
		return;
	}

	switch (intent.Action) {
		case UI_DESYNC_LOAD:
			if (!Host || !LoadEnabled) {
				return;
			}
			break;

		case UI_DESYNC_CONTINUE:
			if (!Host || !ContinueEnabled) {
				return;
			}
			break;

		case UI_DESYNC_QUIT:
			if (!QuitEnabled) {
				return;
			}
			break;

		case UI_ACTION_ACCEPT:
		case UI_DESYNC_CHAT_FOCUS:
			break;

		// Escape reached the dialog as IDCANCEL, which it did not handle.
		default:
			return;
	}

	if (Handler != nullptr) {
		Handler(intent);
	}
}


class UIDesyncView : public UIRmlViewClass
{
	public:
		UIDesyncView(UIDesyncPresenter & presenter) :
			UIRmlViewClass(presenter, "desync.rml"), Box(presenter) {}

		void Dirty(char const * name);
		Rml::ElementFormControlInput * Field(void) const;
		void Scroll_Chat_To_End(void);
		void Focus_Box(void);

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;
		int Action_For(char const * name) const override;

	private:
		void On_Chat_Focus(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIDesyncPresenter & Box;
		Rml::DataModelHandle Model;
};


int UIDesyncView::Action_For(char const * name) const
{
	if (name != nullptr) {
		Rml::String const action(name);

		if (action == "load") {
			return(UI_DESYNC_LOAD);
		}
		if (action == "continue") {
			return(UI_DESYNC_CONTINUE);
		}
		if (action == "quit") {
			return(UI_DESYNC_QUIT);
		}
	}

	return(UIRmlViewClass::Action_For(name));
}


void UIDesyncView::On_Chat_Focus(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const & arguments)
{
	UIIntent intent;
	intent.Action = UI_DESYNC_CHAT_FOCUS;
	intent.Identity = arguments.empty() ? 0 : arguments[0].Get<int>();
	Presenter.Queue(intent);
}


bool UIDesyncView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("desync");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIDesyncRow>(constructor)) {
		handle.RegisterMember("Name", &UIDesyncRow::Name);
		handle.RegisterMember("Host", &UIDesyncRow::Host);
		handle.RegisterMember("Status", &UIDesyncRow::Status);
		handle.RegisterMember("StatusColour", &UIDesyncRow::StatusColour);
	}

	UI_Register_Array<std::vector<UIDesyncRow>>(constructor);
	UI_Register_Array<std::vector<Rml::String>>(constructor);

	constructor.Bind("Host", &Box.Host);
	constructor.Bind("Shrink", &Box.Shrink);
	constructor.Bind("Players", &Box.Players);
	constructor.Bind("Chat", &Box.Chat);
	constructor.Bind("LoadEnabled", &Box.LoadEnabled);
	constructor.Bind("ContinueEnabled", &Box.ContinueEnabled);
	constructor.Bind("QuitEnabled", &Box.QuitEnabled);
	constructor.Bind("Countdown", &Box.Countdown);
	constructor.Bind("CountdownText", &Box.CountdownText);
	constructor.Bind("BarWidth", &Box.BarWidth);
	constructor.Bind("BarColour", &Box.BarColour);

	constructor.BindEventCallback("chatfocus", &UIDesyncView::On_Chat_Focus, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIDesyncView::Bind(void)
{
	Attach_Actions();
	Model.DirtyAllVariables();
	return(true);
}


void UIDesyncView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("desync");
	}

	Model = Rml::DataModelHandle();
}


void UIDesyncView::Dirty(char const * name)
{
	if (Model) {
		Model.DirtyVariable(name);
	}
}


Rml::ElementFormControlInput * UIDesyncView::Field(void) const
{
	if (Document == nullptr) {
		return(nullptr);
	}

	return(rmlui_dynamic_cast<Rml::ElementFormControlInput *>(Document->GetElementById("chat")));
}


void UIDesyncView::Scroll_Chat_To_End(void)
{
	if (Document == nullptr) {
		return;
	}

	Rml::Element * list = Document->GetElementById("chatlist");
	if (list != nullptr) {
		list->SetScrollTop(list->GetScrollHeight() - list->GetClientHeight());
	}
}


void UIDesyncView::Focus_Box(void)
{
	if (Document != nullptr) {
		Document->Focus();
	}
}


static std::unique_ptr<UIDesyncPresenter> _Presenter;
static std::unique_ptr<UIDesyncView> _View;

// Set when the chat list changed. Its rows are laid out by the shell's next tick, so the
// scroll waits for the service after it.
static bool _ScrollPending = false;


bool UI_Desync_Open(bool host)
{
	if (!UI_Is_Initialized() || _View != nullptr) {
		return(false);
	}

	_Presenter = std::make_unique<UIDesyncPresenter>();
	_Presenter->Host = host;
	_Presenter->BarColour = Colour(0, 0, 0);

	// The wait box's Quit starts disabled; the decision box's buttons start enabled.
	_Presenter->QuitEnabled = host;

	if (VideoModeHeight > 0 && VideoModeHeight < BOX_HEIGHT) {
		_Presenter->Shrink = std::min(BOX_HEIGHT - VideoModeHeight, CHAT_HEIGHT * 2 / 3);
	}

	_View = std::make_unique<UIDesyncView>(*_Presenter);
	if (!_View->Prepare()) {
		_View.reset();
		_Presenter.reset();
		return(false);
	}

	_ScrollPending = false;
	_View->Show(true);
	return(true);
}


void UI_Desync_Close(void)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->Discard();
	_View->Close();
	_View.reset();
	_Presenter.reset();
	_ScrollPending = false;
}


bool UI_Desync_Is_Open(void)
{
	return(_View != nullptr);
}


void UI_Desync_Set_Players(std::vector<UIDesyncPlayer> const & players)
{
	if (_View == nullptr) {
		return;
	}

	std::vector<UIDesyncRow> rows;
	for (UIDesyncPlayer const & player : players) {
		UIDesyncRow row;
		row.Name = player.Name;
		row.Host = player.Host;
		row.Status = player.Status;
		row.StatusColour = Colour(player.Red, player.Green, player.Blue);
		rows.push_back(row);
	}

	_Presenter->Players = std::move(rows);
	_View->Dirty("Players");
}


void UI_Desync_Set_Chat(std::vector<std::string> const & lines)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->Chat.clear();
	for (std::string const & line : lines) {
		_Presenter->Chat.push_back(line);
	}

	_View->Dirty("Chat");
	_ScrollPending = true;
}


void UI_Desync_Enable(int action, bool enabled)
{
	if (_View == nullptr) {
		return;
	}

	switch (action) {
		case UI_DESYNC_LOAD:
			_Presenter->LoadEnabled = enabled;
			_View->Dirty("LoadEnabled");
			break;

		case UI_DESYNC_CONTINUE:
			_Presenter->ContinueEnabled = enabled;
			_View->Dirty("ContinueEnabled");
			break;

		case UI_DESYNC_QUIT:
			_Presenter->QuitEnabled = enabled;
			_View->Dirty("QuitEnabled");
			break;

		default:
			break;
	}
}


void UI_Desync_Show_Countdown(void)
{
	if (_View == nullptr || _Presenter->Countdown) {
		return;
	}

	_Presenter->Countdown = true;
	_View->Dirty("Countdown");
}


void UI_Desync_Set_Countdown_Text(char const * text)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->CountdownText = (text != nullptr) ? text : "";
	_View->Dirty("CountdownText");
}


void UI_Desync_Set_Countdown_Bar(int remaining, int total, int red, int green, int blue)
{
	if (_View == nullptr || total <= 0) {
		return;
	}

	int const width = std::max(BAR_MINIMUM, BAR_WIDTH * remaining / total);
	Rml::String const colour = Colour(red, green, blue);

	if (width == _Presenter->BarWidth && colour == _Presenter->BarColour) {
		return;
	}

	_Presenter->BarWidth = width;
	_Presenter->BarColour = colour;
	_View->Dirty("BarWidth");
	_View->Dirty("BarColour");
}


void UI_Desync_Set_Chat_Text(char const * text)
{
	if (_View == nullptr) {
		return;
	}

	Rml::ElementFormControlInput * field = _View->Field();
	if (field != nullptr) {
		field->SetValue((text != nullptr) ? text : "");
	}
}


std::string UI_Desync_Chat_Text(void)
{
	if (_View == nullptr) {
		return(std::string());
	}

	Rml::ElementFormControlInput * field = _View->Field();
	if (field == nullptr) {
		return(std::string());
	}

	return(field->GetValue());
}


void UI_Desync_Focus_Chat(void)
{
	if (_View == nullptr) {
		return;
	}

	Rml::ElementFormControlInput * field = _View->Field();
	if (field != nullptr) {
		field->Focus();
	}
}


void UI_Desync_Focus_Box(void)
{
	if (_View != nullptr) {
		_View->Focus_Box();
	}
}


void UI_Desync_Suspend(bool suspended)
{
	if (_View == nullptr) {
		return;
	}

	_Presenter->Suspended = suspended;
}


void UI_Desync_Service(void (*handler)(UIIntent const & intent))
{
	if (_View == nullptr) {
		return;
	}

	if (_ScrollPending) {
		_ScrollPending = false;
		_View->Scroll_Chat_To_End();
	}

	_Presenter->Handler = handler;
	_Presenter->Drain();
	_Presenter->Handler = nullptr;
}
