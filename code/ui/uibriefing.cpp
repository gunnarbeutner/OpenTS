/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The mission restatement. It preserves what RestateMission::Presentation did: the
// briefing printed a line at a time with its bleep, paginated with a "more" control
// between the pages, and then the resume and video choices. The screen claims no design
// space, so its lettering is drawn at the window's own resolution; only its geometry is
// placed against the plate, wherever the frame fitted it.

#include "always.h"

#include "uibriefing.h"

#include "init.h"

#include "_mixfile.h"
#include "addon.h"
#include "audio/audioengine.h"
#include "ccfile.h"
#include "ccini.h"
#include "mixfile.h"
#include "movie.h"
#include "mpload.h"
#include "scenario.h"
#include "uicontext.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "utf8.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>


enum
{
	UI_BRIEFING_RESUME = UI_ACTION_SCREEN,
	UI_BRIEFING_VIDEO,
	UI_BRIEFING_MORE,

	// The view reports what it printed, so the sound and the page state stay with the
	// presenter rather than with the layout that measured them.
	UI_BRIEFING_LINE,
	UI_BRIEFING_PAGE,
	UI_BRIEFING_PRINTED,
};


enum UIBriefingPhaseType
{
	// No phase has been drawn yet, so the controls are placed whatever the screen is doing.
	UI_BRIEFING_UNSET,

	UI_BRIEFING_PRINTING,
	UI_BRIEFING_PAGED,
	UI_BRIEFING_READY,
};


// Where the page sits in the plate's own artwork, which is what the legacy view laid it out
// in. The line is the menu font's cell, so a page holds fourteen lines at every window size.
static int const BRIEFING_PAGE_X = 110;
static int const BRIEFING_PAGE_Y = 60;
static int const BRIEFING_PAGE_WIDTH = 420;
static int const BRIEFING_PAGE_HEIGHT = 280;
static int const BRIEFING_LINE = 20;

// FULLFNT3 is Arial at ten point drawn into a bitmap, and the shipped Liberation Sans sets a
// briefing line to the same width at this size.
static int const BRIEFING_TYPE = 16;
// The buttons were lettered from the dialogs' sheets, whose own line is this.
static int const BRIEFING_LABEL = 17;

static int const BRIEFING_BUTTON_WIDTH = 150;
static int const BRIEFING_BUTTON_HEIGHT = 24;
static int const BRIEFING_MORE_Y = 340;
static int const BRIEFING_RESUME_Y = 360;
static int const BRIEFING_PAIR_Y = 348;
static int const BRIEFING_PAIR_X = 85;
static int const BRIEFING_PAIR_GAP = 320;

// A line landed every third pass of a four tick timer on the sixty tick clock, after a five
// tick wait.
static std::int64_t const BRIEFING_LINE_MS = 200;
static std::int64_t const BRIEFING_START_MS = 83;


static void Play_Briefing_Bleep(void)
{
	void const * sample = MFCD::Retrieve("BLEEP1.AUD");

	if (sample != nullptr) {
		AudioEngine.Play_Sample(sample, AUDIO_GROUP_SFX, 64.0f / 255.0f, 10);
	}
}


// The page lays the briefing out itself, so a break the briefing carries keeps only its
// break: Word_Wrap turned an '@' into one, and the spaces that followed either mark went.
// A line with nothing on it carries a no-break space, because RmlUi drops a line box that
// holds nothing and the blank lines between the paragraphs would go with it.
static std::string Normalize_Briefing(char const * text)
{
	static char const * const BLANK = "\xc2\xa0";

	std::string out;
	bool afterbreak = false;
	bool written = false;

	for (char const * cursor = text; *cursor != '\0'; cursor++) {
		if (*cursor == '\n' || *cursor == '@') {
			if (!written) {
				out += BLANK;
			}
			out += '\n';
			afterbreak = true;
			written = false;
			continue;
		}

		if (afterbreak && *cursor == ' ') {
			continue;
		}

		afterbreak = false;
		written = true;
		out += *cursor;
	}

	while (!out.empty()) {
		if (out.back() == '\n' || out.back() == ' ') {
			out.pop_back();
		} else if (out.size() >= 2 && out.compare(out.size() - 2, 2, BLANK) == 0) {
			out.erase(out.size() - 2);
		} else {
			break;
		}
	}

	return(out);
}


class UIBriefingPresenter : public UIPresenterClass
{
	public:
		explicit UIBriefingPresenter(ScenarioClass * scen) : Scenario(scen) {}

		void Refresh(void) override;

		std::string Text;
		UIBriefingPhaseType Phase = UI_BRIEFING_PRINTING;
		bool HasVideo = false;
		bool Video = false;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		ScenarioClass * Scenario;
};


void UIBriefingPresenter::Refresh(void)
{
	char buffer[1024];
	buffer[0] = '\0';

	if (Scenario != nullptr) {
		HasVideo = Scenario->BriefMovie != VQ_NONE;

		if (std::strlen(Scenario->BriefingText) != 0) {
			UTF8::Copy(buffer, Scenario->BriefingText);

		} else {
			CCFileClass file;
			char name[32];

			if (Scenario->RequiredAddOn > ADDON_BASE_GAME) {
				std::snprintf(name, sizeof(name), "MISSION%1d.INI", Scenario->RequiredAddOn);
			} else {
				std::snprintf(name, sizeof(name), "MISSION.INI");
			}

			file.Set_Name(name);

			if (file.Is_Available()) {
				CCINIClass ini;
				char section[32];

				ini.Load(file, false);
				ini.Get_String(Scenario->ScenarioName, "Briefing", "", section, sizeof(section));

				if (std::strlen(section) != 0) {
					ini.Get_TextBlock(section, buffer, sizeof(buffer));
				}
			}
		}
	}

	Text = Normalize_Briefing(buffer);

	// A scenario with nothing to say went straight to the choices, as the legacy view did
	// when it had no text to print.
	Phase = Text.empty() ? UI_BRIEFING_READY : UI_BRIEFING_PRINTING;
}


void UIBriefingPresenter::Execute(UIIntent const & intent)
{
	switch (intent.Action) {
		case UI_BRIEFING_LINE:
		case UI_BRIEFING_PRINTED:
			Play_Briefing_Bleep();
			if (intent.Action == UI_BRIEFING_PRINTED) {
				Phase = UI_BRIEFING_READY;
			}
			break;

		case UI_BRIEFING_PAGE:
			Phase = UI_BRIEFING_PAGED;
			break;

		// The legacy view polled no input while a page was printing, so nothing below acts
		// on one that is.
		case UI_BRIEFING_MORE:
			if (Phase == UI_BRIEFING_PAGED) {
				Phase = UI_BRIEFING_PRINTING;
			}
			break;

		case UI_BRIEFING_RESUME:
			if (Phase == UI_BRIEFING_READY) {
				Finish(UI_RESULT_ACCEPTED, 0);
			}
			break;

		case UI_BRIEFING_VIDEO:
			if (Phase == UI_BRIEFING_READY && HasVideo) {
				Video = true;
				Finish(UI_RESULT_ACCEPTED, 1);
			}
			break;

		// Space and escape, which turned the page under the "more" control and resumed the
		// mission under the choices.
		case UI_ACTION_CANCEL:
			if (Phase == UI_BRIEFING_PAGED) {
				Phase = UI_BRIEFING_PRINTING;
			} else if (Phase == UI_BRIEFING_READY) {
				Finish(UI_RESULT_ACCEPTED, 0);
			}
			break;

		default:
			break;
	}
}


class UIBriefingView : public UIRmlViewClass
{
	public:
		UIBriefingView(UIBriefingPresenter & presenter, Point2D const & plate) :
			UIRmlViewClass(presenter, "briefing.rml"), Briefing(presenter), Plate(plate) {}

		void Sync(void) override;
		void ProcessEvent(Rml::Event & event) override;

	protected:
		bool Bind(void) override;
		int Action_For(char const * name) const override;

	private:
		void Lay_Out(void);
		void Place(Rml::Element * element, float x, float y, float width, float height, float type);
		void Offer(Rml::Element * element, bool offered);
		int Measure(std::size_t start, std::size_t end);
		std::size_t Next_Token(std::size_t at) const;
		void Print_Line(void);
		void Queue(int action);

		UIBriefingPresenter & Briefing;
		Point2D Plate;

		Rml::Element * Page = nullptr;
		Rml::Element * Text = nullptr;
		Rml::Element * More = nullptr;
		Rml::Element * Resume = nullptr;
		Rml::Element * Video = nullptr;

		// The context size the layout was made for, so a resize is noticed and the page
		// reflows into the new one.
		int Width = 0;
		int Height = 0;

		int LineHeight = 0;
		int PageHeight = 0;

		// Where the page being printed starts, how much of the briefing has been printed,
		// and how many lines of the page that came to.
		std::size_t Start = 0;
		std::size_t Shown = 0;
		int Lines = 0;

		// The page is full or the briefing is finished, so nothing more is printed until
		// the presenter says a page was turned.
		bool Waiting = false;

		// The phase the controls on screen belong to.
		UIBriefingPhaseType Offered = UI_BRIEFING_UNSET;

		std::int64_t Next = 0;
};


int UIBriefingView::Action_For(char const * name) const
{
	if (name != nullptr) {
		if (std::strcmp(name, "more") == 0) {
			return(UI_BRIEFING_MORE);
		}
		if (std::strcmp(name, "resume") == 0) {
			return(UI_BRIEFING_RESUME);
		}
		if (std::strcmp(name, "video") == 0) {
			return(UI_BRIEFING_VIDEO);
		}
	}

	return(UIRmlViewClass::Action_For(name));
}


bool UIBriefingView::Bind(void)
{
	Page = Document->GetElementById("page");
	Text = Document->GetElementById("text");
	More = Document->GetElementById("more");
	Resume = Document->GetElementById("resume");
	Video = Document->GetElementById("video");

	if (Page == nullptr || Text == nullptr || More == nullptr || Resume == nullptr || Video == nullptr) {
		return(false);
	}

	Attach_Actions();

	// The page it replaces opened without the dialogs' wipe and without their sound.
	Revealed = true;

	Next = Monotonic_Milliseconds() + BRIEFING_START_MS;
	return(true);
}


// Space dismissed the page as escape did, which no document raises on its own.
void UIBriefingView::ProcessEvent(Rml::Event & event)
{
	if (event.GetId() == Rml::EventId::Keydown && Document != nullptr && Document->IsVisible()) {
		if (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN) == Rml::Input::KI_SPACE) {
			UIIntent intent;
			intent.Action = UI_ACTION_CANCEL;
			Presenter.Queue(intent);
			event.StopPropagation();
			return;
		}
	}

	UIRmlViewClass::ProcessEvent(event);
}


void UIBriefingView::Queue(int action)
{
	UIIntent intent;
	intent.Action = action;
	Presenter.Queue(intent);
}


void UIBriefingView::Place(Rml::Element * element, float x, float y, float width, float height, float type)
{
	char buffer[32];

	std::snprintf(buffer, sizeof(buffer), "%.0fpx", x);
	element->SetProperty("left", buffer);
	std::snprintf(buffer, sizeof(buffer), "%.0fpx", y);
	element->SetProperty("top", buffer);
	std::snprintf(buffer, sizeof(buffer), "%.0fpx", width);
	element->SetProperty("width", buffer);
	std::snprintf(buffer, sizeof(buffer), "%.0fpx", height);
	element->SetProperty("height", buffer);
	std::snprintf(buffer, sizeof(buffer), "%.0fpx", height);
	element->SetProperty("line-height", buffer);
	std::snprintf(buffer, sizeof(buffer), "%.0fpx", type);
	element->SetProperty("font-size", buffer);
}


void UIBriefingView::Offer(Rml::Element * element, bool offered)
{
	element->SetProperty("display", offered ? "inline-block" : "none");
}


int UIBriefingView::Measure(std::size_t start, std::size_t end)
{
	Text->SetInnerRML(Rml::StringUtilities::EncodeRml(Rml::String(Briefing.Text, start, end - start)));
	Document->UpdateDocument();
	return((int)Text->GetOffsetHeight());
}


// The next word, with whatever space precedes it, or a single break of its own so a blank
// line costs a line as it did.
std::size_t UIBriefingView::Next_Token(std::size_t at) const
{
	std::string const & text = Briefing.Text;

	if (at >= text.size()) {
		return(text.size());
	}

	if (text[at] == '\n') {
		return(at + 1);
	}

	std::size_t index = at;
	while (index < text.size() && text[index] == ' ') {
		index++;
	}
	while (index < text.size() && text[index] != ' ' && text[index] != '\n') {
		index++;
	}

	return(index);
}


void UIBriefingView::Print_Line(void)
{
	int const target = (Lines + 1) * LineHeight;
	std::size_t probe = Shown;

	while (probe < Briefing.Text.size()) {
		std::size_t const next = Next_Token(probe);
		if (Measure(Start, next) > target) {
			break;
		}
		probe = next;
	}

	// A word wider than the page would otherwise never be taken and the page never fill.
	if (probe == Shown) {
		probe = Next_Token(probe);
	}

	Shown = probe;
	Lines++;
	Measure(Start, Shown);
}


void UIBriefingView::Lay_Out(void)
{
	Rml::Context * const context = UI_Context();
	if (context == nullptr) {
		return;
	}

	Rml::Vector2i const size = context->GetDimensions();

	if (size.x != Width || size.y != Height) {
		Width = size.x;
		Height = size.y;

		// The surfaces are rebuilt with the frame, so the plate under the page is
		// painted again at the size it is now fitted to.
		Load_Title_Page(BRIEFING_PLATE, true);

		// The plate is fitted to the frame at its own shape, so the layout takes the same
		// fit and everything on the page keeps its place in the artwork.
		float const scale = std::min((float)Width / (float)Plate.X, (float)Height / (float)Plate.Y);
		float const left = ((float)Width - Plate.X * scale) / 2.0f;
		float const top = ((float)Height - Plate.Y * scale) / 2.0f;

		LineHeight = (int)(BRIEFING_LINE * scale + 0.5f);
		PageHeight = (int)(BRIEFING_PAGE_HEIGHT * scale + 0.5f);

		Place(Page, left + BRIEFING_PAGE_X * scale, top + BRIEFING_PAGE_Y * scale,
			BRIEFING_PAGE_WIDTH * scale, (float)PageHeight, BRIEFING_TYPE * scale);

		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%dpx", LineHeight);
		Text->SetProperty("line-height", buffer);
		std::snprintf(buffer, sizeof(buffer), "%.0fpx", BRIEFING_TYPE * scale);
		Text->SetProperty("font-size", buffer);

		float const width = BRIEFING_BUTTON_WIDTH * scale;
		float const height = BRIEFING_BUTTON_HEIGHT * scale;
		float const label = BRIEFING_LABEL * scale;

		Place(More, left + (BRIEFING_PAGE_X + (BRIEFING_PAGE_WIDTH - BRIEFING_BUTTON_WIDTH) / 2) * scale,
			top + BRIEFING_MORE_Y * scale, width, height, label);

		if (Briefing.HasVideo) {
			Place(Resume, left + BRIEFING_PAIR_X * scale, top + BRIEFING_PAIR_Y * scale, width, height, label);
			Place(Video, left + (BRIEFING_PAIR_X + BRIEFING_PAIR_GAP) * scale,
				top + BRIEFING_PAIR_Y * scale, width, height, label);
		} else {
			Place(Resume, left + (Plate.X - BRIEFING_BUTTON_WIDTH) * scale / 2.0f,
				top + BRIEFING_RESUME_Y * scale, width, height, label);
		}

		// The whole briefing was centred in the page when it fitted, and sat at the top of
		// the page when it did not. The offset is measured before anything is revealed, so
		// the text that arrives does not move.
		Text->SetProperty("margin-top", "0px");
		int const whole = Measure(0, Briefing.Text.size());
		int const offset = (whole > 0 && whole <= PageHeight) ? (PageHeight - whole) / 2 : 0;
		std::snprintf(buffer, sizeof(buffer), "%dpx", offset);
		Text->SetProperty("margin-top", buffer);

		// What was printed reflows into the new width, so the lines it takes are counted
		// afresh rather than carried over.
		int const printed = Measure(Start, Shown);
		Lines = (LineHeight > 0) ? (printed + LineHeight - 1) / LineHeight : 0;

		Offered = UI_BRIEFING_UNSET;
	}

	// Only on a change, because setting a property lays the document out again.
	if (Offered != Briefing.Phase) {
		Offered = Briefing.Phase;
		Offer(More, Briefing.Phase == UI_BRIEFING_PAGED);
		Offer(Resume, Briefing.Phase == UI_BRIEFING_READY);
		Offer(Video, Briefing.Phase == UI_BRIEFING_READY && Briefing.HasVideo);
	}
}


void UIBriefingView::Sync(void)
{
	if (Document == nullptr) {
		return;
	}

	Lay_Out();

	if (Briefing.Phase != UI_BRIEFING_PRINTING) {
		return;
	}

	if (Waiting) {
		// The presenter took the page break, so the next page starts where this one stopped.
		Waiting = false;
		Start = Shown;
		Lines = 0;
		Measure(Start, Shown);
		Next = Monotonic_Milliseconds() + BRIEFING_LINE_MS;
		return;
	}

	std::int64_t const now = Monotonic_Milliseconds();
	if (now < Next) {
		return;
	}
	Next = now + BRIEFING_LINE_MS;

	if (Shown >= Briefing.Text.size()) {
		Waiting = true;
		Queue(UI_BRIEFING_PRINTED);
		return;
	}

	if ((Lines + 1) * LineHeight > PageHeight) {
		Waiting = true;
		Queue(UI_BRIEFING_PAGE);
		return;
	}

	Print_Line();
	Queue(UI_BRIEFING_LINE);
}


bool UI_Briefing_Screen(ScenarioClass * scen, Point2D const & plate)
{
	if (plate.X <= 0 || plate.Y <= 0) {
		return(false);
	}

	UIBriefingPresenter presenter(scen);
	UIBriefingView view(presenter, plate);

	UIResult const result = UI_Run_Modal(presenter, view);

	return(result.Type == UI_RESULT_ACCEPTED && presenter.Video);
}
