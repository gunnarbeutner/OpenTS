/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/* $Header: /counterstrike/STARTUP.CPP 6     3/15/97 7:18p Steve_tall $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : STARTUP.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : October 3, 1994                                              *
 *                                                                                             *
 *                  Last Update : September 30, 1996 [JLB]                                     *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Prog_End -- Cleans up library systems in prep for game exit.                              *
 *   main -- Initial startup routine (preps library systems).                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

// Terminate the native client; a browser page defines no OpenTS_Quit and stays put.
static void OpenTS_Host_Quit(void)
{
	EM_ASM({ if (typeof window !== "undefined" && window.OpenTS_Quit) { window.OpenTS_Quit(); } });
}
#endif

#include "_alpha.h"
#include "_command.h"
#include "_convert.h"
#include "_deploymentconfig.h"
#include "_font.h"
#include "_keyboar.h"
#include "_mixfile.h"
#include "_rect.h"
#include "_rules.h"
#include "_surface.h"
#include "_tactica.h"
#include "_zbuffer.h"
#include "aircraft.h"
#include "airctype.h"
#include "aitrig.h"
#include "alphashp.h"
#include "anim.h"
#include "animtype.h"
#include "blight.h"
#include "brain.h"
#include "browser.h"
#include "building.h"
#include "builtype.h"
#include "bullet.h"
#include "bullettype.h"
#include "campaign.h"
#include "cell.h"
#include "classfactory.h"
#include "command.h"
#include "conquer.h"
#include "data.h"
#include "dbgprint.h"
#include "deploymentconfig.h"
#include "drive.h"
#include "droppod.h"
#include "audio/audioengine.h"
#include "dsurface.h"
#include "empulse.h"
#include "except.h"
#include "factory.h"
#include "fly.h"
#include "fog.h"
#include "gamedirs.h"
#include "goptions.h"
#include "hostwindow.h"
#include "house.h"
#include "houstype.h"
#include "hover.h"
#include "infantry.h"
#include "infatype.h"
#include "init.h"
#include "ionblast.h"
#include "ipxmgr.h"
#include "isotype.h"
#include "jumpjet.h"
#include "language/language.h"
#include "levitate.h"
#include "light.h"
#include "lightcon.h"
#include "mech.h"
#include "mixfile.h"
#include "misc.h"
#include "movie.h"
#include "msgloop.h"
#include "netdlg.h" // for Shutdown_Network.
#include "overlay.h"
#include "overtype.h"
#include "ovrlight.h"
#include "particle.h"
#include "partsys.h"
#include "platform/process.h"
#include "psystype.h"
#include "ptype.h"
#include "rules.h"
#include "scenario.h"
#include "scheme.h"
#include "screenlayout.h"
#include "script.h"
#include "session.h"
#include "shapeset.h"
#include "side.h"
#include "sidebar.h"
#include "spawner.h"
#include "smudge.h"
#include "smudtype.h"
#include "sun.h"
#include "super.h"
#include "suprtype.h"
#include "surface.h"
#include "tactical.h"
#include "taction.h"
#include "tag.h"
#include "tagtype.h"
#include "taskforc.h"
#include "team.h"
#include "teamtype.h"
#include "teleport.h"
#include "terrain.h"
#include "terrtype.h"
#include "tevent.h"
#include "theme.h"
#include "tiberium.h"
#include "trigger.h"
#include "trigtype.h"
#include "trim.h"
#include "tube.h"
#include "tunnel.h"
#include "tutorial.h"
#include "unit.h"
#include "unittype.h"
#include "vanim.h"
#include "vanimtype.h"
#include "vector.h"
#include "ui/uishell.h"
#include "video.h"
#include "walk.h"
#include "warhead.h"
#include "wave.h"
#include "waypoint.h"
#include "weapon.h"
#include "win.h"
#include "winstub.h"
#include "wwfont.h"
#include "wwmouse.h"
#include "zbuffer.h"


#include <cfloat>
#include <filesystem>
#include <lzo/lzo1x.h>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#endif

//WinTimerClass * WinTimer;

/// <summary>
/// Destroys the drawing surfaces and drops the video mode.
/// This routine is used on the way out of the game so that the display is handed back to
/// Windows in a sane state, rather than left sitting in the game's own video mode.
/// </summary>
/// <remarks>It is safe to call this routine more than once; only the first call does any
/// work, since several shutdown paths lead here.</remarks>
void Reset_Surfaces(void)
{
	static bool surfaces_reset = false;

	if (!surfaces_reset) {
		if (HiddenSurface) {
			delete HiddenSurface;
			HiddenSurface = NULL;
		}
		if (AlternateSurface) {
			delete AlternateSurface;
			AlternateSurface = NULL;
		}
		if (TileSurface) {
			delete TileSurface;
			TileSurface = NULL;
		}
		if (SidebarSurface) {
			delete SidebarSurface;
			SidebarSurface = NULL;
		}
		if (CompositeSurface) {
			delete CompositeSurface;
			CompositeSurface = NULL;
		}
		if (VisibleSurface) {
			delete VisibleSurface;
			VisibleSurface = NULL;
		}

		UI_Shutdown();
		Video_Shutdown();

		surfaces_reset = true;
	}
}

/// <summary>
/// Registers every class a saved game or a unit type can name by class identifier.
/// This runs during startup, before anything that lives in the object database can be
/// created.
/// </summary>
static void RegisterClasses(void)
{
	#define REGISTER_CLASS(_class, _clsid) Register_Class<_class>(_clsid);

	REGISTER_CLASS(WaveClass, ClassID_WaveClass);
	REGISTER_CLASS(TerrainTypeClass, ClassID_TerrainTypeClass);
	REGISTER_CLASS(TerrainClass, ClassID_TerrainClass);
	REGISTER_CLASS(SuperWeaponTypeClass, ClassID_SuperWeaponTypeClass);
	REGISTER_CLASS(SuperClass, ClassID_SuperWeaponClass);
	REGISTER_CLASS(Tactical, ClassID_TacticalMapClass);
	REGISTER_CLASS(CellClass, ClassID_CellClass);
	REGISTER_CLASS(EMPulseClass, ClassID_EMPulseClass);
	REGISTER_CLASS(LightSourceClass, ClassID_LightSource);
	REGISTER_CLASS(SideClass, ClassID_SideClass);
	REGISTER_CLASS(TiberiumClass, ClassID_TiberiumClass);
	REGISTER_CLASS(TubeClass, ClassID_TubeClass);
	REGISTER_CLASS(CampaignClass, ClassID_CampaignClass);
	REGISTER_CLASS(BuildingLightClass, ClassID_BuildingLightClass);
	REGISTER_CLASS(WaypointPathClass, ClassID_WaypointPath);
	REGISTER_CLASS(TEventClass, ClassID_EventClass);
	REGISTER_CLASS(VoxelAnimTypeClass, ClassID_VoxelAnimTypeClass);
	REGISTER_CLASS(VoxelAnimClass, ClassID_VoxelAnimClass);
	REGISTER_CLASS(TActionClass, ClassID_ActionClass);
	REGISTER_CLASS(TriggerClass, ClassID_TriggerClass);
	REGISTER_CLASS(TriggerTypeClass, ClassID_TriggerTypeClass);
	REGISTER_CLASS(ScriptClass, ClassID_ScriptClass);
	REGISTER_CLASS(ScriptTypeClass, ClassID_ScriptTypeClass);
	REGISTER_CLASS(TagClass, ClassID_TagClass);
	REGISTER_CLASS(TagTypeClass, ClassID_TagTypeClass);
	REGISTER_CLASS(TeamClass, ClassID_TeamClass);
	REGISTER_CLASS(TeamTypeClass, ClassID_TeamTypeClass);
	REGISTER_CLASS(TaskForceClass, ClassID_TaskForceClass);
	REGISTER_CLASS(UnitTypeClass, ClassID_UnitTypeClass);
	REGISTER_CLASS(BuildingTypeClass, ClassID_BuildingTypeClass);
	REGISTER_CLASS(AircraftTypeClass, ClassID_AircraftTypeClass);
	REGISTER_CLASS(InfantryTypeClass, ClassID_InfantryTypeClass);
	REGISTER_CLASS(BulletTypeClass, ClassID_BulletTypeClass);
	REGISTER_CLASS(IsometricTileTypeClass, ClassID_IsometricTileTypeClass);
	REGISTER_CLASS(OverlayTypeClass, ClassID_OverlayTypeClass);
	REGISTER_CLASS(SmudgeTypeClass, ClassID_SmudgeTypeClass);
	REGISTER_CLASS(UnitClass, ClassID_UnitClass);
	REGISTER_CLASS(BuildingClass, ClassID_BuildingClass);
	REGISTER_CLASS(AircraftClass, ClassID_AircraftClass);
	REGISTER_CLASS(InfantryClass, ClassID_InfantryClass);
	REGISTER_CLASS(AnimClass, ClassID_AnimClass);
	REGISTER_CLASS(AnimTypeClass, ClassID_AnimTypeClass);
	REGISTER_CLASS(HouseTypeClass, ClassID_HouseTypeClass);
	REGISTER_CLASS(HouseClass, ClassID_HouseClass);
	REGISTER_CLASS(DriveLocomotionClass, ClassID_DriveLocomotion);
	REGISTER_CLASS(JumpjetLocomotionClass, ClassID_JumpjetLocomotion);
	REGISTER_CLASS(HoverLocomotionClass, ClassID_HoverLocomotion);
	REGISTER_CLASS(TunnelLocomotionClass, ClassID_TunnelLocomotion);
	REGISTER_CLASS(WalkLocomotionClass, ClassID_WalkLocomotion);
	REGISTER_CLASS(DropPodLocomotionClass, ClassID_BallisticLocomotion);
	REGISTER_CLASS(FlyLocomotionClass, ClassID_FlyerLocomotion);
	REGISTER_CLASS(TeleportLocomotionClass, ClassID_TeleportLocomotion);
	REGISTER_CLASS(MechLocomotionClass, ClassID_MechLocomotion);
	REGISTER_CLASS(LevitateLocomotionClass, ClassID_LevitateLocomotion);
	REGISTER_CLASS(BulletClass, ClassID_BulletClass);
	REGISTER_CLASS(FactoryClass, ClassID_FactoryClass);
	REGISTER_CLASS(WarheadTypeClass, ClassID_WarheadTypeClass);
	REGISTER_CLASS(WeaponTypeClass, ClassID_WeaponTypeClass);
	REGISTER_CLASS(ParticleClass, ClassID_ParticleClass);
	REGISTER_CLASS(ParticleTypeClass, ClassID_ParticleTypeClass);
	REGISTER_CLASS(ParticleSystemClass, ClassID_ParticleSystemClass);
	REGISTER_CLASS(ParticleSystemTypeClass, ClassID_ParticleSystemTypeClass);
	REGISTER_CLASS(AITriggerTypeClass, ClassID_AITriggerTypeClass);
	REGISTER_CLASS(NeuronClass, ClassID_NeuronClass);
	REGISTER_CLASS(FoggedObjectClass, ClassID_FoggedObjectClass);
	REGISTER_CLASS(AlphaShapeClass, ClassID_AlphaShapeClass);
}

/***********************************************************************************************
 * main -- Initial startup routine (preps library systems).                                    *
 *                                                                                             *
 *    This is the routine that is first called when the program starts up. It basically        *
 *    handles the command line parsing and setting up library systems.                         *
 *                                                                                             *
 * INPUT:   argc  -- Number of command line arguments.                                         *
 *                                                                                             *
 *          argv  -- Pointer to array of command line argument strings.                        *
 *                                                                                             *
 * OUTPUT:  Returns with execution failure code (if any).                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/20/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
int main(int argc, char * argv[])
{
	char	buffer[512];

	// The block store may only be reached from beneath this call; httpsource.h says why. On a
	// page this returns while the engine is still inside it: under the yield scaffold the return
	// is a promise the page holds.

	// A host without a canvas, such as Node, still runs startup far enough to be diagnosed.
#if defined(__EMSCRIPTEN__)
	Browser_Init();
#endif

	Debug_Init(argc, argv);

	Raise_Timer_Resolution();

	// Handed over now because the exception path may not ask the logger for anything: the
	// thread that crashed may be the one holding the logger's lock.
	Exception_Register_Log_File(Debug_Log_File_Name());

	// LZO asks for this before any codec call. It validates rather than initializes, and a
	// vendored static build cannot fail it, but thirdparty/lzo is upgraded in place and a
	// later release may expect the call.
	int const lzo_status = lzo_init();
	if (lzo_status != LZO_E_OK) {
		DebugString("lzo_init failed with %d.\n", lzo_status);
		Host_Message_Box("OpenTS", "The compression library failed its startup check. This build is faulty.", HOST_BOX_OK | HOST_BOX_ERROR);
		return(EXIT_FAILURE);
	}

	if (!Acquire_Single_Instance()) {
		return(EXIT_SUCCESS);
	}

	atexit(Prog_End);

	RegisterClasses();

	/*
	**	Change directory to the where the executable is located.
	*/
	std::error_code directory_error;
	std::filesystem::current_path(std::filesystem::path(Executable_Directory()), directory_error);

	int error_code = EXIT_FAILURE;

	if (Parse_Command_Line(argc, argv) && Apply_Game_Directories()) {

		Exception_Run_Immediate_Test();

		/*
		 * Before anything is read, so that every file the game goes on to open is looked
		 * for where this deployment actually keeps it: the directories are applied, then
		 * the deployment's own file is read, then the folders it names are installed.
		 */
		DeploymentConfig.Read_File(Data_Directory().c_str());
		Init_Search_Folders(DeploymentConfig.SearchPaths.c_str());

		// The shipped UI documents, styles, and fonts. Registered after the deployment's own
		// folders so that one of them can override a shipped file.
		Init_Search_Folders("ui");
		Init_Executable_Folder("ui");

		// The recording's name was settled during static initialization, before there was
		// anywhere for a player's files to go. Naming it again settles it where it belongs.
		Session.RecordFile.Set_Name("RECORD.BIN");

		CDFileClass *cfile = new CDFileClass(DeploymentConfig.SettingsFile.c_str());

		ConfigINI.Load(*cfile, false);
		Options.ScreenWidth = ConfigINI.Get_Int("Video", "ScreenWidth", Options.ScreenWidth);
		Options.ScreenHeight = ConfigINI.Get_Int("Video", "ScreenHeight", Options.ScreenHeight);

		/*
		 * These are wanted before the window, the renderer and the drawing
		 * surfaces exist, well before the rest of the settings are read.
		 */
		Options.Fullscreen = ConfigINI.Get_Bool("Video", "Fullscreen", Options.Fullscreen);
		Options.WindowWidth = ConfigINI.Get_Int("Video", "WindowWidth", Options.WindowWidth);
		Options.WindowHeight = ConfigINI.Get_Int("Video", "WindowHeight", Options.WindowHeight);
		Options.VSync = ConfigINI.Get_Bool("Video", "VSync", Options.VSync);
		Options.Renderer = ConfigINI.Get_Int("Video", "Renderer", Options.Renderer);
		Options.UIScale = ConfigINI.Get_Int("Video", "UIScale", Options.UIScale);

		/*
		 * The command line asks for a window regardless of what the settings say.
		 */
		if (!WindowedMode) {
			WindowedMode = !Options.Fullscreen;
		}

		Keyboard = new KeyboardClass();

		/*
		**	If there is not enough disk space free, don't allow the product to run.
		*/
		if (Disk_Space_Available() < INIT_FREE_DISK_SPACE) {
			snprintf(buffer, sizeof(buffer), Fetch_String(TXT_CRITICALLY_LOW), (INIT_FREE_DISK_SPACE) / (1024 * 1024));
			if (Host_Message_Box(Fetch_String(TXT_SHORT_TITLE), buffer, HOST_BOX_QUESTION | HOST_BOX_YES_NO) == HOST_ANSWER_NO) {
				return(EXIT_FAILURE);
			}
		}

		if (Options.ScreenWidth == -1 || Options.ScreenHeight == -1) {
			Options.ScreenWidth = 640;
			Options.ScreenHeight = 480;
		}

		VisibleRect = Rect(0, 0, Options.ScreenWidth, Options.ScreenHeight);
		VideoModeWidth = Options.ScreenWidth;
		VideoModeHeight = Options.ScreenHeight;

		Host_Create_Window(Options.ScreenWidth, Options.ScreenHeight);

		Exception_Run_Post_Window_Test();

		AudioEngine.Init();

		int drawablewidth = 0;
		int drawableheight = 0;
		int refreshrate = Host_Window_Refresh_Rate();
		NativeWindow nativewindow = Host_Native_Window();
		if (!Host_Window_Drawable_Size(drawablewidth, drawableheight)
			|| !Video_Init(nativewindow, drawablewidth, drawableheight, refreshrate)) {
			Host_Message_Box(Fetch_String(TXT_SHORT_TITLE), Fetch_String(TXT_VIDEO_ERROR), HOST_BOX_OK | HOST_BOX_WARNING);
#if defined(__EMSCRIPTEN__)
			OpenTS_Host_Quit();
#endif
			exit(EXIT_FAILURE);
		}

		VisibleSurface = DSurface::Create_Primary();
		if (VisibleSurface == NULL) {
			Host_Message_Box(Fetch_String(TXT_SHORT_TITLE), Fetch_String(TXT_VIDEO_ERROR), HOST_BOX_OK | HOST_BOX_WARNING);
#if defined(__EMSCRIPTEN__)
			OpenTS_Host_Quit();
#endif
			exit(EXIT_FAILURE);
		}

		// Waiting for focus would park startup for as long as the tab is in the
		// background.
#if defined(_WIN32)
		do {
			Windows_Message_Handler();
		}
		while (!GameInFocus);
#endif

		VisibleSurface->Fill(0);

		// The shell needs the frame's destination, which Video_Init settled, and the game
		// runs without it if it cannot start: a screen that has no RmlUi view is unaffected.
		if (!UI_Init()) {
			DebugString("UI: the shell is unavailable; only the legacy screens will open\n");
		}

		ScreenLayout const layout = Compute_Screen_Layout(VisibleRect);

		Allocate_Surfaces(layout.Hidden, layout.Composite, layout.Tile, layout.Sidebar, false);
		LogicalSurface = HiddenSurface;
		Update_Visible_Surface(HiddenSurface);

		DepthBuffer = new ZBuffer(Rect(TacticalRect.X, TacticalRect.Y, 480, 480 - TacticalRect.Y));
		DepthBuffer->Set_Scroll(ZBUFFER_MAX);

		AlphaBuffer = new ABuffer(Rect(TacticalRect.X, TacticalRect.Y, 480, 480 - TacticalRect.Y));

#if defined(__EMSCRIPTEN__)
		MouseCursor = Browser_Create_Mouse();
#else
		MouseCursor = new WWMouseClass();
#endif
		MouseCursor->Capture_Mouse();

		/*
		**	Check for forced intro movie run disabling. If the conquer
		**	configuration file says "no", then don't run the intro.
		*/
		if (!Special.IsFromInstall && !Spawner_Is_Requested()) {
			// A page installs nothing, so the install sequence plays only when
			// asked for by name.
#if defined(__EMSCRIPTEN__)
			bool const wanted = false;
#else
			bool const wanted = true;
#endif
			Special.IsFromInstall = ConfigINI.Get_Bool("Intro", "PlayIntro", wanted);
		}

		/*
		**	Regardless of whether we should run it or not, here we're
		**	gonna change it to say "no" in the future.
		*/
		if (Special.IsFromInstall == true && !Spawner_Is_Requested()) {
			ConfigINI.Put_Bool("Intro", "PlayIntro", false);

			// Left closed, so that saving opens it for writing itself.
			cfile->Close();
			ConfigINI.Save(*cfile, false);
		}

		cfile->Close();
		delete cfile;

		DebugString("Main_Game\n");

		Main_Game(argc, argv);

		HiddenSurface->Fill(0);
		Update_Visible_Surface(HiddenSurface);

		/*
		**	Flag that this is a clean shutdown (not killed with Ctrl-Alt-Del)
		*/
		ReadyToQuit = 1;

		AudioEngine.End();

		Host_Close_Window();

		error_code = EXIT_SUCCESS;

	} else {

		/*
		 * A startup this early has no window of its own, and may have been given no console
		 * either, so a directory the game cannot use is reported where it will be seen.
		 */
		if (*Game_Directory_Error() != '\0') {
			Host_Message_Box(Fetch_String(TXT_SHORT_TITLE), Game_Directory_Error(), HOST_BOX_OK | HOST_BOX_WARNING);
		}

		// The help and the invalid option message are of no use if the console closes with
		// the process a moment later.
		Debug_Console_Hold();
	}

#if defined(__EMSCRIPTEN__)
	OpenTS_Host_Quit();
#endif

	return(error_code);
}


#if defined(_WIN32)

/// <summary>
/// The Windows entry point. Builds the argument list main receives from the command line the
/// shell handed over, following the shell's own quoting, so a directory whose name holds spaces
/// arrives as the single argument it was written as. The first argument is the executable's
/// full path, whatever the shell was given.
/// </summary>
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, char *, int command_show)
{
	// First, so that everything after it is covered, including the rest of startup.
	Install_Exception_Handler();

	ProgramInstance = instance;
	ShowCommand = command_show;

	// The list lasts as long as the process, as a C runtime's argv does.
	static std::vector<std::string> arguments;
	static std::vector<char *> pointers;

	arguments.push_back(Executable_Path());

	int wide_count = 0;
	LPWSTR * const wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_count);

	if (wide_argv != nullptr) {
		// Index zero names the executable, which is already in place. The arguments name paths
		// the narrow file API opens, so they take its code page, which the manifest makes UTF-8.
		for (int index = 1; index < wide_count; index++) {
			int const length = WideCharToMultiByte(CP_ACP, 0, wide_argv[index], -1, nullptr, 0, nullptr, nullptr);
			if (length <= 1) continue;

			std::string argument(length - 1, '\0');
			WideCharToMultiByte(CP_ACP, 0, wide_argv[index], -1, argument.data(), length, nullptr, nullptr);
			arguments.push_back(argument);
		}

		LocalFree(wide_argv);
	}

	for (std::string & argument : arguments) {
		pointers.push_back(argument.data());
	}
	pointers.push_back(nullptr);

	return(main(int(arguments.size()), pointers.data()));
}

#endif	// _WIN32

/***********************************************************************************************
 * Prog_End -- Cleans up library systems in prep for game exit.                                *
 *                                                                                             *
 *    This routine should be called before the game terminates. It handles cleaning up         *
 *    library systems so that a graceful return to the host operating system is achieved.      *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/20/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void __cdecl Prog_End(void)
{
	int i;

	Restore_Timer_Resolution();

	GameActive = false;

#if defined(__EMSCRIPTEN__)
	OpenTS_Host_Quit();
#endif

	Session.Free_Scenario_Descriptions();

	while (ColorSchemes.Count()) {
		delete ColorSchemes[0];
	}
	ColorSchemes.Clear();

	// The theaters outlive every scenario, so they are not released with its objects.
	TheaterClass::Clear();

	Delete_All_Objects();

	while (Movies.Count()) {
		free((void *)Movies[0]);
		Movies.Delete_Index(0);
	}
	Movies.Clear();

	for (i = 0; i < AllCommands.Count(); i++) {
		delete AllCommands[i];
	}
	AllCommands.Clear();

	Theme.Free_Themes();

	if (RuleINI != NULL) {
		delete RuleINI;
		RuleINI = NULL;
	}
	AIINI.Clear();
	ArtINI.Clear();
	FSRuleINI.Clear();
	FSAIINI.Clear();
	EditorINI.Clear();
	ConfigINI.Clear();
	ConfigINI.Clear();

	SpotLightClass::Clear_All();
	IonBlastClass::Clear_All();

	if (MouseCursor) {
		delete MouseCursor;
		MouseCursor = NULL;
	}

	Map.Free_Cells();
	Map.Clear_Radar();

	Reset_Surfaces();

	if (DepthBuffer != NULL) {
		delete DepthBuffer;
		DepthBuffer = NULL;
	}
	if (AlphaBuffer != NULL) {
		delete AlphaBuffer;
		AlphaBuffer = NULL;
	}

	if (Metal12FontPtr != NULL) {
		delete Metal12FontPtr;
		Metal12FontPtr = NULL;
	}
	if (MapFontPtr != NULL) {
		delete MapFontPtr;
		MapFontPtr = NULL;
	}
	if (Font6Ptr != NULL) {
		delete Font6Ptr;
		Font6Ptr = NULL;
	}
	if (EditorFont != NULL) {
		delete EditorFont;
		EditorFont = NULL;
	}
	if (Font8Ptr != NULL) {
		delete Font8Ptr;
		Font8Ptr = NULL;
	}
	if (GradFont6Ptr != NULL) {
		delete GradFont6Ptr;
		GradFont6Ptr = NULL;
	}

	if (TerrainDrawer != NULL) {
		delete TerrainDrawer;
		TerrainDrawer = NULL;
	}
	if (AnimDrawer != NULL) {
		delete AnimDrawer;
		AnimDrawer = NULL;
	}
	if (NormalDrawer != NULL) {
		delete NormalDrawer;
		NormalDrawer = NULL;
	}
	if (VoxelDrawer != NULL) {
		delete VoxelDrawer;
		VoxelDrawer = NULL;
	}
	if (MouseDrawer != NULL) {
		delete MouseDrawer;
		MouseDrawer = NULL;
	}
	if (SidebarDrawer != NULL) {
		delete SidebarDrawer;
		SidebarDrawer = NULL;
	}
	if (CameoDrawer != NULL) {
		delete CameoDrawer;
		CameoDrawer = NULL;
	}
	if (EightBitDrawer != NULL) {
		delete EightBitDrawer;
		EightBitDrawer = NULL;
	}
	if (EightBitSurface != NULL) {
		delete EightBitSurface;
		EightBitSurface = NULL;
	}

	if (CloakingSurface != NULL) {
		delete (Surface *)CloakingSurface;
		CloakingSurface = NULL;
	}

	if (BuildingTypeClass::BuildingZShape != NULL) {
		delete (void *)BuildingTypeClass::BuildingZShape;
		BuildingTypeClass::BuildingZShape = NULL;
	}

	Map.Shutdown();

	for (i = 0; i < TileDrawers.Count(); i++) {
		delete TileDrawers[i];
	}
	TileDrawers.Clear();

	if (TheaterData) {
		delete TheaterData;
		TheaterData = NULL;
	}
	if (TheaterDat) {
		delete TheaterDat;
		TheaterDat = NULL;
	}
	if (IsometricTheaterData) {
		delete IsometricTheaterData;
		IsometricTheaterData = NULL;
	}

	if (IsometricTileTypeClass::CellShadowShapes != NULL) {
		delete IsometricTileTypeClass::CellShadowShapes;
		IsometricTileTypeClass::CellShadowShapes = NULL;
	}

	if (SlopeZShapes[0] != NULL) {
		delete (ShapeSet *)SlopeZShapes[0];
		SlopeZShapes[0] = NULL;
	}
	if (SlopeZShapes[1] != NULL) {
		delete (ShapeSet *)SlopeZShapes[1];
		SlopeZShapes[1] = NULL;
	}
	if (SlopeZShapes[2] != NULL) {
		delete (ShapeSet *)SlopeZShapes[2];
		SlopeZShapes[2] = NULL;
	}
	if (SlopeZShapes[3] != NULL) {
		delete (ShapeSet *)SlopeZShapes[3];
		SlopeZShapes[3] = NULL;
	}

	if (PreviewSurface != NULL) {
		delete PreviewSurface;
		PreviewSurface = NULL;
	}

	if (MoviesMix != NULL) {
		delete MoviesMix;
		MoviesMix = NULL;
	}
	while (MoviesMixLocal.Count() > 0) {
		delete MoviesMixLocal[0];
		MoviesMixLocal.Delete_Index(0);
	}
	if (ScoresMix != NULL) {
		delete ScoresMix;
		ScoresMix = NULL;
	}
	if (Scores01Mix != NULL) {
		delete Scores01Mix;
		Scores01Mix = NULL;
	}
	if (MainMix != NULL) {
		delete MainMix;
		MainMix = NULL;
	}
	if (ConquerMix != NULL) {
		delete ConquerMix;
		ConquerMix = NULL;
	}

	while (ExpandSideMix.Count() > 0) {
		delete ExpandSideMix[0];
		ExpandSideMix.Delete_Index(0);
	}
	while (ExpandSpeechMix.Count() > 0) {
		delete ExpandSpeechMix[0];
		ExpandSpeechMix.Delete_Index(0);
	}
	while (ExpandMix.Count() > 0) {
		delete ExpandMix[0];
		ExpandMix.Delete_Index(0);
	}
	if (CacheMix != NULL) {
		delete CacheMix;
		CacheMix = NULL;
	}
	if (LocalMix != NULL) {
		delete LocalMix;
		LocalMix = NULL;
	}
	if (SpeechMix != NULL) {
		delete SpeechMix;
		SpeechMix = NULL;
	}
	if (SoundsMix != NULL) {
		delete SoundsMix;
		SoundsMix = NULL;
	}
	if (Sounds01Mix != NULL) {
		delete Sounds01Mix;
		Sounds01Mix = NULL;
	}
	if (MapsMix != NULL) {
		delete MapsMix;
		MapsMix = NULL;
	}
	while (MapsMixLocal.Count() > 0) {
		delete MapsMixLocal[0];
		MapsMixLocal.Delete_Index(0);
	}
	if (MultiMix != NULL) {
		delete MultiMix;
		MultiMix = NULL;
	}
	if (SideCMix != NULL) {
		delete SideCMix;
		SideCMix = NULL;
	}
	if (SideNCMix != NULL) {
		delete SideNCMix;
		SideNCMix = NULL;
	}
	if (SideCDMix != NULL) {
		delete SideCDMix;
		SideCDMix = NULL;
	}
	if (GameMix != NULL) {
		delete GameMix;
		GameMix = NULL;
	}

	if (TacticalMap != NULL) {
		delete TacticalMap;
		TacticalMap = NULL;
	}

	Free_Vocs();

	CDFileClass::Clear_Search_Drives();

	if (Keyboard != NULL) {
		delete Keyboard;
		Keyboard = NULL;
	}

	Shutdown_Network();

	if (UnkBuffer != NULL) {
		delete UnkBuffer;
		UnkBuffer = NULL;
	}

	if (Rule != NULL) {
		delete Rule;
		Rule = NULL;
	}

	if (Scen != NULL) {
		delete Scen;
		Scen = NULL;
	}

	Unregister_Classes();

	Release_Single_Instance();
}

/***********************************************************************************************
 * Emergency_Exit -- Function to call when we want to exit unexpectedly.                       *
 *                   Use this function instead of exit(n) so everything is properly cleaned up.*
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Code to return to the OS                                                          *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    3/13/97 1:32AM ST : Created                                                              *
 *=============================================================================================*/
void Emergency_Exit(void)
{
	AudioEngine.End();

	ReadyToQuit = 1;

	Host_Close_Window();


	if (MouseCursor) {
		MouseCursor->Release_Mouse();
		delete MouseCursor;
	}
	MouseCursor = NULL;

#if defined(_WIN32)
	PostQuitMessage(EXIT_SUCCESS);
#endif

	Shutdown_Network();

	Prog_End();
}
