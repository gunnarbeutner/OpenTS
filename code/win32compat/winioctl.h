/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The device control codes of <winioctl.h>; the engine's only device is the debug monitor card.

#pragma once

#include "windef.h"

#define METHOD_BUFFERED		0
#define METHOD_IN_DIRECT	1
#define METHOD_OUT_DIRECT	2
#define METHOD_NEITHER		3
#define FILE_DEVICE_UNKNOWN	0x00000022
#define CTL_CODE(devicetype, function, method, access) \
	(((devicetype) << 16) | ((access) << 14) | ((function) << 2) | (method))
