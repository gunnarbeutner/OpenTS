/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "windlg.h"

#include "dialogresult.h"
#include "init.h"
#include "msgloop.h"
#include "platform/wait.h"
#include "ui/uiwsstack.h"
#include "video.h"


struct WSScreenEntry
{
	WSScreenHandle Handle;

	// The identifier of the dialog template the screen replaces, which is what finds it.
	int ID;

	WSScreenProc Proc;
};

static WSScreenEntry _Screens[64];
static int _ScreenCount;

static int _LastResponse;

// A screen's handle is the address of one of these, which no window can have.
static char _ScreenHandles[256];
static unsigned int _NextScreenHandle;


static int Screen_Index(WSScreenHandle window)
{
	for (int index = 0; index < _ScreenCount; index++) {
		if (_Screens[index].Handle == window) {
			return(index);
		}
	}
	return(-1);
}


WSScreenHandle WS_Screen_Handle(void)
{
	WSScreenHandle const handle = (WSScreenHandle)&_ScreenHandles[_NextScreenHandle % sizeof(_ScreenHandles)];
	_NextScreenHandle++;
	return(handle);
}


void WS_Push_Screen(WSScreenHandle screen, int id, WSScreenProc proc)
{
	if (_ScreenCount >= (int)ARRAY_SIZE(_Screens)) {
		return;
	}

	_Screens[_ScreenCount].Handle = screen;
	_Screens[_ScreenCount].ID = id;
	_Screens[_ScreenCount].Proc = proc;
	_ScreenCount++;
}


bool WS_Is_Screen(WSScreenHandle window)
{
	return(window != nullptr && Screen_Index(window) >= 0);
}


/// <summary>
/// Closes a screen along with everything stacked on top of it, topmost first, telling each
/// owner as its screen goes.
/// </summary>
/// <param name="window">The screen to close. If this is null, the topmost screen is closed.</param>
/// <param name="id">The response to report to whoever is waiting on this screen.</param>
/// <returns>bool; Was a screen found and closed?</returns>
bool WS_Destroy_Dialog(WSScreenHandle window, int id)
{
	if (window == nullptr) {
		if (_ScreenCount == 0) {
			return(false);
		}
		window = _Screens[_ScreenCount - 1].Handle;
	}

	int const index = Screen_Index(window);
	if (index < 0) {
		return(false);
	}

	while (_ScreenCount > index) {
		WSScreenEntry const entry = _Screens[_ScreenCount - 1];
		_ScreenCount--;
		if (entry.Proc != nullptr) {
			entry.Proc(entry.Handle, WS_SCREEN_DESTROYED);
		}
	}

	_LastResponse = id;
	return(true);
}


WSScreenHandle WS_Find_Dialog(int id)
{
	for (int index = _ScreenCount - 1; index >= 0; index--) {
		if (_Screens[index].ID == id) {
			return(_Screens[index].Handle);
		}
	}
	return(nullptr);
}


WSScreenHandle WS_Next_Lower_Dialog(WSScreenHandle window)
{
	int const index = Screen_Index(window);
	return(index > 0 ? _Screens[index - 1].Handle : nullptr);
}


/// <summary>
/// Runs the game's pump until the screen has been taken off the stack, polling the abort
/// callback and servicing the screen once a pass.
/// </summary>
/// <param name="callback">Optional routine polled on every pass. Should it return true,
/// the screen is cancelled. May be null.</param>
/// <returns>Returns with the response the screen was closed with.</returns>
int WS_Wait_Dialog(WSScreenHandle window, bool (*callback)(void))
{
	while (Screen_Index(window) >= 0) {
		if (callback != nullptr && callback()) {
			WS_Destroy_Dialog(window, DIALOG_CANCEL);
			break;
		}
		Title_Screen_Restore(false);

		Windows_Message_Handler();

		// The clicks the screen took in the pump are acted on here, as a dialog acted on them
		// inside it.
		int const index = Screen_Index(window);
		if (index >= 0 && _Screens[index].Proc != nullptr) {
			_Screens[index].Proc(window, WS_SCREEN_SERVICE);
		}

		Video_Present_If_Dirty();

		Platform_Sleep(0);
	}

	return(_LastResponse);
}


WSScreenHandle WS_Top_Window(void)
{
	return(_ScreenCount > 0 ? _Screens[_ScreenCount - 1].Handle : nullptr);
}


int WS_Top_Window_ID(void)
{
	return(_ScreenCount > 0 ? _Screens[_ScreenCount - 1].ID : 0);
}
