/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The RmlUi half of a screen. It owns the document, turns element events into the
// presenter's intents, and is the only place a screen sees an RmlUi type.

#pragma once

#include "uiscreen.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class ElementDocument; }


/// <summary>
/// A document bound to a presenter. Constructing one loads nothing; Prepare does, and
/// reports whether the screen can be shown at all.
/// </summary>
class UIRmlViewClass : public Rml::EventListener
{
	public:
		UIRmlViewClass(UIPresenterClass & presenter, char const * document);
		virtual ~UIRmlViewClass(void) override;

		UIRmlViewClass(UIRmlViewClass const &) = delete;
		UIRmlViewClass & operator=(UIRmlViewClass const &) = delete;

		// Loads the document and binds it. False means the screen cannot be shown and the
		// resource that was missing has been reported.
		bool Prepare(void);

		void Show(bool modal);
		void Hide(void);

		// Drops listeners and the document. Safe to call twice, and called by the
		// destructor, so a screen that returns early still leaves nothing behind.
		void Close(void);

		bool Is_Open(void) const { return(Document != nullptr); }

		// Writes what the presenter changed into the document. Called after a drain.
		virtual void Sync(void) {}

		void ProcessEvent(Rml::Event & event) override;

	protected:
		// Creates the screen's data model. Called by Prepare before the document is loaded,
		// because RmlUi resolves a `data-model` attribute while it parses and reports a
		// model created afterwards as missing.
		virtual bool Bind_Model(void) { return(true); }

		// Attaches listeners and fills in what the document shows once. Called by Prepare
		// with the document loaded and not yet shown.
		virtual bool Bind(void) { return(true); }

		// Releases the screen's data model. Called by Close, before the storage the model
		// points at can go, so a screen does not have to remember to do it.
		virtual void Release_Model(void) {}

		// Names the action an element's event stands for. An element carries the name in its
		// `data-action` attribute, and a name this returns zero for raises no intent.
		virtual int Action_For(char const * name) const;

		// Listens for clicks on every element under the document that names an action, and
		// for the document's own key presses.
		void Attach_Actions(void);

		Rml::ElementDocument * Document = nullptr;

		// Whether the document has made its opening wipe.
		bool Revealed = false;

		// Whether a press on a check box or a drop-down clicks here. A screen that clicks for
		// its own controls turns this off.
		bool ControlsClick = true;
		UIPresenterClass & Presenter;

	private:
		// RmlUi reports an event at its target as the target phase to both of an element's
		// listeners, so the capture-phase work is a listener of its own.
		class CaptureListener : public Rml::EventListener
		{
			public:
				CaptureListener(UIRmlViewClass & view) : View(view) {}
				void ProcessEvent(Rml::Event & event) override { View.Process_Capture(event); }

			private:
				UIRmlViewClass & View;
		};

		void Process_Capture(Rml::Event & event);

		Rml::String Source;
		CaptureListener Capture{*this};
};
