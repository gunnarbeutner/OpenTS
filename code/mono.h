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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/mono.h                                 $*
 *                                                                                             *
 *                      $Author:: Jani_p                                                      $*
 *                                                                                             *
 *                     $Modtime:: 5/04/01 7:36p                                               $*
 *                                                                                             *
 *                    $Revision:: 5                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

// The monochrome adapter this drew on, and the Windows NT driver that reached it, no longer
// exist, so every drawing call does nothing. The enabled flag is kept because callers decide
// what else to do by it.
class MonoClass {
	public:
		enum MonoClassPageEnums {
			COLUMNS=80,						// Number of columns.
			LINES=25,						// Number of lines.
			MAX_MONO_PAGES=8				// Maximum RAM pages on mono card.
		};

		enum MonoAttribute {
			INVISIBLE=0x00, // Black on black.
			UNDERLINE=0x01, // Underline.
			BLINKING=0x90,  // Blinking white on black.
			NORMAL=0x02,    // White on black.
			INVERSE=0x70    // Black on white.
		};

		MonoClass(void) = default;

		static void Enable(void) {Enabled = true;};
		static void Disable(void) {Enabled = false;};
		static bool Is_Enabled(void) {return(Enabled);};

		void Sub_Window(int = 0, int = 0, int = 80, int = 25) {}
		void Fill_Attrib(int, int, int, int, MonoAttribute) {}
		void Clear(void) {}
		void Set_Cursor(int, int) {}
		void Print(char const *) {}
		void Print(int) {}
		void Printf(char const *, ...) {}
		void Printf(int, ...) {}
		void Text_Print(char const *, int, int, MonoAttribute = NORMAL) {}
		void Text_Print(int, int, int, MonoAttribute = NORMAL) {}
		void View(void) {}
		void Scroll(int = 1) {}
		void Pan(int = 1) {}
		void Set_Default_Attribute(MonoAttribute) {}

		/*
		**	This merely makes a duplicate of the mono object into a newly created mono
		**	object.
		*/
		MonoClass (MonoClass const &);

	private:

		/*
		**	If this is true, then monochrome output is allowed. It defaults to false
		**	so that monochrome output must be explicitly enabled.
		*/
		inline static bool Enabled = false;

		MonoClass & operator = (MonoClass const & );
};
