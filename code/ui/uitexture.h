/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The game's own artwork, as textures a document can name. This is what lets an RmlUi
// screen wear the dialogs' skin rather than a look of its own.

#pragma once

#include <vector>


// Decodes a source a document named, into premultiplied RGBA8. Two forms are understood:
//
//   name.pcx                 a paletted picture from the game's file system
//   key:name.pcx             the same with its magenta key colour transparent
//   frame:WIDTHxHEIGHT       the dialog backdrop composed at that size, for a centred dialog
//   frame:WxH@X,Y            the same, for a dialog placed at X,Y on the backdrop artwork
//   button:u:WIDTHxHEIGHT    a button skin composed at that size, raised
//   button:d:WIDTHxHEIGHT    the same, pressed
//   surface:NAME             a surface registered with UI_Surface_Register
//   NAME.shp#FRAME@NAME.pal  one frame of a shape, through the named palette, index zero
//                            transparent
//
// False means the source is not one of these, which leaves the general image decoder to
// try it, or that the artwork could not be read.
bool UI_Texture_Load(char const * source, std::vector<unsigned char> & rgba, int & width, int & height);

// Drops the cached artwork. Follows the shell's shutdown, so a later run reloads it.
void UI_Texture_Shutdown(void);


class Surface;

// Hands back the surface a screen shows, or null when there is none to show. Called each
// time the texture is loaded, so it reads the surface as it stands then.
typedef Surface * (*UISurfaceSource)(void);

// Makes a surface the engine draws at runtime available to a document as
// `surface:NAME`, which is how a map preview reaches a screen. A later registration under
// the same name replaces an earlier one.
void UI_Surface_Register(char const * name, UISurfaceSource source);

// Says the surface has been drawn again. The texture is released, so the next frame that
// shows it reads the surface afresh; nothing is copied until then.
void UI_Surface_Invalidate(char const * name);
