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

/* $Header: /CounterStrike/WINSTUB.CPP 3     3/13/97 2:06p Steve_tall $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : WINSTUB.CPP                                                  *
 *                                                                                             *
 *                   Programmer : Steve Tall                                                   *
 *                                                                                             *
 *                   Start Date : 10/04/95                                                     *
 *                                                                                             *
 *                  Last Update : October 4th 1995 [ST]                                        *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Overview:                                                                                   *
 *   This file contains stubs for undefined externals when linked under Watcom for Win 95      *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 *                                                                                             *
 * Functions:                                                                                  *
 *   Assert_Failure -- display the line and source file where a failed assert occurred         *
 *   Check_For_Focus_Loss -- check for the end of the focus loss                               *
 *   Focus_Loss -- this function is called when a library function detects focus loss          *
 *   Memory_Error_Handler -- Handle a possibly fatal failure to allocate memory                *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"

#include "winstub.h"

#include "bsurface.h"
#include "ccfile.h"
#include "convert.h"
#include "draw.h"
#include "opents_version.h"
#include "pcx.h"
#include "screenlayout.h"
#include "surface.h"

#include "color.hh"


/// <summary>
/// Fetches the build number of this executable.
/// This routine is used by the network code to check that every machine joining a
/// game is running the same build.
/// </summary>
/// <returns>Returns with the packed project version this executable was built from.</returns>
unsigned int Build_Number(void)
{
	return(OPENTS_VERSION_PACKED);
}


/// <summary>
/// Loads a title screen picture and places it on the surface.
/// This routine is used by the startup and scenario loading sequences to put some
/// artwork on the screen while the game gets itself ready. A paletted picture is
/// drawn through a converter built from the palette supplied.
/// </summary>
/// <param name="name">The name of the picture file to load.</param>
/// <param name="surface">The surface to draw the title screen upon.</param>
/// <param name="palette">The palette to load the picture's colors into.</param>
/// <param name="fill">Magnify the picture to fill the surface?</param>
/// <returns>The size of the picture drawn, or an empty size if there was
/// none.</returns>
/// <remarks>Without fill the picture is centered at its own size, which the
/// score screens and the mission restatement place their artwork against. A
/// filled picture keeps its shape and leaves black beside it.</remarks>
Point2D Load_Title_Screen(char const * name, Surface * surface, PaletteClass * palette, bool fill)
{
	Surface *load_buffer;
	CCFileClass file(name);
	load_buffer = Read_PCX_File (file, palette);

	if (load_buffer == NULL) {
		return(Point2D(0, 0));
	}

	Point2D const size(load_buffer->Get_Width(), load_buffer->Get_Height());
	Rect const dest = fill
		? Fit_Centered(size, surface->Get_Rect())
		: Rect((surface->Get_Width() - size.X) / 2, (surface->Get_Height() - size.Y) / 2, size.X, size.Y);

	if (palette && load_buffer->Bytes_Per_Pixel() == 1) {
		ConvertClass *drawer = new ConvertClass(*palette, *palette, *surface);
		if (dest.Width == size.X && dest.Height == size.Y) {
			Blit_Block(*surface, *drawer, *load_buffer, load_buffer->Get_Rect(), dest.TopLeft, surface->Get_Rect());
		} else {
			/*
			 * Blit_Block converts the palette but does not magnify, so the
			 * conversion runs once at the picture's size and the surface blit
			 * magnifies the result.
			 */
			BSurface converted(size.X, size.Y, surface->Bytes_Per_Pixel());
			converted.Fill(TBLACK);
			Blit_Block(converted, *drawer, *load_buffer, load_buffer->Get_Rect(), Point2D(0, 0), converted.Get_Rect());
			surface->Blit_From(dest, converted, converted.Get_Rect());
		}
		delete drawer;
	} else {
		surface->Blit_From(surface->Get_Rect(), dest, *load_buffer, load_buffer->Get_Rect(), load_buffer->Get_Rect());
	}

	delete load_buffer;

	return(size);
}
