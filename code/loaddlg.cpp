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

#include "autosave.h"

#include "loaddlg.h"

#include "campaign.h"
#include "conquer.h"
#include "data.h"
#include "gamedirs.h"
#include "globals.h"
#include "houstype.h"
#include "init.h"
#include "language/language.h"
#include "msgbox.h"
#include "platform/file.h"
#include "saveload.h"
#include "savemgr.h"
#include "savever.h"
#include "scenario.h"
#include "session.h"
#include "ui/uimission.h"
#include "ui/uiwaitbox.h"
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
	Callback(NULL),
	State(STATE_PENDING)
{
	Style = NONE;
	Description = NULL;
	Callback = NULL;
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
/// <param name="description">The description to suggest for the saved game.</param>
/// <returns>bool; Was the game saved?</returns>
bool LoadOptionsClass::Save(char *description)
{
	Style = SAVE;
	Description = description;
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


/***********************************************************************************************
 * LoadOptionsClass::Process -- main processing routine                                        *
 *                                                                                             *
 * INPUT:                                                                                      *
 *      none.                                                                                  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *      false = User cancelled, true = operation completed                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *      none.                                                                                  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/14/1995 BR : Created.                                                                  *
 *=============================================================================================*/
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

	// A screen that could not be shown did nothing, as a dialog that could not be created did.
	int const state = UI_Mission_Files_Screen(request);
	State = (state == UI_MISSION_FILES_UNAVAILABLE) ? STATE_PENDING : (LoadDialogState)state;

	return(State == STATE_OK);
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
	UIWaitBoxClass box(Fetch_String(TXT_LOADING));
	ScenarioActive = false;
	TacticalActive = false;
	return(Load_Game(file_name));
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
	UIWaitBoxClass box(Fetch_String(TXT_SAVING_GAME));
	return(SaveManager.Request_Save_Game(file_name, descr, false,
		SaveManagerClass::NoticeType::Requested));
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
