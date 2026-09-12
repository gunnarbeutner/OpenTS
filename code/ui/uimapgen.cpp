/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The random map generator screen. Describe and Adopt move the seed to and from the screen's
// model as the dialog's MapSeedClass::Set_Settings and Get_Settings moved it through its
// controls, with the same ranges, the same enabling under a World Domination Tour territory,
// and the same ordering of the combo boxes, so the seed the generator sees is the one the
// dialog would have handed it.

#include "always.h"

#include "uimapgen.h"

#include "addon.h"
#include "ccrand.h"
#include "data.h"
#include "globals.h"
#include "language/language.h"
#include "mapgen.h"
#include "preview.h"
#include "scenario.h"
#include "session.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "uishell.h"
#include "uisystem.h"
#include "uitexture.h"
#include "video.h"
#include "wdtnet.h"
#include "worlddom.h"
#include "xsurface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>


extern bool Debug_Map;
void Write_Scenario_INI(char const * fname, bool write_triggers);


struct UIMapgenOption
{
	Rml::String Name;
	int Value = 0;
};


struct UIMapgenSlider
{
	int Min = 0;
	int Max = 100;
	int Value = 0;
	bool On = true;
};


enum
{
	UI_MAPGEN_ACCEPT = UI_ACTION_ACCEPT,
	UI_MAPGEN_CANCEL = UI_ACTION_CANCEL,
	UI_MAPGEN_SET = UI_ACTION_SCREEN,
	UI_MAPGEN_LOAD,
	UI_MAPGEN_SAVE,
	UI_MAPGEN_DELETE,
	UI_MAPGEN_PREVIEW,
	UI_MAPGEN_SURPRISE,
};


// Which control a UI_MAPGEN_SET intent sets.
enum
{
	UI_MAPGEN_BIOME,
	UI_MAPGEN_TIME,
	UI_MAPGEN_WIDTH,
	UI_MAPGEN_HEIGHT,
	UI_MAPGEN_PLAYERS,
	UI_MAPGEN_CLIFFS,
	UI_MAPGEN_ACCESSIBILITY,
	UI_MAPGEN_HILLS,
	UI_MAPGEN_TIBERIUM,
	UI_MAPGEN_FIELDS,
	UI_MAPGEN_WATER,
	UI_MAPGEN_VEGETATION,
	UI_MAPGEN_CITIES,
	UI_MAPGEN_VEINHOLES,
	UI_MAPGEN_LIFEFORMS,
	UI_MAPGEN_TRANSITIONS,
	UI_MAPGEN_ION_STORMS,
	UI_MAPGEN_CONTROL_COUNT,
};


static char const * const _ControlNames[UI_MAPGEN_CONTROL_COUNT] = {
	"biome", "time", "width", "height", "players", "cliffs", "accessibility", "hills",
	"tiberium", "fields", "water", "vegetation", "cities", "veinholes", "lifeforms",
	"transitions", "ionstorms",
};


static WDTTerritory * Territory(void)
{
	if (Session.Type == GAME_INTERNET && Session.IsWDT) {
		return(WDT_Get_Territory(Session.WDTTerritory));
	}
	return(nullptr);
}


class UIMapgenPresenter : public UIPresenterClass
{
	public:
		Rml::String Variant;

		std::vector<UIMapgenOption> Biomes;
		std::vector<UIMapgenOption> Times;
		std::vector<UIMapgenOption> Sizes;

		int Biome = 0;
		int Time = 0;
		int Width = 0;
		int Height = 0;
		bool BiomeOn = true;
		bool TimeOn = true;
		bool WidthOn = true;
		bool HeightOn = true;

		UIMapgenSlider Players;
		UIMapgenSlider Cliffs;
		UIMapgenSlider Accessibility;
		UIMapgenSlider Hills;
		UIMapgenSlider Tiberium;
		UIMapgenSlider Fields;
		UIMapgenSlider Water;
		UIMapgenSlider Vegetation;
		UIMapgenSlider Cities;
		UIMapgenSlider Veinholes;

		bool Lifeforms = false;
		bool LifeformsOn = true;
		bool Transitions = false;
		bool TransitionsOn = true;
		bool IonStorms = false;

		bool LoadOn = true;
		bool DeleteOn = true;
		bool PreviewOn = true;
		bool SurpriseOn = true;
		bool HasPreview = false;

		// Set when the seed was read into the model afresh, so every control has to be
		// repositioned rather than only the ones an action enables or disables.
		bool Reread = false;

		// Hides and shows the document around a screen this one opens, so the two are never
		// up together.
		std::function<void(bool)> Set_Visible;

		bool (*Callback)() = nullptr;

		void Initialize(void);
		void Refresh(void) override;
		void Service(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Describe(void);
		void Adopt(void);
		void Enable_Files(void);
		void Build_Preview(void);

		int Seed = 0;
};


static void Fill_Options(std::vector<UIMapgenOption> & options, int const * names, int count, bool sorted, int skip)
{
	options.clear();

	for (int index = 0; index < count; index++) {
		if (index == skip) {
			continue;
		}

		UIMapgenOption option;
		option.Name = Fetch_String(names[index]);
		option.Value = index;
		options.push_back(option);
	}

	// The environment and time of day boxes were sorted, so they list by name rather than by
	// the order the generator numbers them in.
	if (sorted) {
		std::sort(options.begin(), options.end(), [](UIMapgenOption const & a, UIMapgenOption const & b) {
			return(stricmp(a.Name.c_str(), b.Name.c_str()) < 0);
		});
	}
}


// A slider with nothing left to choose between was shown disabled over 0 to 100 rather than
// hidden, so the dialog kept its shape.
static void Set_Slider(UIMapgenSlider & slider, int min, int max, int value, bool on)
{
	if (max <= min) {
		slider.Min = 0;
		slider.Max = 100;
		slider.On = false;
	} else {
		slider.Min = min;
		slider.Max = max;
		slider.On = on;
	}
	slider.Value = value;
}


/// <summary>
/// Reads the seed into the model, as the dialog's Set_Settings read it into its controls.
/// </summary>
void UIMapgenPresenter::Describe(void)
{
	static int const biomes[BIOME_COUNT] = {
		TXT_BIOME_TUNDRA, TXT_BIOME_TAIGA, TXT_BIOME_TEMPERATE, TXT_BIOME_DESERT, TXT_BIOME_MUTATED,
	};
	static int const times[TIME_OF_DAY_COUNT] = {
		TXT_TIME_MORNING, TXT_TIME_AFTERNOON, TXT_TIME_DUSK, TXT_TIME_NIGHT,
	};
	static int const sizes[MAPSIZE_COUNT] = {
		TXT_MAPSIZE_SMALL, TXT_MAPSIZE_MEDIUM, TXT_MAPSIZE_LARGE, TXT_MAPSIZE_VERY_LARGE,
	};

	MapSeedClass & seed = RandomMapGen.SeedData;
	WDTTerritory * wdt = Territory();

	seed.Fixup_Settings();

	Fill_Options(Biomes, biomes, BIOME_COUNT, true, Addon_Enabled(ADDON_FIRESTORM) ? -1 : BIOME_MUTATED);
	Fill_Options(Times, times, TIME_OF_DAY_COUNT, true, -1);
	Fill_Options(Sizes, sizes, MAPSIZE_COUNT, false, -1);

	Biome = seed.Biome;
	Time = seed.Time;
	Width = seed.Width;
	Height = seed.Height;
	Seed = seed.Seed;

	BiomeOn = wdt == nullptr || wdt->UserModBiome;
	TimeOn = wdt == nullptr || wdt->UserModTime;
	WidthOn = wdt == nullptr || wdt->UserModWidth;
	HeightOn = wdt == nullptr || wdt->UserModHeight;

	if (wdt != nullptr) {
		Set_Slider(Tiberium, wdt->TiberiumAmountMin, wdt->TiberiumAmountMax, seed.Tiberium, wdt->UserModTiberiumAmount);
		Set_Slider(Hills, wdt->HillsMin, wdt->HillsMax, seed.Hills, wdt->UserModHills);
		Set_Slider(Water, wdt->WaterMin, wdt->WaterMax, seed.WaterAmount, wdt->UserModWater);
		Set_Slider(Cliffs, wdt->CliffsMin, wdt->CliffsMax, seed.Cliffs, wdt->UserModCliffs);
		Set_Slider(Vegetation, wdt->VegetationMin, wdt->VegetationMax, seed.Vegetation, wdt->UserModVegetation);
		Set_Slider(Cities, wdt->CitiesMin, wdt->CitiesMax, seed.Cities, wdt->UserModCities);
		Set_Slider(Fields, wdt->TiberiumFieldsMin, wdt->TiberiumFieldsMax, seed.TiberiumLayout, wdt->UserModTiberiumFields);
		Set_Slider(Accessibility, wdt->AccessibilityMin, wdt->AccessibilityMax, seed.Accessibility, wdt->UserModAccessability);
		Set_Slider(Veinholes, 0, 5, seed.VeinholeMonsters, wdt->UserModVeinholeMonsters);

		LifeformsOn = wdt->UserModTiberiumCreatures;
		TransitionsOn = wdt->UserModTimeTransitions;

		SurpriseOn = wdt->UserModBiome || wdt->UserModTime || wdt->UserModCliffs || wdt->UserModAccessability
			|| wdt->UserModHills || wdt->UserModTiberiumAmount || wdt->UserModTiberiumFields || wdt->UserModWater
			|| wdt->UserModVegetation || wdt->UserModCities || wdt->UserModWidth || wdt->UserModHeight
			|| wdt->UserModVeinholeMonsters;
	} else {
		Set_Slider(Tiberium, 1, 100, seed.Tiberium, true);
		Set_Slider(Players, 2, MAX_PLAYERS, seed.NumPlayers, true);
		Set_Slider(Hills, 0, 100, seed.Hills, true);
		Set_Slider(Water, 0, 100, seed.WaterAmount, true);
		Set_Slider(Cliffs, 0, 100, seed.Cliffs, true);
		Set_Slider(Vegetation, 0, 100, seed.Vegetation, true);
		Set_Slider(Cities, 0, 100, seed.Cities, true);
		Set_Slider(Fields, 0, 100, seed.TiberiumLayout, true);
		Set_Slider(Accessibility, 0, 100, seed.Accessibility, true);
		Set_Slider(Veinholes, 0, 5, seed.VeinholeMonsters, true);

		LifeformsOn = true;
		TransitionsOn = true;
		SurpriseOn = true;
	}

	Lifeforms = seed.TiberiumWildlife > 0;
	Transitions = seed.UseTransitions;
	IonStorms = seed.UseIonStorms;

	Reread = true;
}


/// <summary>
/// Writes the model back into the seed, as the dialog's Get_Settings read its controls.
/// </summary>
void UIMapgenPresenter::Adopt(void)
{
	MapSeedClass & seed = RandomMapGen.SeedData;
	WDTTerritory * wdt = Territory();

	seed.Biome = Biome;
	seed.Time = Time;
	seed.Width = Width;
	seed.Height = Height;
	seed.Seed = Seed;
	seed.Tiberium = Tiberium.Value;
	seed.NumPlayers = (wdt != nullptr) ? 4 : Players.Value;
	seed.Hills = Hills.Value;
	seed.WaterAmount = Water.Value;
	seed.Cliffs = Cliffs.Value;
	seed.Vegetation = Vegetation.Value;
	seed.Cities = Cities.Value;
	seed.Accessibility = Accessibility.Value;
	seed.TiberiumLayout = Fields.Value;

	seed.TiberiumWildlife = 0;
	seed.VeinholeMonsters = 0;
	seed.UseIonStorms = false;
	seed.UseTransitions = false;
	seed.UseBlueTiberium = false;

	if (Addon_Enabled(ADDON_FIRESTORM)) {
		seed.TiberiumWildlife = Lifeforms ? 30 : 0;
		seed.VeinholeMonsters = Veinholes.Value;
		seed.UseIonStorms = IonStorms;
		seed.UseTransitions = Transitions;

		// The dialog compared the amount, a whole number, with 0.75, which any amount of
		// one or more passes; kept as it was.
		seed.UseBlueTiberium = (double)seed.Tiberium > 0.75;
	}

	seed.Fixup_Settings();
}


void UIMapgenPresenter::Enable_Files(void)
{
	bool const present = RandomMapGen.SeedData.Files_Present();
	LoadOn = present;
	DeleteOn = present;
}


void UIMapgenPresenter::Initialize(void)
{
	WDTTerritory * wdt = Territory();

	if (wdt != nullptr) {
		Variant = "wdt";
	} else if (Addon_Enabled(ADDON_FIRESTORM)) {
		Variant = "fs";
	} else {
		Variant = "ts";
	}

	PreviewOn = !Debug_Map;

	if (RandomMapGen.SeedData.Seed == -1) {
		RandomMapGen.SeedData.Seed = Sim_Random_Pick(0U, 65535U);
	}

	Describe();
	Enable_Files();
}


void UIMapgenPresenter::Refresh(void)
{
	HasPreview = RandomMapGen.MapPreview != nullptr && RandomMapGen.MapPreview->Get_Preview_Surface() != nullptr;
}


void UIMapgenPresenter::Service(void)
{
	if (Callback != nullptr) {
		Callback();
	}
}


void UIMapgenPresenter::Build_Preview(void)
{
	Adopt();
	RandomMapGen.Generate_Random_Map(true);
	RandomMapGen.MapPreview->Create_Preview();

	if (RandomMapGen.MapSeeder != nullptr) {
		delete RandomMapGen.MapSeeder;
	}
	RandomMapGen.MapSeeder = new MapSeedClass;
	std::memcpy((void *)RandomMapGen.MapSeeder, (void const *)&RandomMapGen.SeedData, sizeof(MapSeedClass));

	UI_Map_Generator_Repaint();
}


void UIMapgenPresenter::Execute(UIIntent const & intent)
{
	// A disabled button raised nothing in the dialog, so an intent that names one is dropped
	// here as well as in the view.
	if ((intent.Action == UI_MAPGEN_LOAD && !LoadOn) || (intent.Action == UI_MAPGEN_DELETE && !DeleteOn)
		|| (intent.Action == UI_MAPGEN_PREVIEW && !PreviewOn) || (intent.Action == UI_MAPGEN_SURPRISE && !SurpriseOn)) {
		return;
	}

	switch (intent.Action) {
		case UI_MAPGEN_SET: {
			int const value = std::atoi(intent.Text.c_str());

			switch (intent.Identity) {
				case UI_MAPGEN_BIOME:			Biome = value; break;
				case UI_MAPGEN_TIME:			Time = value; break;
				case UI_MAPGEN_WIDTH:			Width = value; break;
				case UI_MAPGEN_HEIGHT:			Height = value; break;
				case UI_MAPGEN_PLAYERS:			Players.Value = value; break;
				case UI_MAPGEN_CLIFFS:			Cliffs.Value = value; break;
				case UI_MAPGEN_ACCESSIBILITY:	Accessibility.Value = value; break;
				case UI_MAPGEN_HILLS:			Hills.Value = value; break;
				case UI_MAPGEN_TIBERIUM:		Tiberium.Value = value; break;
				case UI_MAPGEN_FIELDS:			Fields.Value = value; break;
				case UI_MAPGEN_WATER:			Water.Value = value; break;
				case UI_MAPGEN_VEGETATION:		Vegetation.Value = value; break;
				case UI_MAPGEN_CITIES:			Cities.Value = value; break;
				case UI_MAPGEN_VEINHOLES:		Veinholes.Value = value; break;
				case UI_MAPGEN_LIFEFORMS:		Lifeforms = value != 0; break;
				case UI_MAPGEN_TRANSITIONS:		Transitions = value != 0; break;
				case UI_MAPGEN_ION_STORMS:		IonStorms = value != 0; break;
				default:						break;
			}
			break;
		}

		case UI_MAPGEN_ACCEPT:
			Adopt();
			if (Debug_Map) {
				RandomMapGen.Generate_Random_Map(false);
				Scen->Set_Scenario_Name(Fetch_String(TXT_RANDOM_MAP_DESCRIPTION));
				Write_Scenario_INI("RandMap.Map", true);
			} else if (RandomMapGen.MapPreview == nullptr || RandomMapGen.MapPreview->Get_Preview_Surface() == nullptr) {
				RandomMapGen.Generate_Random_Map(true);
			}
			Finish(UI_RESULT_ACCEPTED, 1);
			break;

		case UI_MAPGEN_CANCEL:
			Finish(UI_RESULT_CANCELLED, 2);
			break;

		// The seed files are listed by the load and save screens, which open over this one,
		// so this document steps aside while they are up.
		case UI_MAPGEN_LOAD: {
			Adopt();
			if (Set_Visible) Set_Visible(false);
			bool const loaded = RandomMapGen.SeedData.LoadOptionsClass::Load();
			if (Set_Visible) Set_Visible(true);
			if (loaded) {
				Build_Preview();
			}
			Describe();
			break;
		}

		case UI_MAPGEN_SAVE:
			Adopt();
			RandomMapGen.SeedData.MapDescription[0] = '\0';
			if (Set_Visible) Set_Visible(false);
			RandomMapGen.SeedData.LoadOptionsClass::Save(RandomMapGen.SeedData.MapDescription);
			if (Set_Visible) Set_Visible(true);
			Enable_Files();
			break;

		case UI_MAPGEN_DELETE:
			Adopt();
			if (Set_Visible) Set_Visible(false);
			RandomMapGen.SeedData.LoadOptionsClass::Delete();
			if (Set_Visible) Set_Visible(true);
			Enable_Files();
			break;

		case UI_MAPGEN_PREVIEW:
			Build_Preview();
			break;

		case UI_MAPGEN_SURPRISE:
			Adopt();
			RandomMapGen.SeedData.Randomize();
			Describe();
			break;

		default:
			break;
	}

	Refresh();
}


class UIMapgenView : public UIRmlViewClass
{
	public:
		UIMapgenView(UIMapgenPresenter & presenter) :
			UIRmlViewClass(presenter, "mapgen.rml"), Mapgen(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UIMapgenPresenter & Mapgen;
		Rml::DataModelHandle Model;
};


void UIMapgenView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	if (name == "set" && arguments.size() > 1) {
		Rml::String const control = arguments[1].Get<Rml::String>();

		for (int index = 0; index < UI_MAPGEN_CONTROL_COUNT; index++) {
			if (control == _ControlNames[index]) {
				intent.Action = UI_MAPGEN_SET;
				intent.Identity = index;
				break;
			}
		}

		if (intent.Action == 0) {
			return;
		}

		// A check box reports whether it is checked, and a range or a combo box reports its
		// value, read from the event for the reason sound.rml's controls are.
		if (event.GetParameters().count("checked") > 0) {
			intent.Text = event.GetParameter<bool>("checked", false) ? "1" : "0";
		} else {
			intent.Text = event.GetParameter<Rml::String>("value", "0");
		}
	} else if (name == "accept") {
		intent.Action = UI_MAPGEN_ACCEPT;
	} else if (name == "cancel") {
		intent.Action = UI_MAPGEN_CANCEL;
	} else if (name == "load") {
		intent.Action = UI_MAPGEN_LOAD;
	} else if (name == "save") {
		intent.Action = UI_MAPGEN_SAVE;
	} else if (name == "delete") {
		intent.Action = UI_MAPGEN_DELETE;
	} else if (name == "preview") {
		intent.Action = UI_MAPGEN_PREVIEW;
	} else if (name == "surprise") {
		intent.Action = UI_MAPGEN_SURPRISE;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UIMapgenView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("mapgen");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UIMapgenOption>(constructor)) {
		handle.RegisterMember("Name", &UIMapgenOption::Name);
		handle.RegisterMember("Value", &UIMapgenOption::Value);
	}
	UI_Register_Array<std::vector<UIMapgenOption>>(constructor);

	if (auto handle = UI_Register_Struct<UIMapgenSlider>(constructor)) {
		handle.RegisterMember("Min", &UIMapgenSlider::Min);
		handle.RegisterMember("Max", &UIMapgenSlider::Max);
		handle.RegisterMember("Value", &UIMapgenSlider::Value);
		handle.RegisterMember("On", &UIMapgenSlider::On);
	}

	constructor.Bind("Variant", &Mapgen.Variant);
	constructor.Bind("Biomes", &Mapgen.Biomes);
	constructor.Bind("Times", &Mapgen.Times);
	constructor.Bind("Sizes", &Mapgen.Sizes);
	constructor.Bind("Biome", &Mapgen.Biome);
	constructor.Bind("Time", &Mapgen.Time);
	constructor.Bind("Width", &Mapgen.Width);
	constructor.Bind("Height", &Mapgen.Height);
	constructor.Bind("BiomeOn", &Mapgen.BiomeOn);
	constructor.Bind("TimeOn", &Mapgen.TimeOn);
	constructor.Bind("WidthOn", &Mapgen.WidthOn);
	constructor.Bind("HeightOn", &Mapgen.HeightOn);
	constructor.Bind("Players", &Mapgen.Players);
	constructor.Bind("Cliffs", &Mapgen.Cliffs);
	constructor.Bind("Accessibility", &Mapgen.Accessibility);
	constructor.Bind("Hills", &Mapgen.Hills);
	constructor.Bind("Tiberium", &Mapgen.Tiberium);
	constructor.Bind("Fields", &Mapgen.Fields);
	constructor.Bind("Water", &Mapgen.Water);
	constructor.Bind("Vegetation", &Mapgen.Vegetation);
	constructor.Bind("Cities", &Mapgen.Cities);
	constructor.Bind("Veinholes", &Mapgen.Veinholes);
	constructor.Bind("Lifeforms", &Mapgen.Lifeforms);
	constructor.Bind("LifeformsOn", &Mapgen.LifeformsOn);
	constructor.Bind("Transitions", &Mapgen.Transitions);
	constructor.Bind("TransitionsOn", &Mapgen.TransitionsOn);
	constructor.Bind("IonStorms", &Mapgen.IonStorms);
	constructor.Bind("LoadOn", &Mapgen.LoadOn);
	constructor.Bind("DeleteOn", &Mapgen.DeleteOn);
	constructor.Bind("PreviewOn", &Mapgen.PreviewOn);
	constructor.Bind("SurpriseOn", &Mapgen.SurpriseOn);
	constructor.Bind("HasPreview", &Mapgen.HasPreview);

	constructor.BindEventCallback("act", &UIMapgenView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIMapgenView::Bind(void)
{
	Attach_Actions();

	Mapgen.Set_Visible = [this](bool visible) {
		if (visible) {
			Show(true);
		} else {
			Hide();
		}
	};

	return(true);
}


// Every control is repositioned when the seed has been read afresh, which is how a load or a
// surprise moves all the sliders; otherwise only what an action enables or disables is
// pushed, so a control being dragged is never written over.
void UIMapgenView::Sync(void)
{
	if (!Model) {
		return;
	}

	if (Mapgen.Reread) {
		Mapgen.Reread = false;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("LoadOn");
	Model.DirtyVariable("DeleteOn");
	Model.DirtyVariable("HasPreview");
}


void UIMapgenView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("mapgen");
	}

	Model = Rml::DataModelHandle();
	Mapgen.Set_Visible = nullptr;
}


static bool _ScreenIsUp = false;


static Surface * Preview_Surface(void)
{
	if (RandomMapGen.MapPreview == nullptr) {
		return(nullptr);
	}
	return(RandomMapGen.MapPreview->Get_Preview_Surface());
}


void UI_Map_Generator_Repaint(void)
{
	if (!_ScreenIsUp) {
		return;
	}

	// The generator runs to completion without returning to the runner, so the preview is
	// put on the screen here, stage by stage, as the dialog repainted.
	UI_Surface_Invalidate("mapgen-preview");

	Rml::Context * context = UI_Context();
	if (context != nullptr) {
		UI_Update_Context(context);
	}
	Video_Present();
}


int UI_Map_Generator_Screen(bool (*callback)())
{
	UIMapgenPresenter presenter;
	UIMapgenView view(presenter);

	presenter.Callback = callback;
	presenter.Initialize();

	UI_Surface_Register("mapgen-preview", Preview_Surface);
	UI_Surface_Invalidate("mapgen-preview");

	_ScreenIsUp = true;
	UIResult const result = UI_Run_Modal(presenter, view);
	_ScreenIsUp = false;

	switch (result.Type) {
		case UI_RESULT_FAILED:
			return(0);

		case UI_RESULT_ACCEPTED:
			return(1);

		default:
			return(2);
	}
}
