/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "_command.h"
#include "command.h"
#include "conquer.h"
#include "scenario.h"
#include "globals.h"
#include "house.h"
#include "techno.h"

#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstring>


namespace {

// Groups are numbered from one for the player and stored from zero on the object.
int const GROUP_MAX = 9;


bool Is_Player_Unit(TechnoClass const * object)
{
	return(object != nullptr && object->IsActive && !object->IsInLimbo &&
		object->House != nullptr && object->House->Is_Player_Control());
}


// A click on an enemy object selects it to be looked at, but nothing the player does not
// command can carry a group, so the selection is counted through this.
TechnoClass const * Selected_Player_Unit(int index)
{
	ObjectClass const * object = CurrentObject[index];

	if (object == nullptr || !object->Is_Techno()) {
		return(nullptr);
	}

	TechnoClass const * techno = (TechnoClass const *)object;

	return(Is_Player_Unit(techno) ? techno : nullptr);
}


// Drives the same command the keyboard reaches, so a group made here is the group the
// hotkeys already know about.
void Run_Command(char const * name)
{
	for (int index = 0; index < AllCommands.Count(); index++) {
		if (strcmp(AllCommands[index]->Get_Unique_Name(), name) == 0) {
			AllCommands[index]->Execute();
			return;
		}
	}
}

}	// namespace


extern "C" {

/// <summary>How many of the player's units are in a group.</summary>
EMSCRIPTEN_KEEPALIVE int OpenTS_Group_Count(int group)
{
	if (group < 1 || group > GROUP_MAX) return(0);

	int count = 0;

	for (int index = 0; index < Technos.Count(); index++) {
		TechnoClass const * object = Technos[index];

		if (Is_Player_Unit(object) && object->Group == group - 1) count++;
	}

	return(count);
}


/// <summary>Acts on a group.</summary>
/// <param name="action">0 selects, 1 centres, 2 assigns the selection, 3 adds it.</param>
EMSCRIPTEN_KEEPALIVE void OpenTS_Group_Do(int group, int action)
{
	if (group < 1 || group > GROUP_MAX) return;

	char name[32];

	// Adding is the two the keyboard uses in turn: the group joins the selection, and the
	// selection then becomes the group.
	if (action == 3) {
		snprintf(name, sizeof(name), "TeamAddSelect_%d", group);
		Run_Command(name);
		snprintf(name, sizeof(name), "TeamCreate_%d", group);
		Run_Command(name);
		return;
	}

	char const * const verbs[] = {"TeamSelect", "TeamCenter", "TeamCreate"};

	if (action < 0 || action >= (int)(sizeof(verbs) / sizeof(verbs[0]))) return;

	snprintf(name, sizeof(name), "%s_%d", verbs[action], group);
	Run_Command(name);
}


/// <summary>
/// Whether anything in a group has taken damage recently.
/// </summary>
/// <returns>1 while at least one member is still flashing, 0 otherwise. This is the timer
/// the radar blip flashes on, so the bar and the radar agree on what is under attack.</returns>
EMSCRIPTEN_KEEPALIVE int OpenTS_Group_Alarm(int group)
{
	if (group < 1 || group > GROUP_MAX) return(0);

	for (int index = 0; index < Technos.Count(); index++) {
		TechnoClass const * object = Technos[index];

		if (!Is_Player_Unit(object) || object->Group != group - 1) continue;
		if (object->RadarFlashTimer > 0) return(1);
	}

	return(0);
}


/// <summary>
/// How much of a group the selection holds.
/// </summary>
/// <returns>0 when none of it is selected, 1 when some is, 2 when all of it is. A group the
/// selection holds entirely is one that assigning the selection elsewhere would empty,
/// because a unit carries a single group.</returns>
EMSCRIPTEN_KEEPALIVE int OpenTS_Group_Overlap(int group)
{
	if (group < 1 || group > GROUP_MAX) return(0);

	int members = 0;
	int selected = 0;

	for (int index = 0; index < Technos.Count(); index++) {
		TechnoClass const * object = Technos[index];

		if (!Is_Player_Unit(object) || object->Group != group - 1) continue;

		members++;
		if (object->IsSelected) selected++;
	}

	if (members == 0 || selected == 0) return(0);
	return((selected == members) ? 2 : 1);
}


/// <summary>
/// How much of the selection a group already holds, which is the other way round from
/// Group_Overlap: this asks about the selection, that asks about the group.
/// </summary>
/// <returns>0 when none of the selection is in the group, 1 when some is, 2 when all of it
/// is. Adding a selection a group already holds entirely would change nothing.</returns>
EMSCRIPTEN_KEEPALIVE int OpenTS_Selection_In_Group(int group)
{
	if (group < 1 || group > GROUP_MAX) return(0);

	int inside = 0;
	int counted = 0;

	for (int index = 0; index < CurrentObject.Count(); index++) {
		TechnoClass const * object = Selected_Player_Unit(index);

		if (object == nullptr) continue;

		counted++;
		if (object->Group == group - 1) inside++;
	}

	if (counted == 0 || inside == 0) return(0);
	return((inside == counted) ? 2 : 1);
}


/// <summary>
/// The group the selection is exactly, or zero when it is not one.
/// </summary>
/// <returns>1 to 9 when every selected object is in that group and the group holds nothing
/// else; zero for an empty, mixed, ungrouped or partial selection.</returns>
EMSCRIPTEN_KEEPALIVE int OpenTS_Selection_Group(void)
{
	int group = -1;
	int counted = 0;

	for (int index = 0; index < CurrentObject.Count(); index++) {
		TechnoClass const * object = Selected_Player_Unit(index);

		if (object == nullptr) continue;

		int const found = object->Group;

		if (found < 0 || found >= GROUP_MAX) return(0);
		if (group == -1) group = found;
		if (found != group) return(0);
		counted++;
	}

	if (counted == 0) return(0);

	// Every selected unit is in the one group; it is that group only if nothing else is.
	return((OpenTS_Group_Count(group + 1) == counted) ? group + 1 : 0);
}


/// <summary>
/// Whether a mission is under way, which is not the same as being outside the main menu:
/// the loading screen and the films before it are outside it too.
/// </summary>
EMSCRIPTEN_KEEPALIVE int OpenTS_In_Mission(void)
{
	if (PlayerPtr == nullptr || Main_Menu_Is_Up) return(0);

	// The loading screen draws itself before the game loop has run at all, and it is as far
	// outside the main menu as a mission is, so the frame count is what tells them apart.
	if (Frame <= 0) return(0);

	// A mission being abandoned drops this as it is asked for, and the menu it returns to
	// takes a moment to come up; without it the bar reappears for that moment.
	if (!GameActive) return(0);

	// What the mission ends into -- the closing film, the score screen, the map choice --
	// runs with the game still active and a player still set, but the tactical view is no
	// longer what is on screen, and a bar acting on it has nothing to act on.
	if (!TacticalActive) return(0);

	// A scripted sequence, a loading screen and a modal screen all take the player's
	// control away, which is the same flag the input loop reads to ignore what is typed.
	if (Scen != nullptr && Scen->IsInputLocked) return(0);

	return(1);
}


/// <summary>
/// Jumps the view to the player's base, and back again when asked while still there.
/// </summary>
EMSCRIPTEN_KEEPALIVE void OpenTS_Center_Base(void)
{
	Run_Command("CenterBase");
}


/// <summary>
/// Drops the whole selection, which a pen cannot otherwise do: the right click that clears
/// it is a second finger the pen does not have.
/// </summary>
EMSCRIPTEN_KEEPALIVE void OpenTS_Selection_Clear(void)
{
	Unselect_All();
}


/// <summary>Reports how much is selected, which is what offers a free slot.</summary>
EMSCRIPTEN_KEEPALIVE int OpenTS_Selection_Count(void)
{
	int count = 0;

	for (int index = 0; index < CurrentObject.Count(); index++) {
		if (Selected_Player_Unit(index) != nullptr) count++;
	}

	return(count);
}

}

#endif
