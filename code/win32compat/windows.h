/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The umbrella header, with the include set <windows.h> has under WIN32_LEAN_AND_MEAN and without it.

#pragma once

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "wincon.h"
#include "winver.h"
#include "winreg.h"

#ifndef WIN32_LEAN_AND_MEAN
#include "mmsystem.h"
#include "winsock.h"
#include "shellapi.h"
#endif
