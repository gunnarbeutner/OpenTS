/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The multimedia timer and wave format of <mmsystem.h>.

#pragma once

#include "windef.h"

// WAVEFORMATEX is packed to eighteen bytes rather than twenty because it is
// written into audio buffers.
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
	WORD wFormatTag;
	WORD nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD nBlockAlign;
	WORD wBitsPerSample;
	WORD cbSize;
} WAVEFORMATEX, * LPWAVEFORMATEX;
#pragma pack(pop)
#define WAVE_FORMAT_PCM				1
DWORD timeGetTime(void);
inline DWORD timeBeginPeriod(UINT period) { (void)period; return(0); }
inline DWORD timeEndPeriod(UINT period) { (void)period; return(0); }
// The multimedia timer, from mmsystem.h.
typedef UINT MMRESULT;
typedef void (CALLBACK * LPTIMECALLBACK)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);

typedef struct timecaps_tag {
	UINT wPeriodMin;
	UINT wPeriodMax;
} TIMECAPS, * LPTIMECAPS;

#define TIMERR_NOERROR	0
#define TIMERR_NOCANDO	97
#define TIME_ONESHOT	0x0000
#define TIME_PERIODIC	0x0001
#define TIME_KILL_SYNCHRONOUS	0x0100

MMRESULT timeGetDevCaps(LPTIMECAPS caps, UINT size);
MMRESULT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags);
MMRESULT timeKillEvent(UINT id);
