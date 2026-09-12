/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/* $Header: /CounterStrike/LOADDLG.CPP 1     3/03/97 10:25a Joe_bostic $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : LOADDLG.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Maria Legg, Joe Bostic, Bill Randolph                        *
 *                                                                                             *
 *                   Start Date : March 19, 1995                                               *
 *                                                                                             *
 *                  Last Update : June 25, 1995 [JLB]                                          *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   LoadOptionsClass::LoadOptionsClass -- class constructor                                   *
 *   LoadOptionsClass::~LoadOptionsClass -- class destructor                                   *
 *   LoadOptionsClass::Process -- main processing routine                                      *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "loaddlg.h"

#include "autosave.h"
#include "campaign.h"
#include "conquer.h"
#include "data.h"
#include "gamedirs.h"
#include "globals.h"
#include "houstype.h"
#include "init.h"
#include "language/language.h"
#include "msgbox.h"
#include "saveload.h"
#include "savemgr.h"
#include "savever.h"
#include "scenario.h"
#include "session.h"
#include "ui/screens/savegame/uisavegame.h"
#include "ui/screens/waitbox/uiwaitbox.h"
#include "ui/uienginehost.h"
#include "ui/uiview.h"
#include "utf8.h"
#include "win.h"

#include <algorithm>
#include <cstdio>
#include <vector>


/***********************************************************************************************
 * LoadOptionsClass::LoadOptionsClass -- class constructor                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *      style      style for this load/save dialog (LOAD/SAVE/DELETE)                          *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *      none.                                                                                  *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *      none.                                                                                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/14/1995 BR : Created.                                                                  *
 *=============================================================================================*/
LoadOptionsClass::LoadOptionsClass(void) :
	Style(NONE),
	Description(NULL),
	DescriptionSize(0),
	State(STATE_PENDING)
{
	Style = NONE;
	Description = NULL;
	Extension = "SAV";
	MinSpaceRequired = 2048;
}


/***********************************************************************************************
 * LoadOptionsClass::~LoadOptionsClass -- class destructor                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *      none.                                                                                  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *      none.                                                                                  *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *      none.                                                                                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/14/1995 BR : Created.                                                                  *
 *=============================================================================================*/
LoadOptionsClass::~LoadOptionsClass(void)
{
}


/// <summary>
/// Brings up the load game dialog.
/// This routine is used by the options menu to let the player pick a saved game and
/// resume it.
/// </summary>
/// <returns>bool; Was a game loaded?</returns>
bool LoadOptionsClass::Load(void)
{
	Style = LOAD;
	Description = NULL;
	return(Dialog());
}


/// <summary>
/// Brings up the save game dialog.
/// This routine is used by the options menu when the player wants to record the current
/// game. The description offered is used to prime the edit field.
/// </summary>
/// <param name="description">The description to suggest for the saved game. After a save, it
/// holds the one the player typed, cut to fit.</param>
/// <param name="size">The bytes the description buffer holds.</param>
/// <returns>bool; Was the game saved?</returns>
bool LoadOptionsClass::Save(char *description, std::size_t size)
{
	Style = SAVE;
	Description = description;
	DescriptionSize = size;
	return(Dialog());
}


/// <summary>
/// Brings up the delete game dialog.
/// This routine is used by the options menu to let the player clear out save games that
/// are no longer wanted.
/// </summary>
/// <returns>bool; Did the player go through with the deletion?</returns>
bool LoadOptionsClass::Delete(void)
{
	Style = WWDELETE;
	Description = NULL;
	return(Dialog());
}


#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>

#include "dbgprint.h"
#include "gamedirs.h"
#include "rawfile.h"

#include <cstdlib>
#include <cstdio>
#include <string>

// Hands a save to the browser as a download. The bytes are read here rather than passed
// through the heap: /save is an ordinary directory to the runtime's filesystem, and the
// page can read it directly.
EM_JS(void, Save_Transfer_Export, (char const * name, void const * data, int size), {
	try {
		var leaf = UTF8ToString(name);
		var bytes = HEAPU8.slice(data, data + size);
		var url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
		var link = document.createElement('a');

		link.href = url;
		link.download = leaf;
		document.body.appendChild(link);
		link.click();
		document.body.removeChild(link);
		setTimeout(function () { URL.revokeObjectURL(url); }, 30000);
	} catch (error) {
		err('OpenTS: save export failed: ' + (error && (error.message || error.name) || error));
	}
});


// Asks for a file and holds it until the engine takes it. Suspends while the picker is up,
// which is why the click has to reach here with the browser still willing to open one; the
// engine drains a click within the frame it arrived in, so the gesture is still live.
//
// A save begins with the four bytes OTSV and carries a 32 byte header. Anything else is
// refused here rather than left for the engine to fail on later.
//
// Nothing is written from the page: where a save belongs is the engine's business, and it
// resolves that through the same file layer it saves with.
EM_ASYNC_JS(int, Save_Transfer_Pick, (void), {
	var OTSV = [0x4F, 0x54, 0x53, 0x56];

	try {
		globalThis.__opentsIncoming = null;

		var input = document.createElement('input');
		input.type = 'file';
		input.accept = '.SAV,.sav';
		input.style.position = 'fixed';
		input.style.left = '-1000px';
		document.body.appendChild(input);

		var chosen = await new Promise(function (resolve) {
			input.addEventListener('change', function () { resolve(input.files[0] || null); });
			input.addEventListener('cancel', function () { resolve(null); });
			input.click();
		});

		document.body.removeChild(input);
		if (!chosen) return 0;

		var bytes = new Uint8Array(await chosen.arrayBuffer());

		if (bytes.length < 32) return -1;

		for (var index = 0; index < OTSV.length; index++) {
			if (bytes[index] !== OTSV[index]) return -1;
		}

		globalThis.__opentsIncoming = bytes;
		return bytes.length;
	} catch (error) {
		err('OpenTS: save import failed: ' + (error && (error.message || error.name) || error));
		return -1;
	}
});


EM_JS(void, Save_Transfer_Take, (void * buffer), {
	var bytes = globalThis.__opentsIncoming;

	if (bytes) HEAPU8.set(bytes, buffer);
	globalThis.__opentsIncoming = null;
});


// Hands the named save to the page as a download.
static void Export_Saved_Game(char const * filename)
{
	// Read through the engine's own file layer rather than by naming a path for the page
	// to open: saves sit in a folder of their own beneath the persistent directory, and
	// the platform file layer resolves that, and its casing, already.
	// The name is held in a local: RawFileClass's constructor keeps the pointer it is given
	// rather than copying it, so a temporary string would leave it dangling and every open
	// would fail on a path that reads correctly.
	std::string const path = Saved_Game_Name(filename);
	RawFileClass file(path.c_str());

	if (file.Is_Available()) {
		int const size = file.Size();
		void * const bytes = (size > 0) ? std::malloc((std::size_t)size) : NULL;

		if (bytes != NULL && file.Open(BufferIOFileClass::READ)) {
			int const got = file.Read(bytes, size);
			file.Close();

			if (got > 0) Save_Transfer_Export(filename, bytes, got);
		}

		std::free(bytes);
	}
}


// Takes a save from the page into the save folder, and says whether one was written.
static bool Import_Saved_Game(LoadOptionsClass & options)
{
	int const size = Save_Transfer_Pick();

	if (size <= 0) return(false);

	void * const bytes = std::malloc((std::size_t)size);

	if (bytes == NULL) return(false);

	Save_Transfer_Take(bytes);

	bool written = false;

	// Named the way the engine names a save of its own, so the dialog lists what arrives
	// and an import never lands on a name already in use.
	char leaf[256];
	options.Pick_Filename(leaf);

	std::string const path = Saved_Game_Name(leaf);
	RawFileClass file(path.c_str());

	if (file.Open(BufferIOFileClass::WRITE)) {
		written = (file.Write(bytes, size) == size);
		file.Close();
	}

	std::free(bytes);
	return(written);
}


#endif	// __EMSCRIPTEN__


/// <summary>
/// Is a saved game of this name already there? Asked before one is written, since a name the
/// folder holds is written over rather than added to.
/// </summary>
static bool Saved_Game_Exists(char const * name)
{
	PlatformFileInfoType info;
	return(Platform_File_Info(Saved_Game_Name(name).c_str(), info));
}


/// <summary>
/// Runs the load, save or delete screen, carries out the player's pick, and reopens the
/// screen after a pick that does not finish the action.
/// </summary>
/// <returns>bool; Did the player commit to the action the screen offers?</returns>
bool LoadOptionsClass::Dialog(void)
{
	if (Style == SAVE && Disk_Space_Available() < MinSpaceRequired) {
		WWMessageBox().Process(TXT_DISKFULL, TXT_OK, TXT_NONE, TXT_NONE);
		return(false);
	}

	// The screen is handed what it cannot reach through the public members.
	UIMissionFilesRequest request;
	request.Options = this;
	request.Style = Style;
	request.Description = Description;
	request.Extension = Extension;
	request.ScanLimit = Scan_Limit();
	request.Saved_Game_Exists = Saved_Game_Exists;
	request.Save_Confirmation = [this](void) { return(Save_Confirmation()); };
#if defined(__EMSCRIPTEN__)
	request.Export = Export_Saved_Game;
	request.Import = [this](void) { return(Import_Saved_Game(*this)); };
#endif

	char buffer[256];

	for (;;) {
		Gather_Files();

		UISaveGameState state;
		switch (Style) {
			case SAVE:
				state.Mode = UI_SAVE_GAME_SAVE;
				state.Title = "SAVE";
				state.AcceptCaption = "Save";
				break;

			case WWDELETE:
				state.Mode = UI_SAVE_GAME_DELETE;
				state.Title = "DELETE";
				state.AcceptCaption = "Delete";
				break;

			default:
				state.Mode = UI_SAVE_GAME_LOAD;
				state.Title = "LOAD";
				state.AcceptCaption = "Load";
				break;
		}

		for (int index = 0; index < Files.Count(); index++) {
			FileEntryClass const * file = Files[index];
			UISaveGameEntry entry;
			entry.Description = file->Descr;
			entry.Valid = file->Valid;
			char date[128];
			char timeofday[128];
			if (Stamp_Strings(*file, date, sizeof(date), timeofday, sizeof(timeofday))) {
				entry.Date = date;
				entry.Time = timeofday;
			}
			state.Entries.push_back(entry);
		}

		state.Selected = Initial_Row();
		state.AcceptEnabled = Files.Count() > 0;
		if (Style == SAVE && Description != NULL) {
			state.Suggested = Description;
			state.Description = Description;
		}

		UISaveGamePresenterClass presenter(std::move(state));
		std::unique_ptr<UIViewClass> view = UI_Save_Game_View(presenter);

		UIResult result = UI_Run_Modal(*view, true);
		if (result == UI_RESULT_FAILED_TO_OPEN) {
			Clear_List();
			return(false);
		}

		if (!presenter.Accepted) {
			Clear_List();
			State = STATE_CLOSE;
			return(false);
		}

		int const row = presenter.State.Selected;
		FileEntryClass * entry = (row >= 0 && row < Files.Count()) ? Files[row] : NULL;
		State = STATE_OK;

		if (entry == NULL) {
			Clear_List();
			return(State == STATE_OK);
		}

		switch (Style) {

			case LOAD:
				if (entry->Num != -1) {
					Init_Campaigns();
				}
				if (!Load_File(entry->Filename)) {
					WWMessageBox().Process(TXT_ERROR_LOADING_GAME, TXT_OK, TXT_NONE, TXT_NONE);
					State = STATE_PENDING;
				}
				break;

			case SAVE: {
				std::string typed = presenter.State.Description;
				if (Description != NULL && DescriptionSize > 0) {
					typed.resize(UTF8::Boundary_Before(typed.c_str(), DescriptionSize - 1));
				}
				if (typed.empty()) {
					WWMessageBox().Process(TXT_MUSTENTER_DESCRIPTION, TXT_OK, TXT_NONE, TXT_NONE);
					State = STATE_PENDING;
					break;
				}

				char const * filename = NULL;
				char picked[256];
				if (entry->Valid) {
					filename = entry->Filename;
				} else {
					Pick_Filename(picked);
					filename = picked;
				}

				if (filename == NULL) {
					break;
				}

				if (Saved_Game_Exists(filename)
					&& WWMessageBox()._Process(TXT_CONFIRM_SAVE, 1, TXT_YES, TXT_NO, TXT_NONE)) {
					State = STATE_PENDING;
					break;
				}

				if (!Save_File(filename, typed.c_str())) {
					WWMessageBox().Process(TXT_ERROR_SAVING_GAME, TXT_OK, TXT_NONE, TXT_NONE);
					State = STATE_PENDING;
					break;
				}

				int const confirmation = Save_Confirmation();
				if (confirmation != TXT_NONE) {
					WWMessageBox().Process(confirmation, TXT_OK, TXT_NONE, TXT_NONE);
				}
				if (Description != NULL) {
					UTF8::Copy(Description, DescriptionSize, typed.c_str());
				}
				break;
			}

			case WWDELETE:
				sprintf(buffer, "%s\n%s", Fetch_String(TXT_DELETE_FILE_QUERY), entry->Descr);
				if (!WWMessageBox()._Process(buffer, 1, TXT_YES, TXT_NO, TXT_NONE)) {
					Delete_File(entry->Filename);
					if (Files.Count() > 1) {
						State = STATE_PENDING;
					}
				} else {
					State = STATE_PENDING;
				}
				break;

			default:
				break;
		}

		Clear_List();

		if (State != STATE_PENDING) {
			return(State == STATE_OK);
		}
	}
}


/// <summary>
/// Fetches a save game filename that is not already in use.
/// This routine is used when the player saves into an empty slot and there is no
/// existing file to write over.
/// </summary>
/// <param name="name">Buffer to fill in with the filename chosen.</param>
/// <remarks>Be sure the buffer is big enough to hold a complete filename.</remarks>
void LoadOptionsClass::Pick_Filename(char *name)
{
	do {
		sprintf(name, "SAVE%04lX.%3s", rand(), Extension);
	} while (Saved_Game_Exists(name));
}


/***********************************************************************************************
 * LoadOptionsClass::Clear_List -- clears the list box & Files arrays                          *
 *                                                                                             *
 * This step is essential, because it frees all the strings allocated for list items.          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *      none.                                                                                  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *      none.                                                                                  *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *      none.                                                                                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/14/1995 BR : Created.                                                                  *
 *=============================================================================================*/
void LoadOptionsClass::Clear_List(void)
{
	/*
	**	Clear the array of game numbers
	*/
	for (int i = 0; i < Files.Count(); i++) {
		delete Files[i];
	}
	Files.Clear();
}


/***********************************************************************************************
 * LoadOptionsClass::Gather_Files -- reads the saved games into the Files list                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *      none.                                                                                  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *      none.                                                                                  *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *      none.                                                                                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/14/1995 BR : Created.                                                                  *
 *   06/25/1995 JLB : Shows which saved games are "(old)".                                     *
 *=============================================================================================*/
void LoadOptionsClass::Gather_Files(void)
{
	FileEntryClass * fdata = NULL;  // for adding entries to 'Files'
	WIN32_FIND_DATAA ff;            // for FindFirstFile

	/*
	**	Make sure the list is empty
	*/
	Clear_List();

	/*
	**	Add the Empty Slot entry
	*/
	if (Style == SAVE) {
		fdata = new FileEntryClass;
		strcpy(fdata->Descr, Fetch_String(TXT_EMPTY_SLOT));
		if (PlayerPtr != NULL) {
			fdata->Scenario = Scen->Scenario;
			fdata->House = Scen->PlayerHouse;
			fdata->Num = Scen->Campaign;
			strcpy(fdata->PlayerName, PlayerPtr->Class->GivenName);
		} else {
			fdata->Scenario = 0;
			fdata->House = (HousesType)Session.House;
			fdata->Num = -1;
			strcpy(fdata->PlayerName, Session.Handle);
		}
		SYSTEMTIME time;
		GetSystemTime(&time);
		SystemTimeToFileTime(&time, &fdata->DateTime);
		fdata->Type = Session.Type;
		fdata->Valid = false;
		Files.Add(fdata);
	}

	char buffer[128];
	sprintf(buffer, "*.%3s", Extension);

	/*
	**	Find all savegame files
	*/
	std::vector<WIN32_FIND_DATAA> found;

	HANDLE hFind = FindFirstFile(Saved_Game_Name(buffer).c_str(), &ff);

	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if ((ff.dwFileAttributes & (FILE_ATTRIBUTE_TEMPORARY|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_HIDDEN)) != 0) {
				continue;
			}
			found.push_back(ff);
		} while (FindNextFile(hFind, &ff));

		FindClose(hFind);
	}

	// Newest first, so a bounded scan reads the headers of the files that matter.
	std::sort(found.begin(), found.end(), [](WIN32_FIND_DATAA const & a, WIN32_FIND_DATAA const & b) {
		return(CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0);
	});
	if (found.size() > Scan_Limit()) {
		found.resize(Scan_Limit());
	}

	fdata = NULL;
	for (WIN32_FIND_DATAA & record : found) {
		if (fdata == NULL) {
			fdata = new FileEntryClass;
		}

		/*
		**	get the game's info; if success, add it to the list
		*/
		if (Read_File(fdata, &record) == true) {
			Files.Add(fdata);
			fdata = NULL;
		}
	}

	if (fdata != NULL) {
		delete fdata;
	}

	if (Files.Count() > 0) {

		/*
		**	Now sort the list in order of Date/Time (newest first, oldest last)
		*/
		qsort((void *)(&Files[0]), Files.Count(), sizeof(class FileEntryClass *), LoadOptionsClass::Compare);
	}
}


bool LoadOptionsClass::Stamp_Strings(FileEntryClass const & entry, char * date, std::size_t datesize,
	char * timeofday, std::size_t timesize)
{
	date[0] = '\0';
	timeofday[0] = '\0';

	if (entry.DateTime.dwHighDateTime == -1 && entry.DateTime.dwLowDateTime == -1) {
		return(false);
	}

	FILETIME ft;
	SYSTEMTIME time;
	FileTimeToLocalFileTime(&entry.DateTime, &ft);
	FileTimeToSystemTime(&ft, &time);
	GetDateFormat(LANG_USER_DEFAULT, TIME_NOMINUTESORSECONDS, &time, NULL, date, (int)datesize);
	GetTimeFormat(LANG_USER_DEFAULT, TIME_NOSECONDS, &time, NULL, timeofday, (int)timesize);
	return(true);
}


int LoadOptionsClass::Initial_Row(void) const
{
	if (Style == LOAD) {
		for (int index = 0; index < Files.Count(); index++) {
			if (Files[index]->Valid) {
				return(index);
			}
		}
	}
	return(0);
}


/// <summary>
/// Are there any save games available to load?
/// This routine is used to decide whether the load option should be offered to the
/// player at all. It settles the question as cheaply as it can, so it stops at the
/// first save game it can actually read.
/// </summary>
/// <returns>bool; Was at least one loadable save game found?</returns>
/// <summary>
/// Should the load option be offered to the player?
/// </summary>
/// <returns>bool; Is the load dialog worth opening?</returns>
bool LoadOptionsClass::Offer_Load(void)
{
#if defined(__EMSCRIPTEN__)
	// The dialog is the only way a save reaches the browser's storage, so it has to open
	// before there is anything in it. Its own load button stays disabled until then.
	return(true);
#else
	return(Files_Present());
#endif
}


bool LoadOptionsClass::Files_Present(void)
{
	bool files_found = false;

	char pattern[64];
	snprintf(pattern, sizeof(pattern), "*.%3s", Extension);

	for (PlatformFileInfoType const & found : Platform_Find_Files(Saved_Game_Name(pattern).c_str())) {
		if (found.IsDirectory || found.IsHidden) {
			continue;
		}

		FileEntryClass entry;
		if (Read_File(&entry, &found) == true) {
			files_found = true;
			break;
		}
	}

	return(files_found);
}


/// <summary>
/// Restores the game held in the file specified.
/// A message box is displayed while the load runs, and the scenario is taken out of
/// play first so that nothing tries to tick while the game state is being replaced.
/// </summary>
/// <returns>bool; Was the game loaded?</returns>
bool LoadOptionsClass::Load_File(const char * file_name)
{
	UIWaitBoxClass box;
	box.Show(Fetch_String(TXT_LOADING));
	ScenarioActive = false;
	TacticalActive = false;
	bool loaded = Load_Game(file_name);
	box.Hide();
	return(loaded);
}


/// <summary>
/// Saves the current game to the file specified.
/// A message box is displayed while the save runs, since writing a save game takes long
/// enough that the player would otherwise think the game had locked up.
/// </summary>
/// <param name="descr">The description to record alongside the saved game.</param>
/// <returns>bool; Was the game saved?</returns>
bool LoadOptionsClass::Save_File(const char * file_name, const char * descr)
{
	UIWaitBoxClass box;
	box.Show(Fetch_String(TXT_SAVING_GAME));
	bool saved = SaveManager.Request_Save_Game(file_name, descr, false,
		SaveManagerClass::NoticeType::Requested);
	box.Hide();
	return(saved);
}


/// <summary>
/// A saved game reports itself in the message list at the frame boundary, so the dialog shows
/// no box of its own.
/// </summary>
int LoadOptionsClass::Save_Confirmation(void) const
{
	return(TXT_NONE);
}


/// <summary>
/// Removes the save game file specified.
/// </summary>
/// <returns>bool; Was the file deleted?</returns>
bool LoadOptionsClass::Delete_File(const char * file_name)
{
	return(Platform_Remove_File(Saved_Game_Name(file_name).c_str()));
}


/// <summary>
/// Fills in a save game list entry from a file found on disk.
/// This routine peeks at the save game's header to recover the description, scenario
/// and player it belongs to. A save written by an older game version is still accepted,
/// but its description is marked so the player can tell.
/// </summary>
/// <param name="fdata">The list entry to fill in.</param>
/// <param name="ff">The find record naming the file to examine.</param>
/// <returns>bool; Was a usable save game found in the file?</returns>
bool LoadOptionsClass::Read_File(FileEntryClass * fdata, PlatformFileInfoType const * ff)
{
	if (fdata == NULL && ff == NULL) {
		return(false);
	}

	SaveVersionInfo savever;

	/*
	 * get the game's info;
	 */
	bool ok = Get_Savefile_Info(ff->Name.c_str(), &savever);
	if (!ok) {
		return(false);
	}

	if (savever.Get_Internal_Version() != ExpectedGameVersion) {
		return(false);
	}

	snprintf(fdata->Descr, sizeof(fdata->Descr), "%s", savever.Get_Scenario_Description());

	fdata->Valid = ok;
	fdata->Scenario = savever.Get_Scenario_Number();
	fdata->Num = savever.Get_Campaign_Number();
	fdata->Type = (GameType)savever.Get_Game_Type();
	strcpy(fdata->Filename, ff->Name.c_str());
	strcpy(fdata->PlayerName, savever.Get_Player_House());
	fdata->DateTime = ff->Modified;
	return(true);
}


MultiplayerLoadOptionsClass::MultiplayerLoadOptionsClass(void)
{
	Extension = "NET";
	Picked[0] = '\0';
}


/// <summary>
/// Records the pick without loading it; every machine loads together once the master asks.
/// </summary>
bool MultiplayerLoadOptionsClass::Load_File(const char * file_name)
{
	std::snprintf(Picked, sizeof(Picked), "%s", file_name);
	return(true);
}


/// <summary>
/// Lists a numbered save of this kind of game and nothing else.
/// </summary>
bool MultiplayerLoadOptionsClass::Read_File(FileEntryClass * entry, PlatformFileInfoType const * ff)
{
	if (entry == NULL || ff == NULL || Multiplayer_Save_Slot(ff->Name.c_str()) < 0) {
		return(false);
	}
	return(LoadOptionsClass::Read_File(entry, ff) && entry->Type == Session.Type);
}
