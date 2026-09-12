/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uirmlview.h"

#include "_rules.h"
#include "dbgprint.h"
#include "rules.h"
#include "uicontext.h"
#include "voc.h"
#include "uishell.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>

#include <cstring>


UIRmlViewClass::UIRmlViewClass(UIPresenterClass & presenter, char const * document)
	: Presenter(presenter), Source(document != nullptr ? document : "")
{
}


UIRmlViewClass::~UIRmlViewClass(void)
{
	Close();
}


int UIRmlViewClass::Action_For(char const * name) const
{
	if (name == nullptr) {
		return(UI_ACTION_NONE);
	}

	if (std::strcmp(name, "accept") == 0) {
		return(UI_ACTION_ACCEPT);
	}

	if (std::strcmp(name, "cancel") == 0) {
		return(UI_ACTION_CANCEL);
	}

	return(UI_ACTION_NONE);
}


/// <summary>
/// Loads the document and gives the screen its chance to bind. Reports the resource that
/// was missing rather than leaving an empty screen up.
/// </summary>
bool UIRmlViewClass::Prepare(void)
{
	if (Document != nullptr) {
		return(true);
	}

	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	if (!Bind_Model()) {
		DebugString("UI: the data model for '%s' would not be created\n", Source.c_str());
		return(false);
	}

	Document = context->LoadDocument(Source);
	if (Document == nullptr) {
		DebugString("UI: document '%s' would not load\n", Source.c_str());
		Release_Model();
		return(false);
	}

	if (!Bind()) {
		Close();
		return(false);
	}

	return(true);
}


// A click on any element that names an action, and the document's own key presses. The
// listeners are attached to the document rather than to each element, so an element added
// later is covered and nothing has to be detached one at a time.
void UIRmlViewClass::Attach_Actions(void)
{
	if (Document == nullptr) {
		return;
	}

	Document->AddEventListener(Rml::EventId::Click, this);
	Document->AddEventListener(Rml::EventId::Keydown, this);

	// RmlUi treats only form controls as disabled, and a <button> is not one, so a disabled
	// button would still take a click. Catching the click on its way down, before any
	// listener on the element itself, is what makes the attribute mean what it says. A text
	// field swallows Enter and Escape before they reach the document, so those are caught on
	// the way down as well.
	Document->AddEventListener(Rml::EventId::Click, &Capture, true);
	Document->AddEventListener(Rml::EventId::Mousedown, &Capture, true);
	Document->AddEventListener(Rml::EventId::Mouseover, &Capture, true);
	Document->AddEventListener(Rml::EventId::Keydown, &Capture, true);
	Document->AddEventListener(Rml::EventId::Change, &Capture, true);
}


void UIRmlViewClass::Process_Capture(Rml::Event & event)
{
	if (!Document->IsVisible()) {
		return;
	}

	Rml::Element * const target = event.GetTargetElement();

	if (event.GetId() == Rml::EventId::Keydown) {
		// Only a text field needs this: anywhere else the keys reach the document below.
		if (target != nullptr && target->GetTagName() == "input"
			&& target->GetAttribute<Rml::String>("type", "text") == "text") {
			int const key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);

			if (key == Rml::Input::KI_ESCAPE || key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
				UIIntent intent;
				intent.Action = (key == Rml::Input::KI_ESCAPE) ? UI_ACTION_CANCEL : UI_ACTION_ACCEPT;
				Presenter.Queue(intent);
				event.StopImmediatePropagation();
			}
		}
		return;
	}

	// A track bar notified its dialog, and clicked unless the dialog had silenced it, only when
	// the player moved it to another step. RmlUi raises a range's change on every drag movement
	// and when the model sets its value, and while the event is dispatched the element still
	// holds its old value after a move but already holds the new one after a set, so a change
	// to the value it holds is not a move and goes no further.
	if (event.GetId() == Rml::EventId::Change) {
		if (target != nullptr && target->GetTagName() == "input"
			&& target->GetAttribute<Rml::String>("type", "") == "range") {
			if (event.GetParameter<float>("value", 0.0f) == target->GetAttribute<float>("value", 0.0f)) {
				event.StopImmediatePropagation();
			} else if (!target->HasAttribute("data-silent")) {
				Sound_Effect(Rule->GenericClick);
			}
		}
		return;
	}

	Rml::Element * button = nullptr;
	bool control = false;

	for (Rml::Element * walk = target; walk != nullptr && walk != Document; walk = walk->GetParentNode()) {
		if (walk->HasAttribute("disabled")) {
			event.StopImmediatePropagation();
			return;
		}
		if (button == nullptr && walk->GetTagName() == "button") {
			button = walk;
		}
		if (walk->GetTagName() == "select"
			|| (walk->GetTagName() == "input" && walk->GetAttribute<Rml::String>("type", "") == "checkbox")) {
			control = true;
		}
	}

	// An owner-draw check box clicked as a press toggled it, and a combo box as it was pressed
	// or a pick was made in its list.
	if (control && ControlsClick && event.GetId() == Rml::EventId::Mousedown && event.GetParameter<int>("button", 0) == 0) {
		Sound_Effect(Rule->GenericClick);
	}

	// An owner-draw button clicked whenever it went down under the left button: when pressed,
	// and again when the pointer came back onto it with the button still held.
	if (button != nullptr) {
		bool const pressed = event.GetId() == Rml::EventId::Mousedown && event.GetParameter<int>("button", 0) == 0;
		bool const reentered = event.GetId() == Rml::EventId::Mouseover && button->IsPseudoClassSet("active");

		if (pressed || reentered) {
			Sound_Effect(Rule->GenericClick);
		}
	}
}


void UIRmlViewClass::ProcessEvent(Rml::Event & event)
{
	// A hidden screen still holds the focus, so its keys arrive here; they belong to whatever
	// replaced it.
	if (!Document->IsVisible()) {
		return;
	}

	// A handler never acts: it records an intent and returns, because acting here would
	// re-enter RmlUi from inside its own event dispatch.
	if (event.GetId() == Rml::EventId::Keydown) {
		int const key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);

		UIIntent intent;

		if (key == Rml::Input::KI_ESCAPE) {
			intent.Action = UI_ACTION_CANCEL;
		} else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
			intent.Action = UI_ACTION_ACCEPT;
		} else {
			return;
		}

		Presenter.Queue(intent);
		event.StopPropagation();
		return;
	}

	Rml::Element * element = event.GetTargetElement();
	if (element == nullptr) {
		return;
	}

	// The attribute is read off the element the event started at, and then up its
	// ancestors, so a click on a label inside a button still reads the button's action.
	for (Rml::Element * walk = element; walk != nullptr && walk != Document; walk = walk->GetParentNode()) {
		Rml::String const name = walk->GetAttribute<Rml::String>("data-action", "");
		if (name.empty()) {
			continue;
		}

		int const action = Action_For(name.c_str());
		if (action == UI_ACTION_NONE) {
			DebugString("UI: document '%s' names an unknown action '%s'\n", Source.c_str(), name.c_str());
			return;
		}

		UIIntent intent;
		intent.Action = action;
		intent.Identity = walk->GetAttribute<int>("data-identity", 0);
		Presenter.Queue(intent);

		event.StopPropagation();
		return;
	}
}


void UIRmlViewClass::Show(bool modal)
{
	if (Document == nullptr) {
		return;
	}

	Document->Show(modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None);

	// Only the first showing: a screen shown again after one nested in it was never taken
	// down as a dialog, so it never opened again.
	if (!Revealed) {
		Revealed = true;
		UI_Begin_Reveal(Document);
	}

	UI_Mark_Overlay_Dirty();
}


void UIRmlViewClass::Hide(void)
{
	if (Document == nullptr) {
		return;
	}

	UI_End_Reveal(Document);
	Document->Hide();

	// RmlUi leaves the focus in a hidden document when no other document is showing, and a
	// modal one cannot give it away. Parking it on the document itself keeps a text field
	// from taking the characters meant for whatever replaced the screen.
	Rml::Context * const context = Document->GetContext();
	Rml::Element * const focus = context != nullptr ? context->GetFocusElement() : nullptr;

	if (focus != nullptr && focus != Document && focus->GetOwnerDocument() == Document) {
		Document->Focus();
	}

	UI_Mark_Overlay_Dirty();
}


void UIRmlViewClass::Close(void)
{
	if (Document == nullptr) {
		// Preparation can have created the model and then failed to load the document, so
		// the model is released whether or not there is a document to close.
		Release_Model();
		return;
	}

	// The listeners go before the document does, so nothing of this view outlives the
	// elements it was attached to.
	Document->RemoveEventListener(Rml::EventId::Click, this);
	Document->RemoveEventListener(Rml::EventId::Keydown, this);
	Document->RemoveEventListener(Rml::EventId::Click, &Capture, true);
	Document->RemoveEventListener(Rml::EventId::Mousedown, &Capture, true);
	Document->RemoveEventListener(Rml::EventId::Mouseover, &Capture, true);
	Document->RemoveEventListener(Rml::EventId::Keydown, &Capture, true);
	Document->RemoveEventListener(Rml::EventId::Change, &Capture, true);

	UI_End_Reveal(Document);
	Document->Close();
	Document = nullptr;
	Revealed = false;

	// The document is gone, so nothing can read the model's storage any more, which is what
	// has to be true before the model goes.
	Release_Model();

	// Closing is deferred to the next update, and the pixels have to go either way.
	UI_Mark_Overlay_Dirty();
}
