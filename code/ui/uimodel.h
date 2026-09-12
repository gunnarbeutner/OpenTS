/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// RmlUi reports every registration of a data type after its first in a context as an error.

#pragma once

#include "uicontext.h"

#include <RmlUi/Core/DataModelHandle.h>


// Inline, so that every file shares the one record of the type.
template <typename T>
inline bool UI_Model_Type_Is_New(void)
{
	static Rml::Context * _registered = nullptr;

	Rml::Context * const context = UI_Context();
	if (_registered == context) {
		return(false);
	}

	_registered = context;
	return(true);
}


// An empty handle, which adds no members, once the struct is registered.
template <typename T>
Rml::StructHandle<T> UI_Register_Struct(Rml::DataModelConstructor & constructor)
{
	if (!UI_Model_Type_Is_New<T>()) {
		return(Rml::StructHandle<T>(nullptr, nullptr));
	}

	return(constructor.RegisterStruct<T>());
}


template <typename Container>
void UI_Register_Array(Rml::DataModelConstructor & constructor)
{
	if (UI_Model_Type_Is_New<Container>()) {
		constructor.RegisterArray<Container>();
	}
}
