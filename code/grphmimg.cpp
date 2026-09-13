/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "grphmimg.h"

#include "_surface.h"
#include "ccfile.h"
#include "dsurface.h"
#include "ini.h"
#include "msanim.h"
#include "mschoice.h"
#include "movies.h"
#include "surface.h"


/// <summary>
/// Creates an image based menu item from its INI description.
/// This routine is used while a graphic menu is being loaded. The position and active
/// area given in the INI are specified relative to the menu backdrop, so the backdrop
/// offset is added to both before the item is created.
/// </summary>
/// <param name="name">The INI section that describes the item.</param>
/// <param name="image_size">The backdrop offset that the item coordinates are relative to.</param>
/// <returns>Returns with a pointer to the item created. If the section names no item
/// identifier, then NULL is returned.</returns>
GraphicMenuItem * GM_Read_Image_Item(const char * name, INIClass const & ini, MSEngine & engine, MSAnim const * backdrop, Point2D & image_size, int scale, int design)
{
	int id = ini.Get_Int(name, "ID", -1);
	if (id == -1) {
		return(NULL);
	}

	// The description is written in the artwork's own pixels, so a page laid out
	// at another size carries its positions across with it.
	Point2D origin(0,0);
	origin = ini.Get_Point(name, "Origin", origin);
	origin = Point2D(origin.X * scale / design, origin.Y * scale / design) + image_size;

	Rect drawn_rect(0,0,0,0);
	drawn_rect = ini.Get_Rect(name, "ActiveRect", drawn_rect);

	Rect active_rect = Rect(drawn_rect.X * scale / design, drawn_rect.Y * scale / design,
		drawn_rect.Width * scale / design, drawn_rect.Height * scale / design) + image_size;

	char image[256];
	char highlighted[256];
	char disabled[256];
	char highlight_sound[256];
	char select_vq[256];

	highlighted[0] = '\0';
	image[0] = '\0';

	ini.Get_String(name, "Image", "", image, sizeof(image));
	ini.Get_String(name, "Highlighted", "", highlighted, sizeof(highlighted));
	ini.Get_String(name, "Disabled", "", disabled, sizeof(disabled));
	ini.Get_String(name, "HighlightSound", "", highlight_sound, sizeof(highlight_sound));
	ini.Get_String(name, "SelectVQ", "", select_vq, sizeof(select_vq));

	return(new GraphicMenuImageItem(id, engine, backdrop, origin, active_rect, drawn_rect, image, highlighted, disabled, highlight_sound, select_vq, scale, design));
}


// How far apart, in RGBClass::Difference units, two pictures may be and still
// count as the same. A release that upscales a face and the backdrop it is
// drawn over separately leaves them close rather than equal, so the shipped
// artwork's exact agreement is not relied on.
static int const SHAPE_TOLERANCE = 64;

// The two bands of the inscribed ellipse that are counted, as hundredths of the
// ellipse equation. The soft rim between them is left out of the count.
static int const SHAPE_CORE_BAND = 90;
static int const SHAPE_CORNER_BAND = 115;

// How much of the core the artwork has to fill, and how little of the corners it
// may reach, before the item is taken to be a disc.
static int const SHAPE_CORE_PERCENT = 75;
static int const SHAPE_CORNER_PERCENT = 2;


// Where a point falls against the ellipse inscribed in a rectangle, as hundredths
// of the ellipse equation: at most 100 inside it.
static long long Ellipse_Position(Rect const & rect, int x, int y)
{
	long long const a = rect.Width - 1;
	long long const b = rect.Height - 1;
	long long const dx = 2 * (long long)x - a;
	long long const dy = 2 * (long long)y - b;

	return((dx * dx * b * b + dy * dy * a * a) * 100 / (a * a * b * b));
}


/// <summary>
/// Reports whether an item's artwork covers an ellipse inscribed in its active
/// area instead of the whole rectangle, by reading the highlight picture against
/// the backdrop it is drawn over: the two agree everywhere the item is not.
/// Returns false whenever the comparison cannot be made, which leaves the item
/// rectangular.
///
/// The two pictures are read at their own resolution rather than through the
/// page's, because a page laid out at a size that is not a whole multiple of the
/// artwork rounds the item's corner onto the screen grid and the two pictures
/// would then be sampled half a pixel apart.
/// </summary>
/// <param name="rect">The item's active area, in the space the page was drawn in.</param>
/// <param name="design">The width that space is, which the backdrop covers.</param>
static bool Artwork_Covers_Ellipse(Rect const & rect, int design, MSAnim const * highlight, MSAnim const * backdrop)
{
	if (highlight == NULL || backdrop == NULL || design <= 0 || rect.Width < 3 || rect.Height < 3) {
		return(false);
	}

	Surface const * face = highlight->Get_Picture();
	Surface const * plate = backdrop->Get_Picture();

	if (face == NULL || plate == NULL) {
		return(false);
	}

	// A paletted picture holds color indices, which cannot be compared as colors.
	if (face->Bytes_Per_Pixel() != (int)sizeof(unsigned short) || plate->Bytes_Per_Pixel() != (int)sizeof(unsigned short)) {
		return(false);
	}

	// Both pictures are whichever copy the release prepared at the same multiple
	// of the drawn size, so one lands on the other pixel for pixel.
	int const prepared = plate->Get_Width() / design;
	if (prepared < 1 || plate->Get_Width() != design * prepared) {
		return(false);
	}
	if (face->Get_Width() != rect.Width * prepared || face->Get_Height() != rect.Height * prepared) {
		return(false);
	}

	int const left = rect.X * prepared;
	int const top = rect.Y * prepared;
	if (left < 0 || top < 0 || left + face->Get_Width() > plate->Get_Width() || top + face->Get_Height() > plate->Get_Height()) {
		return(false);
	}

	unsigned short const * face_pixels = (unsigned short const *)face->Lock();
	unsigned short const * plate_pixels = (unsigned short const *)plate->Lock();
	bool elliptical = false;

	if (face_pixels != NULL && plate_pixels != NULL) {
		int const face_pitch = face->Stride() / (int)sizeof(unsigned short);
		int const plate_pitch = plate->Stride() / (int)sizeof(unsigned short);
		Rect const area(0, 0, face->Get_Width(), face->Get_Height());
		long core = 0;
		long core_covered = 0;
		long corner = 0;
		long corner_covered = 0;

		for (int y = 0; y < area.Height; y++) {
			for (int x = 0; x < area.Width; x++) {
				long long const position = Ellipse_Position(area, x, y);
				bool const is_core = position <= SHAPE_CORE_BAND;
				bool const is_corner = position >= SHAPE_CORNER_BAND;

				if (!is_core && !is_corner) {
					continue;
				}

				RGBClass const drawn = DSurface::Deconstruct_Hicolor_Pixel(face_pixels[y * face_pitch + x]);
				RGBClass const under = DSurface::Deconstruct_Hicolor_Pixel(plate_pixels[(y + top) * plate_pitch + x + left]);
				bool const covered = drawn.Difference(under) > SHAPE_TOLERANCE;

				if (is_core) {
					core++;
					core_covered += covered ? 1 : 0;
				} else {
					corner++;
					corner_covered += covered ? 1 : 0;
				}
			}
		}

		elliptical = core > 0 && corner > 0
			&& core_covered * 100 >= core * SHAPE_CORE_PERCENT
			&& corner_covered * 100 <= corner * SHAPE_CORNER_PERCENT;
	}

	face->Unlock();
	plate->Unlock();

	return(elliptical);
}


/// <summary>
/// Constructs an image based menu item.
/// This routine loads the normal, highlighted and disabled artwork as animations and
/// hands them to the menu engine to display. Only the normal image starts out visible;
/// the others are activated as the item gains the selection or is disabled. The
/// highlighted and disabled artwork may be omitted: an item with no highlight simply does
/// not light up, and one with no disabled artwork keeps its normal image while it is
/// unavailable rather than vanishing from the menu.
/// </summary>
/// <param name="origin">The screen position to display the artwork at.</param>
/// <param name="rect">The screen area the mouse must be within to select this item.</param>
/// <param name="drawn_rect">The same area in the space the page was drawn in.</param>
/// <param name="image">Filename of the artwork shown normally.</param>
/// <param name="highlight_image">Filename of the artwork shown while selected.</param>
/// <param name="disabled_image">Filename of the artwork shown while disabled.</param>
/// <param name="highlight_sound">Filename of the sound to play as this item is selected.</param>
/// <param name="select_vq">Filename of the movie to play when this item is chosen.</param>
/// <param name="scale">The width the page is laid out at, against <paramref name="design"/>.</param>
/// <param name="design">The width the page was drawn at.</param>
GraphicMenuImageItem::GraphicMenuImageItem(int id, MSEngine & engine, MSAnim const * backdrop, Point2D const & origin, Rect const & rect, Rect const & drawn_rect, const char * image, const char * highlight_image, const char * disabled_image, char * highlight_sound, const char * select_vq, int scale, int design) :
	GraphicMenuItem(id),
	Engine(&engine),
	ActiveRect(rect),
	Elliptical(false)
{
	Image = NULL;
	HighlightImage = NULL;
	DisabledImage = NULL;
	HighlightSound = NULL;

	MSSfxEntry * snd = NULL;
	if (strlen(highlight_sound)) {
		snd = new MSSfxEntry("HighlightSound", highlight_sound);
	} else {
		snd = NULL;
	}
	HighlightSound = snd;

	strncpy(SelectVQ, select_vq != NULL ? select_vq : "", sizeof(SelectVQ));

	if (strlen(highlight_image)) {
		HighlightImage = new MSPCXAnim(highlight_image, engine.Get_Anims(), origin, true, scale, design);
		if (HighlightImage != NULL) {
			HighlightImage->Set_Active(false);
			engine.Add_Animation(HighlightImage);
			Elliptical = Artwork_Covers_Ellipse(drawn_rect, design, HighlightImage, backdrop);
		}
	}

	if (strlen(image)) {
		Image = new MSPCXAnim(image, engine.Get_Anims(), origin, true, scale, design);
		if (Image != NULL) {
			engine.Add_Animation(Image);
		}
	}

	if (strlen(disabled_image)) {
		DisabledImage = new MSPCXAnim(disabled_image, engine.Get_Anims(), origin, true, scale, design);
		if (DisabledImage != NULL) {
			DisabledImage->Set_Active(false);
			engine.Add_Animation(DisabledImage);
		}
	}
}


/// <summary>
/// Destroys this menu item.
/// The images belong to the menu engine and are disposed of along with it, so only the
/// highlight sound is freed here.
/// </summary>
GraphicMenuImageItem::~GraphicMenuImageItem(void)
{
	delete(HighlightSound);
}


/// <summary>
/// Is the mouse over this menu item?
/// An item whose artwork was found to be a disc is picked within that disc rather
/// than anywhere in its rectangle, so the corners belong to whatever is behind.
/// </summary>
/// <returns>bool; Is the mouse within the active area of an item that can be selected?</returns>
bool GraphicMenuImageItem::Is_Mouse_Over(Point2D const & mouse)
{
	if (!Enabled || !ActiveRect.Is_Point_Within(mouse)) {
		return(false);
	}

	return(!Elliptical || Ellipse_Position(ActiveRect, mouse.X - ActiveRect.X, mouse.Y - ActiveRect.Y) <= 100);
}


/// <summary>
/// Handles this menu item gaining or losing the selection.
/// This routine swaps between the normal and highlighted images, refreshes the part of
/// the screen this item occupies, and plays the highlight sound as the item is selected.
/// </summary>
/// <param name="selected">Is this item now the selected one?</param>
void GraphicMenuImageItem::On_Selected_Change(bool selected)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
	if (selected) {
		if (HighlightSound != NULL) {
			HighlightSound->Play();
		}
	}
}


/// <summary>
/// Handles this menu item becoming available or unavailable.
/// This routine swaps the disabled image in or out and then refreshes the part of the
/// screen this item occupies so that the change is visible right away.
/// </summary>
void GraphicMenuImageItem::On_Enabled_Change(bool)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
}


/// <summary>
/// Takes the item's artwork off the page, or puts it back.
/// </summary>
void GraphicMenuImageItem::On_Visible_Change(bool)
{
	Update_Images();
	Engine->Restore_Anims(ActiveRect);
	Engine->Restore_And_Advance();
}


/// <summary>
/// Activates the one image the item's current state shows.
/// </summary>
void GraphicMenuImageItem::Update_Images(void)
{
	if (Image != NULL) {
		Image->Set_Active(Visible && !Selected && (Enabled || DisabledImage == NULL));
	}
	if (HighlightImage != NULL) {
		HighlightImage->Set_Active(Visible && Enabled && Selected);
	}
	if (DisabledImage != NULL) {
		DisabledImage->Set_Active(Visible && !Enabled);
	}
}


/// <summary>
/// Performs this menu item's action.
/// The normal item action is performed first. If this item has a selection movie
/// available, the movie is then played and the menu waits for it to finish before
/// carrying on.
/// </summary>
void GraphicMenuImageItem::Action(MSEngine * engine)
{
	GraphicMenuItem::Action(engine);
	if (Movie_Is_Available(SelectVQ)) {
		MSAnim * anim = new MSVQAnim(SelectVQ, AlternateSurface, engine->Get_Anims(), true);
		if (anim != NULL) {
			engine->Wait_For_Anim(anim);
		}
	}
}
