/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The sound and music options screen. The behavior it has to preserve is which call each
// control makes and when: a volume applies with feedback while it is dragged and again
// without feedback when the screen is accepted, shuffle and repeat turn each other off,
// and the track list offers the themes the campaign allows.

#include "always.h"

#include "uisound.h"

#include "_rules.h"
#include "audio/audioengine.h"
#include "dialogresult.h"
#include "globals.h"
#include "goptions.h"
#include "incdec.h"
#include "rules.h"
#include "sounddlg.h"
#include "theme.h"
#include "voc.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include <string>
#include <vector>


// One row of the track list. Identity is the theme rather than the row, so a list built
// from a different set of allowed themes cannot redirect a play request.
struct UISoundTrack
{
	Rml::String Name;
	int Identity = 0;
};


enum
{
	UI_SOUND_ACCEPT = UI_ACTION_ACCEPT,
	UI_SOUND_SCORE_VOLUME = UI_ACTION_SCREEN,
	UI_SOUND_SOUND_VOLUME,
	UI_SOUND_VOICE_VOLUME,
	UI_SOUND_SHUFFLE,
	UI_SOUND_REPEAT,
	UI_SOUND_PLAY,
	UI_SOUND_STOP,
	UI_SOUND_SELECT,
};


class UISoundPresenter : public UIPresenterClass
{
	public:
		// The slider positions, in the same steps the track bars had.
		int ScoreVolume = 0;
		int SoundVolume = 0;
		int VoiceVolume = 0;

		bool Shuffle = false;
		bool Repeat = false;

		// Every control was disabled outright when there was no audio device.
		bool Enabled = false;

		// The dialog had a cut down template for the frontend, without the music controls
		// that only apply to a game in progress.
		bool InGame = false;

		std::vector<UISoundTrack> Tracks;
		int Selected = 0;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;

	private:
		void Apply_Volumes(bool feedback);
};


static int Volume_To_Level(float volume)
{
	// The dialog rounded a stored volume to the nearest step when it set the track bar.
	return((int)(volume * (double)SoundControlsClass::VOLUME_LEVELS + 0.5));
}


void UISoundPresenter::Refresh(void)
{
	Enabled = AudioEngine.Is_Available();
	InGame = (GameActive != false);

	ScoreVolume = Volume_To_Level(Options.ScoreVolume);
	SoundVolume = Volume_To_Level(Options.SoundVolume);
	VoiceVolume = Volume_To_Level(Options.VoiceVolume);

	Shuffle = Options.IsScoreShuffle;
	Repeat = Options.IsScoreRepeat;

	Tracks.clear();
	Selected = 0;

	if (!InGame) {
		return;
	}

	int number = 1;

	for (ThemeType index = THEME_FIRST; index < Theme.Max_Themes(); index++) {
		if (!Theme.Is_Allowed(index)) {
			continue;
		}

		char buffer[100];
		int const length = Theme.Track_Length(index);

		snprintf(buffer, sizeof(buffer), "%02d - %s [%d:%02d]", number, Theme.Full_Name(index), length / 60, length % 60);
		number++;

		UISoundTrack track;
		track.Name = buffer;
		track.Identity = (int)index;

		if (Theme.What_Is_Playing() == index) {
			Selected = (int)Tracks.size();
		}

		Tracks.push_back(track);
	}
}


void UISoundPresenter::Apply_Volumes(bool feedback)
{
	float const levels = (float)SoundControlsClass::VOLUME_LEVELS;

	Options.Set_Score_Volume((float)ScoreVolume / levels, feedback);
	Options.Set_Sound_Volume((float)SoundVolume / levels, feedback);
	Options.Set_Voice_Volume((float)VoiceVolume / levels, feedback);
}


void UISoundPresenter::Execute(UIIntent const & intent)
{
	float const levels = (float)SoundControlsClass::VOLUME_LEVELS;

	switch (intent.Action) {
		case UI_SOUND_SCORE_VOLUME:
			ScoreVolume = intent.Identity;
			Options.Set_Score_Volume((float)ScoreVolume / levels, true);
			break;

		case UI_SOUND_SOUND_VOLUME:
			SoundVolume = intent.Identity;
			Options.Set_Sound_Volume((float)SoundVolume / levels, true);
			break;

		case UI_SOUND_VOICE_VOLUME:
			VoiceVolume = intent.Identity;
			Options.Set_Voice_Volume((float)VoiceVolume / levels, true);
			break;

		case UI_SOUND_SHUFFLE:
			Shuffle = (intent.Identity != 0);
			Options.Set_Shuffle(Shuffle);

			// Turning one on turned the other off; turning one off left the other alone.
			if (Shuffle) {
				Repeat = false;
				Options.Set_Repeat(false);
			}
			break;

		case UI_SOUND_REPEAT:
			Repeat = (intent.Identity != 0);
			Options.Set_Repeat(Repeat);

			if (Repeat) {
				Shuffle = false;
				Options.Set_Shuffle(false);
			}
			break;

		case UI_SOUND_STOP:
			Theme.Queue_Song(THEME_QUIET);
			break;

		case UI_SOUND_SELECT:
			// The list clicked on a press, whether or not the row changed.
			Sound_Effect(Rule->GenericClick);
			if (intent.Identity >= 0 && intent.Identity < (int)Tracks.size()) {
				Selected = intent.Identity;
			}
			break;

		case UI_SOUND_PLAY:
			// The theme is resolved from the row at the moment the request runs, so a list
			// rebuilt in between cannot start a different track.
			if (Selected >= 0 && Selected < (int)Tracks.size()) {
				Theme.Stop();
				Theme.Queue_Song((ThemeType)Tracks[Selected].Identity);
			}
			break;

		case UI_SOUND_ACCEPT:
			// The dialog set every volume again without feedback as it closed, which is
			// what writes them back without a preview sound.
			Apply_Volumes(false);
			Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
			break;

		default:
			break;
	}
}


class UISoundView : public UIRmlViewClass
{
	public:
		UISoundView(UISoundPresenter & presenter) :
			UIRmlViewClass(presenter, "sound.rml"), Sound(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		void On_Action(Rml::DataModelHandle model, Rml::Event & event, Rml::VariantList const & arguments);

		UISoundPresenter & Sound;
		Rml::DataModelHandle Model;

		// Whether the read state has reached the controls yet.
		bool Pushed = false;
};


// A control's new value is taken from the event rather than from the bound variable. RmlUi
// raises the change event before it stores the value, and the order between this listener
// and the binding's own is not defined, so reading the variable here can read the value the
// control has just replaced.
static int Changed_Value(Rml::Event & event, int fallback)
{
	// The range reports its value as a number, and rounding matches how the dialog turned a
	// stored volume into a step.
	return((int)(event.GetParameter<float>("value", (float)fallback) + 0.5f));
}


static bool Changed_Check(Rml::Event & event, bool fallback)
{
	return(event.GetParameter<bool>("checked", fallback));
}


// The document names an action and, for a row, the row it means.
void UISoundView::On_Action(Rml::DataModelHandle, Rml::Event & event, Rml::VariantList const & arguments)
{
	if (arguments.empty()) {
		return;
	}

	Rml::String const name = arguments[0].Get<Rml::String>();
	UIIntent intent;

	if (name == "accept") {
		intent.Action = UI_SOUND_ACCEPT;
	} else if (name == "score") {
		intent.Action = UI_SOUND_SCORE_VOLUME;
		intent.Identity = Changed_Value(event, Sound.ScoreVolume);
	} else if (name == "sound") {
		intent.Action = UI_SOUND_SOUND_VOLUME;
		intent.Identity = Changed_Value(event, Sound.SoundVolume);
	} else if (name == "voice") {
		intent.Action = UI_SOUND_VOICE_VOLUME;
		intent.Identity = Changed_Value(event, Sound.VoiceVolume);
	} else if (name == "shuffle") {
		intent.Action = UI_SOUND_SHUFFLE;
		intent.Identity = Changed_Check(event, Sound.Shuffle) ? 1 : 0;
	} else if (name == "repeat") {
		intent.Action = UI_SOUND_REPEAT;
		intent.Identity = Changed_Check(event, Sound.Repeat) ? 1 : 0;
	} else if (name == "play") {
		intent.Action = UI_SOUND_PLAY;
	} else if (name == "stop") {
		intent.Action = UI_SOUND_STOP;
	} else if (name == "select") {
		intent.Action = UI_SOUND_SELECT;
		intent.Identity = arguments.size() > 1 ? arguments[1].Get<int>() : 0;
	} else {
		return;
	}

	Presenter.Queue(intent);
}


bool UISoundView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	Rml::DataModelConstructor constructor = context->CreateDataModel("sound");
	if (!constructor) {
		return(false);
	}

	if (auto handle = UI_Register_Struct<UISoundTrack>(constructor)) {
		handle.RegisterMember("Name", &UISoundTrack::Name);
		handle.RegisterMember("Identity", &UISoundTrack::Identity);
	}

	UI_Register_Array<std::vector<UISoundTrack>>(constructor);

	// The volumes and the two flags are bound both ways: a control writes the view model
	// and the action that follows reads what it wrote.
	constructor.Bind("ScoreVolume", &Sound.ScoreVolume);
	constructor.Bind("SoundVolume", &Sound.SoundVolume);
	constructor.Bind("VoiceVolume", &Sound.VoiceVolume);
	constructor.Bind("Shuffle", &Sound.Shuffle);
	constructor.Bind("Repeat", &Sound.Repeat);

	constructor.Bind("Enabled", &Sound.Enabled);
	constructor.Bind("InGame", &Sound.InGame);
	constructor.Bind("Tracks", &Sound.Tracks);
	constructor.Bind("Selected", &Sound.Selected);

	constructor.BindEventCallback("act", &UISoundView::On_Action, this);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UISoundView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UISoundView::Sync(void)
{
	if (!Model) {
		return;
	}

	// The first pass is what puts the read state into the controls. After that only what an
	// intent can change from under the player is pushed back: dirtying a volume on every
	// pass would write the model's value over the one a slider is being dragged to.
	if (!Pushed) {
		Pushed = true;
		Model.DirtyAllVariables();
		return;
	}

	Model.DirtyVariable("Shuffle");
	Model.DirtyVariable("Repeat");
	Model.DirtyVariable("Selected");
}


void UISoundView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("sound");
	}

	Model = Rml::DataModelHandle();
}


bool UI_Sound_Screen(void)
{
	UISoundPresenter presenter;
	UISoundView view(presenter);

	UIResult const result = UI_Run_Modal(presenter, view);

	return(result.Type != UI_RESULT_FAILED);
}
