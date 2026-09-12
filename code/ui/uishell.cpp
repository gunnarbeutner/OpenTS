/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The UI shell. It owns the RmlUi context, the overlay pass, and the input scope, and it
// is the only place that includes RmlUi outside the interface adapters beside it.

#include "always.h"

#include "uishell.h"

#include "_keyboar.h"
#include "dbgprint.h"
#include "keyboard.h"
#include "uicontext.h"
#include "uifile.h"
#include "uifontengine.h"
#include "uirender.h"
#include "globals.h"
#include "goptions.h"
#include "uisystem.h"
#include "video.h"
#include "mstimer.h"
#include "wwmouse.h"
#include "audio/audioengine.h"
#include "mixfile.h"
#include "mpload.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StyleTypes.h>


static bool _Initialized = false;
static Rml::Context * _Context = nullptr;

// Set while a document is visible, and cleared by the present that drew it. The overlay
// has no "needs redraw" query, so a visible document keeps asking; the present pacing in
// Video_Present_If_Dirty is what caps the rate.
static bool _OverlayIsDirty = false;

// Guards against a present raised from inside the overlay's own render, which would submit
// the context twice into one frame.
static bool _Rendering = false;

// Guards against an update raised from inside an update, which a wait serviced from a
// document's own event handler would otherwise do.
static bool _Updating = false;

static bool _GameFontsLoaded = false;

static int _ContextWidth = 0;
static int _ContextHeight = 0;

// How many modal screens are open. Only the outermost one's scope brackets the keyboard
// queue; a message box over an options screen must not end the options screen's.
static int _ModalDepth = 0;


static int Translate_Modifiers(unsigned int modifiers)
{
	int state = 0;

	if ((modifiers & UI_MODIFIER_SHIFT) != 0) {
		state |= Rml::Input::KM_SHIFT;
	}
	if ((modifiers & UI_MODIFIER_CONTROL) != 0) {
		state |= Rml::Input::KM_CTRL;
	}
	if ((modifiers & UI_MODIFIER_ALT) != 0) {
		state |= Rml::Input::KM_ALT;
	}
	if ((modifiers & UI_MODIFIER_META) != 0) {
		state |= Rml::Input::KM_META;
	}

	return(state);
}


static Rml::Input::KeyIdentifier Translate_Key(UIKeyType key)
{
	switch (key) {
		case UI_KEY_TAB:		return(Rml::Input::KI_TAB);
		case UI_KEY_RETURN:		return(Rml::Input::KI_RETURN);
		case UI_KEY_ESCAPE:		return(Rml::Input::KI_ESCAPE);
		case UI_KEY_SPACE:		return(Rml::Input::KI_SPACE);
		case UI_KEY_BACKSPACE:	return(Rml::Input::KI_BACK);
		case UI_KEY_DELETE:		return(Rml::Input::KI_DELETE);
		case UI_KEY_INSERT:		return(Rml::Input::KI_INSERT);
		case UI_KEY_HOME:		return(Rml::Input::KI_HOME);
		case UI_KEY_END:		return(Rml::Input::KI_END);
		case UI_KEY_PAGE_UP:	return(Rml::Input::KI_PRIOR);
		case UI_KEY_PAGE_DOWN:	return(Rml::Input::KI_NEXT);
		case UI_KEY_LEFT:		return(Rml::Input::KI_LEFT);
		case UI_KEY_RIGHT:		return(Rml::Input::KI_RIGHT);
		case UI_KEY_UP:			return(Rml::Input::KI_UP);
		case UI_KEY_DOWN:		return(Rml::Input::KI_DOWN);
		default:
			break;
	}

	// The function keys are contiguous from KI_F1, so the block is mapped by offset rather
	// than named one case at a time.
	if (key >= UI_KEY_F1 && key <= UI_KEY_F12) {
		return((Rml::Input::KeyIdentifier)(Rml::Input::KI_F1 + (key - UI_KEY_F1)));
	}

	return(Rml::Input::KI_UNKNOWN);
}


// The context is sized to the frame's destination and laid out from its top left corner, so
// a document's coordinates are physical pixels inside the presented frame and the letterbox
// beside it is never part of the UI.
static void Apply_Frame_Geometry(void)
{
	if (_Context == nullptr) {
		return;
	}

	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	int width = scale.DestWidth > 0 ? scale.DestWidth : 1;
	int height = scale.DestHeight > 0 ? scale.DestHeight : 1;

	if (width != _ContextWidth || height != _ContextHeight) {
		_Context->SetDimensions(Rml::Vector2i(width, height));
		_ContextWidth = width;
		_ContextHeight = height;
	}

	// One authored density-independent pixel is one game logical unit, so a document
	// authored at a legacy dialog's size covers the same part of the screen while its text
	// is rasterized at the window's resolution. The frame keeps its aspect ratio, so the
	// two axis scales differ by less than a pixel and the smaller one serves both.
	float ratio = scale.ScaleX < scale.ScaleY ? scale.ScaleX : scale.ScaleY;
	if (ratio <= 0.0f) {
		ratio = 1.0f;
	}

	UI_Font_Set_Pixel_Ratio(ratio);
	_Context->SetDensityIndependentPixelRatio(ratio);
}


// Client pixels reach a document relative to the frame's destination, and are never divided
// by the density ratio: the context works in physical pixels.
static bool Client_To_Context(int clientx, int clienty, int * contextx, int * contexty)
{
	VideoScaleInfo const & scale = Video_Get_Scale_Info();

	int x = clientx - scale.DestX;
	int y = clienty - scale.DestY;

	*contextx = x;
	*contexty = y;

	return(x >= 0 && y >= 0 && x < scale.DestWidth && y < scale.DestHeight);
}


// The faces every document draws with. RmlUi reads them through the shell's file
// interface, so they resolve from the shipped directory, a search folder, or a mix. A face
// that will not load is reported and the rest are still installed: a screen without text
// still works, and one with text reports the missing face rather than drawing nothing.
static void Load_Fonts(void)
{
	// The family is named here rather than taken from the file, so a stylesheet does not
	// depend on how a font happens to name itself and a face can be swapped without
	// editing every document.
	struct FaceEntry
	{
		char const * File;
		Rml::Style::FontWeight Weight;
	};

	static FaceEntry const faces[] = {
		{ "LiberationSans-Regular.ttf", Rml::Style::FontWeight::Normal },
		{ "LiberationSans-Bold.ttf", Rml::Style::FontWeight::Bold },
	};

	for (FaceEntry const & face : faces) {
		if (!Rml::LoadFontFace(face.File, "opents-sans", Rml::Style::FontStyle::Normal, face.Weight)) {
			DebugString("UI: font face '%s' would not load\n", face.File);
		}
	}
}


/// <summary>
/// Serves the family `opents-dialog` from the dialogs' glyph sheets in the player's game data.
/// When the sheets cannot be read the shipped face is installed under that family instead, so
/// text asking for it still draws.
/// </summary>
/// <remarks>Needs the game's mix files registered, so startup calls it after them.</remarks>
void UI_Load_Game_Fonts(void)
{
	if (!_Initialized || _GameFontsLoaded) {
		return;
	}

	_GameFontsLoaded = true;
	unsigned int const started = System_Milliseconds();

	if (!UI_Font_Load_Dialog_Face()) {
		// The family still has to exist, or text asking for it would draw nothing at all.
		DebugString("UI: the dialog sheets could not be read; the shipped face stands in for them\n");
		Rml::LoadFontFace("LiberationSans-Regular.ttf", "opents-dialog", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
	}

	DebugString("UI: the dialog face took %u ms\n", System_Milliseconds() - started);
}


// The opening wipe a dialog made: its picture revealed from the centre outward between two
// bars sliding apart, twelve of its pixels a side each frame, over about half a second.
static Rml::ElementDocument * _RevealDocument = nullptr;
static std::int64_t _RevealStart = 0;
static bool _RevealActive = false;
static int _RevealLeft = 0;
static int _RevealTop = 0;
static int _RevealRight = 0;
static int _RevealBottom = 0;
static float _RevealRatio = 1.0f;


// A document switches the overlay's reveal on for itself as its contents render, and off for
// any other, so screens already showing are drawn whole while one above them opens.
class UIDocumentClass : public Rml::ElementDocument
{
	public:
		UIDocumentClass(Rml::String const & tag) : Rml::ElementDocument(tag) {}

	protected:
		void OnRender(void) override
		{
			bool const revealing = _RevealActive && this == _RevealDocument;
			UI_Render_Set_Reveal(revealing, _RevealLeft, _RevealTop, _RevealRight, _RevealBottom);
			Rml::ElementDocument::OnRender();
		}
};

static Rml::ElementInstancerGeneric<UIDocumentClass> _DocumentInstancer;


void UI_Begin_Reveal(Rml::ElementDocument * document)
{
	_RevealDocument = document;
	_RevealStart = Monotonic_Milliseconds();
	_RevealActive = false;

	if (Options.SoundVolume > 0.0) {
		AudioEngine.Play_Sample(MixFileClass::Retrieve("EMBLEM.AUD"), AUDIO_GROUP_SFX, 64.0f / 255.0f, 255);
	}

	UI_Mark_Overlay_Dirty();
}


void UI_End_Reveal(Rml::ElementDocument * document)
{
	if (_RevealDocument == document) {
		_RevealDocument = nullptr;
		_RevealActive = false;
	}
}


/// <summary>
/// Works out how far the opening dialog has been revealed. The painter's frame f ended at
/// f * (40 - 240 * (f - 1) / half) milliseconds from the start, half being half the dialog's
/// width in its own pixels, and revealed twelve of those pixels a side per frame.
/// </summary>
static void Update_Reveal(void)
{
	_RevealActive = false;

	if (_RevealDocument == nullptr) {
		return;
	}

	if (!_RevealDocument->IsVisible()) {
		_RevealDocument = nullptr;
		return;
	}

	Rml::Element * dialog = _RevealDocument->QuerySelector(".dialog");
	if (dialog == nullptr) {
		dialog = _RevealDocument;
	}

	Rml::Vector2f const offset = dialog->GetAbsoluteOffset(Rml::BoxArea::Border);
	Rml::Vector2f const size = dialog->GetBox().GetSize(Rml::BoxArea::Border);

	_RevealRatio = _Context->GetDensityIndependentPixelRatio();
	int const half = (int)(size.x / _RevealRatio / 2.0f);
	if (half <= 0) {
		return;
	}

	std::int64_t const elapsed = Monotonic_Milliseconds() - _RevealStart;
	int frame = 1;
	while (frame * (40 - 240 * (frame - 1) / half) <= elapsed) {
		frame++;
	}

	int const step = 12 * frame;
	if (step >= half) {
		_RevealDocument = nullptr;
		return;
	}

	float const centre = offset.x + size.x / 2.0f;
	_RevealLeft = (int)(centre - step * _RevealRatio);
	_RevealRight = (int)(centre + step * _RevealRatio + 0.5f);
	_RevealTop = (int)offset.y;
	_RevealBottom = (int)(offset.y + size.y + 0.5f);
	_RevealActive = true;
}


// The bars ride the edges of the revealed band, inside it.
static void Draw_Reveal_Bars(void)
{
	float const height = (float)(_RevealBottom - _RevealTop);
	Rml::Vector2i const right = UI_Render_Picture_Size("rightbar.pcx");

	UI_Render_Picture("leftbar.pcx", (float)_RevealLeft, (float)_RevealTop, _RevealRatio, height);
	UI_Render_Picture("rightbar.pcx", _RevealRight - right.x * _RevealRatio, (float)_RevealTop, _RevealRatio, height);
}


bool UI_Init(void)
{
	if (_Initialized) {
		return(true);
	}

	Rml::SetFileInterface(UI_File_Interface());
	Rml::SetSystemInterface(UI_System_Interface());

	if (!UI_Render_Init()) {
		DebugString("UI: the renderer would not start; the shell stays off\n");
		return(false);
	}

	Rml::SetRenderInterface(UI_Render_Interface());
	Rml::SetFontEngineInterface(UI_Font_Engine());

	if (!Rml::Initialise()) {
		DebugString("UI: RmlUi would not initialise\n");
		UI_Render_Shutdown();
		return(false);
	}

	Rml::Factory::RegisterElementInstancer("body", &_DocumentInstancer);

	Load_Fonts();

	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	_ContextWidth = scale.DestWidth > 0 ? scale.DestWidth : 1;
	_ContextHeight = scale.DestHeight > 0 ? scale.DestHeight : 1;

	_Context = Rml::CreateContext("main", Rml::Vector2i(_ContextWidth, _ContextHeight));
	if (_Context == nullptr) {
		DebugString("UI: the context would not be created\n");
		Rml::Shutdown();
		UI_Render_Shutdown();
		return(false);
	}

	_Initialized = true;
	Apply_Frame_Geometry();

	return(true);
}


void UI_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	// Rml::Shutdown releases the context, so the pointer is dropped rather than the context
	// removed first, and the render interface outlives the documents that hold its handles.
	_Context = nullptr;
	_Initialized = false;

	Rml::Shutdown();
	UI_Render_Shutdown();

	_GameFontsLoaded = false;

	_OverlayIsDirty = false;
	_ContextWidth = 0;
	_ContextHeight = 0;
}


bool UI_Is_Initialized(void)
{
	return(_Initialized);
}


void UI_On_Frame_Size_Changed(void)
{
	if (!_Initialized) {
		return;
	}

	Apply_Frame_Geometry();
	UI_Render_On_Reset();

	// The documents keep their state, but nothing of the old size is still on the target.
	UI_Mark_Overlay_Dirty();
}


static bool Any_Document_Visible(void)
{
	if (_Context == nullptr) {
		return(false);
	}

	for (int index = 0; index < _Context->GetNumDocuments(); index++) {
		Rml::ElementDocument * document = _Context->GetDocument(index);
		if (document != nullptr && document->IsVisible()) {
			return(true);
		}
	}

	return(false);
}


void UI_Tick(void)
{
	if (!_Initialized || _Context == nullptr || _Updating) {
		return;
	}

	// Call_Back is reached from inside waits that the shell's own work can enter again, and
	// RmlUi promises nothing about an update raised from its own event dispatch.
	_Updating = true;
	Apply_Frame_Geometry();
	UI_Update_Context(_Context);
	_Updating = false;

	if (Any_Document_Visible()) {
		UI_Mark_Overlay_Dirty();
	}
}


void UI_Render_Overlay(void)
{
	if (!_Initialized || _Context == nullptr || _Rendering) {
		return;
	}

	// Nothing is submitted when no document is showing, so the overlay costs a state change
	// and no draw call while the game is playing without one.
	if (!Any_Document_Visible()) {
		_OverlayIsDirty = false;
		return;
	}

	VideoScaleInfo const & scale = Video_Get_Scale_Info();
	if (scale.DestWidth <= 0 || scale.DestHeight <= 0) {
		return;
	}

	_Rendering = true;
	UI_Render_Begin(scale.DestX, scale.DestY, scale.DestWidth, scale.DestHeight);
	Update_Reveal();
	_Context->Render();
	UI_Render_Set_Reveal(false, 0, 0, 0, 0);
	if (_RevealActive) {
		Draw_Reveal_Bars();
	}
	UI_Render_End();
	_Rendering = false;

	// A wipe under way wants its next frame whether or not anything else changed.
	_OverlayIsDirty = (_RevealDocument != nullptr);
}


bool UI_Overlay_Is_Dirty(void)
{
	return(_OverlayIsDirty);
}


void UI_Mark_Overlay_Dirty(void)
{
	_OverlayIsDirty = true;
}


void UI_Handle_Mouse_Move(int clientx, int clienty, unsigned int modifiers)
{
	if (!_Initialized || _Context == nullptr) {
		return;
	}

	int x = 0;
	int y = 0;

	// A move outside the frame is reported as a leave rather than clamped to an edge, so
	// the letterbox cannot hold an element hovered.
	if (!Client_To_Context(clientx, clienty, &x, &y)) {
		_Context->ProcessMouseLeave();
		return;
	}

	_Context->ProcessMouseMove(x, y, Translate_Modifiers(modifiers));
}


bool UI_Handle_Mouse_Button(UIMouseButtonType button, bool down, int clientx, int clienty, unsigned int modifiers)
{
	if (!_Initialized || _Context == nullptr) {
		return(false);
	}

	int x = 0;
	int y = 0;
	bool inside = Client_To_Context(clientx, clienty, &x, &y);

	// The position travels with the button so a press cannot land where the last move left
	// the cursor. A release is delivered even outside the frame, because the element that
	// took the press owns the gesture wherever it ends.
	if (inside || !down) {
		_Context->ProcessMouseMove(x, y, Translate_Modifiers(modifiers));
	} else {
		return(false);
	}

	int index = 0;
	switch (button) {
		case UI_MOUSE_RIGHT:
			index = 1;
			break;

		case UI_MOUSE_MIDDLE:
			index = 2;
			break;

		default:
			index = 0;
			break;
	}

	// RmlUi reports false when the event was used by an element, so the sense is inverted
	// to say whether the shell consumed it.
	bool propagated = down
		? _Context->ProcessMouseButtonDown(index, Translate_Modifiers(modifiers))
		: _Context->ProcessMouseButtonUp(index, Translate_Modifiers(modifiers));

	if (!propagated) {
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	return(false);
}


bool UI_Handle_Mouse_Wheel(float delta, unsigned int modifiers)
{
	if (!_Initialized || _Context == nullptr) {
		return(false);
	}

	bool propagated = _Context->ProcessMouseWheel(delta, Translate_Modifiers(modifiers));
	if (!propagated) {
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	return(false);
}


bool UI_Handle_Key(UIKeyType key, bool down, unsigned int modifiers)
{
	if (!_Initialized || _Context == nullptr) {
		return(false);
	}

	Rml::Input::KeyIdentifier identifier = Translate_Key(key);
	if (identifier == Rml::Input::KI_UNKNOWN) {
		return(false);
	}

	bool propagated = down
		? _Context->ProcessKeyDown(identifier, Translate_Modifiers(modifiers))
		: _Context->ProcessKeyUp(identifier, Translate_Modifiers(modifiers));

	if (!propagated) {
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	return(false);
}


bool UI_Handle_Text(char const * utf8)
{
	if (!_Initialized || _Context == nullptr || utf8 == nullptr) {
		return(false);
	}

	bool propagated = _Context->ProcessTextInput(Rml::String(utf8));
	if (!propagated) {
		UI_Mark_Overlay_Dirty();
		return(true);
	}

	return(false);
}


void UI_On_Focus_Lost(void)
{
	if (!_Initialized || _Context == nullptr) {
		return;
	}

	_Context->ProcessMouseLeave();
	UI_Mark_Overlay_Dirty();
}


bool UI_Modal_Is_Active(void)
{
	if (!_Initialized || _Context == nullptr) {
		return(false);
	}

	for (int index = 0; index < _Context->GetNumDocuments(); index++) {
		Rml::ElementDocument * document = _Context->GetDocument(index);
		if (document != nullptr && document->IsVisible() && document->IsModal()) {
			return(true);
		}
	}

	return(false);
}


Rml::Context * UI_Context(void)
{
	return(_Context);
}


// A modal screen frees the pointer from the game, as the dialogs it replaced did, so it shows
// even while a scripted sequence has hidden the game's.
void UI_Begin_Modal(void)
{
	_ModalDepth++;

	if (_ModalDepth == 1) {
		Menu_Capture_Mouse();
		if (Keyboard != nullptr) {
			Keyboard->Clear();
		}
	}
}


void UI_End_Modal(void)
{
	if (_ModalDepth <= 0) {
		return;
	}

	_ModalDepth--;

	if (_ModalDepth == 0) {
		if (Keyboard != nullptr) {
			Keyboard->Clear();
		}
		Menu_Release_Mouse();
	}

	UI_Mark_Overlay_Dirty();
}
