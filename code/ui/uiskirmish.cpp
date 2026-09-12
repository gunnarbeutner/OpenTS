/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The skirmish setup screen. What it has to preserve is what the dialog wrote into Session,
// Session.Options, Options, BuildLevel and Session.Players, in which order, and on which way
// out: accept harvests every control, cancel keeps only the name, side and colour.

#include "always.h"

#include "uiskirmish.h"

#include "_rules.h"
#include "data.h"
#include "dialogresult.h"
#include "globals.h"
#include "goptions.h"
#include "houstype.h"
#include "language/language.h"
#include "mapgen.h"
#include "mplayer.h"
#include "msgbox.h"
#include "netshare.h"
#include "preview.h"
#include "rules.h"
#include "session.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uitexture.h"
#include "utf8.h"
#include "win.h"
#include "xsurface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>


// The dialog's MP_MIN_MONEY.
static int const SKIRMISH_MIN_MONEY = 2500;

// The credits track bar was told to step by this with OD_SETTRACKSTEP.
static int const SKIRMISH_MONEY_STEP = 250;

// The size, in logical pixels, of the frame the dialog blitted the preview into.
static int const SKIRMISH_PREVIEW_WIDTH = 322;
static int const SKIRMISH_PREVIEW_HEIGHT = 172;

static char const SKIRMISH_PREVIEW_SOURCE[] = "skirmish-preview";


// One entry of the side box. Country is the house type index the box kept as item data.
struct UISkirmishSide
{
	std::string Name;
	int Country = 0;
};


// One entry of the colour box, drawn in the colour it names.
struct UISkirmishColor
{
	std::string Name;
	std::string Swatch;
	int Index = 0;
};


enum
{
	UI_SKIRMISH_ACCEPT = UI_ACTION_ACCEPT,
	UI_SKIRMISH_CANCEL = UI_ACTION_CANCEL,

	UI_SKIRMISH_UNITS = UI_ACTION_SCREEN,
	UI_SKIRMISH_CREDITS,
	UI_SKIRMISH_TECH,
	UI_SKIRMISH_DIFFICULTY,
	UI_SKIRMISH_PLAYERS,
	UI_SKIRMISH_SPEED,
	UI_SKIRMISH_NAME,
	UI_SKIRMISH_SIDE,
	UI_SKIRMISH_COLOR,
	UI_SKIRMISH_BASES,
	UI_SKIRMISH_CRATES,
	UI_SKIRMISH_FOG,
	UI_SKIRMISH_BRIDGES,
	UI_SKIRMISH_REDEPLOY,
	UI_SKIRMISH_SHORT,
	UI_SKIRMISH_ENGINEERS,
	UI_SKIRMISH_MAP,
};


class UISkirmishPresenter : public UIPresenterClass
{
	public:
		UISkirmishPresenter(void);

		// The track bar ranges, fixed for the life of the screen.
		int UnitMin = 0;
		int UnitMax = 0;
		int CreditsMin = 0;
		int CreditsMax = 0;
		int TechMin = 1;
		int TechMax = 0;

		// The track bar positions, as Slider_GetPos would have read them. Speed is the
		// position, which is six less the game speed.
		int UnitCount = 0;
		int Credits = 0;
		int TechLevel = 0;
		int Difficulty = 0;
		int AIPlayers = 0;
		int Speed = 0;

		std::string Name;

		std::vector<UISkirmishSide> Sides;
		int Country = HOUSE_FIRST;

		std::vector<UISkirmishColor> Colors;
		int Color = 0;

		bool Bases = false;
		bool Crates = false;
		bool Fog = false;
		bool Bridges = false;
		bool Redeploy = false;
		bool Short = false;
		bool Engineers = false;

		std::string Scenario;

		// Where the preview lands inside its frame, letterboxed as Blit_Preview placed it.
		bool HasPreview = false;
		int PreviewLeft = 0;
		int PreviewTop = 0;
		int PreviewWidth = 0;
		int PreviewHeight = 0;

		// Raised whenever an intent changed something the player did not change directly,
		// so the view knows to push it.
		int Revision = 0;

		// Hides the screen while the map picker, which draws into the software frame, is up.
		std::function<void(bool)> Show_Screen;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Accept(void);
		void Cancel(void);
		void Pick_Map(void);
		void Keep_Name_Side_And_Color(void);
		void Measure_Preview(void);
		void Read_Random_Map_Preview(void);

		bool Initialized = false;
};


static int Clamp_To(int value, int minimum, int maximum)
{
	if (value < minimum) {
		return(minimum);
	}
	if (value > maximum) {
		return(maximum);
	}
	return(value);
}


static std::string Document_Text(char const * text)
{
	return((text != nullptr) ? std::string(text) : std::string());
}


// The colour is packed as Lobby_Color packs it, red in the low byte.
static std::string Swatch_Of(std::uint32_t color)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02xff", color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
	return(std::string(buffer));
}


static Surface * Skirmish_Preview_Surface(void)
{
	if (MultiplayerMapPreview == nullptr) {
		return(nullptr);
	}

	return(MultiplayerMapPreview->Get_Preview_Surface());
}


UISkirmishPresenter::UISkirmishPresenter(void)
{
	UnitMin = SessionClass::CountMin[1];
	UnitMax = SessionClass::CountMax[1];

	// The track bar only ever answered a multiple of its step, so the top of the range is
	// the last multiple that fits.
	CreditsMin = SKIRMISH_MIN_MONEY;
	CreditsMax = Rule->MPMaxMoney;
	if (CreditsMax > CreditsMin) {
		CreditsMax = CreditsMin + ((CreditsMax - CreditsMin) / SKIRMISH_MONEY_STEP) * SKIRMISH_MONEY_STEP;
	}

	TechMin = 1;
	TechMax = MPLAYER_BUILD_LEVEL_MAX;
}


/// <summary>
/// Puts up what the dialog's WM_INITDIALOG did, with the same side effects in the same order:
/// the first scenario is selected, the player and computer lists are emptied, and the
/// preview is rebuilt. Runs once, after the document has been prepared.
/// </summary>
void UISkirmishPresenter::Refresh(void)
{
	if (Initialized) {
		return;
	}
	Initialized = true;

	UnitCount = Clamp_To(Session.Options.UnitCount, UnitMin, UnitMax);

	TechLevel = Clamp_To(BuildLevel, TechMin, TechMax);

	Difficulty = Clamp_To(Session.Options.AIDifficulty, 0, 2);

	// Slider_GetPos answered the position rounded down to the step.
	Credits = Clamp_To(Session.Options.Credits, CreditsMin, CreditsMax);
	Credits = SKIRMISH_MONEY_STEP * (Credits / SKIRMISH_MONEY_STEP);

	AIPlayers = Clamp_To(Session.Options.AIPlayers > 1 ? Session.Options.AIPlayers : 1, 1, 7);

	Speed = Clamp_To(6 - Session.Options.GameSpeed, 0, 6);

	Name = Document_Text(Session.Handle);

	// The multiplayable houses in type order, with the player's house selected, or the first
	// entry when it is not among them.
	Sides.clear();
	for (int index = 0; index < HouseTypes.Count(); index++) {
		HouseTypeClass * house = HouseTypes[index];
		if (house->IsMultiplay) {
			UISkirmishSide side;
			side.Name = Document_Text(house->GivenName);
			side.Country = index;
			Sides.push_back(side);
		}
	}

	Country = HOUSE_FIRST;
	if (!Sides.empty()) {
		Country = Sides[0].Country;
		for (UISkirmishSide const & side : Sides) {
			if (side.Country == Session.House) {
				Country = side.Country;
				break;
			}
		}
	}

	static int const names[MAX_PLAYERS] = {
		TXT_GOLD, TXT_RED, TXT_BLUE, TXT_GREEN, TXT_ORANGE, TXT_SKY_BLUE, TXT_PURPLE, TXT_PINK
	};

	Colors.clear();
	for (int index = 0; index < MAX_PLAYERS; index++) {
		UISkirmishColor color;
		color.Name = Fetch_String(names[index]);
		color.Swatch = Swatch_Of(PlayerColorTable[index]);
		color.Index = index;
		Colors.push_back(color);
	}

	Color = Clamp_To(Session.PrefColor, 0, MAX_PLAYERS - 1);

	Set_Scenario_Info_From_Index(0);
	Session.Options.ScenarioIndex = 0;
	Scenario = Document_Text(Session.Options.ScenarioDescription);
	Clear_Vector(&Session.Players);
	Clear_Vector(&Session.Computers);

	Bases = Session.Options.Bases;
	Crates = Session.Options.Goodies;
	Fog = Session.Options.FogOfWar;
	Bridges = Session.Options.BridgeDestruction;
	Redeploy = Session.Options.MCVRedeploy;
	Engineers = Session.Options.CrapEngineers;
	Short = Session.Options.ShortGame;

	Update_Network_Dialog_Preview(nullptr);
	Measure_Preview();
}


/// <summary>
/// Places the preview inside its frame the way Blit_Preview did: scaled by the smaller of
/// the two ratios, in thousandths, and centred. Also makes the next frame read the surface
/// again, since it may have been rebuilt.
/// </summary>
void UISkirmishPresenter::Measure_Preview(void)
{
	Surface * surface = Skirmish_Preview_Surface();

	HasPreview = (surface != nullptr && surface->Get_Width() > 0 && surface->Get_Height() > 0);

	if (HasPreview) {
		int const width = surface->Get_Width();
		int const height = surface->Get_Height();
		int const scale = std::min(1000 * SKIRMISH_PREVIEW_WIDTH / width, 1000 * SKIRMISH_PREVIEW_HEIGHT / height);

		PreviewLeft = SKIRMISH_PREVIEW_WIDTH / 2 - (scale * width) / 2000;
		PreviewTop = SKIRMISH_PREVIEW_HEIGHT / 2 - (scale * height) / 2000;
		PreviewWidth = (scale * width) / 1000;
		PreviewHeight = (scale * height) / 1000;
	}

	UI_Surface_Invalidate(SKIRMISH_PREVIEW_SOURCE);
	Revision++;
}


void UISkirmishPresenter::Read_Random_Map_Preview(void)
{
	delete MultiplayerMapPreview;
	MultiplayerMapPreview = new MapPreviewClass;
	MultiplayerMapPreview->Read_PCX_Preview("RandMap.img");
}


// What both ways out of the dialog read back.
void UISkirmishPresenter::Keep_Name_Side_And_Color(void)
{
	UTF8::Copy(Session.Handle, Name.c_str());
	Session.House = Country;
	Session.ColorIdx = Color;
	Session.PrefColor = Session.ColorIdx;
}


void UISkirmishPresenter::Accept(void)
{
	// The map has to have a start position for the player and for every computer player.
	int const waypoint_count = RandomMapWaypointCount(Session.Options.ScenarioIndex);
	int const waypoint = AIPlayers + 1;

	if (waypoint_count < waypoint) {
		char buffer[256];
		std::snprintf(buffer, sizeof(buffer), Fetch_String(TXT_SCENARIO_TOO_SMALL), waypoint_count);
		WWMessageBox().Process(buffer, TXT_OK);
		return;
	}

	UTF8::Copy(Session.Handle, Name.c_str());

	Session.Options.UnitCount = UnitCount;
	BuildLevel = TechLevel;
	Session.Options.Credits = Credits;
	Session.Options.AIDifficulty = (DiffType)Difficulty;
	Session.Options.AIPlayers = AIPlayers;
	Session.Options.GameSpeed = 6 - Speed;
	Options.GameSpeed = Session.Options.GameSpeed;

	Session.House = Country;
	Session.ColorIdx = Color;
	Session.PrefColor = Session.ColorIdx;

	NodeNameType * who = new NodeNameType;
	if (who) {
		strcpy(who->Name, Session.Handle);
		who->Player.House = Session.House;
		who->Player.Color = Session.ColorIdx;
		who->Player.ProcessTime = -1;
		Session.Players.Add(who);
	}

	Session.Options.Bases = Bases;
	Session.Options.Goodies = Crates;
	Session.Options.FogOfWar = Fog;
	Session.Options.BridgeDestruction = Bridges;
	Session.Options.MCVRedeploy = Redeploy;
	Session.Options.ShortGame = Short;
	Session.Options.HarvTruce = false;
	Session.Options.CrapEngineers = Engineers;

	if (MultiplayerMapPreview) {
		delete MultiplayerMapPreview;
		MultiplayerMapPreview = nullptr;
	}

	Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
}


void UISkirmishPresenter::Cancel(void)
{
	Keep_Name_Side_And_Color();
	Finish(UI_RESULT_CANCELLED, DIALOG_CANCEL);
}


/// <summary>
/// Runs the map picker with the screen hidden, as the dialog hid itself around it, and
/// takes the map it answers with, or restores the one that was selected.
/// </summary>
void UISkirmishPresenter::Pick_Map(void)
{
	int const old_scen = Session.Options.ScenarioIndex;

	if (Show_Screen) {
		Show_Screen(false);
	}

	int const picked = Scenario_Dialog(nullptr);

	// Nothing the player did to this screen while it was hidden is acted on.
	Discard();

	if (picked == DIALOG_CANCEL) {
		Session.Options.ScenarioIndex = old_scen;
		Set_Scenario_Info_From_Index(old_scen);
		Update_Network_Dialog_Preview(nullptr);

		if (Show_Screen) {
			Show_Screen(true);
		}

		if (stricmp(Session.Scenarios[Session.Options.ScenarioIndex]->Get_Filename(), RANDOM_MAP_FILE_NAME) == 0) {
			Read_Random_Map_Preview();
			if (MultiplayerMapPreview->Get_Preview_Surface() == nullptr) {
				Update_Network_Dialog_Preview(nullptr);
			}
		} else {
			Update_Network_Dialog_Preview(nullptr);
		}
	} else {
		if (Show_Screen) {
			Show_Screen(true);
		}

		if (Set_Scenario_Info_From_Index(Session.Options.ScenarioIndex) == true) {
			Scenario = Document_Text(Session.Options.ScenarioDescription);

			if (stricmp(Session.Scenarios[Session.Options.ScenarioIndex]->Get_Filename(), "RandMap.Sed") == 0) {
				// The dialog replaced a preview that was there and then read the new one
				// back without checking, so with no preview at all it read through null.
				if (MultiplayerMapPreview != nullptr) {
					Read_Random_Map_Preview();
				}
				if (MultiplayerMapPreview == nullptr || MultiplayerMapPreview->Get_Preview_Surface() == nullptr) {
					Update_Network_Dialog_Preview(nullptr);
				}
			} else {
				Update_Network_Dialog_Preview(nullptr);
			}
		} else {
			Session.Options.ScenarioIndex = old_scen;
		}
	}

	Measure_Preview();
}


void UISkirmishPresenter::Execute(UIIntent const & intent)
{
	// An accept writes the player list, so nothing queued behind the answer may run.
	if (Has_Result()) {
		return;
	}

	switch (intent.Action) {
		case UI_SKIRMISH_UNITS:
			UnitCount = Clamp_To(intent.Identity, UnitMin, UnitMax);
			break;

		case UI_SKIRMISH_CREDITS:
			Credits = SKIRMISH_MONEY_STEP * (Clamp_To(intent.Identity, CreditsMin, CreditsMax) / SKIRMISH_MONEY_STEP);
			break;

		case UI_SKIRMISH_TECH:
			TechLevel = Clamp_To(intent.Identity, TechMin, TechMax);
			break;

		case UI_SKIRMISH_DIFFICULTY:
			Difficulty = Clamp_To(intent.Identity, 0, 2);
			break;

		case UI_SKIRMISH_PLAYERS:
			AIPlayers = Clamp_To(intent.Identity, 1, 7);
			break;

		case UI_SKIRMISH_SPEED:
			Speed = Clamp_To(intent.Identity, 0, 6);
			break;

		case UI_SKIRMISH_NAME:
			Name = intent.Text;
			break;

		case UI_SKIRMISH_SIDE:
			Country = intent.Identity;
			break;

		case UI_SKIRMISH_COLOR:
			Color = Clamp_To(intent.Identity, 0, MAX_PLAYERS - 1);
			break;

		case UI_SKIRMISH_BASES:
			// Clearing bases cleared the short game box, which needs them.
			if (Bases && intent.Identity == 0) {
				Short = false;
				Revision++;
			}
			Bases = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_SHORT:
			// Checking the short game box checked bases, and clearing it left them alone.
			if (!Short && intent.Identity != 0) {
				Bases = true;
				Revision++;
			}
			Short = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_CRATES:
			Crates = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_FOG:
			Fog = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_BRIDGES:
			Bridges = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_REDEPLOY:
			Redeploy = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_ENGINEERS:
			Engineers = (intent.Identity != 0);
			break;

		case UI_SKIRMISH_MAP:
			Pick_Map();
			break;

		case UI_SKIRMISH_ACCEPT:
			Accept();
			break;

		case UI_SKIRMISH_CANCEL:
			Cancel();
			break;

		default:
			break;
	}
}


// What the document binds. It is the view's own copy rather than the presenter's state,
// because a control writes its binding before or after its change event reaches the view,
// and the presenter has to see a change as a change.
struct UISkirmishModel
{
	int UnitCount = 0;
	int Credits = 0;
	int TechLevel = 0;
	int Difficulty = 0;
	int AIPlayers = 0;
	int Speed = 0;

	Rml::String Name;
	Rml::String Side;
	Rml::String Color;

	bool Bases = false;
	bool Crates = false;
	bool Fog = false;
	bool Bridges = false;
	bool Redeploy = false;
	bool Short = false;
	bool Engineers = false;

	Rml::String Scenario;

	bool HasPreview = false;
	int PreviewLeft = 0;
	int PreviewTop = 0;
	int PreviewWidth = 0;
	int PreviewHeight = 0;
};


class UISkirmishView : public UIRmlViewClass
{
	public:
		UISkirmishView(UISkirmishPresenter & presenter) :
			UIRmlViewClass(presenter, "skirmish.rml"), Skirmish(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);
		void Set_Range(char const * id, int minimum, int maximum, int step);
		void Push_Changed(void);

		UISkirmishPresenter & Skirmish;
		UISkirmishModel Bound;
		Rml::DataModelHandle Model;

		bool Pushed = false;
		int Revision = 0;
};


// A control's new value is taken from the event rather than from the binding. RmlUi raises
// the change event before it stores the value, and the order between this listener and the
// binding's own is not defined.
static int Changed_Value(Rml::Event & event, int fallback)
{
	return((int)(event.GetParameter<float>("value", (float)fallback) + 0.5f));
}


static int Changed_Check(Rml::Event & event, bool fallback)
{
	return(event.GetParameter<bool>("checked", fallback) ? 1 : 0);
}


static int Changed_Choice(Rml::Event & event, int fallback)
{
	Rml::String const value = event.GetParameter<Rml::String>("value", "");
	if (value.empty()) {
		return(fallback);
	}
	return(std::atoi(value.c_str()));
}


void UISkirmishView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	struct SliderEntry
	{
		char const * Name;
		int Action;
		int const * Current;
	};

	SliderEntry const sliders[] = {
		{ "units", UI_SKIRMISH_UNITS, &Skirmish.UnitCount },
		{ "credits", UI_SKIRMISH_CREDITS, &Skirmish.Credits },
		{ "tech", UI_SKIRMISH_TECH, &Skirmish.TechLevel },
		{ "difficulty", UI_SKIRMISH_DIFFICULTY, &Skirmish.Difficulty },
		{ "players", UI_SKIRMISH_PLAYERS, &Skirmish.AIPlayers },
		{ "speed", UI_SKIRMISH_SPEED, &Skirmish.Speed },
	};

	struct CheckEntry
	{
		char const * Name;
		int Action;
		bool const * Current;
	};

	CheckEntry const checks[] = {
		{ "bases", UI_SKIRMISH_BASES, &Skirmish.Bases },
		{ "crates", UI_SKIRMISH_CRATES, &Skirmish.Crates },
		{ "fog", UI_SKIRMISH_FOG, &Skirmish.Fog },
		{ "bridges", UI_SKIRMISH_BRIDGES, &Skirmish.Bridges },
		{ "redeploy", UI_SKIRMISH_REDEPLOY, &Skirmish.Redeploy },
		{ "short", UI_SKIRMISH_SHORT, &Skirmish.Short },
		{ "engineers", UI_SKIRMISH_ENGINEERS, &Skirmish.Engineers },
	};

	for (SliderEntry const & slider : sliders) {
		if (name == slider.Name) {
			intent.Action = slider.Action;
			intent.Identity = Changed_Value(event, *slider.Current);
			Presenter.Queue(intent);
			return;
		}
	}

	for (CheckEntry const & check : checks) {
		if (name == check.Name) {
			intent.Action = check.Action;
			intent.Identity = Changed_Check(event, *check.Current);
			Presenter.Queue(intent);
			return;
		}
	}

	if (name == "name") {
		intent.Action = UI_SKIRMISH_NAME;
		intent.Text = event.GetParameter<Rml::String>("value", Skirmish.Name);
	} else if (name == "side") {
		intent.Action = UI_SKIRMISH_SIDE;
		intent.Identity = Changed_Choice(event, Skirmish.Country);
	} else if (name == "color") {
		intent.Action = UI_SKIRMISH_COLOR;
		intent.Identity = Changed_Choice(event, Skirmish.Color);
	} else if (name == "map") {
		intent.Action = UI_SKIRMISH_MAP;
	} else if (name == "accept") {
		intent.Action = UI_SKIRMISH_ACCEPT;
	} else if (name == "cancel") {
		intent.Action = UI_SKIRMISH_CANCEL;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UISkirmishView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("skirmish");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UISkirmishSide>(constructor)) {
		handle.RegisterMember("Name", &UISkirmishSide::Name);
		handle.RegisterMember("Country", &UISkirmishSide::Country);
	}

	if (auto handle = UI_Register_Struct<UISkirmishColor>(constructor)) {
		handle.RegisterMember("Name", &UISkirmishColor::Name);
		handle.RegisterMember("Swatch", &UISkirmishColor::Swatch);
		handle.RegisterMember("Index", &UISkirmishColor::Index);
	}

	UI_Register_Array<std::vector<UISkirmishSide>>(constructor);
	UI_Register_Array<std::vector<UISkirmishColor>>(constructor);

	// The two lists do not change while the screen is up, so they are bound to the
	// presenter's own.
	constructor.Bind("Sides", &Skirmish.Sides);
	constructor.Bind("Colors", &Skirmish.Colors);

	constructor.Bind("UnitCount", &Bound.UnitCount);
	constructor.Bind("Credits", &Bound.Credits);
	constructor.Bind("TechLevel", &Bound.TechLevel);
	constructor.Bind("Difficulty", &Bound.Difficulty);
	constructor.Bind("AIPlayers", &Bound.AIPlayers);
	constructor.Bind("Speed", &Bound.Speed);

	constructor.Bind("Name", &Bound.Name);
	constructor.Bind("Side", &Bound.Side);
	constructor.Bind("Color", &Bound.Color);

	constructor.Bind("Bases", &Bound.Bases);
	constructor.Bind("Crates", &Bound.Crates);
	constructor.Bind("Fog", &Bound.Fog);
	constructor.Bind("Bridges", &Bound.Bridges);
	constructor.Bind("Redeploy", &Bound.Redeploy);
	constructor.Bind("Short", &Bound.Short);
	constructor.Bind("Engineers", &Bound.Engineers);

	constructor.Bind("Scenario", &Bound.Scenario);

	constructor.Bind("HasPreview", &Bound.HasPreview);
	constructor.Bind("PreviewLeft", &Bound.PreviewLeft);
	constructor.Bind("PreviewTop", &Bound.PreviewTop);
	constructor.Bind("PreviewWidth", &Bound.PreviewWidth);
	constructor.Bind("PreviewHeight", &Bound.PreviewHeight);

	constructor.BindEventCallback("act", &UISkirmishView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


void UISkirmishView::Set_Range(char const * id, int minimum, int maximum, int step)
{
	Rml::Element * element = Document->GetElementById(id);
	if (element == nullptr) {
		return;
	}

	element->SetAttribute("min", minimum);
	element->SetAttribute("max", maximum);
	element->SetAttribute("step", step);
}


bool UISkirmishView::Bind(void)
{
	// The ranges go on before the first push, so a position is never clamped to a range
	// the track bar did not have.
	Set_Range("units", Skirmish.UnitMin, Skirmish.UnitMax, 1);
	Set_Range("credits", Skirmish.CreditsMin, Skirmish.CreditsMax, SKIRMISH_MONEY_STEP);
	Set_Range("tech", Skirmish.TechMin, Skirmish.TechMax, 1);
	Set_Range("difficulty", 0, 2, 1);
	Set_Range("players", 1, 7, 1);
	Set_Range("speed", 0, 6, 1);

	Attach_Actions();
	return(true);
}


// What an intent can change without the player having changed that control: the check box
// the other one forces, and what the map picker leaves behind.
void UISkirmishView::Push_Changed(void)
{
	Bound.Bases = Skirmish.Bases;
	Bound.Short = Skirmish.Short;
	Bound.Scenario = Skirmish.Scenario;
	Bound.HasPreview = Skirmish.HasPreview;
	Bound.PreviewLeft = Skirmish.PreviewLeft;
	Bound.PreviewTop = Skirmish.PreviewTop;
	Bound.PreviewWidth = Skirmish.PreviewWidth;
	Bound.PreviewHeight = Skirmish.PreviewHeight;

	Model.DirtyVariable("Bases");
	Model.DirtyVariable("Short");
	Model.DirtyVariable("Scenario");
	Model.DirtyVariable("HasPreview");
	Model.DirtyVariable("PreviewLeft");
	Model.DirtyVariable("PreviewTop");
	Model.DirtyVariable("PreviewWidth");
	Model.DirtyVariable("PreviewHeight");
}


void UISkirmishView::Sync(void)
{
	if (!Model) {
		return;
	}

	// The first pass puts the read state into every control. After that only what an intent
	// changed from under the player is pushed, so a track bar being dragged keeps the value
	// it is being dragged to.
	if (!Pushed) {
		Pushed = true;
		Revision = Skirmish.Revision;

		Bound.UnitCount = Skirmish.UnitCount;
		Bound.Credits = Skirmish.Credits;
		Bound.TechLevel = Skirmish.TechLevel;
		Bound.Difficulty = Skirmish.Difficulty;
		Bound.AIPlayers = Skirmish.AIPlayers;
		Bound.Speed = Skirmish.Speed;
		Bound.Name = Skirmish.Name;
		Bound.Side = std::to_string(Skirmish.Country);
		Bound.Color = std::to_string(Skirmish.Color);
		Bound.Crates = Skirmish.Crates;
		Bound.Fog = Skirmish.Fog;
		Bound.Bridges = Skirmish.Bridges;
		Bound.Redeploy = Skirmish.Redeploy;
		Bound.Engineers = Skirmish.Engineers;

		Push_Changed();
		Model.DirtyAllVariables();
		return;
	}

	if (Revision != Skirmish.Revision) {
		Revision = Skirmish.Revision;
		Push_Changed();
	}
}


void UISkirmishView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("skirmish");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Skirmish_Screen(int & rc)
{
	UI_Surface_Register(SKIRMISH_PREVIEW_SOURCE, Skirmish_Preview_Surface);

	UISkirmishPresenter presenter;
	UISkirmishView view(presenter);

	presenter.Show_Screen = [&view](bool show) {
		if (show) {
			view.Show(true);
		} else {
			view.Hide();
		}
	};

	UIResult const result = UI_Run_Modal(presenter, view);

	// The driver deletes the preview next, and a texture must not outlive it.
	UI_Surface_Invalidate(SKIRMISH_PREVIEW_SOURCE);

	if (result.Type == UI_RESULT_FAILED) {
		return(false);
	}

	if (result.Type == UI_RESULT_ACCEPTED || result.Type == UI_RESULT_CANCELLED) {
		rc = result.Code;
	}

	return(true);
}
