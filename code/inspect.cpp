/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// What an automated check can read of a running engine: the phase it is
// waiting in, whether input is still queued, and the interface on screen.

#include "always.h"

#if defined(__EMSCRIPTEN__)

#include "_keyboar.h"
#include "_rect.h"
#include "_tactica.h"
#include "aircraft.h"
#include "browser.h"
#include "building.h"
#include "gadget.h"
#include "globals.h"
#include "grphmenu.h"
#include "house.h"
#include "infantry.h"
#include "keyboard.h"
#include "newmenu.h"
#include "phase.h"
#include "tactical.h"
#include "techno.h"
#include "unit.h"
#include "win32user.h"

#include <emscripten/emscripten.h>

#include <string>


static void Append_JSON_String(std::string & out, char const * text)
{
	out += '"';
	for (; text != nullptr && *text != '\0'; text++) {
		char const character = *text;
		switch (character) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)character < 0x20) {
					char escaped[8];
					snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned char)character);
					out += escaped;
				} else {
					out += character;
				}
				break;
		}
	}
	out += '"';
}


static void Describe_Gadgets(std::string & out)
{
	char buffer[128];
	bool first = true;

	out += '[';
	for (LinkClass * link = GadgetClass::Get_Last_List(); link != nullptr; link = link->Get_Next()) {
		GadgetClass * gadget = static_cast<GadgetClass *>(link);

		if (!first) out += ',';
		first = false;

		snprintf(buffer, sizeof(buffer),
			"{\"id\":%u,\"rect\":[%d,%d,%d,%d],\"enabled\":%s,\"focus\":%s,\"text\":",
			gadget->Get_ID(), gadget->X, gadget->Y, gadget->Width, gadget->Height,
			gadget->Is_Disabled() ? "false" : "true",
			gadget->Has_Focus() ? "true" : "false");
		out += buffer;

		char const * caption = gadget->Get_Caption();
		if (caption != nullptr) {
			Append_JSON_String(out, caption);
		} else {
			out += "null";
		}
		out += '}';
	}
	out += ']';
}


template<class T>
static void Describe_Object_List(std::string & out, DynamicVectorClass<T *> const & list,
	char const * kind, bool & first)
{
	char buffer[160];

	for (int index = 0; index < list.Count(); index++) {
		TechnoClass const * object = list[index];

		if (object == nullptr || !object->IsActive || object->IsInLimbo) continue;
		if (object->House == nullptr || !object->House->Is_Player_Control()) continue;

		Point2D pixel;
		if (!TacticalMap->Coord_To_Pixel(object->Center_Coord(), pixel)) continue;

		ObjectTypeClass const * type = object->Class_Of();
		Point3D const size = (type != nullptr) ? type->Pixel_Dimensions() : Point3D(0, 0, 0);
		int const width = (size.X > 0) ? size.X : 24;
		int const height = (size.Y > 0) ? size.Y : 24;

		if (!first) out += ',';
		first = false;

		snprintf(buffer, sizeof(buffer),
			"{\"kind\":\"%s\",\"rect\":[%d,%d,%d,%d],\"selected\":%s,\"type\":",
			kind, TacticalRect.X + pixel.X - width / 2, TacticalRect.Y + pixel.Y - height / 2,
			width, height, object->IsSelected ? "true" : "false");
		out += buffer;
		Append_JSON_String(out, (type != nullptr) ? type->Name() : "");
		out += ",\"name\":";
		Append_JSON_String(out, object->Full_Name());
		out += '}';
	}
}


// The player's own objects within the tactical view, with where they are drawn.
static void Describe_Objects(std::string & out)
{
	bool first = true;

	out += '[';
	if (TacticalMap != nullptr && PlayerPtr != nullptr) {
		Describe_Object_List(out, Units, "unit", first);
		Describe_Object_List(out, Infantry, "infantry", first);
		Describe_Object_List(out, Aircraft, "aircraft", first);
		Describe_Object_List(out, Buildings, "building", first);
	}
	out += ']';
}


extern "C" {


EMSCRIPTEN_KEEPALIVE char const * OpenTS_Phase(void)
{
	return(Phase_Top());
}


EMSCRIPTEN_KEEPALIVE char const * OpenTS_Phase_Stack(void)
{
	return(Phase_Describe());
}


EMSCRIPTEN_KEEPALIVE char const * OpenTS_Phase_Detail(void)
{
	return(Phase_Detail());
}


EMSCRIPTEN_KEEPALIVE int OpenTS_Phase_Serial(void)
{
	return((int)Phase_Serial());
}


// How many places still hold input the engine has not read: the page's queue,
// the keyboard buffer, and the window message queue. Called from the page
// while the engine is suspended, so nothing here may pump or yield.
EMSCRIPTEN_KEEPALIVE int OpenTS_Input_Pending(void)
{
	int pending = Browser_Pending_Events();

	if (Keyboard != nullptr && !Keyboard->Is_Buffer_Empty()) {
		pending++;
	}
	if (Win32_User_Has_Pending_Messages()) {
		pending++;
	}

	return(pending);
}


// The interface on screen as JSON, valid until the next call.
EMSCRIPTEN_KEEPALIVE char const * OpenTS_UI(void)
{
	static std::string _text;
	char buffer[64];

	_text.clear();
	_text += "{\"phase\":";
	Append_JSON_String(_text, Phase_Top());
	_text += ",\"stack\":";
	Append_JSON_String(_text, Phase_Describe());
	_text += ",\"detail\":";
	Append_JSON_String(_text, Phase_Detail());
	snprintf(buffer, sizeof(buffer), ",\"serial\":%u", Phase_Serial());
	_text += buffer;

	_text += ",\"menu\":";
	GraphicMenu const * menu = Current_Graphic_Menu();
	if (menu != nullptr) {
		_text += "{\"section\":";
		Append_JSON_String(_text, menu->Name.c_str());
		_text += ",\"items\":";
		menu->Describe(_text, New_Menu_Item_Name);
		_text += '}';
	} else {
		_text += "null";
	}

	_text += ",\"windows\":";
	Win32_User_Describe(_text);

	_text += ",\"gadgets\":";
	Describe_Gadgets(_text);

	_text += ",\"objects\":";
	Describe_Objects(_text);

	_text += '}';
	return(_text.c_str());
}


}

#endif	// __EMSCRIPTEN__
