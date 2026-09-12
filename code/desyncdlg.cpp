/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "desyncdlg.h"

#include "_map.h"
#include "_xmouse.h"
#include "chat.h"
#include "conquer.h"
#include "data.h"
#include "dbgprint.h"
#include "globals.h"
#include "house.h"
#include "ipxmgr.h"
#include "language/language.h"
#include "loaddlg.h"
#include "misc.h"
#include "mpload.h"
#include "netdlg.h"
#include "netglobal.h"
#include "platform/wait.h"
#include "savemgr.h"
#include "session.h"
#include "syncreport.h"
#include "ui/uidesync.h"

#include <algorithm>
#include <cstdio>
#include <cstring>


namespace {

	constexpr int CHAT_BACKLOG_MAX = 50;


	// How much of the countdown is left, and the bar's colour, which goes from green to yellow
	// to red as the load nears.
	int Countdown_Remaining(int & red, int & green)
	{
		int const total = (int)MultiplayerLoadClass::COUNTDOWN_MS;
		int const remaining = std::clamp((int)SaveManager.MultiplayerLoad.Milliseconds_Left(Monotonic_Milliseconds()), 0, total);
		int const elapsed = total - remaining;

		red = 0;
		green = 200;
		if (elapsed > total * 2 / 5) {
			red = 200;
			if (elapsed > total * 4 / 5) {
				green = 0;
			}
		}

		return(remaining);
	}


	void Show_Countdown_Bar(void)
	{
		if (!SaveManager.MultiplayerLoad.Is_Pending()) {
			return;
		}

		int red = 0;
		int green = 0;
		int const remaining = Countdown_Remaining(red, green);
		UI_Desync_Set_Countdown_Bar(remaining, (int)MultiplayerLoadClass::COUNTDOWN_MS, red, green, 0);
	}

}	// namespace


/// <summary>
/// Shows the dialog and pumps it until the master has decided, or this player has quit. Game
/// logic is halted for the duration; chat, sign-offs, heartbeats and the master's decision
/// still come through, since the network is serviced the whole time.
/// </summary>
DesyncDialogClass::OutcomeType DesyncDialogClass::Run(void)
{
	DebugString("Out-of-sync dialog opening on frame %d\n", Frame);

	// A raised suspension makes a nested dialog's pump service the network instead of the game.
	TacticalActive = false;
	Session.Suspended++;

	Decision = 0;
	ContinueReceived = false;
	CountdownActive = false;
	QuitEnabled = false;
	LastCountdownSecond = -1;
	ChatBacklog.clear();
	OpenedAt = Monotonic_Milliseconds();
	State.Begin(OpenedAt);

	Create_Dialog();

	// The box's buttons and chat field act where the dialog's WM_COMMAND acted.
	auto const execute = [](UIIntent const & intent) {
		switch (intent.Action) {
			case UI_DESYNC_LOAD:
				DesyncDialog.Decision = UI_DESYNC_LOAD;
				break;

			case UI_DESYNC_CONTINUE:
				DesyncDialog.Decision = UI_DESYNC_CONTINUE;
				break;

			case UI_DESYNC_QUIT:
				DesyncDialog.Decision = UI_DESYNC_QUIT;
				break;

			case UI_ACTION_ACCEPT:
				DesyncDialog.Send_Chat();
				break;

			case UI_DESYNC_CHAT_FOCUS:
				DesyncDialog.On_Chat_Edit_Focus(intent.Identity != 0);
				break;

			default:
				break;
		}
	};

	OutcomeType outcome = OutcomeType::Continue;

	if (!Open) {
		DebugString("The out-of-sync dialog could not be created; continuing\n");
	} else {
		// A promotion whose box cannot be reopened ends the dialog as a Continue.
		while (Is_Active()) {
			Call_Back();
			UI_Desync_Service(execute);

			if (Decision == UI_DESYNC_QUIT) {
				outcome = OutcomeType::Quit;
				break;
			}

			std::int64_t now = Monotonic_Milliseconds();
			if (!IsHostDialog && !QuitEnabled && now - OpenedAt >= DesyncClass::QUIT_DELAY_MS) {
				QuitEnabled = true;
				UI_Desync_Enable(UI_DESYNC_QUIT, true);
			}

			if (!CountdownActive && SaveManager.MultiplayerLoad.Is_Pending()) {
				Start_Countdown();
			}

			if (CountdownActive) {
				Update_Countdown_Text();
				Show_Countdown_Bar();
				if (SaveManager.MultiplayerLoad.Is_Due(now)) {
					outcome = OutcomeType::Load;
					break;
				}
			} else if (ContinueReceived || Decision == UI_DESYNC_CONTINUE) {
				if (Decision == UI_DESYNC_CONTINUE) {
					Send_Continue();
				}
				outcome = OutcomeType::Continue;
				break;
			} else if (Decision == UI_DESYNC_LOAD) {
				UI_Desync_Suspend(true);
				SaveManager.Multiplayer_Load_Prompt();
				UI_Desync_Service(execute);
				UI_Desync_Suspend(false);
				UI_Desync_Focus_Box();
			}

			Decision = 0;
			Platform_Sleep(10);
		}
	}

	Destroy_Dialog();

	Session.Suspended--;
	TacticalActive = true;
	Map.Flag_To_Redraw(GS_REDRAW_ALL);

	DebugString("Out-of-sync dialog closed with outcome %d\n", (int)outcome);
	return(outcome);
}


void DesyncDialogClass::Service(void)
{
	if (!Is_Active()) {
		return;
	}

	std::int64_t now = Monotonic_Milliseconds();
	if (State.Heartbeat_Is_Due(now)) {
		Send_Heartbeat();
		State.Heartbeat_Sent(now);
	}
	Check_Timeouts();
}


void DesyncDialogClass::Notify_Chat(char const * name, char const * text)
{
	if (!Is_Active()) {
		return;
	}

	char buffer[MAX_MESSAGE_LENGTH + MAX_MESSAGE_PREFIX];
	std::snprintf(buffer, sizeof(buffer), "%s: %s", name, text);
	Append_Chat_Line(buffer);
}


void DesyncDialogClass::Notify_Player_Left(int house, char const * name)
{
	if (!Is_Active()) {
		return;
	}

	State.Mark_Left(house, name);

	if (name != NULL && name[0] != '\0') {
		char buffer[128];
		std::snprintf(buffer, sizeof(buffer), Fetch_String(TXT_LEFT_GAME), name);
		Append_Chat_Line(buffer);
	}

	Update_Player_List();
	Become_Host_If_Promoted();
}


void DesyncDialogClass::Notify_Continue(void)
{
	if (!Is_Active()) {
		return;
	}

	DebugString("The master chose to continue without the players out of sync\n");
	ContinueReceived = true;
}


void DesyncDialogClass::Notify_Heartbeat(int house)
{
	if (Is_Active()) {
		State.Heard(house, Monotonic_Milliseconds());
	}
}


void DesyncDialogClass::Notify_Master_Changed(void)
{
	if (!Is_Active()) {
		return;
	}

	Update_Player_List();
	Become_Host_If_Promoted();
}


/// <summary>
/// Creates the variant the local player gets: the decision dialog for the master, the wait
/// dialog for everyone else.
/// </summary>
void DesyncDialogClass::Create_Dialog(void)
{
	IsHostDialog = Session.Am_I_Master();

	Open = UI_Desync_Open(IsHostDialog);
	if (!Open) {
		return;
	}

	Update_Player_List();

	if (IsHostDialog) {
		bool const can_load = SaveManager.Multiplayer_Load_Is_Allowed() && MultiplayerLoadOptionsClass().Files_Present();
		UI_Desync_Enable(UI_DESYNC_LOAD, can_load && !CountdownActive);
		UI_Desync_Enable(UI_DESYNC_CONTINUE, !CountdownActive);
	} else {
		UI_Desync_Enable(UI_DESYNC_QUIT, QuitEnabled);
	}

	Refill_Chat_List();

	UI_Desync_Set_Chat_Text(Fetch_String(TXT_CHAT_HINT));
	ChatPlaceholderActive = true;

	if (CountdownActive) {
		UI_Desync_Show_Countdown();
		Update_Countdown_Text();
		Show_Countdown_Bar();
	}
}


void DesyncDialogClass::Destroy_Dialog(void)
{
	if (Open) {
		UI_Desync_Close();
		Open = false;
	}
}


/// <summary>
/// Replaces the wait dialog with the decision dialog once this machine has become master,
/// unless a load is already counting down, when there is nothing left to decide.
/// </summary>
void DesyncDialogClass::Become_Host_If_Promoted(void)
{
	if (!Is_Active() || IsHostDialog || CountdownActive || !Session.Am_I_Master()) {
		return;
	}

	DebugString("This machine is the new master; switching to the decision dialog\n");
	Destroy_Dialog();
	Create_Dialog();
}


void DesyncDialogClass::Update_Player_List(void)
{
	if (!Is_Active()) {
		return;
	}

	std::vector<UIDesyncPlayer> rows;
	int const master = Session.Master_Player_ID();

	for (int house = 0; house < MAX_PLAYERS && house < Houses.Count(); house++) {
		HouseClass const * housep = Houses[house];
		bool const left = State.Has_Left(house);

		// A player who left stays listed, though their seat is no longer human.
		if (housep == NULL || (!housep->IsHuman && !left)) {
			continue;
		}

		// The roster entry is gone by now, so the kept name is the only copy while the list rebuilds.
		char const * name = left && State.Left_Name(house)[0] != '\0' ? State.Left_Name(house) : housep->IniName.c_str();

		UIDesyncPlayer player;
		player.Name = name;
		player.Host = (house == master);
		player.Red = 0;
		player.Green = 200;
		player.Blue = 0;

		int text = TXT_OK;
		if (left) {
			text = TXT_SYNC_STATUS_LEFT;
			player.Red = 200;
			player.Green = 0;
		} else if (Sync_Is_Out_Of_Sync(house)) {
			text = TXT_SYNC_STATUS_OUT;
			player.Red = 200;
		}
		player.Status = Fetch_String(text);
		rows.push_back(player);
	}

	UI_Desync_Set_Players(rows);
}


void DesyncDialogClass::Refill_Chat_List(void)
{
	if (!Is_Active()) {
		return;
	}

	UI_Desync_Set_Chat(ChatBacklog);
}


void DesyncDialogClass::Append_Chat_Line(char const * line)
{
	ChatBacklog.emplace_back(line);
	if (ChatBacklog.size() > CHAT_BACKLOG_MAX) {
		ChatBacklog.erase(ChatBacklog.begin());
	}

	if (!Is_Active()) {
		return;
	}

	UI_Desync_Set_Chat(ChatBacklog);
}


void DesyncDialogClass::Send_Chat(void)
{
	if (!Is_Active() || ChatPlaceholderActive) {
		return;
	}

	char buffer[MAX_MESSAGE_LENGTH];

	// The dialog's GetWindowText cut the text to the buffer the same way.
	std::snprintf(buffer, sizeof(buffer), "%s", UI_Desync_Chat_Text().c_str());
	if (buffer[0] == '\0') {
		return;
	}

	UI_Desync_Set_Chat_Text("");
	UI_Desync_Focus_Chat();

	Session.MessageScope = ChatScopeType::Everyone;
	Session.MessageAddress = IPXAddressClass();
	Chat_Send(buffer);
}


void DesyncDialogClass::On_Chat_Edit_Focus(bool gained)
{
	if (!Is_Active()) {
		return;
	}

	if (gained && ChatPlaceholderActive) {
		UI_Desync_Set_Chat_Text("");
		ChatPlaceholderActive = false;
	} else if (!gained && UI_Desync_Chat_Text().empty()) {
		UI_Desync_Set_Chat_Text(Fetch_String(TXT_CHAT_HINT));
		ChatPlaceholderActive = true;
	}
}


void DesyncDialogClass::Send_Heartbeat(void)
{
	if (PlayerPtr == NULL || Session.Players.Count() == 0) {
		return;
	}

	GlobalPacketType packet;
	NetGlobal::Initialize_Packet(packet, NET_DESYNC_HEARTBEAT);
	std::snprintf(packet.Name, sizeof(packet.Name), "%s", Session.Players[0]->Name);

	for (int index = 1; index < Session.Players.Count(); index++) {
		Ipx.Send_Global_Message(&packet, sizeof(packet), 0, &Session.Players[index]->Address);
	}
	Ipx.Service();
}


void DesyncDialogClass::Send_Continue(void)
{
	DebugString("Telling every seat to continue without the players out of sync\n");

	GlobalPacketType packet;
	NetGlobal::Initialize_Packet(packet, NET_DESYNC_CONTINUE);
	std::snprintf(packet.Name, sizeof(packet.Name), "%s", Session.Players[0]->Name);

	for (int index = 1; index < Session.Players.Count(); index++) {
		Ipx.Send_Global_Message(&packet, sizeof(packet), 1, &Session.Players[index]->Address);
		Ipx.Service();
	}
}


/// <summary>
/// Drops the seats that have fallen silent, so a machine that died without a sign-off neither
/// holds up the decision nor lingers in the seats a later load reconciles.
/// </summary>
void DesyncDialogClass::Check_Timeouts(void)
{
	std::int64_t const now = Monotonic_Milliseconds();

	for (int index = Session.Players.Count() - 1; index >= 1; index--) {
		int const house = Session.Players[index]->Player.ID;
		if (!State.Is_Silent(house, now)) {
			continue;
		}

		DebugString("No heartbeat from %s (house %d) for %d seconds; dropping the seat\n",
			Session.Players[index]->Name, house, (int)(DesyncClass::HEARTBEAT_TIMEOUT_MS / 1000));

		std::string const name = Session.Players[index]->Name;
		Destroy_Connection(house, 1);
		Notify_Player_Left(house, name.c_str());
	}
}


void DesyncDialogClass::Start_Countdown(void)
{
	DebugString("Counting down to the multiplayer load\n");

	CountdownActive = true;
	LastCountdownSecond = -1;

	if (!Is_Active()) {
		return;
	}

	Append_Chat_Line(Fetch_String(TXT_LOADING_SAVED_GAME));

	UI_Desync_Show_Countdown();
	Update_Countdown_Text();
	Show_Countdown_Bar();

	if (IsHostDialog) {
		UI_Desync_Enable(UI_DESYNC_LOAD, false);
		UI_Desync_Enable(UI_DESYNC_CONTINUE, false);
	}
}


void DesyncDialogClass::Update_Countdown_Text(void)
{
	if (!Is_Active() || !CountdownActive || !SaveManager.MultiplayerLoad.Is_Pending()) {
		return;
	}

	int const seconds = SaveManager.MultiplayerLoad.Seconds_Left(Monotonic_Milliseconds());
	if (seconds == LastCountdownSecond) {
		return;
	}
	LastCountdownSecond = seconds;

	char buffer[128];
	std::snprintf(buffer, sizeof(buffer),
		Fetch_String(seconds == 1 ? TXT_LOADING_IN_SECOND : TXT_LOADING_IN_SECONDS), seconds);
	UI_Desync_Set_Countdown_Text(buffer);
}
