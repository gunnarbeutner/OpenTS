/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The network lobby's screens. What the drivers see of a screen is the dialog it replaces:
// the controls answer the calls the dialog's controls answered and report what the player
// did in the notifications the dialog procedures read. The owner-draw controls the dialogs
// used are the model for that, including the notifications they raised for changes a dialog
// made itself, because the lobby's handlers act on those.

#include "always.h"

#include "uilobby.h"

#include "_rules.h"
#include "bsurface.h"
#include "dbgprint.h"
#include "dialogresult.h"
#include "language/language.h"
#include "lobbymsg.h"
#include "netshare.h"
#include "preview.h"
#include "rules.h"
#include "srfcache.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uiscreen.h"
#include "uishell.h"
#include "uitexture.h"
#include "utf8.h"
#include "video.h"
#include "voc.h"
#include "xsurface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>


// What a view hands its presenter, with more than a UIIntent carries.
struct UILobbyInput
{
	enum KindType
	{
		CLICK,
		CHECK,
		SCROLL,
		PICK,
		TEXT,
		ENTER,
		DOWN,
		DOUBLE,
	};

	KindType Kind = CLICK;
	int Control = 0;
	int Row = -1;
	int Button = 0;
	int Value = 0;
	int Serial = 0;
	std::string Text;
};


enum
{
	UI_LOBBY_ACTION_ACCEPT = UI_ACTION_ACCEPT,
	UI_LOBBY_ACTION_CANCEL = UI_ACTION_CANCEL,
	UI_LOBBY_ACTION_INPUT = UI_ACTION_SCREEN,
};


// The dialogs' text colour, for a row or item that names no colour of its own.
static char const * const _TextColor = "#70ff00ff";


static Rml::String Css_Color(int color)
{
	if (color == -1) {
		return(_TextColor);
	}

	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02xff", color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
	return(buffer);
}


static Surface * Preview_Surface(void)
{
	if (MultiplayerMapPreview == nullptr) {
		return(nullptr);
	}
	return(MultiplayerMapPreview->Get_Preview_Surface());
}


static bool Has_Preview(void)
{
	return(Preview_Surface() != nullptr);
}


// The preview's texture is read from the surface when it is first drawn, so a preview
// replaced behind a screen's back would go on showing the old one. Every screen that shows it
// asks here, and the texture is let go when the surface is not the one it was read from.
static void Check_Preview(void)
{
	static MapPreviewClass * _shown = nullptr;
	static Surface * _surface = nullptr;

	Surface * const surface = Preview_Surface();
	if (MultiplayerMapPreview != _shown || surface != _surface) {
		_shown = MultiplayerMapPreview;
		_surface = surface;
		UI_Surface_Invalidate("lobby-preview");
	}
}


// The player list's pictures were drawn with pure magenta left out. A picture reaches a
// document opaque, so these copies put black in its place, which is close to the dimmed list
// they sit on.
struct UILobbyPicture
{
	char const * Name;
	BSurface * Copy;
};

static UILobbyPicture _Pictures[] = {
	{"gdii.pcx", nullptr},
	{"nodi.pcx", nullptr},
	{"wolhost.pcx", nullptr},
	{"wolacpt.pcx", nullptr},
};


static Surface * Keyed_Picture(int index)
{
	UILobbyPicture & picture = _Pictures[index];
	if (picture.Copy != nullptr) {
		return(picture.Copy);
	}

	// The owner-draw dialogs put these in the cache when the first of them opened.
	Surface * source = SurfaceCache.GetSurface(picture.Name);
	if (source == nullptr && SurfaceCache.CachePCX(picture.Name)) {
		source = SurfaceCache.GetSurface(picture.Name);
	}
	if (source == nullptr || source->Bytes_Per_Pixel() != 2) {
		return(nullptr);
	}

	int const width = source->Get_Width();
	int const height = source->Get_Height();
	picture.Copy = new BSurface(width, height, 2);

	unsigned char const * from = (unsigned char const *)source->Lock();
	unsigned char * to = (unsigned char *)picture.Copy->Lock();
	if (from != nullptr && to != nullptr) {
		for (int y = 0; y < height; y++) {
			unsigned short const * in = (unsigned short const *)(from + y * source->Stride());
			unsigned short * out = (unsigned short *)(to + y * picture.Copy->Stride());
			for (int x = 0; x < width; x++) {
				out[x] = (in[x] == 0xF81F) ? 0 : in[x];
			}
		}
	}
	if (to != nullptr) {
		picture.Copy->Unlock();
	}
	if (from != nullptr) {
		source->Unlock();
	}

	return(picture.Copy);
}


static Surface * Picture_GDI(void) { return(Keyed_Picture(0)); }
static Surface * Picture_Nod(void) { return(Keyed_Picture(1)); }
static Surface * Picture_Host(void) { return(Keyed_Picture(2)); }
static Surface * Picture_Accepted(void) { return(Keyed_Picture(3)); }


static bool _SurfacesRegistered = false;


static void Register_Surfaces(void)
{
	if (_SurfacesRegistered) {
		return;
	}

	UI_Surface_Register("lobby-preview", Preview_Surface);
	UI_Surface_Register("lobby-gdii.pcx", Picture_GDI);
	UI_Surface_Register("lobby-nodi.pcx", Picture_Nod);
	UI_Surface_Register("lobby-wolhost.pcx", Picture_Host);
	UI_Surface_Register("lobby-wolacpt.pcx", Picture_Accepted);
	_SurfacesRegistered = true;
}


static Rml::String Picture_Source(std::string const & name)
{
	return(name.empty() ? Rml::String() : Rml::String("surface:lobby-") + name);
}


// Several of the family's screens can be open at once, two of a kind among them when a join
// is confirmed under a message box, so each takes a data model of its own. The documents
// name their model "lobby", and the name is replaced as the document is read.
static int _NextModel = 0;


/// <summary>
/// A view of this family: loads its document under a model name of its own and binds it.
/// </summary>
class UILobbyViewBase : public UIRmlViewClass
{
	public:
		UILobbyViewBase(UIPresenterClass & presenter, char const * document) :
			UIRmlViewClass(presenter, document), File(document)
		{
			ControlsClick = false;
			char name[32];
			std::snprintf(name, sizeof(name), "lobby%d", ++_NextModel);
			ModelName = name;
		}

		bool Open(void);

		Rml::ElementDocument * Get_Document(void) const { return(Document); }

	protected:
		void Release_Model(void) override;

		Rml::String File;
		Rml::String ModelName;
		Rml::DataModelHandle Model;
		bool Pushed = false;
};


bool UILobbyViewBase::Open(void)
{
	Rml::Context * context = UI_Context();
	Rml::FileInterface * files = Rml::GetFileInterface();
	if (context == nullptr || files == nullptr || Document != nullptr) {
		return(Document != nullptr);
	}

	Rml::String text;
	if (!files->LoadFile(File, text)) {
		DebugString("UI: document '%s' would not load\n", File.c_str());
		return(false);
	}

	Rml::String const placeholder = "data-model=\"lobby\"";
	std::size_t const at = text.find(placeholder);
	if (at != Rml::String::npos) {
		text.replace(at, placeholder.size(), "data-model=\"" + ModelName + "\"");
	}

	if (!Bind_Model()) {
		DebugString("UI: the data model for '%s' would not be created\n", File.c_str());
		return(false);
	}

	Document = context->LoadDocumentFromMemory(text, File);
	if (Document == nullptr) {
		DebugString("UI: document '%s' would not load\n", File.c_str());
		Release_Model();
		return(false);
	}

	if (!Bind()) {
		Close();
		return(false);
	}

	return(true);
}


void UILobbyViewBase::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel(ModelName);
	}
	Model = Rml::DataModelHandle();
}


/// <summary>
/// The presenter of a family screen. The view queues what the player did as an input; the
/// screen that owns the presenter acts on it when the presenter is drained.
/// </summary>
class UILobbyPresenter : public UIPresenterClass
{
	public:
		void Queue_Input(UILobbyInput const & input)
		{
			Inputs.push_back(input);

			UIIntent intent;
			intent.Action = UI_LOBBY_ACTION_INPUT;
			Queue(intent);
		}

		void Drop_All(void)
		{
			Discard();
			Inputs.clear();
		}

		std::function<void(UILobbyInput const &)> Act_Input;
		std::function<void(int)> Act_Key;

	protected:
		void Execute(UIIntent const & intent) override
		{
			if (intent.Action == UI_LOBBY_ACTION_INPUT) {
				if (Inputs.empty()) {
					return;
				}
				UILobbyInput const input = Inputs.front();
				Inputs.pop_front();
				if (Act_Input) {
					Act_Input(input);
				}
				return;
			}

			if (Act_Key) {
				Act_Key(intent.Action);
			}
		}

	private:
		std::deque<UILobbyInput> Inputs;
};


/*
 * The game list and the host's and the guest's setup.
 */

enum UILobbyControlType
{
	UI_CONTROL_BUTTON,
	UI_CONTROL_CHECK,
	UI_CONTROL_TRACK,
	UI_CONTROL_LIST,
	UI_CONTROL_COMBO,
	UI_CONTROL_EDIT,
	UI_CONTROL_TEXT,
	UI_CONTROL_MESSAGES,
};


enum
{
	UI_KIND_GAME_LIST = 1 << UI_LOBBY_GAME_LIST,
	UI_KIND_HOST = 1 << UI_LOBBY_HOST,
	UI_KIND_GUEST = 1 << UI_LOBBY_GUEST,
	UI_KIND_SETUP = UI_KIND_HOST | UI_KIND_GUEST,
	UI_KIND_ALL = UI_KIND_GAME_LIST | UI_KIND_SETUP,
};


struct UILobbyControlSpec
{
	int Id;
	UILobbyControlType Type;
	char const * Name;
	unsigned int Kinds;

	// The guest's template disabled its game options.
	bool GuestDisabled;
};


static UILobbyControlSpec const _Specs[] = {
	{IDC_YOURNAME, UI_CONTROL_EDIT, "name", UI_KIND_GAME_LIST, false},
	{IDC_INPUT, UI_CONTROL_EDIT, "input", UI_KIND_ALL, false},
	{IDC_PMESSAGES, UI_CONTROL_MESSAGES, "messages", UI_KIND_ALL, false},
	{IDC_GAMELIST, UI_CONTROL_LIST, "games", UI_KIND_GAME_LIST, false},
	{IDC_USERS, UI_CONTROL_LIST, "users", UI_KIND_ALL, false},
	{IDC_GAMELIST_JOIN, UI_CONTROL_BUTTON, "join", UI_KIND_GAME_LIST, false},
	{IDC_GAMELIST_NEW, UI_CONTROL_BUTTON, "new", UI_KIND_GAME_LIST, false},
	{DIALOG_CANCEL, UI_CONTROL_BUTTON, "cancel", UI_KIND_ALL, false},
	{IDC_YOURSIDE, UI_CONTROL_COMBO, "side", UI_KIND_SETUP, false},
	{IDC_YOURCOLOR, UI_CONTROL_COMBO, "color", UI_KIND_SETUP, false},
	{IDC_SCENARIONAME, UI_CONTROL_TEXT, "scenario", UI_KIND_SETUP, false},
	{IDC_MULTIMAP, UI_CONTROL_BUTTON, "multimap", UI_KIND_HOST, false},
	{IDC_KICK, UI_CONTROL_BUTTON, "kick", UI_KIND_HOST, false},
	{IDC_GO, UI_CONTROL_BUTTON, "go", UI_KIND_HOST, false},
	{IDC_ACCEPT, UI_CONTROL_BUTTON, "accept", UI_KIND_GUEST, false},
	{IDC_GAME_SPEED_SLIDER, UI_CONTROL_TRACK, "speed", UI_KIND_SETUP, true},
	{IDC_AIPLAYERS, UI_CONTROL_TRACK, "aiplayers", UI_KIND_SETUP, true},
	{IDC_AILEVEL_SLIDER, UI_CONTROL_TRACK, "ailevel", UI_KIND_SETUP, true},
	{IDC_UNITCOUNT, UI_CONTROL_TRACK, "units", UI_KIND_SETUP, true},
	{IDC_TECHLEVEL, UI_CONTROL_TRACK, "tech", UI_KIND_SETUP, true},
	{IDC_CREDITS, UI_CONTROL_TRACK, "credits", UI_KIND_SETUP, true},
	{IDC_ALLIES, UI_CONTROL_CHECK, "allies", UI_KIND_SETUP, true},
	{IDC_HARVTRUCE, UI_CONTROL_CHECK, "truce", UI_KIND_SETUP, true},
	{IDC_BASES, UI_CONTROL_CHECK, "bases", UI_KIND_SETUP, true},
	{IDC_REDEPLOY_MCV, UI_CONTROL_CHECK, "redeploy", UI_KIND_SETUP, true},
	{IDC_FOG_OF_WAR, UI_CONTROL_CHECK, "fog", UI_KIND_SETUP, true},
	{IDC_BRIDGE_DESTROY, UI_CONTROL_CHECK, "bridges", UI_KIND_SETUP, true},
	{IDC_CRATES, UI_CONTROL_CHECK, "crates", UI_KIND_SETUP, true},
	{IDC_SHORT_GAME, UI_CONTROL_CHECK, "short", UI_KIND_SETUP, true},
	{IDC_MULTI_ENGINEER, UI_CONTROL_CHECK, "engineers", UI_KIND_SETUP, true},
};


struct UILobbyButtonView
{
	bool On = true;
};


struct UILobbyCheckView
{
	bool Checked = false;
	bool On = true;
};


struct UILobbyTrackView
{
	int Min = 0;
	int Max = 100;
	int Step = 1;
	int Value = 0;
	bool On = true;
};


struct UILobbyRowView
{
	Rml::String Text;
	Rml::String Color;
	Rml::String Emblem;
	Rml::String Mark;
	bool Picked = false;
};


struct UILobbyLineView
{
	Rml::String Text;
	Rml::String Color;
};


struct UILobbyListView
{
	std::vector<UILobbyRowView> Rows;
	std::vector<UILobbyLineView> Lines;
	bool On = true;
};


struct UILobbyOptionView
{
	Rml::String Text;
	Rml::String Color;
	Rml::String Value;
};


struct UILobbyComboView
{
	std::vector<UILobbyOptionView> Items;
	Rml::String Value;
	bool On = true;
};


struct UILobbyTextView
{
	Rml::String Text;
	int Limit = -1;
};


/// <summary>
/// One control of a screen: the state the driver reads and writes, as the owner-draw control
/// kept it, and what the document is bound to.
/// </summary>
struct UILobbyControl
{
	UILobbyControlSpec const * Spec = nullptr;
	bool Dirty = true;

	bool Enabled = true;

	// Text fields and statics.
	std::string Text;
	int Limit = -1;
	bool TextDirty = false;

	bool Checked = false;

	// A track bar kept the Win32 control's range and position until it was subclassed, and
	// its own afterwards: the position relative to the minimum, and a step.
	int PlainMin = 0;
	int PlainMax = 100;
	int PlainPos = 0;
	int Minimum = 0;
	int Range = 0;
	int Value = 0;
	int Step = 0;

	// Lists and combo boxes.
	std::vector<UILobbyItem> Items;
	std::vector<int> Sel;
	int CurSel = -1;
	bool Multiple = false;

	// Message lists, and how many more passes to hold the list at its last line.
	std::vector<UILobbyLineView> Lines;
	int Scroll = 0;

	UILobbyButtonView ButtonView;
	UILobbyCheckView CheckView;
	UILobbyTrackView TrackView;
	UILobbyListView ListView;
	UILobbyComboView ComboView;
	UILobbyTextView TextView;
};


class UILobbyScreen;


class UILobbyView : public UILobbyViewBase
{
	public:
		UILobbyView(UILobbyScreen & screen, UILobbyPresenter & presenter, char const * document) :
			UILobbyViewBase(presenter, document), Screen(screen), Lobby(presenter) {}
		~UILobbyView(void) { Close(); }

		// Hides the base's Close, which knows nothing of the listener this view adds; the
		// screen closes its view through this one.
		void Close(void);

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;

	private:
		// Return in the chat field and a pick in a drop-down are the lobby's own, so they are
		// caught on the way down ahead of the base view's capture listener, which would take
		// Return as an accept and stop it.
		class EarlyListener : public Rml::EventListener
		{
			public:
				EarlyListener(UILobbyView & view) : View(view) {}
				void ProcessEvent(Rml::Event & event) override { View.Process_Early(event); }

			private:
				UILobbyView & View;
		};

		void Process_Early(Rml::Event & event);
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UILobbyScreen & Screen;
		UILobbyPresenter & Lobby;
		EarlyListener Early{*this};

		Rml::String Kind;
		bool Guest = false;
		bool HasPreview = false;
		UILobbyButtonView Absent;

		// Set while the view puts the driver's values into the document, whose controls
		// report those as changes of their own.
		bool Applying = false;

		// A combo box item picked with the mouse is reported from the click, since a pick of
		// the item already chosen changes nothing and raises no change of its own; this keeps
		// the change a new pick does raise from being reported twice.
		int PickedControl = 0;

		// A second press that makes a double click was a double click alone to the list.
		int DownSerial = 0;
};


/// <summary>
/// A game list, host or guest screen, keyed by its handle on the WS_ stack.
/// </summary>
class UILobbyScreen
{
	public:
		UILobbyScreen(void const * handle, UILobbyKind kind, UILobbyHandler handler);
		~UILobbyScreen(void);

		bool Open(void);

		UILobbyControl * Find(int id);

		void Notify(UILobbyControl & control, UILobbyNotifyType type, int value = 0, std::string const & text = std::string());

		void List_Reset(UILobbyControl & control);
		void List_Set_Sel(UILobbyControl & control, bool selected, int index);
		void List_Set_Cur_Sel(UILobbyControl & control, int index);
		bool List_Get_Sel(UILobbyControl const & control, int index) const;
		void List_Select_Range(UILobbyControl & control, bool selected, int first, int last);

		void Track_Set_Range(UILobbyControl & control, int minimum, int maximum);
		void Track_Set_Position(UILobbyControl & control, int position);
		int Track_Get_Position(UILobbyControl const & control) const;

		void Subclass(void);
		void Service(void);

		void Act(UILobbyInput const & input);
		void Act_Key(int action);

		void const * Handle;
		UILobbyKind Kind;
		UILobbyHandler Handler;
		bool Subclassed = false;

		// The press a double click came with, which the list saw as the double click alone.
		int SupersededDown = 0;

		// Set while the screen acts on its inputs, so a close raised by one of them waits.
		int Busy = 0;
		bool Closed = false;

		std::deque<UILobbyControl> Controls;
		UILobbyPresenter Presenter;
		UILobbyView View;
};


static std::vector<std::unique_ptr<UILobbyScreen>> _Screens;


static UILobbyScreen * Screen_Of(void const * handle)
{
	if (handle == nullptr) {
		return(nullptr);
	}

	for (std::unique_ptr<UILobbyScreen> & screen : _Screens) {
		if (screen->Handle == handle && !screen->Closed) {
			return(screen.get());
		}
	}
	return(nullptr);
}


static char const * Document_For(UILobbyKind kind)
{
	return(kind == UI_LOBBY_GAME_LIST ? "gamelist.rml" : "gameopts.rml");
}


UILobbyScreen::UILobbyScreen(void const * handle, UILobbyKind kind, UILobbyHandler handler) :
	Handle(handle), Kind(kind), Handler(handler), View(*this, Presenter, Document_For(kind))
{
	unsigned int const bit = 1u << kind;

	for (UILobbyControlSpec const & spec : _Specs) {
		if ((spec.Kinds & bit) == 0) {
			continue;
		}

		Controls.emplace_back();
		UILobbyControl & control = Controls.back();
		control.Spec = &spec;
		control.Enabled = !(kind == UI_LOBBY_GUEST && spec.GuestDisabled);

		// The host's and the guest's player lists took several rows at once.
		control.Multiple = (spec.Id == IDC_USERS && kind != UI_LOBBY_GAME_LIST);
	}

	Presenter.Act_Input = [this](UILobbyInput const & input) { Act(input); };
	Presenter.Act_Key = [this](int action) { Act_Key(action); };
}


UILobbyScreen::~UILobbyScreen(void)
{
	Presenter.Drop_All();
	View.Close();
}


bool UILobbyScreen::Open(void)
{
	return(View.Open());
}


UILobbyControl * UILobbyScreen::Find(int id)
{
	for (UILobbyControl & control : Controls) {
		if (control.Spec->Id == id) {
			return(&control);
		}
	}
	return(nullptr);
}


// The owner-draw controls sent their notifications synchronously, from inside the call that
// caused them, and so is the handler called.
void UILobbyScreen::Notify(UILobbyControl & control, UILobbyNotifyType type, int value, std::string const & text)
{
	if (Handler == nullptr || Closed) {
		return;
	}

	UILobbyCommand command;
	command.Control = control.Spec->Id;
	command.Notify = type;
	command.Value = value;
	command.Text = text;
	Handler(Handle, command);
}


// LB_RESETCONTENT. A combo box raised nothing for it.
void UILobbyScreen::List_Reset(UILobbyControl & control)
{
	control.Items.clear();
	control.Sel.clear();
	control.CurSel = -1;
	control.Dirty = true;

	if (control.Spec->Type == UI_CONTROL_LIST && Subclassed) {
		Notify(control, UI_LOBBY_SELCHANGE);
	}
}


// LB_SETSEL, with the owner-draw list's clamp of an index past the end to the last row.
void UILobbyScreen::List_Set_Sel(UILobbyControl & control, bool selected, int index)
{
	if (index < -1) {
		return;
	}

	int const count = (int)control.Items.size();
	if (index >= count - 1) {
		index = count - 1;
	}

	if (index >= (int)control.Sel.size()) {
		control.Sel.resize(index + 1, 0);
	}

	if (index == -1) {
		std::fill(control.Sel.begin(), control.Sel.end(), selected ? 1 : 0);
	} else {
		control.Sel[index] = selected ? 1 : 0;
	}
	control.Dirty = true;

	if (Subclassed) {
		Notify(control, UI_LOBBY_SELCHANGE);
	}
}


// LB_SETCURSEL, which unselected the old row and selected the new one through LB_SETSEL, each
// raising its own notification, and then raised one more. CB_SETCURSEL raised nothing.
void UILobbyScreen::List_Set_Cur_Sel(UILobbyControl & control, int index)
{
	int const count = (int)control.Items.size();

	if (control.Spec->Type == UI_CONTROL_COMBO) {
		if (index >= -1 && index < count) {
			control.CurSel = index;
			control.Dirty = true;
		}
		return;
	}

	if (index >= -1 && index < count) {
		if (control.CurSel != -1) {
			List_Set_Sel(control, false, control.CurSel);
		}
		control.CurSel = index;
		if (index != -1) {
			List_Set_Sel(control, true, index);
		}
	}
	control.Dirty = true;

	if (Subclassed) {
		Notify(control, UI_LOBBY_SELCHANGE);
	}
}


bool UILobbyScreen::List_Get_Sel(UILobbyControl const & control, int index) const
{
	if (index < 0 || index >= (int)control.Sel.size()) {
		return(false);
	}
	return(control.Sel[index] != 0);
}


// LB_SELITEMRANGE.
void UILobbyScreen::List_Select_Range(UILobbyControl & control, bool selected, int first, int last)
{
	if (last < 0 || last < first) {
		return;
	}

	int const count = (int)control.Items.size();
	if (last >= count) {
		last = count - 1;
	}

	if (last >= (int)control.Sel.size()) {
		control.Sel.resize(last + 1, 0);
	}

	for (int index = std::max(first, 0); index <= last; index++) {
		control.Sel[index] = selected ? 1 : 0;
	}
	control.Dirty = true;

	if (Subclassed) {
		Notify(control, UI_LOBBY_SELCHANGE);
	}
}


/// <summary>
/// TBM_SETRANGE. Once subclassed, the owner-draw track bar kept its position relative to the
/// minimum and, having clamped it against the new range, clamped it again against the new
/// minimum itself, so a track bar left at its minimum moved one step past it. It reported
/// the change like any other.
/// </summary>
void UILobbyScreen::Track_Set_Range(UILobbyControl & control, int minimum, int maximum)
{
	control.Dirty = true;

	if (!Subclassed) {
		control.PlainMin = (short)minimum;
		control.PlainMax = (short)maximum;
		return;
	}

	int const oldminimum = control.Minimum;
	int const oldrange = control.Range;
	int const oldvalue = control.Value;

	control.Minimum = (unsigned short)minimum;
	control.Range = (unsigned short)maximum - control.Minimum;
	if (control.Value > control.Range) {
		control.Value = control.Range;
	}
	if (control.Value < control.Minimum) {
		control.Value = control.Minimum;
	}

	if (control.Value != oldvalue || control.Range != oldrange || control.Minimum != oldminimum) {
		Notify(control, UI_LOBBY_SCROLLED, Track_Get_Position(control));
	}
}


// TBM_SETPOS. An owner-draw track bar ignored a position outside its range.
void UILobbyScreen::Track_Set_Position(UILobbyControl & control, int position)
{
	control.Dirty = true;

	if (!Subclassed) {
		control.PlainPos = std::min(std::max(position, control.PlainMin), control.PlainMax);
		return;
	}

	int const oldvalue = control.Value;
	if (position - control.Minimum <= control.Range && position - control.Minimum >= 0) {
		control.Value = position - control.Minimum;
	}

	if (control.Value != oldvalue) {
		Notify(control, UI_LOBBY_SCROLLED, Track_Get_Position(control));
	}
}


// TBM_GETPOS, rounded down to the step as the owner-draw track bar rounded it.
int UILobbyScreen::Track_Get_Position(UILobbyControl const & control) const
{
	if (!Subclassed) {
		return(control.PlainPos);
	}

	int const step = (control.Step != 0) ? control.Step : 1;
	return(step * ((control.Minimum + control.Value) / step));
}


/// <summary>
/// What subclassing did to the owner-draw controls: a track bar took over the Win32
/// control's range and position, and a list forgot its selection.
/// </summary>
void UILobbyScreen::Subclass(void)
{
	for (UILobbyControl & control : Controls) {
		switch (control.Spec->Type) {
			case UI_CONTROL_TRACK:
				control.Minimum = control.PlainMin;
				control.Range = control.PlainMax - control.PlainMin;
				control.Value = control.PlainPos - control.PlainMin;
				if (control.Range == 0) {
					control.Range = 100;
				}
				break;

			case UI_CONTROL_LIST:
				control.CurSel = -1;
				break;

			default:
				break;
		}
		control.Dirty = true;
	}

	Subclassed = true;
}


/// <summary>
/// Acts on one thing the player did, as the owner-draw control that took the click or key
/// acted on it.
/// </summary>
void UILobbyScreen::Act(UILobbyInput const & input)
{
	UILobbyControl * control = Find(input.Control);
	if (control == nullptr || Closed) {
		return;
	}

	// A disabled window took no clicks.
	if (!control->Enabled && input.Kind != UILobbyInput::TEXT && input.Kind != UILobbyInput::ENTER) {
		return;
	}

	switch (input.Kind) {
		case UILobbyInput::CLICK:
			Notify(*control, UI_LOBBY_CLICKED);
			break;

		case UILobbyInput::CHECK:
			control->Checked = (input.Value != 0);
			control->Dirty = true;
			Sound_Effect(Rule->GenericClick);
			Notify(*control, UI_LOBBY_CLICKED, control->Checked ? 1 : 0);
			break;

		case UILobbyInput::SCROLL: {
			int value = input.Value - control->Minimum;
			value = std::min(std::max(value, 0), control->Range);
			if (value != control->Value) {
				control->Value = value;
				control->Dirty = true;
				Sound_Effect(Rule->GenericClick);
				Notify(*control, UI_LOBBY_SCROLLED, Track_Get_Position(*control));
			}
			break;
		}

		case UILobbyInput::PICK:
			// The drop-down clicked, set the selection, closed and told the dialog, whether
			// or not the pick changed anything.
			Sound_Effect(Rule->GenericClick);
			if (input.Value >= 0 && input.Value < (int)control->Items.size()) {
				control->CurSel = input.Value;
			}
			control->Dirty = true;
			Notify(*control, UI_LOBBY_SELCHANGE, control->CurSel);
			break;

		case UILobbyInput::TEXT:
			control->Text = input.Text;
			Notify(*control, UI_LOBBY_TEXT_CHANGED, 0, control->Text);
			break;

		case UILobbyInput::ENTER:
			// The edit held the line break it had added when it told the dialog.
			control->Text = input.Text;
			Notify(*control, UI_LOBBY_TEXT_ENTERED, 0, input.Text);
			break;

		case UILobbyInput::DOWN: {
			if (input.Serial == SupersededDown) {
				break;
			}

			// A press past the last row still named a row, one the list had no entry for.
			int const row = (input.Row >= 0) ? input.Row : (int)control->Items.size();

			if (input.Button == 1) {
				List_Set_Sel(*control, false, -1);
				List_Set_Cur_Sel(*control, -1);
				Notify(*control, UI_LOBBY_SELCHANGE);
				break;
			}

			if (control->Multiple) {
				bool const select = !List_Get_Sel(*control, row);
				Sound_Effect(Rule->GenericClick);
				List_Set_Sel(*control, select, row);
			} else {
				Sound_Effect(Rule->GenericClick);
				List_Set_Cur_Sel(*control, row);
			}
			Notify(*control, UI_LOBBY_SELCHANGE);
			break;
		}

		case UILobbyInput::DOUBLE:
			Notify(*control, UI_LOBBY_DBLCLK);
			break;

		default:
			break;
	}
}


// Escape reached the dialog as IDCANCEL through the dialog manager; Enter as IDOK, which no
// lobby dialog acted on.
void UILobbyScreen::Act_Key(int action)
{
	if (action != UI_LOBBY_ACTION_CANCEL) {
		return;
	}

	UILobbyControl * cancel = Find(DIALOG_CANCEL);
	if (cancel != nullptr && cancel->Enabled) {
		Notify(*cancel, UI_LOBBY_CLICKED);
	}
}


void UILobbyScreen::Service(void)
{
	if (Closed) {
		return;
	}

	Busy++;
	Presenter.Drain();
	Busy--;

	if (Closed) {
		return;
	}

	View.Sync();
}


static void Refresh_View(UILobbyScreen & screen, UILobbyControl & control)
{
	switch (control.Spec->Type) {
		case UI_CONTROL_BUTTON:
			control.ButtonView.On = control.Enabled;
			break;

		case UI_CONTROL_CHECK:
			control.CheckView.Checked = control.Checked;
			control.CheckView.On = control.Enabled;
			break;

		case UI_CONTROL_TRACK: {
			UILobbyTrackView & view = control.TrackView;
			if (screen.Subclassed) {
				view.Min = control.Minimum;
				view.Max = control.Minimum + control.Range;
			} else {
				view.Min = control.PlainMin;
				view.Max = control.PlainMax;
			}
			view.Step = (control.Step != 0) ? control.Step : 1;
			view.Value = screen.Track_Get_Position(control);
			view.On = control.Enabled;
			break;
		}

		case UI_CONTROL_LIST: {
			std::vector<UILobbyRowView> & rows = control.ListView.Rows;
			rows.clear();
			for (int index = 0; index < (int)control.Items.size(); index++) {
				UILobbyItem const & item = control.Items[index];
				UILobbyRowView row;
				row.Text = item.Text;
				row.Color = Css_Color(item.Color);
				row.Emblem = Picture_Source(item.Emblem);
				row.Mark = Picture_Source(item.Mark);
				row.Picked = screen.List_Get_Sel(control, index);
				rows.push_back(row);
			}
			control.ListView.On = control.Enabled;
			break;
		}

		case UI_CONTROL_COMBO: {
			std::vector<UILobbyOptionView> & items = control.ComboView.Items;
			items.clear();
			for (int index = 0; index < (int)control.Items.size(); index++) {
				UILobbyOptionView option;
				option.Text = control.Items[index].Text;
				option.Color = Css_Color(control.Items[index].Color);
				option.Value = std::to_string(index);
				items.push_back(option);
			}
			control.ComboView.Value = std::to_string(control.CurSel);
			control.ComboView.On = control.Enabled;
			break;
		}

		case UI_CONTROL_EDIT:
		case UI_CONTROL_TEXT:
			control.TextView.Text = control.Text;
			control.TextView.Limit = control.Limit;
			break;

		case UI_CONTROL_MESSAGES:
			control.ListView.Lines = control.Lines;
			break;
	}
}


bool UILobbyView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel(ModelName);
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UILobbyButtonView>(constructor)) {
		handle.RegisterMember("On", &UILobbyButtonView::On);
	}
	if (auto handle = UI_Register_Struct<UILobbyCheckView>(constructor)) {
		handle.RegisterMember("Checked", &UILobbyCheckView::Checked);
		handle.RegisterMember("On", &UILobbyCheckView::On);
	}
	if (auto handle = UI_Register_Struct<UILobbyTrackView>(constructor)) {
		handle.RegisterMember("Min", &UILobbyTrackView::Min);
		handle.RegisterMember("Max", &UILobbyTrackView::Max);
		handle.RegisterMember("Step", &UILobbyTrackView::Step);
		handle.RegisterMember("Value", &UILobbyTrackView::Value);
		handle.RegisterMember("On", &UILobbyTrackView::On);
	}
	if (auto handle = UI_Register_Struct<UILobbyRowView>(constructor)) {
		handle.RegisterMember("Text", &UILobbyRowView::Text);
		handle.RegisterMember("Color", &UILobbyRowView::Color);
		handle.RegisterMember("Emblem", &UILobbyRowView::Emblem);
		handle.RegisterMember("Mark", &UILobbyRowView::Mark);
		handle.RegisterMember("Picked", &UILobbyRowView::Picked);
	}
	UI_Register_Array<std::vector<UILobbyRowView>>(constructor);
	if (auto handle = UI_Register_Struct<UILobbyLineView>(constructor)) {
		handle.RegisterMember("Text", &UILobbyLineView::Text);
		handle.RegisterMember("Color", &UILobbyLineView::Color);
	}
	UI_Register_Array<std::vector<UILobbyLineView>>(constructor);
	if (auto handle = UI_Register_Struct<UILobbyListView>(constructor)) {
		handle.RegisterMember("Rows", &UILobbyListView::Rows);
		handle.RegisterMember("Lines", &UILobbyListView::Lines);
		handle.RegisterMember("On", &UILobbyListView::On);
	}
	if (auto handle = UI_Register_Struct<UILobbyOptionView>(constructor)) {
		handle.RegisterMember("Text", &UILobbyOptionView::Text);
		handle.RegisterMember("Color", &UILobbyOptionView::Color);
		handle.RegisterMember("Value", &UILobbyOptionView::Value);
	}
	UI_Register_Array<std::vector<UILobbyOptionView>>(constructor);
	if (auto handle = UI_Register_Struct<UILobbyComboView>(constructor)) {
		handle.RegisterMember("Items", &UILobbyComboView::Items);
		handle.RegisterMember("Value", &UILobbyComboView::Value);
		handle.RegisterMember("On", &UILobbyComboView::On);
	}
	if (auto handle = UI_Register_Struct<UILobbyTextView>(constructor)) {
		handle.RegisterMember("Text", &UILobbyTextView::Text);
		handle.RegisterMember("Limit", &UILobbyTextView::Limit);
	}

	for (UILobbyControl & control : Screen.Controls) {
		char const * const name = control.Spec->Name;

		switch (control.Spec->Type) {
			case UI_CONTROL_BUTTON: constructor.Bind(name, &control.ButtonView); break;
			case UI_CONTROL_CHECK: constructor.Bind(name, &control.CheckView); break;
			case UI_CONTROL_TRACK: constructor.Bind(name, &control.TrackView); break;
			case UI_CONTROL_LIST: constructor.Bind(name, &control.ListView); break;
			case UI_CONTROL_MESSAGES: constructor.Bind(name, &control.ListView); break;
			case UI_CONTROL_COMBO: constructor.Bind(name, &control.ComboView); break;
			case UI_CONTROL_EDIT: constructor.Bind(name, &control.TextView); break;
			case UI_CONTROL_TEXT: constructor.Bind(name, &control.TextView); break;
		}
	}

	Kind = (Screen.Kind == UI_LOBBY_HOST) ? "host" : (Screen.Kind == UI_LOBBY_GUEST) ? "guest" : "list";
	Guest = (Screen.Kind == UI_LOBBY_GUEST);
	HasPreview = Has_Preview();

	// The document serves both setups, so it names the controls of each; the other kind's
	// are bound to stand-ins that are never shown.
	for (UILobbyControlSpec const & spec : _Specs) {
		if (Screen.Find(spec.Id) == nullptr && spec.Type == UI_CONTROL_BUTTON) {
			constructor.Bind(spec.Name, &Absent);
		}
	}

	constructor.Bind("Kind", &Kind);
	constructor.Bind("Guest", &Guest);
	constructor.Bind("HasPreview", &HasPreview);

	constructor.BindEventCallback("act", &UILobbyView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UILobbyView::Bind(void)
{
	// Listeners at one element run in the order they were added.
	Document->AddEventListener(Rml::EventId::Mousedown, &Early, true);
	Document->AddEventListener(Rml::EventId::Keydown, &Early, true);
	Document->AddEventListener(Rml::EventId::Click, &Early, true);
	Attach_Actions();
	return(true);
}


void UILobbyView::Close(void)
{
	if (Document != nullptr) {
		Document->RemoveEventListener(Rml::EventId::Mousedown, &Early, true);
		Document->RemoveEventListener(Rml::EventId::Keydown, &Early, true);
		Document->RemoveEventListener(Rml::EventId::Click, &Early, true);
	}
	UILobbyViewBase::Close();
}


static UILobbyControl * Control_Named(UILobbyScreen & screen, Rml::String const & name)
{
	for (UILobbyControl & control : screen.Controls) {
		if (name == control.Spec->Name) {
			return(&control);
		}
	}
	return(nullptr);
}


void UILobbyView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.size() < 2) {
		return;
	}

	Rml::String const action = arguments[0].Get<Rml::String>();
	UILobbyControl * control = Control_Named(Screen, arguments[1].Get<Rml::String>());
	if (control == nullptr) {
		return;
	}

	UILobbyInput input;
	input.Control = control->Spec->Id;

	if (action == "click") {
		input.Kind = UILobbyInput::CLICK;
	} else if (action == "check") {
		// The new state is read from the event; see UISoundView.
		input.Kind = UILobbyInput::CHECK;
		input.Value = event.GetParameter<bool>("checked", !control->Checked) ? 1 : 0;

		// A check the driver set raises the same event as a click, and BM_SETCHECK raised
		// nothing.
		if ((input.Value != 0) == control->Checked) {
			return;
		}
	} else if (action == "scroll") {
		input.Kind = UILobbyInput::SCROLL;
		input.Value = (int)(event.GetParameter<float>("value", 0.0f) + 0.5f);
		if (Applying || input.Value == Screen.Track_Get_Position(*control)) {
			return;
		}
	} else if (action == "pick") {
		// A pick is reported from the click that made it. Any other change is the document
		// setting the box, which the box is put back from when it is not the driver's.
		if (PickedControl == input.Control) {
			PickedControl = 0;
			return;
		}
		Rml::String const value = event.GetParameter<Rml::String>("value", "-1");
		if (std::atoi(value.c_str()) != control->CurSel) {
			control->Dirty = true;
		}
		return;
	} else if (action == "text") {
		input.Kind = UILobbyInput::TEXT;
		input.Text = event.GetParameter<Rml::String>("value", "");
	} else if (action == "down" || action == "double") {
		input.Kind = (action == "down") ? UILobbyInput::DOWN : UILobbyInput::DOUBLE;
		input.Button = event.GetParameter<int>("button", 0);

		for (Rml::Element * walk = event.GetTargetElement(); walk != nullptr && walk != event.GetCurrentElement(); walk = walk->GetParentNode()) {
			if (walk->HasAttribute("data-row")) {
				input.Row = walk->GetAttribute<int>("data-row", -1);
				break;
			}
		}

		if (input.Kind == UILobbyInput::DOWN) {
			input.Serial = ++DownSerial;
		} else {
			Screen.SupersededDown = DownSerial;
		}
	} else {
		return;
	}

	Lobby.Queue_Input(input);
}


void UILobbyView::Process_Early(Rml::Event & event)
{
	if (!Document->IsVisible()) {
		return;
	}

	Rml::Element * const target = event.GetTargetElement();

	if (event.GetId() == Rml::EventId::Mousedown || event.GetId() == Rml::EventId::Keydown) {
		PickedControl = 0;
	}

	// Return in a chat field was the owner-draw edit adding a line break to its text and
	// telling the dialog its text was full.
	if (event.GetId() == Rml::EventId::Keydown && target != nullptr && target->GetId() == "input") {
		int const key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
		if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
			auto * field = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(target);

			// WM_GETTEXT with room for 255 characters, the line break stripped and put
			// back only when it was among them.
			std::string text = (field != nullptr ? field->GetValue() : Rml::String()) + "\r\n";
			text.resize(UTF8::Boundary_Before(text.c_str(), 255));
			std::string stripped;
			for (char c : text) {
				if (c != '\r' && c != '\n') {
					stripped += c;
				}
			}
			if (stripped.size() != text.size()) {
				stripped += "\r\n";
			}

			UILobbyControl * control = Screen.Find(IDC_INPUT);
			if (control != nullptr) {
				UILobbyInput input;
				input.Kind = UILobbyInput::ENTER;
				input.Control = IDC_INPUT;
				input.Text = stripped;
				Lobby.Queue_Input(input);
			}
			event.StopImmediatePropagation();
			return;
		}
	}

	// A pick in a drop-down, caught before the drop-down acts on it.
	if (event.GetId() == Rml::EventId::Click && target != nullptr) {
		Rml::Element * option = nullptr;
		for (Rml::Element * walk = target; walk != nullptr && walk != Document; walk = walk->GetParentNode()) {
			if (option == nullptr && walk->GetTagName() == "option") {
				option = walk;
			}
			if (option != nullptr && walk->GetTagName() == "select") {
				UILobbyControl * control = Control_Named(Screen, walk->GetId());
				if (control != nullptr && control->Enabled && !walk->HasAttribute("disabled")) {
					UILobbyInput input;
					input.Kind = UILobbyInput::PICK;
					input.Control = control->Spec->Id;
					input.Value = std::atoi(option->GetAttribute<Rml::String>("value", "-1").c_str());
					Lobby.Queue_Input(input);
					PickedControl = input.Control;
				}
				break;
			}
		}
	}
}


void UILobbyView::Sync(void)
{
	if (!Model || Document == nullptr) {
		return;
	}

	Check_Preview();

	bool const haspreview = Has_Preview();
	if (haspreview != HasPreview) {
		HasPreview = haspreview;
		Model.DirtyVariable("HasPreview");
	}

	bool const first = !Pushed;
	Pushed = true;

	for (UILobbyControl & control : Screen.Controls) {
		if (!control.Dirty && !first) {
			continue;
		}
		control.Dirty = false;

		Refresh_View(Screen, control);
		Model.DirtyVariable(control.Spec->Name);

		if (control.Spec->Type == UI_CONTROL_MESSAGES) {
			control.Scroll = 2;
		}

		// A range clamps its value to the range it has at the moment, so the range goes in
		// before the value, in that order, rather than in whatever order bindings apply.
		if (control.Spec->Type == UI_CONTROL_TRACK) {
			Rml::Element * box = Document->GetElementById(control.Spec->Name);
			Rml::Element * range = nullptr;
			for (int child = 0; box != nullptr && child < box->GetNumChildren(); child++) {
				if (box->GetChild(child)->GetTagName() == "input") {
					range = box->GetChild(child);
				}
			}
			if (range != nullptr) {
				UILobbyTrackView const & view = control.TrackView;
				Applying = true;
				range->SetAttribute("min", view.Min);
				range->SetAttribute("max", view.Max);
				range->SetAttribute("step", view.Step);
				range->SetAttribute("value", view.Value);
				if (view.On) {
					range->RemoveAttribute("disabled");
				} else {
					range->SetAttribute("disabled", "");
				}
				Applying = false;
			}
		}
	}

	for (UILobbyControl & control : Screen.Controls) {
		if (control.Spec->Type == UI_CONTROL_EDIT && (control.TextDirty || first)) {
			control.TextDirty = false;
			auto * field = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(Document->GetElementById(control.Spec->Name));
			if (field != nullptr && field->GetValue() != control.Text) {
				field->SetValue(control.Text);
			}
		}
	}
}


// A message list scrolled to its last line whenever a line was added. The new line is laid
// out by the next update, so the list is held at its end for the pass after that as well.
static void Scroll_Messages(UILobbyScreen & screen)
{
	UILobbyControl * control = screen.Find(IDC_PMESSAGES);
	Rml::ElementDocument * document = screen.View.Get_Document();
	if (control == nullptr || control->Scroll <= 0 || document == nullptr) {
		return;
	}

	Rml::Element * list = document->GetElementById("messages");
	if (list != nullptr) {
		list->SetScrollTop(list->GetScrollHeight());
	}
	control->Scroll--;
}


bool UI_Lobby_Open(void const * screen, UILobbyKind kind, UILobbyHandler handler)
{
	if (!UI_Is_Initialized() || screen == nullptr || Screen_Of(screen) != nullptr) {
		return(false);
	}

	Register_Surfaces();

	std::unique_ptr<UILobbyScreen> created(new UILobbyScreen(screen, kind, handler));
	if (!created->Open()) {
		return(false);
	}

	_Screens.push_back(std::move(created));
	UI_Begin_Modal();
	return(true);
}


static void Collect_Closed(void)
{
	for (auto walk = _Screens.begin(); walk != _Screens.end(); ) {
		if ((*walk)->Closed && (*walk)->Busy == 0) {
			walk = _Screens.erase(walk);
		} else {
			++walk;
		}
	}
}


void UI_Lobby_Close(void const * screen)
{
	UILobbyScreen * found = Screen_Of(screen);
	if (found == nullptr) {
		return;
	}

	found->Closed = true;
	found->Presenter.Drop_All();
	found->View.Close();

	Collect_Closed();
	UI_End_Modal();
}


bool UI_Lobby_Is_Open(void const * screen)
{
	return(Screen_Of(screen) != nullptr);
}


void UI_Lobby_Show(void const * screen, bool show)
{
	UILobbyScreen * found = Screen_Of(screen);
	if (found == nullptr) {
		return;
	}

	found->View.Sync();
	if (show) {
		found->View.Show(true);
	} else {
		found->View.Hide();
	}
}


void UI_Lobby_Subclass(void const * screen)
{
	UILobbyScreen * found = Screen_Of(screen);
	if (found != nullptr) {
		found->Subclass();
	}
}


void UI_Lobby_Service(void const * screen)
{
	UILobbyScreen * found = Screen_Of(screen);
	if (found == nullptr) {
		return;
	}

	found->Service();
	Collect_Closed();

	found = Screen_Of(screen);
	if (found != nullptr) {
		Scroll_Messages(*found);
	}

	// The lobby's own loop presents nothing, since a dialog put itself on the screen.
	UI_Mark_Overlay_Dirty();
	Video_Present_If_Dirty();
}


void UI_Lobby_Service_All(void)
{
	std::vector<void const *> handles;
	for (std::unique_ptr<UILobbyScreen> & screen : _Screens) {
		if (!screen->Closed) {
			handles.push_back(screen->Handle);
		}
	}

	for (void const * handle : handles) {
		UI_Lobby_Service(handle);
	}
}


// Brings the lobby screens up to date without acting on anything, for a box shown over them.
static void Sync_All(void)
{
	for (std::unique_ptr<UILobbyScreen> & screen : _Screens) {
		if (!screen->Closed) {
			screen->View.Sync();
			Scroll_Messages(*screen);
		}
	}
}


static UILobbyControl * Control_Of(void const * screen, int control)
{
	UILobbyScreen * found = Screen_Of(screen);
	return((found != nullptr) ? found->Find(control) : nullptr);
}


bool UI_Lobby_Has_Control(void const * screen, int control)
{
	return(Control_Of(screen, control) != nullptr);
}


void UI_Lobby_Enable(void const * screen, int control, bool enable)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr && found->Enabled != enable) {
		found->Enabled = enable;
		found->Dirty = true;
	}
}


bool UI_Lobby_Is_Enabled(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return(found != nullptr && found->Enabled);
}


void UI_Lobby_Set_Text(void const * screen, int control, char const * text)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found == nullptr) {
		return;
	}

	found->Text = (text != nullptr) ? text : "";
	found->TextDirty = true;
	found->Dirty = true;
}


std::string UI_Lobby_Get_Text(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return((found != nullptr) ? found->Text : std::string());
}


void UI_Lobby_Set_Limit(void const * screen, int control, int limit)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr) {
		found->Limit = limit;
		found->Dirty = true;
	}
}


void UI_Lobby_Set_Check(void const * screen, int control, bool checked)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr) {
		found->Checked = checked;
		found->Dirty = true;
	}
}


bool UI_Lobby_Get_Check(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return(found != nullptr && found->Checked);
}


void UI_Lobby_Set_Range(void const * screen, int control, int minimum, int maximum)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * track = (found != nullptr) ? found->Find(control) : nullptr;
	if (track != nullptr && track->Spec->Type == UI_CONTROL_TRACK) {
		found->Track_Set_Range(*track, minimum, maximum);
	}
}


void UI_Lobby_Set_Step(void const * screen, int control, int step)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr && found->Spec->Type == UI_CONTROL_TRACK) {
		found->Step = step;
		found->Dirty = true;
	}
}


void UI_Lobby_Set_Position(void const * screen, int control, int position)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * track = (found != nullptr) ? found->Find(control) : nullptr;
	if (track != nullptr && track->Spec->Type == UI_CONTROL_TRACK) {
		found->Track_Set_Position(*track, position);
	}
}


int UI_Lobby_Get_Position(void const * screen, int control)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * track = (found != nullptr) ? found->Find(control) : nullptr;
	if (track == nullptr || track->Spec->Type != UI_CONTROL_TRACK) {
		return(0);
	}
	return(found->Track_Get_Position(*track));
}


void UI_Lobby_Reset(void const * screen, int control)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * list = (found != nullptr) ? found->Find(control) : nullptr;
	if (list != nullptr && (list->Spec->Type == UI_CONTROL_LIST || list->Spec->Type == UI_CONTROL_COMBO)) {
		found->List_Reset(*list);
	}
}


void UI_Lobby_Add(void const * screen, int control, UILobbyItem const & item)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr && (found->Spec->Type == UI_CONTROL_LIST || found->Spec->Type == UI_CONTROL_COMBO)) {
		found->Items.push_back(item);
		found->Dirty = true;
	}
}


int UI_Lobby_Get_Count(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return((found != nullptr) ? (int)found->Items.size() : 0);
}


std::string UI_Lobby_Get_Item_Text(void const * screen, int control, int index)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found == nullptr || index < 0 || index >= (int)found->Items.size()) {
		return(std::string());
	}
	return(found->Items[index].Text);
}


void UI_Lobby_Set_Item_Data(void const * screen, int control, int index, int data)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr && index >= 0 && index < (int)found->Items.size()) {
		found->Items[index].Data = data;
	}
}


void UI_Lobby_Set_Item_Color(void const * screen, int control, int index, int color)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found != nullptr && index >= 0 && index < (int)found->Items.size()) {
		found->Items[index].Color = color;
		found->Dirty = true;
	}
}


int UI_Lobby_Get_Item_Data(void const * screen, int control, int index)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found == nullptr || index < 0 || index >= (int)found->Items.size()) {
		return(LOBBY_ERR);
	}
	return(found->Items[index].Data);
}


void UI_Lobby_Set_Cur_Sel(void const * screen, int control, int index)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * list = (found != nullptr) ? found->Find(control) : nullptr;
	if (list != nullptr) {
		found->List_Set_Cur_Sel(*list, index);
	}
}


int UI_Lobby_Get_Cur_Sel(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return((found != nullptr) ? found->CurSel : -1);
}


void UI_Lobby_Set_Sel(void const * screen, int control, bool selected, int index)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * list = (found != nullptr) ? found->Find(control) : nullptr;
	if (list != nullptr) {
		found->List_Set_Sel(*list, selected, index);
	}
}


bool UI_Lobby_Get_Sel(void const * screen, int control, int index)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * list = (found != nullptr) ? found->Find(control) : nullptr;
	return(list != nullptr && found->List_Get_Sel(*list, index));
}


void UI_Lobby_Select_Range(void const * screen, int control, bool selected, int first, int last)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * list = (found != nullptr) ? found->Find(control) : nullptr;
	if (list != nullptr) {
		found->List_Select_Range(*list, selected, first, last);
	}
}


bool UI_Lobby_Is_Multiple(void const * screen, int control)
{
	UILobbyControl * found = Control_Of(screen, control);
	return(found != nullptr && found->Multiple);
}


bool UI_Lobby_Is_Dropped(void const * screen, int control)
{
	UILobbyScreen * found = Screen_Of(screen);
	UILobbyControl * combo = (found != nullptr) ? found->Find(control) : nullptr;
	Rml::ElementDocument * document = (found != nullptr) ? found->View.Get_Document() : nullptr;
	if (combo == nullptr || document == nullptr) {
		return(false);
	}

	auto * select = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(document->GetElementById(combo->Spec->Name));
	return(select != nullptr && select->IsSelectBoxVisible());
}


/// <summary>
/// Adds a message the way the owner-draw message list took it: a line for each line break,
/// each cut at its first break, the oldest line dropped once the list holds more than 500. A
/// line too wide for the list wraps within its row rather than taking a row of its own.
/// </summary>
void UI_Lobby_Add_Message(void const * screen, int control, char const * text, int color)
{
	UILobbyControl * found = Control_Of(screen, control);
	if (found == nullptr || text == nullptr) {
		return;
	}

	std::string remaining = text;
	while (!remaining.empty()) {
		std::size_t const newline = remaining.find('\n');
		std::string line = (newline == std::string::npos) ? remaining : remaining.substr(0, newline + 1);
		remaining.erase(0, line.size());

		std::size_t const end = line.find_first_of("\r\n");
		if (end != std::string::npos) {
			line.resize(end);
		}

		if (found->Lines.size() > 500) {
			found->Lines.erase(found->Lines.begin());
		}

		UILobbyLineView view;
		view.Text = line;
		view.Color = Css_Color(color);
		found->Lines.push_back(view);
	}

	found->Dirty = true;
}


void UI_Lobby_Preview_Changed(void)
{
	UI_Surface_Invalidate("lobby-preview");
}


/*
 * The message box.
 */

class UINetMessageBox;


class UINetMessageBoxView : public UILobbyViewBase
{
	public:
		UINetMessageBoxView(UINetMessageBox & box, UILobbyPresenter & presenter) :
			UILobbyViewBase(presenter, "netmsgbox.rml"), Box(box), Lobby(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UINetMessageBox & Box;
		UILobbyPresenter & Lobby;
};


class UINetMessageBox
{
	public:
		UINetMessageBox(void const * handle) : Handle(handle), View(*this, Presenter)
		{
			Presenter.Act_Input = [this](UILobbyInput const & input) { Press(input.Control); };
			Presenter.Act_Key = [this](int action) {
				Press(action == UI_LOBBY_ACTION_ACCEPT ? DIALOG_OK : DIALOG_CANCEL);
			};
		}

		~UINetMessageBox(void)
		{
			Presenter.Drop_All();
			View.Close();
		}

		// The first button the dialog saw destroyed it; nothing reached it after that.
		void Press(int id)
		{
			if (Pressed == 0) {
				Pressed = id;
			}
		}

		void const * Handle;
		Rml::String Message;
		Rml::String Layout;
		bool Ok = false;
		bool Cancel = false;
		bool Yes = false;
		bool No = false;
		int Pressed = 0;

		UILobbyPresenter Presenter;
		UINetMessageBoxView View;
};


bool UINetMessageBoxView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel(ModelName);
	if (!constructor) {
		return(false);
	}

	constructor.Bind("Message", &Box.Message);
	constructor.Bind("Layout", &Box.Layout);
	constructor.Bind("Ok", &Box.Ok);
	constructor.Bind("Cancel", &Box.Cancel);
	constructor.Bind("Yes", &Box.Yes);
	constructor.Bind("No", &Box.No);
	constructor.BindEventCallback("act", &UINetMessageBoxView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UINetMessageBoxView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UINetMessageBoxView::On_Action(Rml::DataModelHandle, Rml::Event &, Rml::VariantList const & arguments)
{
	if (arguments.size() < 2) {
		return;
	}

	Rml::String const name = arguments[1].Get<Rml::String>();
	UILobbyInput input;

	if (name == "ok") {
		input.Control = DIALOG_OK;
	} else if (name == "cancel") {
		input.Control = DIALOG_CANCEL;
	} else if (name == "yes") {
		input.Control = DIALOG_YES;
	} else if (name == "no") {
		input.Control = DIALOG_NO;
	} else {
		return;
	}

	Lobby.Queue_Input(input);
}


void UINetMessageBoxView::Sync(void)
{
	if (Model && !Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
	}
}


static std::vector<std::unique_ptr<UINetMessageBox>> _Boxes;


static UINetMessageBox * Box_Of(void const * handle)
{
	for (std::unique_ptr<UINetMessageBox> & box : _Boxes) {
		if (box->Handle == handle) {
			return(box.get());
		}
	}
	return(nullptr);
}


// Closing a document leaves the focus nowhere, so the newest lobby screen still showing takes
// it back.
static void Refocus_Lobby(void)
{
	for (auto walk = _Screens.rbegin(); walk != _Screens.rend(); ++walk) {
		Rml::ElementDocument * document = (*walk)->View.Get_Document();
		if (!(*walk)->Closed && document != nullptr && document->IsVisible()) {
			document->Show(Rml::ModalFlag::Modal);
			return;
		}
	}
}


bool UI_Net_Message_Box_Open(void const * screen, int id, char const * text, bool ok, bool cancel, bool yes, bool no)
{
	if (!UI_Is_Initialized() || screen == nullptr || Box_Of(screen) != nullptr) {
		return(false);
	}

	std::unique_ptr<UINetMessageBox> box(new UINetMessageBox(screen));

	box->Message = (text != nullptr) ? text : "";
	box->Layout = (id == IDD_MSGBOX_2) ? "two" : (id == IDD_MSGBOX_3_LARGE) ? "large" : "small";
	box->Ok = ok;
	box->Cancel = cancel;
	box->Yes = yes;
	box->No = no;

	if (!box->View.Open()) {
		return(false);
	}

	box->View.Sync();
	box->View.Show(true);

	_Boxes.push_back(std::move(box));
	UI_Begin_Modal();
	return(true);
}


void UI_Net_Message_Box_Close(void const * screen)
{
	for (auto walk = _Boxes.begin(); walk != _Boxes.end(); ++walk) {
		if ((*walk)->Handle == screen) {
			_Boxes.erase(walk);
			UI_End_Modal();
			Refocus_Lobby();
			return;
		}
	}
}


int UI_Net_Message_Box_Service(void const * screen)
{
	UINetMessageBox * box = Box_Of(screen);
	if (box == nullptr) {
		return(0);
	}

	box->Presenter.Drain();
	box->View.Sync();

	// The lobby's dialogs went on showing the messages that arrived under the box.
	Sync_All();

	UI_Mark_Overlay_Dirty();

	int const pressed = box->Pressed;
	box->Pressed = 0;
	return(pressed);
}


/*
 * The map picker.
 */

class UIMapSelect;


class UIMapSelectView : public UILobbyViewBase
{
	public:
		UIMapSelectView(UIMapSelect & pick, UILobbyPresenter & presenter) :
			UILobbyViewBase(presenter, "mapselect.rml"), Pick(pick), Lobby(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIMapSelect & Pick;
		UILobbyPresenter & Lobby;
		bool HasPreview = false;
};


class UIMapSelect
{
	public:
		UIMapSelect(void const * handle) : Handle(handle), View(*this, Presenter)
		{
			Presenter.Act_Input = [this](UILobbyInput const & input) { Act(input); };
			Presenter.Act_Key = [this](int action) {
				Press(action == UI_LOBBY_ACTION_ACCEPT ? DIALOG_OK : DIALOG_CANCEL);
			};
		}

		~UIMapSelect(void)
		{
			Presenter.Drop_All();
			View.Close();
		}

		void Press(int id)
		{
			if (Pressed == 0) {
				Pressed = id;
			}
		}

		// The list was owner-draw like the lobby's, and its dialog acted on none of its
		// notifications.
		void Act(UILobbyInput const & input)
		{
			if (input.Kind != UILobbyInput::DOWN) {
				Press(input.Control);
				return;
			}

			if (input.Button == 1) {
				Selected = -1;
			} else {
				Sound_Effect(Rule->GenericClick);
				if (input.Row >= 0 && input.Row < (int)Maps.size()) {
					Selected = input.Row;
				}
			}
			Changed = true;
		}

		void const * Handle;
		std::vector<Rml::String> Maps;
		int Selected = -1;
		bool Changed = true;
		int Pressed = 0;

		UILobbyPresenter Presenter;
		UIMapSelectView View;
};


bool UIMapSelectView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel(ModelName);
	if (!constructor) {
		return(false);
	}

	UI_Register_Array<std::vector<Rml::String>>(constructor);
	constructor.Bind("Maps", &Pick.Maps);
	constructor.Bind("Selected", &Pick.Selected);
	HasPreview = Has_Preview();
	constructor.Bind("HasPreview", &HasPreview);
	constructor.BindEventCallback("act", &UIMapSelectView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIMapSelectView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIMapSelectView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.size() < 2) {
		return;
	}

	Rml::String const action = arguments[0].Get<Rml::String>();
	Rml::String const name = arguments[1].Get<Rml::String>();
	UILobbyInput input;

	if (action == "down") {
		input.Kind = UILobbyInput::DOWN;
		input.Button = event.GetParameter<int>("button", 0);
		for (Rml::Element * walk = event.GetTargetElement(); walk != nullptr && walk != event.GetCurrentElement(); walk = walk->GetParentNode()) {
			if (walk->HasAttribute("data-row")) {
				input.Row = walk->GetAttribute<int>("data-row", -1);
				break;
			}
		}
	} else if (name == "ok") {
		input.Control = DIALOG_OK;
	} else if (name == "cancel") {
		input.Control = DIALOG_CANCEL;
	} else if (name == "random") {
		input.Control = IDC_CREATE_RANDOM_MAP;
	} else {
		return;
	}

	Lobby.Queue_Input(input);
}


void UIMapSelectView::Sync(void)
{
	if (!Model) {
		return;
	}

	Check_Preview();

	if (!Pushed) {
		Pushed = true;
		Pick.Changed = false;
		HasPreview = Has_Preview();
		Model.DirtyAllVariables();
		return;
	}

	if (Pick.Changed) {
		Pick.Changed = false;
		Model.DirtyVariable("Maps");
		Model.DirtyVariable("Selected");
	}

	bool const haspreview = Has_Preview();
	if (haspreview != HasPreview) {
		HasPreview = haspreview;
		Model.DirtyVariable("HasPreview");
	}
}


static std::vector<std::unique_ptr<UIMapSelect>> _Picks;


static UIMapSelect * Pick_Of(void const * handle)
{
	for (std::unique_ptr<UIMapSelect> & pick : _Picks) {
		if (pick->Handle == handle) {
			return(pick.get());
		}
	}
	return(nullptr);
}


static void Fill_Maps(UIMapSelect & pick, std::vector<std::string> const & maps, int selected)
{
	pick.Maps.clear();
	for (std::string const & map : maps) {
		pick.Maps.push_back(map);
	}
	pick.Selected = (selected >= 0 && selected < (int)maps.size()) ? selected : -1;
	pick.Changed = true;
}


bool UI_Map_Select_Open(void const * screen, std::vector<std::string> const & maps, int selected)
{
	if (!UI_Is_Initialized() || screen == nullptr || Pick_Of(screen) != nullptr) {
		return(false);
	}

	Register_Surfaces();

	std::unique_ptr<UIMapSelect> pick(new UIMapSelect(screen));
	Fill_Maps(*pick, maps, selected);

	if (!pick->View.Open()) {
		return(false);
	}

	pick->View.Sync();
	pick->View.Show(true);

	_Picks.push_back(std::move(pick));
	UI_Begin_Modal();
	return(true);
}


void UI_Map_Select_Close(void const * screen)
{
	for (auto walk = _Picks.begin(); walk != _Picks.end(); ++walk) {
		if ((*walk)->Handle == screen) {
			_Picks.erase(walk);
			UI_End_Modal();
			Refocus_Lobby();
			return;
		}
	}
}


void UI_Map_Select_Show(void const * screen, bool show)
{
	UIMapSelect * pick = Pick_Of(screen);
	if (pick == nullptr) {
		return;
	}

	if (show) {
		pick->View.Sync();
		pick->View.Show(true);
	} else {
		pick->View.Hide();
	}
}


void UI_Map_Select_Set_Maps(void const * screen, std::vector<std::string> const & maps, int selected)
{
	UIMapSelect * pick = Pick_Of(screen);
	if (pick != nullptr) {
		Fill_Maps(*pick, maps, selected);
	}
}


int UI_Map_Select_Get_Selection(void const * screen)
{
	UIMapSelect * pick = Pick_Of(screen);
	return((pick != nullptr) ? pick->Selected : -1);
}


int UI_Map_Select_Service(void const * screen)
{
	UIMapSelect * pick = Pick_Of(screen);
	if (pick == nullptr) {
		return(0);
	}

	pick->Presenter.Drain();
	pick->View.Sync();
	UI_Mark_Overlay_Dirty();

	int const pressed = pick->Pressed;
	pick->Pressed = 0;
	return(pressed);
}
