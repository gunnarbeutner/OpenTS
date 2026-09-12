/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(_WIN32)

#include "platform/process.h"

#include "dbgprint.h"

#include <windows.h>
#include <mmsystem.h>

#include <vector>

// Named by the first releases, so a copy of the original game and its AutoPlay
// launcher see this one as the same program.
static char const APP_GUID[] = "29e3bb2a-2f36-11d3-a72c-0090272fa661";
static char const AUTOPLAY_GUID[] = "b350c6d2-2f36-11d3-a72c-0090272fa661";

static HANDLE AppMutex = nullptr;
static HANDLE AutoPlayMutex = nullptr;
static bool TimerResolutionRaised = false;


std::string Executable_Path(void)
{
	std::vector<char> buffer(MAX_PATH);

	for (;;) {
		DWORD const length = GetModuleFileNameA(nullptr, buffer.data(), DWORD(buffer.size()));
		if (length == 0) {
			return(std::string());
		}

		// A full buffer means the path was cut short.
		if (length < buffer.size()) {
			return(std::string(buffer.data(), length));
		}

		buffer.resize(buffer.size() * 2);
	}
}


bool Executable_Image_Range(std::uintptr_t & base, std::size_t & size)
{
	base = 0;
	size = 0;

	HMODULE const module = GetModuleHandleA(nullptr);
	if (module == nullptr) {
		return(false);
	}

	IMAGE_DOS_HEADER const * const dos = (IMAGE_DOS_HEADER const *)module;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return(false);
	}

	IMAGE_NT_HEADERS const * const nt = (IMAGE_NT_HEADERS const *)((char const *)module + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return(false);
	}

	base = (std::uintptr_t)module;
	size = nt->OptionalHeader.SizeOfImage;
	return(true);
}


std::uint32_t Process_Id(void)
{
	return(GetCurrentProcessId());
}


/// <summary>
/// Claims the game's single-instance mutex and waits for the AutoPlay launcher to let go of
/// its own. When another copy already holds the game's mutex, its window is brought forward
/// and false is returned. Release_Single_Instance lets both go.
/// </summary>
bool Acquire_Single_Instance(void)
{
	AppMutex = CreateMutexA(nullptr, FALSE, APP_GUID);

	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND const main_wnd = FindWindowA(APP_GUID, nullptr);
		if (main_wnd != nullptr) {
			SetForegroundWindow(main_wnd);
			ShowWindow(main_wnd, SW_RESTORE);
		}
		if (AppMutex != nullptr) {
			CloseHandle(AppMutex);
			AppMutex = nullptr;
		}
		DebugString("TibSun is already running...Bail!\n");
		return(false);
	}

	DebugString("Create AppMutex okay.\n");

	// Holding the AutoPlay mutex as well keeps the launcher from starting while the game runs.
	do {
		AutoPlayMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, AUTOPLAY_GUID);
		if (AutoPlayMutex != nullptr) {
			DebugString("Waiting for Autoplay to quit!\n");
			if (WaitForSingleObject(AutoPlayMutex, 30000) == WAIT_FAILED) {
				DebugString("Failed waiting for AutoPlayMutex\n");
				CloseHandle(AutoPlayMutex);
				AutoPlayMutex = nullptr;
			}
		}

		if (AutoPlayMutex == nullptr) {
			AutoPlayMutex = CreateMutexA(nullptr, FALSE, AUTOPLAY_GUID);
			if (GetLastError() == ERROR_ALREADY_EXISTS) {
				CloseHandle(AutoPlayMutex);
				AutoPlayMutex = nullptr;
				Sleep(2500);
			} else {
				DebugString("Create AutoPlayMutex.\n");
			}
		}
	} while (AutoPlayMutex == nullptr);

	DebugString("Got AutoPlayMutex okay.\n");
	return(true);
}


void Release_Single_Instance(void)
{
	if (AutoPlayMutex != nullptr) {
		CloseHandle(AutoPlayMutex);
		AutoPlayMutex = nullptr;
	}
	if (AppMutex != nullptr) {
		CloseHandle(AppMutex);
		AppMutex = nullptr;
	}
}


void Raise_Timer_Resolution(void)
{
	if (!TimerResolutionRaised) {
		TimerResolutionRaised = timeBeginPeriod(1) == TIMERR_NOERROR;
	}
}


void Restore_Timer_Resolution(void)
{
	if (TimerResolutionRaised) {
		timeEndPeriod(1);
		TimerResolutionRaised = false;
	}
}

#endif	// _WIN32
