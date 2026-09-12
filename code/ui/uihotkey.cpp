/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The keyboard configuration screen. What it has to preserve is how the dialog changed the
// live hotkey index: Assign rebinds the selected command at once, an empty key box unbinds
// it, OK writes the index to KEYBOARD.INI, and Cancel and Reset All reload it from the files.
//
// The dialog's hot key box was a window control that took every key while it had the focus.
// The shell hands a document only the keys it names, and passes the rest to the keyboard
// queue, so while the box here has the focus it takes its keys from that queue instead. The
// queue carries the same key codes and modifier bits the box answered with.

#include "always.h"

#include "uihotkey.h"

#include "_command.h"
#include "_keyboar.h"
#include "_rules.h"
#include "ccfile.h"
#include "ccini.h"
#include "cdfile.h"
#include "command.h"
#include "dbgprint.h"
#include "dialogresult.h"
#include "globals.h"
#include "goptions.h"
#include "index.h"
#include "init.h"
#include "keyboard.h"
#include "keyname.h"
#include "language/language.h"
#include "msgbox.h"
#include "rules.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uiscreens.h"
#include "vector.h"
#include "voc.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>


enum
{
	UI_HOTKEY_CATEGORY = UI_ACTION_SCREEN,
	UI_HOTKEY_SELECT,
	UI_HOTKEY_ASSIGN,
	UI_HOTKEY_RESET,
};


// The key box's own answer: the virtual key in the low byte and the held modifiers above it.
static unsigned short const HOTKEY_MASK = 0xFF | WWKEY_SHIFT_BIT | WWKEY_CTRL_BIT | WWKEY_ALT_BIT;


static Rml::String From_Game_Text(char const * text)
{
	return(std::string(text != nullptr ? text : ""));
}


static Rml::String Hotkey_Name(int key)
{
	char buffer[64];
	Build_Hotkey_String((KeyNumType)key, buffer);
	return(From_Game_Text(buffer));
}


// The two lists were sorted by the window system, which compares without regard to case.
static bool Sorts_Before(std::string const & left, std::string const & right)
{
	return(stricmp(left.c_str(), right.c_str()) < 0);
}


// One row of the drop list or the command list.
struct UIHotkeyRow
{
	Rml::String Name;
};


class UIHotkeyPresenter : public UIPresenterClass
{
	public:
		std::vector<UIHotkeyRow> Categories;
		int Category = 0;

		std::vector<UIHotkeyRow> Commands;
		int Selected = -1;

		Rml::String Description;
		Rml::String CurrentShortcut;
		Rml::String AssignedTo;

		// The key held in the key box, in the box's own form, and how it reads.
		int HotKey = 0;
		Rml::String HotKeyText;

		// Set when the dialog would have moved the focus to the key box, and whether the key
		// box has the focus now, which only the view can tell.
		bool FocusKeyBox = false;
		bool KeyBoxFocused = false;

		// Set when a list was rebuilt, so the view pushes it and the drop list's choice
		// with it rather than every pass.
		bool CategoriesChanged = false;
		bool CommandsChanged = false;

		void Refresh(void) override;

		// Reads the keys the player pressed since the last pass. They reach the key box
		// only while it has the focus, as keys reached the dialog's box; the rest are
		// dropped, as they were when the dialog had the focus elsewhere.
		void Service(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Reinit(void);
		void Fill_Commands(void);
		void Show_Command(void);
		void Apply_Hotkey(void);
		void Capture(int key);
		void Accept(void);

		CommandClass const * Selected_Command(void) const;

		// The category names as the commands spell them, row for row with Categories.
		std::vector<std::string> CategoryNames;

		// The command each row of the list stands for, row for row.
		std::vector<CommandClass const *> CommandPointers;

		// The category the list was last filled for, so choosing the same one again does
		// not refill it, which is the test the dialog made.
		int FilledCategory = -1;
};


CommandClass const * UIHotkeyPresenter::Selected_Command(void) const
{
	if (Selected < 0 || Selected >= (int)CommandPointers.size()) {
		return(nullptr);
	}

	return(CommandPointers[Selected]);
}


// HKD_REINIT: every category once, the first chosen, and the list filled for it.
void UIHotkeyPresenter::Reinit(void)
{
	std::vector<std::string> categories;

	for (int index = 0; index < AllCommands.Count(); index++) {
		char const * category = AllCommands[index]->Get_Category();
		if (category == nullptr) {
			category = "";
		}

		// The dialog asked the combo box for an item starting with the name, without regard
		// to case, and added the name only when there was none.
		std::size_t const length = std::strlen(category);
		bool found = false;
		for (std::string const & existing : categories) {
			if (existing.size() >= length && strnicmp(existing.c_str(), category, length) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			categories.push_back(category);
		}
	}

	std::stable_sort(categories.begin(), categories.end(), Sorts_Before);

	CategoryNames = categories;
	Categories.clear();
	for (std::string const & name : categories) {
		Categories.push_back(UIHotkeyRow{From_Game_Text(name.c_str())});
	}

	Category = 0;
	CategoriesChanged = true;
	Fill_Commands();
	FilledCategory = -1;
}


// HKD_FILL_COMMANDS: the commands of the chosen category, none of them selected. Only the
// description is cleared; the labels below the list keep what they last showed.
void UIHotkeyPresenter::Fill_Commands(void)
{
	if (Category == FilledCategory) {
		return;
	}

	FilledCategory = Category;

	std::string const category = (Category >= 0 && Category < (int)CategoryNames.size()) ? CategoryNames[Category] : "";

	std::vector<CommandClass const *> commands;
	for (int index = 0; index < AllCommands.Count(); index++) {
		CommandClass const * command = AllCommands[index];
		if (stricmp(command->Get_Category(), category.c_str()) == 0) {
			commands.push_back(command);
		}
	}

	std::stable_sort(commands.begin(), commands.end(), [](CommandClass const * left, CommandClass const * right) {
		return(Sorts_Before(left->Get_Display_Name(), right->Get_Display_Name()));
	});

	CommandPointers = commands;
	Commands.clear();
	for (CommandClass const * command : commands) {
		Commands.push_back(UIHotkeyRow{From_Game_Text(command->Get_Display_Name())});
	}

	Selected = -1;
	Description.clear();
	CommandsChanged = true;
}


// HKD_SHOW_COMMAND: the selected command's description and shortcut, and an empty key box.
// With nothing selected it changes nothing.
void UIHotkeyPresenter::Show_Command(void)
{
	CommandClass const * command = Selected_Command();
	if (command == nullptr) {
		return;
	}

	Description = From_Game_Text(command->Get_Description());

	int key = 0;
	for (int index = 0; index < HotkeyCommands.Count(); index++) {
		if (HotkeyCommands.Fetch_By_Position(index) == command) {
			key = HotkeyCommands.Fetch_ID_By_Position(index);
			break;
		}
	}

	CurrentShortcut = Hotkey_Name(key);

	HotKey = 0;
	HotKeyText = Hotkey_Name(HotKey);

	AssignedTo.clear();
}


// HKD_APPLY_HOTKEY: the command loses the key it had, and takes the one in the box from
// whichever command held it. An empty box leaves the command with no key at all.
void UIHotkeyPresenter::Apply_Hotkey(void)
{
	CommandClass const * command = Selected_Command();
	if (command == nullptr) {
		return;
	}

	for (int index = 0; index < HotkeyCommands.Count(); index++) {
		if (HotkeyCommands.Fetch_By_Position(index) == command) {
			HotkeyCommands.Remove_Index(HotkeyCommands.Fetch_ID_By_Position(index));
			break;
		}
	}

	if (HotKey != 0) {
		HotkeyCommands.Remove_Index(HotKey);
		HotkeyCommands.Add_Index(HotKey, command);
	}
}


// EN_CHANGE on the key box: the box shows the key and the label names the command that has
// it now.
void UIHotkeyPresenter::Capture(int key)
{
	HotKey = key;
	HotKeyText = Hotkey_Name(HotKey);

	char const * name = "";
	if (HotkeyCommands.Is_Present(HotKey)) {
		name = HotkeyCommands[HotKey]->Get_Display_Name();
		if (name == nullptr) {
			name = "";
		}
	}

	AssignedTo = From_Game_Text(name);
}


void UIHotkeyPresenter::Accept(void)
{
	CCINIClass ini;
	ini.Clear();

	for (int index = 0; index < HotkeyCommands.Count(); index++) {
		CommandClass const * command = HotkeyCommands.Fetch_By_Position(index);
		int const key = HotkeyCommands.Fetch_ID_By_Position(index);
		ini.Put_Int("Hotkey", command->Get_Unique_Name(), key);
	}

	CDFileClass file("Keyboard.ini");
	ini.Save(file, false);

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


void UIHotkeyPresenter::Refresh(void)
{
	HotKeyText = Hotkey_Name(HotKey);
	Reinit();
}


void UIHotkeyPresenter::Service(void)
{
	while (Keyboard->Check() != 0) {
		unsigned short const key = (unsigned short)Keyboard->Get();

		if (!KeyBoxFocused || (key & WWKEY_RLS_BIT) != 0) {
			continue;
		}

		// A mouse button or a modifier on its own is not a binding, and Tab and Escape went
		// to the dialog rather than to the box.
		unsigned int const code = key & 0xFF;
		if (code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON
			|| code == VK_SHIFT || code == VK_CONTROL || code == VK_MENU
			|| code == VK_TAB || code == VK_ESCAPE) {
			continue;
		}

		Capture(key & HOTKEY_MASK);
	}
}


void UIHotkeyPresenter::Execute(UIIntent const & intent)
{
	// The dialog's loop ended at its first answer, so nothing queued behind that answer is
	// acted on.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_HOTKEY_CATEGORY:
			if (intent.Identity >= 0 && intent.Identity < (int)Categories.size()) {
				Category = intent.Identity;
				Fill_Commands();
			}
			break;

		case UI_HOTKEY_SELECT:
			// The list clicked on a press, whether or not the row changed.
			Sound_Effect(Rule->GenericClick);
			if (intent.Identity >= 0 && intent.Identity < (int)Commands.size()) {
				Selected = intent.Identity;
				Show_Command();
				FocusKeyBox = true;
			}
			break;

		case UI_HOTKEY_ASSIGN:
			Apply_Hotkey();
			Show_Command();
			break;

		case UI_HOTKEY_RESET:
			if (WWMessageBox()._Process(TXT_RESET_HOTKEYS, DIALOG_OK, TXT_YES, TXT_NO, TXT_NONE, false) == 0) {
				DebugString("Deleting users KEYBOARD.INI\n");

				// Only the player's own file is discarded; the defaults a deployment ships
				// are what the reset falls back on.
				CCFileClass file("KEYBOARD.INI");
				file.Delete();
				Init_Hotkeys();
				Reinit();
			}
			break;

		case UI_ACTION_ACCEPT:
			Accept();
			break;

		case UI_ACTION_CANCEL:
			Init_Hotkeys();
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		default:
			break;
	}
}


class UIHotkeyView : public UIRmlViewClass
{
	public:
		UIHotkeyView(UIHotkeyPresenter & presenter) :
			UIRmlViewClass(presenter, "hotkey.rml"), Keys(presenter) {}

		void Sync(void) override;
		void ProcessEvent(Rml::Event & event) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);
		bool Key_Box_Focused(void) const;

		UIHotkeyPresenter & Keys;
		Rml::DataModelHandle Model;
		Rml::Element * KeyBox = nullptr;
		bool Pushed = false;
};


bool UIHotkeyView::Key_Box_Focused(void) const
{
	if (Document == nullptr || KeyBox == nullptr) {
		return(false);
	}

	for (Rml::Element * walk = Document->GetFocusLeafNode(); walk != nullptr; walk = walk->GetParentNode()) {
		if (walk == KeyBox) {
			return(true);
		}
	}

	return(false);
}


// Enter belongs to the key box while it has the focus, as the box asked the dialog for every
// key; it is left alone here so it reaches the keyboard queue and the box.
void UIHotkeyView::ProcessEvent(Rml::Event & event)
{
	if (event.GetId() == Rml::EventId::Keydown && Key_Box_Focused()) {
		int const key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
		if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
			return;
		}
	}

	UIRmlViewClass::ProcessEvent(event);
}


void UIHotkeyView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
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
	} else if (name == "assign") {
		intent.Action = UI_HOTKEY_ASSIGN;
	} else if (name == "reset") {
		intent.Action = UI_HOTKEY_RESET;
	} else if (name == "select") {
		intent.Action = UI_HOTKEY_SELECT;
		intent.Identity = arguments.size() > 1 ? arguments[1].Get<int>() : -1;
	} else if (name == "category") {
		// The new choice comes from the event, which RmlUi raises before the binding
		// stores it.
		intent.Action = UI_HOTKEY_CATEGORY;
		Rml::String const value = event.GetParameter<Rml::String>("value", "");
		if (value.empty()) {
			return;
		}
		intent.Identity = std::atoi(value.c_str());

		// RmlUi raises a change event for the choice the model pushes into the drop list,
		// too, and the dialog heard only the player's.
		if (intent.Identity == Keys.Category) {
			return;
		}
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIHotkeyView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("hotkey");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIHotkeyRow>(constructor)) {
		handle.RegisterMember("Name", &UIHotkeyRow::Name);
	}
	UI_Register_Array<std::vector<UIHotkeyRow>>(constructor);

	constructor.Bind("Categories", &Keys.Categories);
	constructor.Bind("Category", &Keys.Category);
	constructor.Bind("Commands", &Keys.Commands);
	constructor.Bind("Selected", &Keys.Selected);
	constructor.Bind("Description", &Keys.Description);
	constructor.Bind("CurrentShortcut", &Keys.CurrentShortcut);
	constructor.Bind("AssignedTo", &Keys.AssignedTo);
	constructor.Bind("HotKeyText", &Keys.HotKeyText);

	constructor.BindEventCallback("act", &UIHotkeyView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIHotkeyView::Bind(void)
{
	KeyBox = Document->GetElementById("keybox");
	Attach_Actions();
	return(true);
}


void UIHotkeyView::Sync(void)
{
	if (!Model) {
		return;
	}

	if (Keys.FocusKeyBox) {
		Keys.FocusKeyBox = false;
		if (KeyBox != nullptr) {
			KeyBox->Focus();
		}
	}

	Keys.KeyBoxFocused = Key_Box_Focused();

	if (!Pushed) {
		Pushed = true;
		Keys.CategoriesChanged = false;
		Keys.CommandsChanged = false;
		Model.DirtyAllVariables();
		return;
	}

	// The drop list's choice is pushed only with a rebuilt list: dirtying it every pass would
	// write it over a choice the drop list is making.
	if (Keys.CategoriesChanged) {
		Keys.CategoriesChanged = false;
		Model.DirtyVariable("Categories");
		Model.DirtyVariable("Category");
	}

	if (Keys.CommandsChanged) {
		Keys.CommandsChanged = false;
		Model.DirtyVariable("Commands");
	}

	Model.DirtyVariable("Selected");
	Model.DirtyVariable("Description");
	Model.DirtyVariable("CurrentShortcut");
	Model.DirtyVariable("AssignedTo");
	Model.DirtyVariable("HotKeyText");
}


void UIHotkeyView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("hotkey");
	}

	Model = Rml::DataModelHandle();
	KeyBox = nullptr;
}


bool UI_Hotkey_Screen(void)
{
	UIHotkeyPresenter presenter;
	UIHotkeyView view(presenter);

	UIResult const result = UI_Run_Modal(presenter, view);

	return(result.Type != UI_RESULT_FAILED);
}


// Registered through the dialog driver rather than through the screen, so a run exercises
// the path a caller takes, the selector included.
static bool Open_Hotkey_Dialog(void)
{
	Options.Hotkey_Dialog();
	return(true);
}


static UIScreenRegistration _Register("keyboard", Open_Hotkey_Dialog);
