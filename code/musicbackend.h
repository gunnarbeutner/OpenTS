/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#if defined(__EMSCRIPTEN__)

// A theme is named by its legacy filename, such as "IONSTORM.AUD"; only the
// base name selects the manifest's AAC copy.
bool Music_Browser_Available(char const * theme_filename);

// Returns a handle for the other Music_Browser_* calls, or -1 when the
// manifest carries no browser copy. Volume is linear, 0 to 255.
int Music_Browser_Play(char const * theme_filename, int volume);

void Music_Browser_Stop(int handle);

void Music_Browser_Fade(int handle, int milliseconds);

// False once the track has ended, faded out, or failed.
bool Music_Browser_Still_Playing(int handle);

void Music_Browser_Set_Volume(int handle, int volume);

// Silences and restores whatever is playing, for the window losing and regaining focus.
// A track autoplay is still holding, or one that ended meanwhile, is left alone.
void Music_Browser_Pause(void);
void Music_Browser_Resume(void);

#endif
