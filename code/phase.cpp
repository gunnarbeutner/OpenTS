/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "phase.h"

#include <string>
#include <vector>



namespace {

struct PhaseEntry
{
	char const * Name;
	std::string Detail;
};

struct LayerSource
{
	char const * Name;
	int (*Count)(void);
};

std::vector<PhaseEntry> _Stack;
std::vector<LayerSource> _Layers;
std::string _Description = "none";
unsigned int _Serial = 0;


std::string Describe(void)
{
	std::string text;

	for (PhaseEntry const & entry : _Stack) {
		if (!text.empty()) text += '/';
		text += entry.Name;
	}

	for (LayerSource const & source : _Layers) {
		int count = source.Count();
		for (int index = 0; index < count; index++) {
			if (!text.empty()) text += '/';
			text += source.Name;
		}
	}

	if (text.empty()) text = "none";
	return(text);
}

}	// namespace


PhaseScope::PhaseScope(char const * name, char const * detail)
{
	_Stack.push_back(PhaseEntry{name, detail != nullptr ? detail : ""});
	Phase_Changed();
}


PhaseScope::~PhaseScope(void)
{
	if (!_Stack.empty()) _Stack.pop_back();
	Phase_Changed();
}


void Phase_Register_Layers(char const * name, int (*count)(void))
{
	for (LayerSource const & source : _Layers) {
		if (source.Count == count) return;
	}
	_Layers.push_back(LayerSource{name, count});
}


void Phase_Changed(void)
{
	std::string now = Describe();
	if (now == _Description) return;

	_Description = now;
	_Serial++;
	Phase_Event("phase", _Description.c_str());
}


char const * Phase_Top(void)
{
	std::string::size_type slash = _Description.rfind('/');
	return(_Description.c_str() + (slash == std::string::npos ? 0 : slash + 1));
}


char const * Phase_Describe(void)
{
	return(_Description.c_str());
}


char const * Phase_Detail(void)
{
	return(_Stack.empty() ? "" : _Stack.back().Detail.c_str());
}


unsigned int Phase_Serial(void)
{
	return(_Serial);
}


void Phase_Event(char const * name, char const * detail)
{
	(void)name;
	(void)detail;
}
