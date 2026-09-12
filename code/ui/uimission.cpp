/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The load, save and delete screens. What they preserve is LoadOptionsClass::Dialog: the rows
// Fill_List builds and the one it selects, the description the save field is primed with, the
// file a save goes into, the confirmations and their defaults, and the state the dialog closes
// in, which is what the caller is answered with.

#include "always.h"

#include "uimission.h"

#include "_rules.h"
#include "data.h"
#include "dialogresult.h"
#include "gamedirs.h"
#include "globals.h"
#include "house.h"
#include "houstype.h"
#include "init.h"
#include "language/language.h"
#include "loaddlg.h"
#include "msgbox.h"
#include "platform/file.h"
#include "platform/filetime.h"
#include "rules.h"
#include "scenario.h"
#include "session.h"
#include "sun.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uiscreens.h"
#include "utf8.h"
#include "voc.h"
#include "win.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>


enum
{
	UI_MISSION_ACCEPT = UI_ACTION_ACCEPT,
	UI_MISSION_CANCEL = UI_ACTION_CANCEL,

	UI_MISSION_PRESS = UI_ACTION_SCREEN,
	UI_MISSION_SELECT,
	UI_MISSION_LIST,
	UI_MISSION_CHOOSE,
	UI_MISSION_TEXT,
	UI_MISSION_EXPORT,
	UI_MISSION_IMPORT,
};


// What the description field is asked to do with its focus and selection. The owner-draw edit
// cleared its selection whenever it took the focus, so a field primed before the dialog was
// active showed its caret at the end, while one primed by a click had its text selected.
enum UIMissionFieldRequest
{
	UI_MISSION_FIELD_CARET_END,
	UI_MISSION_FIELD_SELECT_ALL,
	UI_MISSION_FIELD_FOCUS,
};


// One row of the list, in the three columns the dialog added.
struct UIMissionRow
{
	Rml::String Description;
	Rml::String Date;
	Rml::String Time;
};


class UIMissionPresenter : public UIPresenterClass
{
	public:
		UIMissionPresenter(UIMissionFilesRequest const & request) : Request(request) {}
		~UIMissionPresenter(void) override;

		std::vector<UIMissionRow> Rows;
		int Selected = -1;
		bool AcceptEnabled = false;
		bool ExportEnabled = false;
		bool Transfer = false;

		// Moves whenever Rows is rebuilt, so the view rebuilds the list only then.
		int RowsSerial = 0;

		// The description field. A programmatic change moves the serial; typing only updates
		// the text, since the field already shows it.
		Rml::String Field;
		UIMissionFieldRequest FieldRequest = UI_MISSION_FIELD_CARET_END;
		int FieldSerial = 0;

		// Hides the screen around a load, as the dialog was hidden.
		std::function<void(bool)> Show_View;

		void Refresh(void) override;

		// Runs the object's callback, as the dialog's loop did on every pass.
		void Service(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Fill(UIMissionFieldRequest request);
		void Clear(void);
		void Rebuild_Rows(void);
		void Selection_Changed(UIMissionFieldRequest request);
		FileEntryClass * Selected_Entry(void) const;

		void Accept(void);
		void Accept_Load(FileEntryClass & entry);
		void Accept_Save(FileEntryClass * entry);
		void Accept_Delete(FileEntryClass & entry);

		UIMissionFilesRequest const & Request;
		std::vector<FileEntryClass *> Entries;
};


UIMissionPresenter::~UIMissionPresenter(void)
{
	Clear();
}


void UIMissionPresenter::Clear(void)
{
	for (FileEntryClass * entry : Entries) {
		delete entry;
	}
	Entries.clear();
}


static int __cdecl Compare_Entries(void const * p1, void const * p2)
{
	FileEntryClass const * const first = *(FileEntryClass * const *)p1;
	FileEntryClass const * const second = *(FileEntryClass * const *)p2;

	if (first->DateTime < second->DateTime) return(1);
	if (second->DateTime < first->DateTime) return(-1);
	return(0);
}


/// <summary>
/// Rebuilds the list the way Fill_List does, selects the row it selects, and raises the
/// notification its selection raised.
/// </summary>
void UIMissionPresenter::Fill(UIMissionFieldRequest request)
{
	Clear();

	if (Request.Style == LoadOptionsClass::SAVE) {
		FileEntryClass * slot = new FileEntryClass;
		std::strcpy(slot->Descr, Fetch_String(TXT_EMPTY_SLOT));
		if (PlayerPtr != NULL) {
			slot->Scenario = Scen->Scenario;
			slot->House = Scen->PlayerHouse;
			slot->Num = Scen->Campaign;
			std::strcpy(slot->PlayerName, PlayerPtr->Class->GivenName);
		} else {
			slot->Scenario = 0;
			slot->House = (HousesType)Session.House;
			slot->Num = -1;
			std::strcpy(slot->PlayerName, Session.Handle);
		}
		slot->DateTime = File_Time_Now();
		slot->Type = Session.Type;
		slot->Valid = false;
		Entries.push_back(slot);
	}

	char pattern[128];
	std::snprintf(pattern, sizeof(pattern), "*.%3s", Request.Extension);

	std::vector<PlatformFileInfoType> found;

	for (PlatformFileInfoType & record : Platform_Find_Files(Saved_Game_Name(pattern).c_str())) {
		if (record.IsDirectory || record.IsHidden) {
			continue;
		}
		found.push_back(std::move(record));
	}

	std::sort(found.begin(), found.end(), [](PlatformFileInfoType const & a, PlatformFileInfoType const & b) {
		return(b.Modified < a.Modified);
	});
	if (found.size() > Request.ScanLimit) {
		found.resize(Request.ScanLimit);
	}

	// An entry a read refused is handed to the next read rather than replaced, as Fill_List
	// did, so a derived Read_File sees the same object it did there.
	FileEntryClass * entry = nullptr;
	for (PlatformFileInfoType const & file : found) {
		if (entry == nullptr) {
			entry = new FileEntryClass;
		}
		if (Request.Options->Read_File(entry, &file)) {
			Entries.push_back(entry);
			entry = nullptr;
		}
	}
	delete entry;

	if (!Entries.empty()) {
		std::qsort(Entries.data(), Entries.size(), sizeof(FileEntryClass *), Compare_Entries);
	}

	Rebuild_Rows();

	Selected = -1;
	if (!Entries.empty()) {
		if (Request.Style == LoadOptionsClass::LOAD) {
			for (int index = 0; index < (int)Entries.size(); index++) {
				if (Entries[index]->Valid) {
					Selected = index;
					break;
				}
			}
		} else {
			Selected = 0;
		}

		// The owner-draw list told the dialog about a selection it made itself.
		Selection_Changed(request);
	}
}


void UIMissionPresenter::Rebuild_Rows(void)
{
	Rows.clear();

	// The dialog also asked for a '*' beside a save of any other kind of game, in a column
	// it never added, so no row was ever marked.
	for (FileEntryClass const * entry : Entries) {
		UIMissionRow row;
		row.Description = entry->Descr;

		CalendarTimeType local;

		if (entry->DateTime.High() != 0xFFFFFFFF && entry->DateTime.Low() != 0xFFFFFFFF
			&& Local_Calendar_Time(entry->DateTime, local)) {

			// The C locale's short date, the same on every target, which fits the column the
			// list was laid out for where a four-digit year does not.
			std::tm parts = {};
			parts.tm_year = local.Year - 1900;
			parts.tm_mon = local.Month - 1;
			parts.tm_mday = local.Day;
			parts.tm_hour = local.Hour;
			parts.tm_min = local.Minute;
			parts.tm_sec = local.Second;
			parts.tm_wday = local.DayOfWeek;

			char buffer[32];
			if (std::strftime(buffer, sizeof(buffer), "%m/%d/%y", &parts) != 0) {
				row.Date = buffer;
			}
			if (std::strftime(buffer, sizeof(buffer), "%H:%M", &parts) != 0) {
				row.Time = buffer;
			}
		}

		Rows.push_back(row);
	}

	RowsSerial++;
}


FileEntryClass * UIMissionPresenter::Selected_Entry(void) const
{
	if (Selected < 0 || Selected >= (int)Entries.size()) {
		return(nullptr);
	}
	return(Entries[Selected]);
}


// What the dialog procedures did on a list notification: the save dialog copied the picked
// game's description into its field, and the browser build's export follows the pick.
void UIMissionPresenter::Selection_Changed(UIMissionFieldRequest request)
{
	FileEntryClass const * const entry = Selected_Entry();

	ExportEnabled = (entry != nullptr && entry->Valid && entry->Filename[0] != '\0');

	if (Request.Style != LoadOptionsClass::SAVE || entry == nullptr) {
		return;
	}

	if (entry->Valid) {
		Field = entry->Descr;
	} else if (Request.Description != nullptr) {
		Field = Request.Description;
	}

	FieldRequest = request;
	FieldSerial++;
}


void UIMissionPresenter::Refresh(void)
{
#if defined(__EMSCRIPTEN__)
	Transfer = (Request.Style != LoadOptionsClass::WWDELETE);
#endif

	Fill(UI_MISSION_FIELD_CARET_END);
	AcceptEnabled = !Entries.empty();
}


void UIMissionPresenter::Service(void)
{
	if (Request.Options->Callback != nullptr) {
		Request.Options->Callback();
	}
}


void UIMissionPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_MISSION_SELECT:
			if (intent.Identity >= 0 && intent.Identity < (int)Entries.size()) {
				Selected = intent.Identity;
			}
			break;

		case UI_MISSION_LIST:
			// A press anywhere in the list clicked and notified, whether or not it landed
			// on a row.
			Sound_Effect(Rule->GenericClick);
			Selection_Changed(UI_MISSION_FIELD_SELECT_ALL);
			break;

		case UI_MISSION_CHOOSE:
			// Only the load dialog acted on a double click.
			if (Request.Style == LoadOptionsClass::LOAD && !Entries.empty()) {
				Accept();
			}
			break;

		case UI_MISSION_TEXT:
			Field = intent.Text;
			break;

		case UI_MISSION_ACCEPT:
		case UI_MISSION_PRESS:
			// Enter reached the dialog as IDOK even with the button disabled, and with an
			// empty list that closed the dialog as though a game had been loaded. Enter is
			// held to what the button allows.
			if (AcceptEnabled) {
				Accept();
			}
			break;

		case UI_MISSION_CANCEL:
			Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
			break;

		case UI_MISSION_EXPORT: {
			FileEntryClass const * const entry = Selected_Entry();
			if (ExportEnabled && entry != nullptr && Request.Export) {
				Request.Export(entry->Filename);
			}
			break;
		}

		case UI_MISSION_IMPORT:
			if (Request.Import && Request.Import()) {
				Fill(UI_MISSION_FIELD_SELECT_ALL);
				AcceptEnabled = !Entries.empty();
			}
			break;

		default:
			break;
	}
}


/// <summary>
/// Acts on the picked row as the dialog's loop did once its state read IDOK. A choice the
/// action refuses leaves the screen up, as the dialog went back to pending.
/// </summary>
void UIMissionPresenter::Accept(void)
{
	FileEntryClass * const entry = Selected_Entry();

	if (entry == nullptr) {
		Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
		return;
	}

	switch (Request.Style) {
		case LoadOptionsClass::LOAD:
			Accept_Load(*entry);
			break;

		case LoadOptionsClass::SAVE:
			Accept_Save(entry);
			break;

		case LoadOptionsClass::WWDELETE:
			Accept_Delete(*entry);
			break;

		default:
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;
	}
}


void UIMissionPresenter::Accept_Load(FileEntryClass & entry)
{
	if (entry.Num != -1) {
		Init_Campaigns();
	}

	if (Show_View) {
		Show_View(false);
	}

	if (!Request.Options->Load_File(entry.Filename)) {
		WWMessageBox().Process(TXT_ERROR_LOADING_GAME, TXT_OK, TXT_NONE, TXT_NONE);
		if (Show_View) {
			Show_View(true);
		}
		return;
	}

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


void UIMissionPresenter::Accept_Save(FileEntryClass * entry)
{
	char buffer[256];

	// The dialog read the field through GetWindowText with room for DESCRIP_MAX+36
	// characters, the terminator included.
	std::size_t const length = UTF8::Boundary_Before(Field.c_str(), DESCRIP_MAX + 35);
	std::memcpy(buffer, Field.data(), length);
	buffer[length] = '\0';

	if (std::strlen(buffer) == 0) {
		WWMessageBox().Process(TXT_MUSTENTER_DESCRIPTION, TXT_OK, TXT_NONE, TXT_NONE);
		FieldRequest = UI_MISSION_FIELD_FOCUS;
		FieldSerial++;
		return;
	}

	char test_filename[256];
	char const * filename = nullptr;

	if (entry != nullptr && entry->Valid) {
		filename = entry->Filename;
	} else {
		Request.Options->Pick_Filename(test_filename);
		filename = test_filename;
	}

	bool const exists = Request.Saved_Game_Exists != nullptr && Request.Saved_Game_Exists(filename);
	if (exists && WWMessageBox()._Process(TXT_CONFIRM_SAVE, 1, TXT_YES, TXT_NO, TXT_NONE)) {
		return;
	}

	if (!Request.Options->Save_File(filename, buffer)) {
		WWMessageBox().Process(TXT_ERROR_SAVING_GAME, TXT_OK, TXT_NONE, TXT_NONE);
		return;
	}

	int const confirmation = Request.Save_Confirmation ? Request.Save_Confirmation() : TXT_NONE;
	if (confirmation != TXT_NONE) {
		WWMessageBox().Process(confirmation, TXT_OK, TXT_NONE, TXT_NONE);
	}
	if (Request.Description != nullptr) {
		std::strcpy(Request.Description, buffer);
	}

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


void UIMissionPresenter::Accept_Delete(FileEntryClass & entry)
{
	char buffer[256];
	std::snprintf(buffer, sizeof(buffer), "%s\n%s", Fetch_String(TXT_DELETE_FILE_QUERY), entry.Descr);

	if (WWMessageBox()._Process(buffer, 1, TXT_YES, TXT_NO, TXT_NONE)) {
		return;
	}

	Request.Options->Delete_File(entry.Filename);

	// The row goes whether or not the file did, and the first row is picked again.
	FileEntryClass * const gone = Entries[Selected];
	Entries.erase(Entries.begin() + Selected);
	delete gone;
	Rebuild_Rows();

	if (!Entries.empty()) {
		Selected = 0;
		Selection_Changed(UI_MISSION_FIELD_SELECT_ALL);
		return;
	}

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


class UIMissionView : public UIRmlViewClass
{
	public:
		UIMissionView(UIMissionPresenter & presenter, char const * document) :
			UIRmlViewClass(presenter, document), Mission(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);
		void Apply_Field(void);

		UIMissionPresenter & Mission;
		Rml::DataModelHandle Model;
		Rml::ElementFormControlInput * Field = nullptr;

		bool Pushed = false;
		int RowsSerial = -1;
		int FieldSerial = -1;
};


void UIMissionView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	int const identity = arguments.size() > 1 ? arguments[1].Get<int>() : 0;
	UIIntent intent;

	if (name == "accept") {
		intent.Action = UI_MISSION_PRESS;
	} else if (name == "cancel") {
		intent.Action = UI_MISSION_CANCEL;
	} else if (name == "select") {
		intent.Action = UI_MISSION_SELECT;
		intent.Identity = identity;
	} else if (name == "list") {
		intent.Action = UI_MISSION_LIST;
	} else if (name == "choose") {
		intent.Action = UI_MISSION_CHOOSE;
	} else if (name == "text") {
		// The new text is read from the event rather than from the field; see UISoundView.
		intent.Action = UI_MISSION_TEXT;
		intent.Text = event.GetParameter<Rml::String>("value", Mission.Field);
	} else if (name == "export") {
		intent.Action = UI_MISSION_EXPORT;
	} else if (name == "import") {
		intent.Action = UI_MISSION_IMPORT;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIMissionView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("mission");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIMissionRow>(constructor)) {
		handle.RegisterMember("Description", &UIMissionRow::Description);
		handle.RegisterMember("Date", &UIMissionRow::Date);
		handle.RegisterMember("Time", &UIMissionRow::Time);
	}

	UI_Register_Array<std::vector<UIMissionRow>>(constructor);

	constructor.Bind("Rows", &Mission.Rows);
	constructor.Bind("Selected", &Mission.Selected);
	constructor.Bind("AcceptEnabled", &Mission.AcceptEnabled);
	constructor.Bind("ExportEnabled", &Mission.ExportEnabled);
	constructor.Bind("Transfer", &Mission.Transfer);

	constructor.BindEventCallback("act", &UIMissionView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIMissionView::Bind(void)
{
	Attach_Actions();

	// Only the save document has a field.
	Field = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(Document->GetElementById("description"));
	return(true);
}


void UIMissionView::Apply_Field(void)
{
	if (Field == nullptr || FieldSerial == Mission.FieldSerial) {
		return;
	}

	// Focus is taken only once the document is showing, since showing a modal document
	// focuses the document itself; the value can go in before that.
	Field->SetValue(Mission.Field);
	if (!Document->IsVisible()) {
		return;
	}

	FieldSerial = Mission.FieldSerial;

	// Taking the focus clears the selection, so the selection is set after it.
	Field->Focus();

	switch (Mission.FieldRequest) {
		case UI_MISSION_FIELD_CARET_END: {
			int const end = (int)Rml::StringUtilities::LengthUTF8(Mission.Field);
			Field->SetSelectionRange(end, end);
			break;
		}

		case UI_MISSION_FIELD_SELECT_ALL:
			Field->Select();
			break;

		default:
			break;
	}
}


void UIMissionView::Sync(void)
{
	if (!Model) {
		return;
	}

	// The first pass puts the read state into the document before it is shown. After that
	// only what an intent can change is pushed, and the list only when it was rebuilt.
	if (!Pushed) {
		Pushed = true;
		RowsSerial = Mission.RowsSerial;
		Model.DirtyAllVariables();
		Apply_Field();
		return;
	}

	if (RowsSerial != Mission.RowsSerial) {
		RowsSerial = Mission.RowsSerial;
		Model.DirtyVariable("Rows");
	}

	Model.DirtyVariable("Selected");
	Model.DirtyVariable("AcceptEnabled");
	Model.DirtyVariable("ExportEnabled");

	Apply_Field();
}


void UIMissionView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("mission");
	}

	Model = Rml::DataModelHandle();
	Field = nullptr;
}


int UI_Mission_Files_Screen(UIMissionFilesRequest const & request)
{
	char const * document = nullptr;

	switch (request.Style) {
		case LoadOptionsClass::LOAD:
			document = "load.rml";
			break;

		case LoadOptionsClass::SAVE:
			document = "save.rml";
			break;

		case LoadOptionsClass::WWDELETE:
			document = "delete.rml";
			break;

		default:
			return(UI_MISSION_FILES_UNAVAILABLE);
	}

	if (request.Options == nullptr) {
		return(UI_MISSION_FILES_UNAVAILABLE);
	}

	UIMissionPresenter presenter(request);
	UIMissionView view(presenter, document);

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
			return(UI_MISSION_FILES_UNAVAILABLE);

		// The dialog's loop closed when the session ended under it.
		case UI_RESULT_SESSION_ENDED:
			return(DIALOG_CANCEL);

		default:
			return(result.Code);
	}
}


// Registered through the dialog drivers rather than through the screen, so a run exercises
// the path a caller takes, the selector included.
static bool Open_Load_Dialog(void)
{
	LoadOptionsClass().Load();
	return(true);
}


// Primed as the in-game options prime it.
static bool Open_Save_Dialog(void)
{
	char description[512];
	description[0] = '\0';
	if (Scen != nullptr) {
		UTF8::Copy(description, Scen->Description);
	}

	LoadOptionsClass().Save(description);
	return(true);
}


static bool Open_Delete_Dialog(void)
{
	LoadOptionsClass().Delete();
	return(true);
}


static UIScreenRegistration _RegisterLoad("mission-load", Open_Load_Dialog);
static UIScreenRegistration _RegisterSave("mission-save", Open_Save_Dialog);
static UIScreenRegistration _RegisterDelete("mission-delete", Open_Delete_Dialog);
