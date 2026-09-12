/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The screen contract. A presenter holds what a screen knows and does; a view renders it
// with one toolkit. No toolkit type appears here, so a screen's behavior can be tested and
// reasoned about apart from how it is drawn.

#pragma once

#include <deque>
#include <string>


enum UIResultType
{
	// The screen is still open. A runner keeps going while a result reads as this.
	UI_RESULT_NONE,

	UI_RESULT_ACCEPTED,
	UI_RESULT_CANCELLED,

	// The session ended under the screen, which a caller must notice before it touches a
	// scenario again. The dialog drivers reported this by returning true.
	UI_RESULT_SESSION_ENDED,

	// The screen could not be prepared: a document, a style, or a font was missing.
	UI_RESULT_FAILED,
};


// What a screen returns. Code carries the value the dialog driver returned, so a caller
// that still speaks in DIALOG_OK and DIALOG_CANCEL keeps working.
struct UIResult
{
	UIResultType Type = UI_RESULT_NONE;
	int Code = 0;
};


// The actions every view raises on its own: Enter asks to accept and Escape to cancel,
// whatever the document holds. A screen numbers its own actions from UI_ACTION_SCREEN, so
// neither key can be mistaken for one of them, and a screen whose dialog ignored a key
// ignores the action.
enum
{
	UI_ACTION_NONE = 0,
	UI_ACTION_ACCEPT = 1,
	UI_ACTION_CANCEL = 2,
	UI_ACTION_SCREEN = 16,
};


// What a view asks the presenter to do. It holds identities and copied text, never a
// document node, a borrowed buffer, or an engine pointer, so an intent outlives the event
// that raised it and means the same thing when it is executed.
struct UIIntent
{
	int Action = 0;
	int Identity = 0;
	std::string Text;
};


/// <summary>
/// A screen's behavior, with no knowledge of how it is drawn.
/// </summary>
class UIPresenterClass
{
	public:
		UIPresenterClass(void) = default;
		virtual ~UIPresenterClass(void) = default;

		UIPresenterClass(UIPresenterClass const &) = delete;
		UIPresenterClass & operator=(UIPresenterClass const &) = delete;

		// Records an intent for the runner to execute at its next safe point. A view's event
		// handler calls this and does nothing else, because acting inside a toolkit's event
		// dispatch would re-enter the toolkit.
		void Queue(UIIntent const & intent);

		// Executes every queued intent, in order. Called by the runner after the toolkit's
		// update returns.
		void Drain(void);

		// Drops queued intents without executing them, for a screen that is closing or has
		// lost its eligibility. Accepted effects are never undone.
		void Discard(void);

		bool Has_Result(void) const { return(Result.Type != UI_RESULT_NONE); }
		UIResult const & Get_Result(void) const { return(Result); }

		// Reads current engine state into whatever the view renders. Called once before the
		// screen becomes interactive and again after a drain changed something.
		virtual void Refresh(void) {}

		// Work a dialog driver did on every pass of its loop beyond servicing the game, such
		// as a caller's callback. The runner calls it once a pass, before the drain.
		virtual void Service(void) {}

	protected:
		virtual void Execute(UIIntent const & intent) = 0;

		// Ends the screen. The first result a screen reports is the one it keeps, so an
		// intent that arrives after it cannot change the answer.
		void Finish(UIResultType type, int code);

	private:
		std::deque<UIIntent> Queued;
		UIResult Result;

		// Set while Drain is running, so an intent queued by an executing intent waits for
		// the next drain rather than extending this one.
		bool Draining = false;
};
