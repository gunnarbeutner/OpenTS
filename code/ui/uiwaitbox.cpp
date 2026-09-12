/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uiwaitbox.h"

#include "_keyboar.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "uicontext.h"
#include "uishell.h"
#include "uisystem.h"
#include "video.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <string>


static Rml::ElementDocument * _Document = nullptr;
static Rml::DataModelHandle _Model;

// The model's storage. It outlives the model because the model is removed before the box
// is considered closed.
static Rml::String _Message;
static Rml::String _Cancel;
static bool _HasCancel = false;
static bool * _Cancelled = nullptr;


// Raising the flag and posting a key touch nothing of RmlUi, so the one action this box
// takes is safe to take from inside its own event dispatch; there is no runner to hand it to.
static void On_Cancel(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const &)
{
	if (_Cancelled == nullptr) {
		return;
	}

	if (Keyboard != nullptr) {
		Keyboard->Put(KN_ESC);
	}

	*_Cancelled = true;
}


// The box is not driven by a modal runner, so nothing else updates the context and presents
// the frame between the caller's own steps. This draws it now.
static void Redraw_Now(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return;
	}

	UI_Update_Context(context);
	UI_Mark_Overlay_Dirty();
	Video_Present();
}


bool UI_Wait_Box_Open(char const * message, char const * cancel, bool * cancelled)
{
	if (!UI_Is_Initialized() || _Document != nullptr) {
		return(false);
	}

	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	_Message = (message != nullptr) ? message : "";
	_HasCancel = (cancel != nullptr);
	_Cancel = (cancel != nullptr) ? cancel : "";
	_Cancelled = cancelled;

	Rml::DataModelConstructor constructor = context->CreateDataModel("waitbox");
	if (!constructor) {
		return(false);
	}

	constructor.Bind("Message", &_Message);
	constructor.Bind("Cancel", &_Cancel);
	constructor.Bind("HasCancel", &_HasCancel);
	constructor.BindEventCallback("cancel", &On_Cancel);
	_Model = constructor.GetModelHandle();

	_Document = context->LoadDocument("waitbox.rml");
	if (_Document == nullptr) {
		DebugString("UI: document 'waitbox.rml' would not load\n");
		context->RemoveDataModel("waitbox");
		_Model = Rml::DataModelHandle();
		return(false);
	}

	_Model.DirtyAllVariables();
	return(true);
}


void UI_Wait_Box_Show(void)
{
	if (_Document == nullptr) {
		return;
	}

	_Document->Show();
	Redraw_Now();
}


void UI_Wait_Box_Set_Text(char const * message)
{
	if (_Document == nullptr) {
		return;
	}

	_Message = (message != nullptr) ? message : "";
	_Model.DirtyVariable("Message");

	// The dialog repainted before the call returned, and a caller in a countdown relies on
	// that: its loop does not present a frame of its own.
	Redraw_Now();
}


void UI_Wait_Box_Close(void)
{
	if (_Document == nullptr) {
		return;
	}

	Rml::Context * context = UI_Context();

	_Document->Close();
	_Document = nullptr;

	if (context != nullptr) {
		context->RemoveDataModel("waitbox");
	}
	_Model = Rml::DataModelHandle();
	_Cancelled = nullptr;

	// Closing is deferred to the next update, and the pixels have to go either way.
	UI_Mark_Overlay_Dirty();
}


bool UI_Wait_Box_Is_Open(void)
{
	return(_Document != nullptr);
}


UIWaitBoxClass::UIWaitBoxClass(char const * message, char const * cancel, bool * cancelled) :
	IsOpen(UI_Wait_Box_Open(message, cancel, cancelled))
{
	if (IsOpen) {
		UI_Wait_Box_Show();
		if (Keyboard != nullptr) {
			Keyboard->Clear();
		}
	}
}


UIWaitBoxClass::~UIWaitBoxClass(void)
{
	if (IsOpen) {
		if (Keyboard != nullptr) {
			Keyboard->Clear();
		}
		UI_Wait_Box_Close();
	}
}


void UIWaitBoxClass::Set_Text(char const * message)
{
	if (IsOpen) {
		UI_Wait_Box_Set_Text(message);
	}
}
