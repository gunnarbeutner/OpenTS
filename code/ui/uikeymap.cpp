/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "uikeymap.h"

#include "keyboard.h"


UIKeyType UI_Key_From_Virtual(unsigned int key)
{
	switch (key) {
		case VK_TAB:		return(UI_KEY_TAB);
		case VK_RETURN:		return(UI_KEY_RETURN);
		case VK_ESCAPE:		return(UI_KEY_ESCAPE);
		case VK_SPACE:		return(UI_KEY_SPACE);
		case VK_BACK:		return(UI_KEY_BACKSPACE);
		case VK_DELETE:		return(UI_KEY_DELETE);
		case VK_INSERT:		return(UI_KEY_INSERT);
		case VK_HOME:		return(UI_KEY_HOME);
		case VK_END:		return(UI_KEY_END);
		case VK_PRIOR:		return(UI_KEY_PAGE_UP);
		case VK_NEXT:		return(UI_KEY_PAGE_DOWN);
		case VK_LEFT:		return(UI_KEY_LEFT);
		case VK_RIGHT:		return(UI_KEY_RIGHT);
		case VK_UP:			return(UI_KEY_UP);
		case VK_DOWN:		return(UI_KEY_DOWN);
		default:
			break;
	}

	if (key >= VK_F1 && key <= VK_F12) {
		return((UIKeyType)(UI_KEY_F1 + (int)(key - VK_F1)));
	}

	return(UI_KEY_UNKNOWN);
}
