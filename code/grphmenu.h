/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "keyboard.h"
#include "msanim.h"
#include "msengine.h"
#include "rect.h"

#include <string>

class GraphicMenuItem;
class INIClass;
class MSSfxEntry;
template<class T> class DynamicVectorClass;

class GraphicMenuString
{
	public:
		GraphicMenuString(void)
		{
			Buffer = new char[10];
		}
		~GraphicMenuString(void) {}

		void Set(char const * string)
		{
			strcpy(Buffer, string);
		}


		void Replace(char const * string)
		{
			delete [] Buffer;
			int len = strlen(string) + 1;
			Buffer = new char[len + 1];
			strcpy(Buffer, string);
		}


		void Replace_With_Extension(char const * string, char const * ext, int extlen)
		{
			delete [] Buffer;
			int len = strlen(string) + extlen;
			Buffer = new char[len + 1];
			strcpy(Buffer, string);
			strcat(Buffer, ext);
		}

		void Set_With_Extension(char const *string, char const *ext)
		{
			strcpy(Buffer, string);
			strcat(Buffer, ext);
		}

		void Set_Extension(char const * ext)
		{
			strcat(Buffer, ext);
		}

		void Release(void)
		{
			delete [] Buffer;
		}

		void Reserve(int len)
		{
			Buffer = new char[len];
		}

		char const * Peek(void)
		{
			return(Buffer);
		}

	private:
		/*
		 * Pointer to the heap block that holds the string. It is not freed when the object
		 * is destroyed, so the owner must call the Release function before letting the
		 * string go.
		 */
		char * Buffer;
};

/*
 * Presentation returns this, not a choice, when the frame is moving to a new
 * size; the caller builds the page again against the new frame.
 */
int const GMENU_REDISPLAY = -2;

class GraphicMenu
{
	friend GraphicMenu * _Graphic_Menu(INIClass const & ini, const char * name);

	typedef DynamicVectorClass<GraphicMenuItem *> ITEM_LIST;

	public:
		GraphicMenu(void);
		virtual ~GraphicMenu(void);

		void Set_Animation(MSAnim * anim);
		void Set_Theme_Name(const char * name);
		void Set_Theme_Playing(bool playing) { ThemeIsPlaying = playing; }

		void Add_Item(GraphicMenuItem * item);

		void Set_Item_Enabled(int item_id, bool enabled);
		void Set_Item_Visible(int item_id, bool visible);
		int Presentation(void);
		GraphicMenuItem * Get_Item_Under_Mouse(Point2D const & mouse);
		GraphicMenuItem * Get_Item_For_Key(KeyNumType key);

		// Appends the items as a JSON array, with their active areas in frame
		// pixels and the names the caller's table gives their identifiers.
		void Describe(std::string & out, char const * (*name)(int)) const;

	public:
		// The INI section the menu was read from.
		std::string Name;

		/*
		 * This is the animation engine that drives the menu. It owns the backdrop and the
		 * items' animations, paces the display, and is handed to the chosen item so that
		 * the item can act upon the screen.
		 */
		MSEngine Engine;

		/*
		 * This is the name of the still picture that backs the menu, which is "Title.PCX"
		 * until the menu description supplies another. The shell menu takes a copy of it so
		 * that the same backdrop can stay up after this menu has gone.
		 */
		GraphicMenuString BackgroundName;

		/*
		 * This is the name of the music theme played while the menu is up. It starts when
		 * the menu takes control and is faded out again once the player picks something.
		 */
		GraphicMenuString ThemeName;

	private:
		/*
		 * Pointer to the animation playing behind the menu. The engine owns it -- this is
		 * kept only so that a fresh backdrop can take its place and so that the menu can
		 * let it play out before drawing over it.
		 */
		MSAnim * CurrentAnim;

		/*
		 * The backdrop size the items were placed against, and so the design
		 * space the page is magnified out of; empty until a backdrop supplies
		 * one, which leaves the page drawn at frame size.
		 */
		Point2D LayoutSize;

		/*
		 * A page put up again to follow the frame to a new size takes over the
		 * music from the copy it replaces rather than restarting the track.
		 */
		bool ThemeIsPlaying;

		/*
		 * These are the items the player may pick from. The menu owns them and destroys
		 * them along with itself.
		 */
		ITEM_LIST Items;
};


GraphicMenu * Do_Graphic_Menu(const char * ini, const char * name);

// The menu whose Presentation is waiting for a choice, or null.
GraphicMenu * Current_Graphic_Menu(void);
