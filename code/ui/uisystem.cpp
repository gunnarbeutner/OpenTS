/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// RmlUi's system interface. Its clock is wall time and never a deterministic game timer,
// so an animating document cannot influence the simulation.

#include "always.h"

#include "uisystem.h"

#include "data.h"
#include "dbgprint.h"
#include "mstimer.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/SystemInterface.h>

#include <opents_stringnames.h>

#include <algorithm>
#include <string>
#include <unordered_map>


class UISystemInterface : public Rml::SystemInterface
{
	public:
		double GetElapsedTime() override;
		bool LogMessage(Rml::Log::Type type, Rml::String const & message) override;
		int TranslateString(Rml::String & translated, Rml::String const & input) override;
};


static UISystemInterface _Interface;

// The millisecond clock the engine already keeps counts from an arbitrary origin, so the
// first reading becomes this one's zero.
static unsigned int _Origin = 0;
static bool _OriginTaken = false;

// Set while a context updates its data views.
static bool _UpdatingViews = false;


double UISystemInterface::GetElapsedTime()
{
	unsigned int now = System_Milliseconds();

	if (!_OriginTaken) {
		_Origin = now;
		_OriginTaken = true;
	}

	return((double)(now - _Origin) / 1000.0);
}


bool UISystemInterface::LogMessage(Rml::Log::Type type, Rml::String const & message)
{
	char const * label = "info";

	switch (type) {
		case Rml::Log::LT_ERROR:
			label = "error";
			break;

		case Rml::Log::LT_ASSERT:
			label = "assert";
			break;

		case Rml::Log::LT_WARNING:
			label = "warning";
			break;

		default:
			break;
	}

	DebugString("UI: %s: %s\n", label, message.c_str());

	// Returning false keeps RmlUi from raising its own dialog for an assertion, which
	// would need the UI that is reporting the fault.
	return(false);
}




// The identifiers a document may name, by name. Built once from the generated table, which
// the build collects from the language header.
static std::unordered_map<std::string, int> const & String_Names(void)
{
	static std::unordered_map<std::string, int> const names = [] {
		std::unordered_map<std::string, int> map;
		map.reserve((std::size_t)UIStringNameCount);

		for (int index = 0; index < UIStringNameCount; index++) {
			map.emplace(UIStringNames[index].Name, UIStringNames[index].Identifier);
		}

		return(map);
	}();

	return(names);
}


/// <summary>
/// Replaces every `[[TXT_NAME]]` in a document's text with the string that identifier
/// names, in UTF-8.
/// </summary>
/// <returns>How many names were replaced, which is what RmlUi uses to decide whether the
/// text has to be parsed again.</returns>
int UISystemInterface::TranslateString(Rml::String & translated, Rml::String const & input)
{
	translated = input;

	if (_UpdatingViews) {
		return(0);
	}

	// The common case is text with no name in it at all, and RmlUi calls this for every
	// text node.
	if (input.find("[[") == Rml::String::npos) {
		return(0);
	}

	int replaced = 0;
	std::size_t at = 0;

	while ((at = translated.find("[[", at)) != Rml::String::npos) {
		std::size_t const end = translated.find("]]", at + 2);
		if (end == Rml::String::npos) {
			break;
		}

		std::string const name = translated.substr(at + 2, end - at - 2);
		auto const found = String_Names().find(name);

		if (found == String_Names().end()) {
			// An unknown name is reported and left as it stands, so a misspelling is
			// visible in the picture and named in the log rather than silently empty.
			DebugString("UI: no string identifier is named '%s'\n", name.c_str());
			at = end + 2;
			continue;
		}

		std::string const utf8 = Fetch_String(found->second);

		translated.replace(at, end + 2 - at, utf8);
		at += utf8.length();
		replaced++;
	}

	return(replaced);
}


Rml::SystemInterface * UI_System_Interface(void)
{
	return(&_Interface);
}


void UI_Update_Context(Rml::Context * context)
{
	bool const outer = _UpdatingViews;
	_UpdatingViews = true;
	context->Update();
	_UpdatingViews = outer;
}
