/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "dict.h"
#include "globals.h"
#include "lobbymsg.h"
#include "preview.h"
#include "wstring.h"

#include <cstdint>

class HouseClass;

// The buttons ODMessageBox offers. The values are those of the Windows message box styles.
enum ODMessageBoxType
{
	OD_BOX_OK = 0x00,
	OD_BOX_OK_CANCEL = 0x01,
	OD_BOX_YES_NO = 0x04,
};

int ODMessageBox(const char *text, int type, bool (*callback)(void), bool large = false);
LobbyResult ODMessageBox_Proc(WSScreenHandle window, unsigned int message, LobbyWParam wparam, LobbyLParam lparam);

bool Set_Scenario_Info_From_Index(int index);
void Commit_Session_Specials(void);
void PregameSetup(void);
void Update_Network_Dialog_Preview(WSScreenHandle win);
void Receive_Random_Map_Preview(void);
void Send_Preview_To_Guests(void);
int CountAliveTeams(HouseClass * house);

int RandomMapWaypointCount(int index);
int Scenario_Dialog(WSScreenHandle hWndParent);

unsigned int Wstring_Hash(Wstring & string);


void __cdecl PMessagePrintf(int color, const char * fmt, ...);

WSScreenHandle GameoptWindow(void);

void PumpGameopts(bool, bool = false);
bool DecodePubGameopt(char * options, char * name);
void SendPublicGameopts(char const * options);
void SendPrivateGameopts(char const * player, char const * options);
void DisplayGameopts(WSScreenHandle window, bool initialize);

char * CalcRandomMapDigest(void);
int CreateRandomMap(void);

extern std::uint32_t PlayerColorTable[MAX_PLAYERS];

/*
 * These are the predefined colors that PMessagePrintf displays its messages in.
 */
extern const std::uint32_t ColorSystem;
extern const std::uint32_t ColorUser;
extern const std::uint32_t ColorPriv;
extern const std::uint32_t ColorPrivAction;
extern const std::uint32_t ColorAction;
extern const std::uint32_t ColorOp;
extern const std::uint32_t ColorPaged;
extern const std::uint32_t ColorMe;
extern const std::uint32_t ColorNoJoin;


extern MapPreviewClass *MultiplayerMapPreview;

extern bool IsRandomMap;

extern int IsColorChangePending;
