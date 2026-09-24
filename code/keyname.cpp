/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "keyname.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif


#if defined(_WIN32)

// Windows names the key the way the player's own keyboard layout does.
static void Append_Key_Name(unsigned short key, bool extended, char * buffer)
{
	char name[32];

	// Bit 25 asks for the name without telling left from right.
	LONG param = (LONG)(MapVirtualKey(key, 0) << 16) | (1 << 0) | (1 << 25);
	if (extended) {
		param |= (1 << 24);
	}

	if (GetKeyNameText(param, name, sizeof(name)) > 0) {
		strcat(buffer, name);
	}
}

#else

struct KeyNameEntry
{
	unsigned short Key;
	unsigned char Scan;
	bool Extended;
	char const * Name;
};


// A US layout's names, looked up as Windows does: the key gives a scan code and the scan code
// gives the name, so Home without the extended flag reads as the keypad's "Num 7".
static KeyNameEntry const _KeyNames[] = {
	{ VK_ESCAPE,	0x01, false, "Esc" },
	{ VK_1,			0x02, false, "1" },
	{ VK_2,			0x03, false, "2" },
	{ VK_3,			0x04, false, "3" },
	{ VK_4,			0x05, false, "4" },
	{ VK_5,			0x06, false, "5" },
	{ VK_6,			0x07, false, "6" },
	{ VK_7,			0x08, false, "7" },
	{ VK_8,			0x09, false, "8" },
	{ VK_9,			0x0A, false, "9" },
	{ VK_0,			0x0B, false, "0" },
	{ VK_NONE_BD,	0x0C, false, "-" },
	{ VK_NONE_BB,	0x0D, false, "=" },
	{ VK_BACK,		0x0E, false, "Backspace" },
	{ VK_TAB,		0x0F, false, "Tab" },
	{ VK_Q,			0x10, false, "Q" },
	{ VK_W,			0x11, false, "W" },
	{ VK_E,			0x12, false, "E" },
	{ VK_R,			0x13, false, "R" },
	{ VK_T,			0x14, false, "T" },
	{ VK_Y,			0x15, false, "Y" },
	{ VK_U,			0x16, false, "U" },
	{ VK_I,			0x17, false, "I" },
	{ VK_O,			0x18, false, "O" },
	{ VK_P,			0x19, false, "P" },
	{ VK_NONE_DB,	0x1A, false, "[" },
	{ VK_NONE_DD,	0x1B, false, "]" },
	{ VK_RETURN,	0x1C, false, "Enter" },
	{ VK_CONTROL,	0x1D, false, "Ctrl" },
	{ VK_A,			0x1E, false, "A" },
	{ VK_S,			0x1F, false, "S" },
	{ VK_D,			0x20, false, "D" },
	{ VK_F,			0x21, false, "F" },
	{ VK_G,			0x22, false, "G" },
	{ VK_H,			0x23, false, "H" },
	{ VK_J,			0x24, false, "J" },
	{ VK_K,			0x25, false, "K" },
	{ VK_L,			0x26, false, "L" },
	{ VK_NONE_BA,	0x27, false, ";" },
	{ VK_NONE_DE,	0x28, false, "'" },
	{ VK_NONE_C0,	0x29, false, "`" },
	{ VK_SHIFT,		0x2A, false, "Shift" },
	{ VK_NONE_DC,	0x2B, false, "\\" },
	{ VK_Z,			0x2C, false, "Z" },
	{ VK_X,			0x2D, false, "X" },
	{ VK_C,			0x2E, false, "C" },
	{ VK_V,			0x2F, false, "V" },
	{ VK_B,			0x30, false, "B" },
	{ VK_N,			0x31, false, "N" },
	{ VK_M,			0x32, false, "M" },
	{ VK_NONE_BC,	0x33, false, "," },
	{ VK_NONE_BE,	0x34, false, "." },
	{ VK_NONE_BF,	0x35, false, "/" },
	{ VK_MULTIPLY,	0x37, false, "Num *" },
	{ VK_MENU,		0x38, false, "Alt" },
	{ VK_SPACE,		0x39, false, "Space" },
	{ VK_CAPITAL,	0x3A, false, "Caps Lock" },
	{ VK_F1,		0x3B, false, "F1" },
	{ VK_F2,		0x3C, false, "F2" },
	{ VK_F3,		0x3D, false, "F3" },
	{ VK_F4,		0x3E, false, "F4" },
	{ VK_F5,		0x3F, false, "F5" },
	{ VK_F6,		0x40, false, "F6" },
	{ VK_F7,		0x41, false, "F7" },
	{ VK_F8,		0x42, false, "F8" },
	{ VK_F9,		0x43, false, "F9" },
	{ VK_F10,		0x44, false, "F10" },
	{ VK_NUMLOCK,	0x45, false, "Num Lock" },
	{ VK_SCROLL,	0x46, false, "Scroll Lock" },
	{ VK_NUMPAD7,	0x47, false, "Num 7" },
	{ VK_NUMPAD8,	0x48, false, "Num 8" },
	{ VK_NUMPAD9,	0x49, false, "Num 9" },
	{ VK_SUBTRACT,	0x4A, false, "Num -" },
	{ VK_NUMPAD4,	0x4B, false, "Num 4" },
	{ VK_NUMPAD5,	0x4C, false, "Num 5" },
	{ VK_NUMPAD6,	0x4D, false, "Num 6" },
	{ VK_ADD,		0x4E, false, "Num +" },
	{ VK_NUMPAD1,	0x4F, false, "Num 1" },
	{ VK_NUMPAD2,	0x50, false, "Num 2" },
	{ VK_NUMPAD3,	0x51, false, "Num 3" },
	{ VK_NUMPAD0,	0x52, false, "Num 0" },
	{ VK_DECIMAL,	0x53, false, "Num Del" },
	{ VK_F11,		0x57, false, "F11" },
	{ VK_F12,		0x58, false, "F12" },
	{ VK_DIVIDE,	0x35, true,  "Num /" },
	{ VK_SNAPSHOT,	0x37, true,  "Print Screen" },
	{ VK_PAUSE,		0x45, true,  "Pause" },
	{ VK_HOME,		0x47, true,  "Home" },
	{ VK_UP,		0x48, true,  "Up" },
	{ VK_PRIOR,		0x49, true,  "Page Up" },
	{ VK_LEFT,		0x4B, true,  "Left" },
	{ VK_RIGHT,		0x4D, true,  "Right" },
	{ VK_END,		0x4F, true,  "End" },
	{ VK_DOWN,		0x50, true,  "Down" },
	{ VK_NEXT,		0x51, true,  "Page Down" },
	{ VK_INSERT,	0x52, true,  "Insert" },
	{ VK_DELETE,	0x53, true,  "Delete" },
	{ VK_NONE_5B,	0x5B, true,  "Left Windows" },
	{ VK_NONE_5C,	0x5C, true,  "Right Windows" },
	{ VK_NONE_5D,	0x5D, true,  "Application" },
};


static void Append_Key_Name(unsigned short key, bool extended, char * buffer)
{
	unsigned char scan = 0;
	for (KeyNameEntry const & entry : _KeyNames) {
		if (entry.Key == key) {
			scan = entry.Scan;
			break;
		}
	}

	for (KeyNameEntry const & entry : _KeyNames) {
		if (entry.Scan == scan && entry.Extended == extended) {
			strcat(buffer, entry.Name);
			return;
		}
	}
}

#endif


void Build_Hotkey_String(KeyNumType key, char * buffer)
{
	buffer[0] = '\0';

	if ((key & WWKEY_ALT_BIT) != 0) {
		Append_Key_Name(VK_MENU, false, buffer);
		strcat(buffer, "+");
	}

	if ((key & WWKEY_CTRL_BIT) != 0) {
		Append_Key_Name(VK_CONTROL, false, buffer);
		strcat(buffer, "+");
	}

	if ((key & WWKEY_SHIFT_BIT) != 0) {
		Append_Key_Name(VK_SHIFT, false, buffer);
		strcat(buffer, "+");
	}

	// A key with the release bit is named as an extended key.
	Append_Key_Name((unsigned short)(key & 0xFF), (key & WWKEY_RLS_BIT) != 0, buffer);
}
