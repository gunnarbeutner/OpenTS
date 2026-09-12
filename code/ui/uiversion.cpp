/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The version information screen. It is read only: it collects the lines the legacy
// dialog's list box held, in the same order, and closes on a dismissal.

#include "always.h"

#include "uiversion.h"

#include "addon.h"
#include "data.h"
#include "dialogresult.h"
#include "getcpu.h"
#include "globals.h"
#include "language/language.h"
#include "uicontext.h"
#include "uimodel.h"
#include "uirmlview.h"
#include "uirunner.h"
#include "utf8.h"
#include "version.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <opents_build.h>

#include <cstdio>
#include <string>
#include <vector>


class UIVersionPresenter : public UIPresenterClass
{
	public:
		std::vector<Rml::String> Lines;

		void Refresh(void) override;

	protected:
		void Execute(UIIntent const & intent) override;
};


/// <summary>
/// Collects the title, the versions, the build stamp, the processor, and the language
/// build, in the order the legacy list box held them.
/// </summary>
void UIVersionPresenter::Refresh(void)
{
	char buffer[256];

	Lines.clear();

	if (Addon_Installed(ADDON_FIRESTORM)) {
		UTF8::Copy(buffer, Fetch_String(TXT_SHORT_TITLE));
		UTF8::Append(buffer, ": ");
		UTF8::Append(buffer, Get_Addon_Title(ADDON_FIRESTORM));
		Lines.push_back(buffer);
	} else {
		Lines.push_back(Fetch_String(TXT_SHORT_TITLE));
	}

	snprintf(buffer, sizeof(buffer), "Version %s", Version_Name());
	Lines.push_back(buffer);

	snprintf(buffer, sizeof(buffer), "Internal Version %s", VerNum.Version_Name());
	Lines.push_back(buffer);

#ifdef _DEBUG
	snprintf(buffer, sizeof(buffer), "Debug Build: %s - %s", OPENTS_BUILD_DESCRIPTION, OPENTS_COMMIT_DATE);
#else
	snprintf(buffer, sizeof(buffer), "Release Build: %s - %s", OPENTS_BUILD_DESCRIPTION, OPENTS_COMMIT_DATE);
#endif
	Lines.push_back(buffer);

	{
		int cpu_type = 5;
		char vendor[32];
		vendor[0] = '\0';
		Get_CPU_Type(cpu_type, vendor, sizeof(vendor) - 1);

		snprintf(buffer, sizeof(buffer), "CPU vendor: %s", vendor);
		Lines.push_back(buffer);
	}

	Get_Language_Version(buffer);
	Lines.push_back(buffer);
}


void UIVersionPresenter::Execute(UIIntent const & intent)
{
	// The legacy dialog answered a dismissal with whichever of OK and Cancel raised it, and
	// its caller only waited for either. Both map to the one way out this screen has.
	if (intent.Action == UI_ACTION_ACCEPT || intent.Action == UI_ACTION_CANCEL) {
		Finish(UI_RESULT_ACCEPTED, DIALOG_OK);
	}
}


class UIVersionView : public UIRmlViewClass
{
	public:
		UIVersionView(UIVersionPresenter & presenter) :
			UIRmlViewClass(presenter, "version.rml"), Version(presenter) {}

		void Sync(void) override;

	protected:
		bool Bind_Model(void) override;
		bool Bind(void) override;
		void Release_Model(void) override;

	private:
		UIVersionPresenter & Version;
		Rml::DataModelHandle Model;
};


bool UIVersionView::Bind_Model(void)
{
	Rml::Context * context = UI_Context();
	if (context == nullptr) {
		return(false);
	}

	// The binding's storage is the presenter's vector. The presenter outlives the view, and
	// the view releases the model before either goes, so the model never points at storage
	// that has gone.
	Rml::DataModelConstructor constructor = context->CreateDataModel("version");
	if (!constructor) {
		return(false);
	}

	UI_Register_Array<std::vector<Rml::String>>(constructor);
	constructor.Bind("Lines", &Version.Lines);

	Model = constructor.GetModelHandle();
	return(true);
}


bool UIVersionView::Bind(void)
{
	Attach_Actions();
	return(true);
}


void UIVersionView::Release_Model(void)
{
	Rml::Context * context = UI_Context();

	if (context != nullptr && Model) {
		context->RemoveDataModel("version");
	}

	Model = Rml::DataModelHandle();
}


void UIVersionView::Sync(void)
{
	if (Model) {
		Model.DirtyVariable("Lines");
	}
}


bool UI_Version_Screen(void)
{
	UIVersionPresenter presenter;
	UIVersionView view(presenter);

	UIResult const result = UI_Run_Modal(presenter, view);

	return(result.Type != UI_RESULT_FAILED);
}
