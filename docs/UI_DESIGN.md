# UI system design

Status: in progress. The [migration plan](#migration-plan) records each step
that has landed, with its evidence and the deviations it took; a step not
marked as landed is still proposal. This page owns the UI architecture and migration;
[Building OpenTS](BUILDING.md) owns build support and
[Project direction](DIRECTION.md) the wider architecture.

## Where the UI stands today

OpenTS has three UI systems plus a few bespoke screens. They share the software
frame and the keyboard queue but nothing else.

| System | Files | Used by | Draws into |
| --- | --- | --- | --- |
| RmlUi shell | `code/ui/`, with the documents, styles and fonts in `ui/` | main menu, options, skirmish, load and save, network lobbies, disconnect and desync, map generator, message boxes, progress and wait boxes, version | a GPU overlay over the presented frame |
| GadgetClass | `gadget.cpp`, `control.cpp`, `toggle.cpp`, `list.cpp`, `edit.cpp`, `slider.cpp`, ... | sidebar, radar, tactical buttons, message list, checklist, mission restate | `LogicalSurface` (`SidebarSurface`, `HiddenSurface`) |
| MSEngine | `msengine.cpp`, `msanim.cpp`, `grphmenu.cpp` | graphic menu, map select, score screens, WDT screens, credits | `AlternateSurface`, `HiddenSurface` |
| Bespoke | `progress.cpp`, `score.cpp`, `movies.cpp` | loading screen, score, movies | `HiddenSurface` |

Every screen that was an owner-draw dialog is an RmlUi screen, and OwnerDraw,
its dialog templates and `Language.dll` are deleted ([step 13](#migration-plan)).
The crash reporter's dialog in `except.cpp` is the one Win32 dialog left.

Before the migration, OwnerDraw was the largest system and the least portable.
Each dialog was a real Win32 child window of `MainWindow`, created from a
resource template by `CreateDialogIndirectParam`. Every control was
subclassed; its window procedure painted into `AlternateSurface` and blitted
the result into `VisibleSurface` itself. `Draw_Dialog_Back` composed
`dbak6440.pcx`, the side bars, and sixteen glow passes into a cached surface
and assumed 640x400 art centered on the screen. Text was GDI "MS Sans Serif"
at 14 and 12 pixels through `WS_Get_Font`, plus the `dlgsys` remap sheets for
list text. `Heal_Dialog_Controls` forced every child window to repaint after
each `Update_Visible_Surface`, so a dialog repainted once per game frame. The
templates held 322 `CONTROL` entries: 103 owner-draw buttons, 40 track bars,
26 combo boxes, 26 list boxes, 23 edit boxes, and 45 check boxes. Two entry
APIs shared one dialog stack, `g_Dialogs` in `windlg.cpp`:
`OwnerDraw::Begin_Dialog` and the `WS_` family the network lobbies used.

The frame path is simple and hardware-presented. The game draws 16-bit pixels
into system-memory surfaces. `Video_Present` hands `VisibleSurface` to
`Backend_Present`, which uploads it as one texture, draws one quad with the
embedded `vs_ocornut_imgui` program on view `VIEW_PRESENT` (with a
`VIEW_PRESCALE` pass for the pixel-art filter), and `Backend_End_Frame` calls
`bgfx::frame()` after the shell's overlay. bgfx runs single-threaded, and
`_Presenting` guards against a present nested inside another. Presents are
paced to the refresh interval and happen only when the frame or the overlay is
dirty. The mouse pointer is a host cursor built from the game's shapes
(`Host_Create_Cursor`: a Win32 cursor on Windows, a CSS cursor on the page), so
it never touches a surface. The window is per-monitor DPI aware, and
`VideoScaleInfo` records where the logical frame lands in the physical client
area.

Input reaches the game the same way on both hosts. On Windows, `Windows_Procedure` in
`code/hostwindow_win32.cpp` takes a mouse position into the frame, offers the
message to the shell's hook (`UI_Handle_Window_Message`), and then hands keys to
`Keyboard->Post_Key_Event` and buttons to `Game_Window_Mouse_Button`, which
offers them to `Map` first. On the page, `Browser_Service` does the same from
the page's events through its own event hook. Both put keys and mouse buttons
with their position into the `KN_` queue. Gadgets poll that queue in
`GadgetClass::Input`; `WWKeyboardClass::Down` asks the host for the key state
(`Host_Key_Is_Down`), so withholding a queued key does not hide a held key from
polling code.

Every screen owns its loop. In game, `Main_Loop` runs input, logic, and render.
A modal screen spins in `UI_Run_Modal`, whose pass calls `UI_Service_Game`:
`Main_Loop` in a network session and `Call_Back` otherwise, so multiplayer
keeps stepping under a screen, as it did under
`OwnerDraw::Dialog_Message_Handler`. The lobbies keep `WS_Wait_Dialog` with a
callback, over the screen stack in `windlg.cpp`. MSEngine screens spin on
`Engine.Wait_Delay`. `RestateMission` mixes gadgets with MSEngine.
`Keyboard->Clear()`, which a modal screen calls as it opens and closes, pumps
messages through `Fill_Buffer_From_System`, so cleanup can re-enter UI code.

Nearly every legacy flow hid or destroyed its parent before opening a child:
the main menu hid around the version dialog, the options driver ended the main
dialog before a sub-dialog, in-game options hid around save and load, skirmish
hid around the scenario picker. The lobby kept its host and game-list dialogs
alive together.

Facts elsewhere in the tree that bind the design:

- `Fetch_String` returns a pointer into the static table in
  `code/languagestrings.cpp`, compiled into every target. String identifiers
  are `#define`s in `language.h`, and the name table documents use is
  generated from it. Before the migration it loaded through the ANSI
  `LoadString` out of `Language.dll` into a 128-slot reused cache.
- `CDFileClass::Has_Directory` treats `\`, `/`, and `:` as directory marks,
  and such names skip the user-path redirect; a bare name is tried in the
  user path, the current directory, the search paths, and then the mix files.
- `Map.Input`, and with it `SidebarClass::AI`, is polled from `Main_Loop` and
  from the network and timer waits in `queue.cpp`.
- `FactoryClass::Has_Changed` clears `IsDifferent`, which `FactoryClass::Serialize`
  writes. `StripClass::Serialize` writes `IsScrolling`, `Flasher`, `Scroller`,
  `Slid`, and `LastSlid` beside `TopIndex` and `Buildables`.
- `SidebarClass::Reposition_Sidebar` registers the cameo tooltips itself,
  independent of gadget registration; `CCToolTip` paints into game surfaces.
- `ProgressScreenClass::Set_Progress_Percent` redraws through
  `Display_Progress` at once, and `Display_Progress` plays the milestone sound
  from the draw path.

Three consequences shaped the design. A new UI must fit the blocking-loop
shape, or every driver has to be rewritten in the same change; the loop shape
is fine and the screen bodies were the problem. Anything drawn into the
software frame sits under anything drawn by bgfx, and a visible legacy window
took the mouse before the shell could see it. Input must be claimed before it
enters the `KN_` queue, and legacy child windows had to keep receiving their
own messages until they were gone.

## Goals and limits

Goals:

- Replace OwnerDraw with RmlUi documents, one screen per change, with the
  legacy screen available behind a switch until OwnerDraw is retired.
- Make screens interchangeable at the screen level: a model or presenter that
  knows nothing about the toolkit, and one view per toolkit.
- Leave GadgetClass and MSEngine in place; migrate them later through the same
  screen contract when a screen is worth it. The sidebar follows only after
  the Win32 dialogs are gone, as a player-selectable alternative to the
  gadget sidebar.
- Add Dear ImGui on the same shell for developer tooling, available to a
  player-facing feature if one wants it.
- Keep modding open: documents, styles, and images load through the game's
  file system with mix-file support from the first screen.

Limits, chosen to keep the work bounded:

- No widget-level abstraction across GadgetClass, OwnerDraw, RmlUi, and
  ImGui. Views are whole documents.
- No scripting layer (RmlUi's Lua plugin), no reactive framework, no global
  message bus, no runtime plugin system.
- No RmlUi render effects (filters, layers, shaders, box shadows) and no
  clip mask in the first renderer. The renderer implements the eight
  required methods; the rest stays default until a screen needs it.
- No arbitrary layering of native and GPU UI. The coexistence rule under
  [Input and focus](#input-and-focus) is the whole policy.
- No user UI scale setting yet. Documents follow the frame scale.
- The exception and assertion dialogs stay plain Win32. They must work when
  the renderer is the thing that failed.

## Architecture

Three parts, from the bottom up.

The **UI shell** is one module that owns the RmlUi context, the ImGui
context, the bgfx overlay pass, the input hook, and the modal runner. It is
the only code that includes RmlUi or ImGui headers, the way `bgfxbackend.cpp`
is the only code that includes bgfx.

A **screen** is a presenter plus a view. The presenter is a plain C++ object:
it holds a view-model struct, answers queries, and executes actions. It never
sees an `HWND`, a `Surface`, an `Rml::Element`, or an ImGui call. A view
renders the view-model and turns user actions into intents. The RmlUi view is
a document with a data model; the legacy view is the existing dialog
procedure wrapped so it reads and writes the same presenter; an ImGui view is
possible for a feature that wants it. A screen returns a result the way a
dialog returns `rc` today.

The **toolkits** are RmlUi, preferred for player-facing screens; the existing
systems for screens not yet migrated; and ImGui, primarily for tools.

Dependency rules:

- Presenter headers contain no `HWND`, control IDs, `GadgetClass`, RmlUi,
  ImGui, or bgfx types.
- Views use their toolkit directly. There is no shared widget API.
- RmlUi data bindings and document nodes stay inside the RmlUi view.
- Renderer handles stay inside `code/ui/uirender.cpp`.
- The shell knows which presentation owns a region and an input scope. It
  does not know production rules, save semantics, or option behavior.
- Existing callers keep their screen functions; composition sits behind
  them.

A read-only screen needs a data builder and a close result. A presenter with
actions is added only where a screen has real state transitions.

### Code layout

New files live in `code/ui/`. The recursive glob in `code/CMakeLists.txt`
picks them up, and the directory lets the RmlUi, ImGui, and bgfx include
paths be scoped to the files that need them, as `bgfxbackend.cpp` is scoped
today.

| File | Holds | State |
| --- | --- | --- |
| `uishell.h`, `uishell.cpp` | init and shutdown, resize, input dispatch, tick, overlay render entry, selector | Built |
| `uievent.hh` | the shell's own input vocabulary, which no window system appears in | Built |
| `uiwin32.h`, `uiwin32.cpp` | the `Windows_Procedure` hook, translating messages into those events | Built |
| `uibrowser.cpp` | the page's event hook, translating the page's events into those events | Built |
| `uiwsstack.h` | the screen handles the network lobby's `WS_` stack in `code/windlg.cpp` holds | Built |
| `uirender.cpp` | RmlUi render interface on bgfx; with `bgfxbackend.cpp`, the only file that includes bgfx | Built |
| `uisystem.cpp` | RmlUi system interface: time, logging to `DebugString`, cursor, clipboard, string translation | Built, less cursor, clipboard, and translation |
| `uifile.cpp` | RmlUi file interface over `CCFileClass` | Built |
| `uitexture.cpp` | the game's dialog artwork as textures: paletted pictures, shape frames through a named palette, the composed backdrop, the composed button skins, and surfaces the engine draws at runtime | Built |
| `uiprobes.cpp` | register entries that exercise shell capabilities no screen reaches yet | Built |
| `uiscreens.h`, `uiscreens.cpp` | the register of migrated screens, so a screen can be opened without its menu route | Built |
| `uiscreen.h`, `uirmlview.h`, `uirunner.h` | presenter, intent, and result contracts; the RmlUi view base; the modal runner | Built |
| `uimodel.h` | data type registration that tolerates a screen opening twice | Built |
| `uifont.cpp`, `uifontengine.cpp` | the dialog face from the owner-draw glyph sheets, and the font engine that serves it beside FreeType | Built |
| `uidev.cpp` | ImGui context and developer overlays | Planned |
| one file per screen | presenter, view-model binding, and the RmlUi view glue | Built for every screen in the migration plan's landed steps |

Shipped UI files (documents, styles, images, the font) live in `ui/` at the
repository root. The build copies the tree beside the executable, and the CI
artifacts and the release zip carry it from there.

## Rendering

RmlUi and ImGui render as GPU overlays on top of the presented frame, at the
physical resolution of the window. `Backend_Present` splits in two:
`Backend_Present` submits the frame quad as now but no longer calls
`bgfx::frame()`; a new `Backend_End_Frame` does. `video.cpp` calls the shell's
render between them:

```cpp
Backend_Present(pixels, ...);   // VIEW_PRESCALE, VIEW_PRESENT
UI_Render_Overlay();            // VIEW_UI, then VIEW_DEV
Backend_End_Frame();            // bgfx::frame()
```

No other code begins or ends a bgfx frame. The view identifiers move from
`bgfxbackend.cpp` into a small shared header so both translation units agree
on the order. The overlay views use the frame destination rectangle from
`Video_Get_Scale_Info` as their viewport and an orthographic transform of the
destination size, so UI coordinates are physical pixels relative to the
frame's top-left corner. Draw order is the software frame and its scaling
passes, RmlUi documents in the context's document order, ImGui, then the
hardware cursor.

One RmlUi context holds every document. A second context is justified only by
an independent coordinate space or lifetime. Data-model names are unique
among live screens, binding storage is owned by the view and outlives the
model, and a model is removed before its storage is destroyed.

### Renderer

The render interface is a bgfx implementation of RmlUi's eight required
methods:

| Capability | Behavior |
| --- | --- |
| Compiled geometry | Static index buffer; the vertices are kept on the processor and offset into a transient buffer at each draw. RmlUi hands a mesh over in its element's coordinates with the element's position as a per-draw translation, and the presenter's embedded imgui program transforms by the view and projection alone, so a model matrix set per draw is ignored. A program of the engine's own that reads a model matrix would return the vertices to a static buffer. Order preserved; released on request; never dependent on transient memory from a previous frame. |
| Textures | RGBA8, premultiplied alpha as the interface specifies, created and released explicitly, cached by source string, sized for the 32-bit process. A source is offered to `uitexture.cpp` first, which answers a paletted picture, `frame:WIDTHxHEIGHT` with an optional `@X,Y`, or `button:u\|d:WIDTHxHEIGHT`; anything else goes to the general decoder. |
| Blending | `ONE, INV_SRC_ALPHA`; vertex colors follow the same premultiplied contract with no double premultiplication. |
| Scissor | `bgfx::setScissor` in physical target coordinates, intersected with the viewport, empty regions handled. |
| Projection | The overlay view's orthographic transform; no game-image filter state inherited. |
| Reset and resize | Target-dependent resources recreated, viewport and scissor refreshed, a full redraw requested; existing documents redraw without reload. |

The program is the embedded imgui vertex and fragment shader that
`bgfxbackend.cpp` already carries. Its attributes (position, texture
coordinate, color) match RmlUi's vertex and ImGui's vertex, each with its own
layout. Clip masks, transforms, layers, filters, and shaders are deferred;
shipped documents stay within a declared profile (text, images, ordinary
layout, borders, basic decorators), and a document check enforces it.

### Invalidation

`Video_Present_If_Dirty` grows a second dirty flag for the overlay: a present
happens when either flag is set, but the texture upload happens only when the
frame is dirty. RmlUi has no "needs redraw" query, so the shell marks the
overlay dirty on every tick that a document is visible or an ImGui window is
open, and the present pacing caps the rate. Closing or hiding a document also
marks the overlay dirty so its pixels disappear. A visible menu at 4K then
costs a few draw calls per refresh, not a 16 MB upload. Invalidation raised
during a present is kept for the next one rather than cleared with the
current frame.

Movies keep their own presenter path; the shell renders nothing while a movie
plays.

## The dialogs' own look

A migrated screen wears the skin the owner-draw dialogs wore, because a screen that looks
like itself rather than like the game is not a replacement. Three things carry it.

**The backdrop.** `OwnerDraw::Draw_Dialog_Back` was not a border and a fill: it sampled
`dbak6440.pcx` at the dialog's own position, tiled `leftbar.pcx` and `rightbar.pcx` down
the edges, blitted the four `bar_*.pcx` corners, and laid sixteen passes of white over the
inside, fading from 96 to 6 as they moved inward. `uitexture.cpp` composes the same thing
into one texture, named for the size it was composed at, and a screen asks for it:

```css
#version { width: 408dp; height: 172dp; decorator: image(frame:408x172); }
```

Because the wallpaper and the dialog are both centred on the frame, the part of the
wallpaper that shows through depends on the difference in their sizes alone, not on the
resolution, so one texture per dialog size is correct at every resolution.

**The buttons.** The painter drew a left cap, the middle strip tiled from its own centre,
and a right cap, from `b{u,d}e_{li,mi,ri}{24,30}.pcx`. A stylesheet cannot say
"tiled from its own centre", so these are composed too, and a screen names the size it
gave the button: `decorator: image(button:u:93x24)`, with the pressed set on `:active`.
The pressed skin is composed two pixels lower, as the painter drew it, and the shared
`:active` padding moves the caption two pixels right and four down, so a screen must not
set a button's padding of its own.

**The colours.** `ownrdraw.cpp` set them: text `RGB(112,255,0)`, dimmed text
`RGB(16,144,16)`, disabled `RGB(144,144,144)`, frame `RGB(78,182,220)`. The stylesheet
uses those rather than colours of its own.

Two things a screen must get right or it will not match:

- `box-sizing: border-box` on the dialog. The size a screen asks for is the size the
  backdrop was composed at, so the padding that clears the 24-pixel bars comes out of it
  rather than adding to it.
- Author at the size the dialog template declares, converted with MS Sans Serif 8's base
  units of 6 and 13: a 272 by 106 template is 408 by 172. That is what the original game
  produces.
- A control was a pixel wider and a pixel taller than that conversion gives. `Resize_Dialog`
  in `code/windlg.cpp` recomputed every control rectangle from `right - left + 1` and
  `bottom - top + 1`, so an 18 unit button measured 30 pixels rather than 29. The documents
  are not corrected pixel by pixel for this, because a pixel on an edge is invisible against
  the backdrop and correcting it would touch every rule in every screen. It is corrected
  where it changes what is drawn: the painter picked the tall button skin at 30 pixels and
  the short one below that, so an 18 unit button takes the tall set and a 14 or 16 unit
  button the short one.

**The lettering.** The dialogs draw their text from the `dlgsys` glyph sheets through
`ODDrawCharRemap`, which shifts each palette entry's hue toward the requested colour and
keeps the sheet's own shading and dark shadow: ODColorText RGB(112,255,0) comes out as
(181,251,0) with green bevels. `code/ui/uifontengine.cpp` is an RmlUi font engine that
serves the family `opents-dialog` from those sheets as pre-coloured bitmap glyphs, remapped
per colour the same way (`code/ui/uifont.cpp`), and forwards every other family to RmlUi's
own FreeType engine. The body style asks for the family at 17dp, the sheets' line. The
sheets are only readable once the mix files are registered, so the face arrives after the
shell starts; until then, or when the sheets cannot be read, the shipped face stands in
under the same family. At 1280x800 every text and shadow pixel matched the legacy view; at
a frame scaled by a fraction the colours and sizes matched, but smoothed edges fell on a
different sub-pixel phase, because RmlUi rounds an element's position before the glyphs are
placed. The face takes no font effects.

**The second face.** The sheets were not the only lettering. `OD_Draw_Text` wrote through
the surface's device context in MS Sans Serif, at 12 pixels for list rows (`ODListFontPtr`)
and at 14 for group box captions, tooltips and the hotkey box (`ODFontPtr`); only what the
painter drew itself came from `dlgsys`. Those places ask for `opents-sans` at 12dp and 14dp
instead. MS Sans Serif is a Windows bitmap face that cannot be shipped, and Liberation Sans
is the metric match for Arial, which is what Windows substitutes for MS Sans Serif once its
bitmap sizes run out; no closer substitute is available under a license the project can
carry. Rows step 14 pixels, as the owner-draw lists did, which is `ODListFontSize + 2`.

## Writing a screen

Everything below was learned by getting it wrong first. A screen that follows it works; a
screen that does not fails in ways that look like RmlUi being broken.

### The files a screen owns

A screen adds `code/ui/ui<name>.cpp`, `code/ui/ui<name>.h` and `ui/<name>.rml`, and edits
the one driver that opens it. Nothing else: the source glob picks the new files up,
and a screen puts itself in the register from its own translation unit, so no shared table
has to grow.

```cpp
static UIScreenRegistration _Register("sound", Open_Sound_Dialog);
```

Register the **driver**, not the screen function, so that a run exercises the path a caller
takes. A run finds the index it wants by name through `OpenTS_UI_Screen_Count` and
`OpenTS_UI_Screen_Name`, so the order screens register in does not matter.

### The driver

The driver opens the screen and carries on as the dialog's driver did:

```cpp
if (!UI_Sound_Screen()) {
    DebugString("SoundControls: the sound options screen could not be shown\n");
}
```

A screen that cannot be prepared answers as a dialog that could not be created did. A
screen that reports the session ended has to be reported the way the dialog's driver
reported it, not as a dismissal.

### The presenter

Read engine state in `Refresh`, write it in `Execute`, and keep every toolkit type out of
the header. Establish what the dialog procedure does before changing anything: which call
each control makes, in which order, and with which arguments. Classify the result as
preserved, fixed, or intentionally changed, and say which in the change that lands it.

### The view

Each of these rules has already cost a day:

1. **Create the data model before the document loads.** RmlUi resolves `data-model` while it
   parses and reports a model created afterwards as missing. That is what `Bind_Model` is
   for; `Bind` runs after the load, for listeners.
2. **Read a control's new value from the change event, not from the bound variable.** RmlUi
   raises the event before it stores the value, and the order between a screen's listener
   and the binding's own is not defined. `event.GetParameter<float>("value", fallback)` for
   a range, `event.GetParameter<bool>("checked", fallback)` for a check box.
3. **`Sync` pushes the whole model once and then only what an intent changes.** Dirtying a
   bound value on every pass writes the model's value over the one the player is dragging a
   control to, which looks exactly like a control that does not work.
4. **An event handler queues an intent and returns.** Acting inside RmlUi's own event
   dispatch re-enters it. The runner drains the queue after `Context::Update` returns.
5. **Release the data model in `Release_Model`.** The base class calls it after the document
   is closed and before the presenter's storage goes, so a screen does not have to remember.
6. **Number a screen's own actions from `UI_ACTION_SCREEN`.** The view base raises
   `UI_ACTION_ACCEPT` for Enter and `UI_ACTION_CANCEL` for Escape in every screen, so a
   screen action numbered 1 or 2 is triggered by those keys. A screen whose dialog ignored a
   key ignores the action; the sound screen, whose dialog had no cancel, does.
7. **Disable a button with the `disabled` attribute, and check it in the presenter too.**
   RmlUi treats only form controls as disabled and a `<button>` is not one, so the view base
   catches a click on anything carrying the attribute before the element's own listeners
   see it, and the stylesheet selects `button[disabled]`. A presenter still drops an intent
   that names a disabled control, since a key or a later change could raise it.
8. **Register a data type through `UI_Register_Struct` and `UI_Register_Array`** from
   `uimodel.h`. RmlUi keeps a type for the life of the context and reports every later
   registration as an error, which a screen opened twice, or a type two screens share,
   would otherwise raise.
9. **Hide a screen around a nested one with `Hide` and `Show`.** RmlUi leaves the focus in a
   hidden modal document when no other document is showing, so `Hide` parks it on the
   document itself and the view base ignores events while the document is hidden. Keys and
   characters then reach the nested screen, whichever toolkit draws it.

### The document

- Author at the size the dialog template declares, converted with MS Sans Serif 8's base
  units of 6 and 13: a 272 by 106 template is 408 by 172. Give the dialog
  `box-sizing: border-box`, and ask for its backdrop with `decorator: image(frame:408x172)`.
- Take the skin from the artwork, not from colours of your own: `decorator:
  image(button:u:93x24)` for a button with the pressed set on `:active`, `class="list"` for
  a list, the shared `input` rules for a track bar and a check box, `class="text"` on an
  `<input type="text">` for a text field, and a plain `<select>` for a combo box. The shared
  stylesheet already carries all of it; a screen should add geometry, not appearance. A
  combo box takes no height of its own: the painter drew every closed box 24 pixels tall
  whatever its template said, its drop arrow at the artwork's own 18 by 22, and stepped its
  dropped rows 16 pixels.
- Reuse `[[TXT_NAME]]` wherever an identifier exists. Many dialog templates carry their
  labels as literal control text with no identifier at all, and those stay literal in the
  document; a localized build ships a translated copy of the document.
- RmlUi leaves an unknown element inline, so a line of text wants `display: block`.
- A flex container lays out its element children and leaves a bare text node undrawn, so
  wrap text that sits directly inside one.
- A widget acts on a press only when the press landed on the element it listens for. A
  track bar's track must fill its control's height, or most of the row is dead to a click.
- An owner-draw track bar showed its value in a 50 pixel trough to its right unless its
  dialog sent it `OD_TRACKNUMBERS` with zero; wrap a numbered one in a `.numbered` box with
  a `.number` beside the input. It clicked when the player moved it unless its dialog sent
  `OD_TRACKSILENT`, which a document says with `data-silent=""` on the input; the view base
  plays the click.
- A list gets the owner-draw scroll bar from the shared `scrollbarvertical` rules. The
  owner-draw list showed only whole rows; an RmlUi list shows the part of the next row
  that fits. The painter narrowed the list and created the bar beside it as a child of the
  dialog, so the two frames closed against each other; RmlUi puts the bar inside the
  element it scrolls, so the shared rules pull it one pixel right and let its own frame
  serve as the list's right edge. Two differences remain: the track is dimmed by the list
  behind it as well as by its own lightening, and RmlUi sizes the grip by the visible
  fraction where the painter sized it by the range, so only the 14 pixel floor is kept.
- A control with no caption of its own carries a `.tooltip` child, which the shared rules
  draw as `OwnerDraw::Show_Tooltip` drew one and the document shows on `:hover`. The
  painter's tips waited for the pointer to rest, followed it, and fell back to the text of
  the list cell under it; none of that is here, so a list row too long for its box is still
  cut short with no way to read the rest.
- A dialog its driver moved with `OwnerDraw::Move_Dialog` showed the wallpaper at its own
  place, not the centre, so its backdrop names that place in the 640 by 400 artwork:
  `frame:306x239@167,147` for a dialog centred across and 147 pixels down.
- A legacy driver's loop that did more than service the game each pass, such as running a
  caller's callback, does it in the presenter's `Service`, which the runner calls every
  pass.

### Evidence

Every screen lands with a harness run that opens it through its driver and exercises what
its callers read: each control, each way out, and the value a caller is answered with. Run
it at a native resolution and at a letterboxed one. The owner-draw views are gone, so a
side-by-side against one needs a build from before step 13. Say what you did not run.

## Coordinates

Three spaces exist and the shell owns every conversion between them:

| Space | Purpose |
| --- | --- |
| Native client pixels | Window messages and the drawable size. |
| Game logical coordinates | Existing surfaces, tactical input, legacy geometry. |
| UI coordinates | RmlUi and ImGui layout inside the overlay viewport. |

| Quantity | Value |
| --- | --- |
| Context dimensions | `DestWidth` by `DestHeight` from `VideoScaleInfo`, physical pixels. |
| Document origin | `DestX`, `DestY` in the client area. |
| Density-independent pixel ratio | `min(ScaleX, ScaleY)`; one authored `dp` is one game logical unit. |
| Pointer input to the overlay | Client pixels minus the destination origin; never divided by the ratio. |
| Pointer input to the game | `((x - DestX) / ScaleX, (y - DestY) / ScaleY)`, only for consumers that are eligible. |
| Wheel position | Arrives in screen space; converted to client space once. |

A document authored at a legacy dialog's logical size therefore appears at
the same on-screen size while text is rasterized at physical resolution. The
presenter fits uniformly and truncates the destination extents, so `ScaleX`
and `ScaleY` can differ by less than a pixel across the frame; the uniform
ratio serves everything inside a document. A document whose edge must meet a
software-drawn edge, such as the future sidebar meeting the tactical
viewport, gets its outer bounds from the exact mapping, both edges rounded as
the presenter rounds, and receives them as physical pixels; only its interior
is authored in `dp`. Letterbox space outside the viewport is inactive for UI
and never becomes an edge click through clamping; a captured release is still
delivered there. `Video_Set_Mode` and `Video_On_Resize` notify the shell so
the context, mapping, clipping, and cursor scale change together.

## Input and focus

### Coexistence rule

An RmlUi or ImGui document may be shown only while every legacy dialog is
hidden or destroyed. A legacy dialog may be shown only while no overlay
document is visible. Both halves follow from the survey: legacy pixels are
under the overlay, and a visible legacy window takes the mouse before the
shell sees it. `Windows_Message_Handler` skips hidden dialogs in its
`IsDialogMessage` loop so a hidden parent cannot take Tab, Enter, or Escape
from an overlay child. Debug assertions in `OwnerDraw::Begin_Dialog`,
`OwnerDraw::Display_Dialog`, `WS_Create_Dialog`, and the shell's show path
enforce the rule. The legacy flows already satisfy it except the lobby, which
migrates as one family. Since step 13 no legacy dialog remains, so the rule
constrains nothing.

### Hook and priority

The shell gets a hook in `Windows_Procedure`, after a mouse position is taken
into the frame and before the game's own input handling:

```cpp
if (UI_Handle_Window_Message(hwnd, message, wParam, lParam)) {
    return(0);
}
```

Placing it before the game's handling keeps consumed input out of the `KN_`
queue. The hook covers mouse, wheel, key, and text messages only. On the page,
`code/ui/uibrowser.cpp` hooks `Browser_Service` the same way
(`Browser_Set_Event_Hook`), so it sees each queued event before the game does.

The hook itself is the only part of the shell that names a window message.
It lives in `code/ui/uiwin32.cpp` and translates each message into the
shell's own event vocabulary in `code/ui/uievent.hh`, which the shell and
every view are written against. Retiring the Win32 message loop therefore
costs that one file rather than the shell, which is why the split exists.
Activation, size, paint, transport, and system messages continue on their
paths. Forwarded or re-targeted messages are delivered to a toolkit once.

Priority follows scope and capture, not toolkit:

1. Application lifetime handling: activation, shutdown.
2. The active exclusive modal, legacy or overlay.
3. An ImGui window that owns focus or capture, per ImGui's capture flags.
4. A HUD document for its region, focused field, or capture.
5. Gameplay input that remains eligible.

The rules the hook applies, in order:

1. If ImGui wants the mouse or keyboard, ImGui takes the message. ImGui is
   fed input first and its capture flags decide suppression; capture is not
   a filter on delivery.
2. If a modal document is shown, RmlUi takes every mouse and key message.
   This mirrors `IgnoreInput` around a legacy dialog and composes with the
   scenario's own input locks rather than replacing them.
3. Otherwise mouse moves are always delivered and never consumed, so the
   game keeps tracking the cursor. A button or wheel message is consumed when
   RmlUi reports that the mouse is interacting with an element (its mouse
   functions return `false` for that). Keys and text are consumed when an
   element stopped their propagation, or whenever the focused element is a
   text field. Documents that float over the game mark their body
   `pointer-events: none` so empty space passes through.
4. A button press that a toolkit consumed sets mouse capture on `MainWindow`
   until the release, and the owner of a press owns its release: crossing a
   region or opening a modal in between completes or cancels that gesture
   without activating the newly focused screen.

Gameplay code that polls `Down` still sees held keys; eligibility is applied
at the consumers, `GScreenClass::Input` and the gadget and scroll paths, not
by falsifying physical state.

### Focus, cursor, clipboard, text

The shell clears the keyboard queue when a modal document opens or closes,
after marking the screen closing so the pump inside `Keyboard->Clear()`
cannot re-enter it. Focus loss cancels capture, drags, and composition;
focus return does not replay held keys as presses. Cursor requests from RmlUi
(`pointer`, `text`) map to `Win_Cursor_Set` and the previous request is
restored on close. The clipboard interface uses the Win32 clipboard.

Text input arrives as `WM_CHAR` with surrogate pairs joined. Consuming a
physical key never suppresses the text message it generates. Editable
screens ship only after Tab and Shift+Tab, Enter and Escape, repeat,
modifiers, paste, dead keys, and IME composition have been exercised for the
supported languages; the read-only pilot proves none of that.

## Screens

The screen contract is two small classes. The presenter is toolkit-free; a
view binds it:

```cpp
class UIPresenterClass {                          // uiscreen.h: no toolkit types
    public:
        void Queue(UIIntent const & intent);              // from any view's events
        void Drain(void);                                 // owner's safe point: Execute each, in order
        virtual void Execute(UIIntent const & intent) = 0;
        virtual void Refresh(void) = 0;                   // engine state into the view-model
        std::optional<UIResult> Result;
};

class UIRmlViewClass {                            // uirmlview.h: owns the document
    public:
        UIRmlViewClass(UIPresenterClass & presenter, char const * document);
        virtual void Bind(Rml::DataModelConstructor & model) = 0;   // view-model fields and events
        virtual void Sync(void) = 0;                                // dirty what Execute changed
};
```

The view-model is a struct of plain values and vectors that RmlUi's data
binding renders; the document uses `data-model`, `data-value`, `data-for`,
and `data-event-click="queue('ok')"`. Intents are small tagged values holding
identities and copied data, never DOM pointers, borrowed buffers, `HWND`s, or
unprotected engine pointers. The presenter copies what it needs out of
`Options`, `Session`, or the scenario into the view-model and writes back on
accept, which is what the dialog procedures do today with `TempOptions`. A
query never clears an engine dirty flag, advances a timer, consumes a factory
notification, or emits an event; where an existing getter has such an effect,
it stays on the behavior path and a separate query is added.

Executing an intent:

1. Verify the screen and the scenario or session are still alive and the
   originating scope is still eligible; a screen carries a lifetime token and
   a scenario generation for this.
2. Resolve the supplied identity against current state.
3. Apply the feature's existing validation, feedback, and rejection at their
   existing boundaries; styling adds no new restriction.
4. Use the current service or command path with its ordering and effects.
5. Refresh the view-model or produce a result.

Intents are executed in order. When a scope is suspended or loses
eligibility, its undrained intents are discarded, not replayed; accepted
effects are not undone. Production clicks and commands are never coalesced.

Each screen defines its result and, where relevant, distinguishes accepted,
cancelled, session ended, and failed to open. A wrapper maps these onto the
existing return values, including `DIALOG_OK` and `DIALOG_CANCEL` from
`code/dialogresult.h`, which keep the numbers `IDOK` and `IDCANCEL` had.
Settings that apply immediately do not gain an apply-and-cancel transaction.

Until step 13 deleted both, the legacy view for a migrated screen was the
existing driver behind a selector:

```cpp
int WWMessageBox::Process(...) {
    if (UI_Use_Rml()) return(UI_Message_Box(...));
    // existing OwnerDraw path, deleted with OwnerDraw
}
```

Selection was latched at screen entry or at scenario load, never mid-gesture.
Preparation (documents, bindings, resources, host scope) completes before a
view becomes interactive; a preparation failure reports the resource, and the
driver answers as a dialog that could not be created did. After activation, a
view failure recreates presentation against the surviving presenter state and
never replays accepted intents.

## Scheduling

Three kinds of work keep their owners: game behavior and command production
run at their existing call points with their existing gates; toolkit input,
layout, and animation run at the shell's service points on the application
thread; GPU submission runs in the presenter. UI animation uses wall-clock
time and never reads or advances deterministic game timers.

A migrated dialog driver keeps its shape. `Run_Modal` is the RmlUi twin of
the `Dialog_Message_Handler` loop:

```cpp
UIResult UI_Run_Modal(UIScreen & screen);
// each pass:
//   Windows_Message_Handler();
//   Main_Loop() in a network session, else Call_Back();   -- same test as today
//   context->Update();
//   execute the screen's queued intents;
//   mark the overlay dirty, Video_Present_If_Dirty();
// until the screen has a result or Main_Loop reports the game ended.
```

The result carries the game-ended flag the way `Dialog_Message_Handler`
returns `true`, so callers keep their logic. Wrappers keep their service
paths: the main menu keeps title-screen maintenance, an in-game screen keeps
the guarded multiplayer pump, lobby and loading flows keep their own work.

Event handlers never act directly. A toolkit event queues an intent, and the
runner executes the queue after `Context::Update` returns. RmlUi gives no
guarantee about re-entering `Update` from its own event dispatch, so a nested
modal (options opening a message box) starts from the queue, one level up,
where `Run_Modal` nests cleanly; and the legacy code already works this way,
`WM_COMMAND` writing `rc` for the driver to act on after the pump. A modal
document is shown with RmlUi's modal flag, which keeps other documents from
taking focus; blocking the game's input is the shell's job through the hook.
Paint handlers and the pump never drain intents, advance game logic, or
update the context; a nested update or present request is recorded and
served at the next safe point.

Non-modal documents are updated by a `UI_Tick` call in `Main_Loop` next to
`Map.Input` and rendered by every present.

Teardown order: mark the screen closing and invalidate its token, then drop
focus and capture and discard its intents, then detach listeners and data
models and release documents while their storage lives, then remove the
shell registration, and only then clear the keyboard queue or return focus.
Focus returns only to a still-valid owner and never foregrounds the game
while another application is active. A session may end during a multiplayer
modal; closing must not recreate a destroyed HUD or touch a stale scenario
pointer.

## Assets and strings

### Files

The shipped files live in `ui/` at the repository root. Every target but the
page copies the directory beside the executable after the link; a page has no
such directory and these files are not in the asset manifest, which carries a
data release's archives and films, so the WebAssembly build embeds the
directory into the module's own file system instead. Startup registers `ui` as
a search folder through `Init_Search_Folders`, after the deployment's own
folders, so one of those can override a shipped file.

The RmlUi file interface is a thin wrapper over `CCFileClass`. Documents,
styles, images, and fonts use flat basenames, and the interface resolves
every relative reference by basename, so the same files load from a loose
`ui/` directory or from a mix. The run directory's `ui/` is added to the
`CDFileClass` search paths; the existing order then applies: user path,
current directory, search paths, mix files. A mod overrides a document by
placing a file earlier in that order or by shipping it in a mix. The `ui/`
directory on disk is a packaging convenience, not part of the lookup key.
The adapter validates sizes, reads, and seeks; RmlUi uses `size_t` where the
engine uses `int`, and a clamped seek must not look like success. A missing
required document, style, or font fails preparation with the name reported.

### Images

Images resolve by extension. PNG and TGA decode through `bimg_decode`. PCX goes through
`Read_PCX_File` with the palette the file carries. A shape frame is named
`name.shp#frame@name.pal` and decoded to RGBA with index zero transparent, as
every blitter treats it. The palette is required rather than defaulted: a
palette file holds six-bit components, which are widened to eight, and a
shape has no palette of its own, so the one that draws it correctly is a
choice each screen makes and checks against its legacy view. Surfaces the engine draws at runtime (the map preview, the
desync host icons, a progress bar) reach a document as `surface:NAME`. A screen
registers a function that hands back the surface with `UI_Surface_Register`,
and calls `UI_Surface_Invalidate` when the engine has drawn it again; that
releases the texture, so the next frame that shows it reads the surface
afresh and nothing is copied until then. No custom element was needed: an
`<img>` or an image decorator names the source like any other. Original game art stays local runtime data
outside version control; documents receive artwork identities, never engine
pointers.

### Fonts

Dialog text uses the bitmap engine for the game's glyph sheets, described under [The
dialogs' own look](#the-dialogs-own-look); every other family goes to RmlUi's FreeType
engine. The shipped face is Liberation Sans 2.1.5, in `ui/` under the SIL Open Font
License, which stands in when the sheets cannot be read. It is metric-compatible with the
Arial-class faces the legacy dialogs drew with, so a migrated screen's text keeps the
proportions of the one it replaces. The regular and bold faces are registered under the
family name `opents-sans` rather than the name inside the files, so a document does not
depend on how a face names itself and the face can be replaced without editing every
document.

RmlUi takes one font engine per process, which is why the dialog engine wraps the FreeType
one rather than replacing it. The game's other bitmap fonts, which the post-migration
sidebar view will need, can be served by the same engine.

### Strings

Engine text is UTF-8: `Fetch_String` returns it, and INI files, saves and the
lobby hold it. A screen passes it to RmlUi unchanged, and text typed into a
field goes into engine buffers unchanged. What the transition does not remove: fixed-size engine buffers, packet fields, and
file names are sized in bytes, so a field's character limit is a byte limit
and truncation never splits a sequence; and `WWFontClass` indexes glyphs by
byte, which bounds in-game text to the range the transition supports.

Documents reference strings by name: `[[TXT_OK]]`. RmlUi passes every text
node through `SystemInterface::TranslateString`, where the shell maps the name
to its identifier and inserts the resource string. The names are
`#define`s in `code/language/language.h`, so `cmake/StringNames.cmake`
generates the name table into the build's generated directory; no
hand-maintained list. A name that table does not carry is reported and left in
the text as it stands, so a misspelling is visible rather than silently empty.
Dynamic text, including player and map names and error strings, is inserted as
text, never as markup.

## Configuration

The UI has no key of its own. `[Video] LegacyDialogs` in `SUN.INI` returned
every migrated screen to its owner-draw view while those views existed, and
`UI_Use_Rml` read it at a screen's entry. Both were deleted with OwnerDraw in
step 13, before any release carried them. Nothing reads the key, so one left
in `SUN.INI` is ignored. There is no build option: RmlUi is always compiled and
linked, so one configuration matrix carries the evidence.
A sidebar view key follows the sidebar view.

## Dear ImGui

ImGui is vendored as a submodule, compiled into Debug and Release, and
rendered by a small bgfx adapter in `uirender.cpp` that reuses the same
program and view setup as the RmlUi renderer, on `VIEW_DEV`. Its platform
adapter feeds it input through the shell hook and follows the pinned
version's backend contract for texture creation and destruction. Overlays are
armed by the developer-mode flags the manual documents; tool visibility and
frame rate never touch deterministic state. The first uses are single-window
diagnostics such as frame benchmarks, network statistics, and object and
house inspectors. A player-facing feature may choose an ImGui view through
the same screen contract; it then meets the same coexistence, input, and
evidence rules as an RmlUi view. Docking, extra native viewports, and editor
architecture are separate work.

## Sidebar

The sidebar is scheduled after the Win32 dialogs are gone. Two pieces of
work exist, in order: a toolkit-neutral split of `SidebarClass` into model
and view with the gadget view as the only view, and later an RmlUi view
covering the whole sidebar column (radar, credits, power, strips, and mode
buttons) that a player selects instead of the gadget view. No presentation
bridge and no tooltip adapter are built, because no legacy dialog exists by
then to coexist with.

The current HUD crosses `GScreen`, `Map`, `Display`, `Radar`, `Power`,
`Sidebar`, `Tab`, `Scroll`, and `Mouse`. `SidebarClass` stays as a forwarding
facade for its callers: catalog additions, factory linking, scroll commands,
redraw requests from houses and buildings. The model keeps `Column[]`, the
buildable lists, `TopIndex`, the mode flags, `Serialize`, and gains explicit
actions and queries: select a slot, scroll or page a column, toggle repair,
sell, power, and waypoint modes, and read each slot's identity, cameo, name,
cost, progress, ready state, queue count, and enabled state. The logic in
`SelectClass::Action` and the button handlers moves into those actions; the
gadgets call them.

Invariants the split preserves:

- Every field `Serialize` writes stays in the model, including the scroll
  and flash animation fields, so the save format does not move. Toolkit state
  never enters a serialized class. A save does not select the view and the
  view does not change the save schema.
- Shared behavior, `StripClass::AI`'s factory changes, completion events, and
  announcements, runs at every existing eligible `Map.Input` poll, including
  the network and timer waits, whichever view is selected. It is not one
  update per simulation frame or per toolkit update.
- Only the behavior path calls `Has_Changed`. Snapshots, bindings, rendering,
  and a hidden view never consume it.
- A slot is identified by `(RTTIType, ID)` revalidated at execution, never by
  a captured index; reordering must not redirect a click, and a scenario load
  invalidates old intents even if numbers are reused.
- A dimmed cameo is not a disabled control. Draw code computes darkening
  separately from `SelectClass::Action`; a click can still announce a
  condition or enqueue a request that `HouseClass::Begin_Production` rejects.
  Rejection is not moved earlier.
- The selected view owns tooltip registration for its regions and removes the
  registrations `Reposition_Sidebar` makes for regions it covers.
- Before scenario destruction or load, pending intents and bindings are
  invalidated; after pointer fixup, the selected view is recreated from
  current state.

## Progress and other systems

Progress tracking, clamping, milestone text and sound, and the readiness
queries that `scenario.cpp` consumes move out of the draw path into shared
behavior, so a repaint cannot repeat a milestone sound and a hidden
presentation cannot lose one. The screen exposes phase, progress, status, and
the operations the loader supports; no cancellation is added to a loader that
cannot cancel. Loading stays on its thread with explicit cooperative service
points that drain nothing unrelated while scenario objects are being
replaced, and the first paint happens before long work begins.

MSEngine screens (campaign selection, briefings, score screens) are features
with animation, audio, and navigation. RmlUi can replace their layout and
controls while the existing image, animation, video, and audio services
supply content; their waits, focus pause, and callbacks stay explicit, and
replacing their timing with CSS animation is a deliberate per-screen choice.
A stable bespoke screen may stay bespoke. The message list and restate screen
can follow the screen contract when someone wants them.

## Compatibility

| Boundary | Requirement |
| --- | --- |
| Gameplay | Command meaning, eligibility, ordering, and side-effect ownership preserved; presenters use the same calls the dialog procedures use. |
| Determinism and networking | UI timing stays out of the simulation; sidebar poll placement and serialized reads unchanged; lobby and in-game screens send the same events. |
| Saves and replays | Representations and reconstruction paths unchanged; toolkit objects and view preference never enter game state. |
| Class layout and COM | Presenters and adapters stay outside layout-sensitive structures. |
| Configuration | Existing keys and defaults unchanged; new keys get owning documentation. |
| Localization | The UTF-8 transition owns the encoding change; the UI adds no conversion of its own. |
| Mods and resources | Legacy asset semantics unchanged; document paths, binding names, event names, and the styling profile are experimental until versioned with the first supported override package. |
| Build | 32-bit MSVC with the static CRT for every new dependency. |

## Dependencies

Additions, as submodules under `thirdparty/` pinned at tested tags like bgfx,
built static with the static CRT that `thirdparty/CMakeLists.txt` forces:

| Project | License | Notes |
| --- | --- | --- |
| RmlUi 6.3 | MIT | `RMLUI_FONT_ENGINE=freetype`, no samples, no backends, static. Vendored. |
| FreeType 2.13.3 | FTL | zlib, bzip2, PNG, HarfBuzz, and Brotli disabled; aliased as `Freetype::Freetype` for RmlUi's find, and the host is never probed for one. Vendored. |
| Dear ImGui | MIT | core sources compiled into a small target; no bundled backends. Not vendored until a developer overlay needs it. |

`THIRD_PARTY_NOTICES.md` names RmlUi, FreeType, and the two container
libraries RmlUi bundles. The submodules carry their own license texts, so
`thirdparty/licenses/`, which holds the texts for code vendored without a
tree, is unchanged; no packaging license copy exists in the repository yet.
CI already checks out submodules recursively. `bimg_decode` has lost
`EXCLUDE_FROM_ALL` and is linked, which is what decodes a document's images
until `uitexture.cpp` exists. The build stamp step gains the string-name
generator with the first screen that shows text. Dependency upgrades are
separate changes.

## Migration plan

Each step is one pull request unless noted, builds and runs on its own, and
leaves the game playable. A behavior-heavy screen takes two changes: the
first extracts its behavior behind the presenter with the legacy view still
selected and classifies as preserved; the second adds the RmlUi view. A leaf
takes one. Sizes are rough: S under a day of focused work, M a few days, L a
week or more. The order is bottom-up because of the coexistence rule: a
screen migrates only after every screen it can open has migrated.

No step waits on the process-code-page transition: step 3 converts a resource
string where it enters a document instead. Steps 1 and 2 need no text at all.

1. **Dependencies** (S, landed). Submodules, CMake, notices, `BUILDING.md`. No
   engine code uses them. RmlUi 6.3 and FreeType 2.13.3 are pinned under
   `thirdparty/`, built static with every optional FreeType dependency
   refused and only RmlUi's core library compiled. Dear ImGui is not vendored
   yet: nothing needs it until a developer overlay does, and an unused
   dependency was not worth carrying.

   Evidence: configures and builds on the WebAssembly target and on the macOS
   substitute. The supported Visual Studio 2022 Win32 target has not been
   built, so this is not a support claim for it. FreeType reports a damaged
   font by `longjmp`, so it is compiled and linked `-sSUPPORT_LONGJMP=wasm`
   to match the engine's `-fwasm-exceptions`; without that the WebAssembly
   link fails on `emscripten_longjmp`.
2. **Shell** (M, landed). `code/ui/` holds the context and overlay pass
   (`uishell.cpp`), the bgfx render interface (`uirender.cpp`), the system
   interface (`uisystem.cpp`), the file interface over `CCFileClass`
   (`uifile.cpp`), and the message translator (`uiwin32.cpp`). The backend
   split landed with it: `Backend_Present` no longer ends the bgfx frame,
   `Backend_End_Frame` does, `video.cpp` renders the overlay between them,
   and `code/viewid.hh` holds the view order both translation units agree on.
   `Backend_Present` also gained an `upload` flag, so a present that only a
   document asked for reuses the frame already on the device instead of
   uploading it again; `Video_Present` still always uploads, because a caller
   that asks for a present outright may have drawn without marking the frame.

   Three things differ from what this page proposed:

   - The test document is written into `uishell.cpp` rather than shipped as a
     file. That keeps a diagnostic available when the document tree is the
     thing at fault. It does carry a text label, which is what reports that
     the shipped face loaded: a probe drawing boxes and no text means it did
     not.
   - It is toggled by exported entry points rather than a developer key, so
     the harness can raise it in any phase, the title screen included, where
     `Debug_Key` does not run.
   - `UI_Tick` is called from `Call_Back` as well as from `Main_Loop`. Every
     screen's wait reaches `Call_Back`, and without it a document loaded in a
     phase with no loop of its own is never updated and so never renders,
     which is what the first run of the probe showed. Both call sites are
     guarded against re-entry.

   The shipped tree, the font, and the search-folder registration landed with
   it, so `uifile.cpp` is exercised: both faces load by basename through
   `CCFileClass` out of the embedded `ui/` directory.

   Evidence, all on the WebAssembly target through
   `tools/harness/harness.py`, against an OpenTS-Assets web tree: the probe
   renders over the Tiberian Sun main menu and over mission `GDI1A.MAP` in
   play; at a 1280x800 window with the frame following it, and at a 800x600
   frame letterboxed into a 1024x768 window, where the document's origin
   follows the frame's destination and one authored unit covers one game
   logical unit; a click on it is taken by the document and does not reach the
   menu item underneath it; a click beside it opens the legacy options dialog,
   which the probe takes none of; that dialog draws correctly with the probe
   up; and toggling the probe off leaves the frame beneath it intact. The 15
   `ctest` targets pass on the macOS substitute. Not run: the supported
   Visual Studio 2022 Win32 target, a repeated open-and-close leak check, and
   focus loss and return.
3. **Version dialog** (S, leaf, landed). The integration pilot. It brought with
   it the pieces the step needed and that later steps share: the screen
   contract (`uiscreen.h`), the RmlUi view base (`uirmlview.h`), the modal
   runner (`uirunner.cpp`), the generated string-name table, and
   `TranslateString` behind the `[[TXT_NAME]]` form. `Version_Dialog` reads
   `UI_Use_Rml` and falls back to its own dialog when the screen cannot be
   prepared.

   The screen is read only, so it has no intents of its own beyond a
   dismissal, and it collects the six lines the legacy list box held in the
   same order. The document is authored at the template's own size, 408 by 172
   logical pixels, which is what its 272 by 106 dialog units come to at MS
   Sans Serif 8.

   Two things differ from what this page proposed. The modal runner arrived
   here rather than in step 4, because the pilot needs one. And the encoding
   prerequisite is met by converting a resource string where it enters a
   document, in `TranslateString`, using the `UTF8::From_Windows_1252` the
   tree already has, rather than by the process-code-page transition; that
   transition is still worth making, but it is no longer in the way. The
   conversion was later removed: the engine's strings were already UTF-8, so
   it double-encoded every character outside ASCII.

   Evidence, on the WebAssembly target through the harness: the screen opens
   over the main menu with the six lines and the button, centred at 1280x800
   and at an 800x600 frame letterboxed into 1024x768; the button takes a
   hover; it is dismissed by clicking the button, by Enter, and by Escape, and
   each returns to the menu with the frame beneath it intact; both faces load
   by basename through `CCFileClass`. The 15 `ctest` targets pass on the macOS
   substitute. Not run: the supported Visual Studio 2022 Win32 target, focus
   loss and return, and a repeated open-and-close leak check.

   The main menu keeps hiding around the legacy dialog and does not hide
   around the RmlUi view, which the coexistence rule does not require: no
   legacy dialog is shown while the view is.
4. **Message boxes** (M, leaf, landed for the modal box). The runner landed
   with step 3, so what was left was `WWMessageBox::_Process`, which every
   `Process` forwarder goes through. The box is authored at the template's own
   390 by 137 logical pixels, and it keeps what a caller reads: the button
   index, the visual order (first, third, second, the order the slots sat in),
   the outer slots for two buttons and the middle one for a lone button,
   Enter answering with the caller's default, Escape answering as the second
   button did whether or not that button is shown, and a box with no button
   answering zero without being shown. A session that ends under the box is
   reported apart from a preparation failure, so only the second falls through
   to the dialog.

   `OwnerDraw::Custom_Message_Box` is not migrated here. It is modeless: it
   returns a window the caller keeps, updates, and reads a cancel flag from,
   which is the progress and wait shape rather than this one, so it moves to
   step 6 with the boxes it resembles.

   Evidence, on the WebAssembly target through the harness, raised through
   `WWMessageBox::_Process` so the selector is exercised too: with three
   buttons, clicking the first, second and third answers 0, 1 and 2; Enter
   answers 0; Escape answers 1. With two buttons Escape answers 1 and the
   buttons keep the outer slots; with one the button is centred and Enter
   answers 0. `--ini Video.LegacyDialogs=yes` opens the OwnerDraw dialog
   instead. Not run: the supported Visual Studio 2022 Win32 target, and the
   multiplayer cases where `Main_Loop` runs under the box.
5. **Sound** (M, landed as one change). The behavior pilot.
   `SoundControlsClass::Dialog` reads the selector. One document serves both
   templates: the music controls that only apply to a game in progress are
   dropped when `GameActive` is false, the test the dialog used to choose
   between its two templates.

   What it preserves: a volume rounds to the same ten steps, applies with
   feedback as it is moved and again without feedback when the screen is
   accepted; shuffle and repeat each turn the other off when switched on and
   leave it alone when switched off; stop queues the quiet theme; play stops
   and queues the selected one; the track list holds the themes
   `Theme::Is_Allowed` permits, numbered and timed in the same format, with
   the playing one selected; and every control is dead when there is no audio
   device. There is nothing to cancel, because a volume applies as it moves,
   so accept is the only way out, as it was.

   Two rules came out of it that every later screen needs, and that cost a
   debugging cycle each:

   - A control's new value is read from the change event, never from the bound
     variable. RmlUi raises the event before it stores the value, and the
     order between a screen's listener and the binding's own is not defined.
   - `Sync` pushes the whole model once, when the screen opens, and afterwards
     only what an intent can change. Dirtying a bound value on every pass
     writes the model's value over the one the player is dragging a control
     to, which looks exactly like a control that does not work.

   Evidence, on the WebAssembly target through the harness, raised through
   `SoundControlsClass::Dialog` so the selector is exercised: the screen opens
   with the three volumes at their stored steps and the real track list;
   clicking a volume track moves it, and the value is still there when the
   screen is accepted and opened again; shuffle and repeat turn each other
   off; `--ini Video.LegacyDialogs=yes` opens the OwnerDraw dialog, which
   chooses the same one of its two templates. Not run: the supported Visual
   Studio 2022 Win32 target, the in-game service path, and a play or stop
   heard rather than inferred.

   Because it landed as one change rather than two, the presenter was never
   exercised behind the legacy view, so nothing independently confirms the two
   views make the same calls; the list above is read from the dialog procedure
   rather than observed from both.
6. **Progress and wait** (S, leaf; the wait box has landed). The "please wait"
   box that `OwnerDraw::Custom_Message_Box` puts up is a modeless RmlUi
   document in `code/ui/uiwaitbox.cpp`. It is not run by a loop of its own, so
   it is drawn when it is shown and again when its text changes, the way the
   dialog repainted before returning; a caller that blocks in `Save_Game` has
   the box on the screen first. The callers are unchanged: `Custom_Message_Box`
   hands back a token rather than a window, and `Display_Dialog`,
   `Set_Custom_Message_Box_Text` and `End_Dialog` recognize it, so the saving
   and loading boxes in `savemgr.cpp` and `loaddlg.cpp` move without an edit to
   either. The cancel button and its flag are kept although no caller asks for
   them.

   Evidence, on the WebAssembly target through the harness: the box opens,
   changes its text, and closes through those four calls, in the dialogs'
   skin; and an in-game save and load through the proven sequence log
   `SAVING GAME ... - Complete` and `LOADING GAME ... - Complete` with the box
   routed to RmlUi. Not run: the cancel button, and the multiplayer load
   countdown in `savemgr.cpp` that drives the text.

   `IDD_PROGRESS_WAIT`'s dialog mode is the modeless box in
   `code/ui/uiprogress.cpp`. `ProgressScreenClass` keeps its interface: its
   dialog branches test a flag the box sets, since `Dialog` stays null for it,
   and `Set_Graphic_Data` no longer copies the hidden surface to the screen for
   the box, which draws nothing into the frame; in a menu that surface still
   holds old title art, and copying it put that art on the screen. The bar is
   `PROGBAR2.SHP` through `PALETTE.PAL`, the palette the normal drawer uses, cut
   at the product of its width and the fraction done, inside the template's
   group box in the frame colour. The full-screen loading mode draws onto the
   game's own surface and is not a Win32 dialog at all. Its callers, the map
   generator and the network file transfer, move with steps 12 and 11.

   Evidence: raised through `ProgressScreenClass` as the generator drives it,
   the box shows the red bar half done, and a side-by-side against the legacy
   dialog under the same calls agrees on the caption, the bar colour and cut,
   and the frame; the screen beneath is left intact.

   Both boxes can appear over a dialog that has not migrated yet, which the
   coexistence rule forbids for screens in general. They are exempt because
   they take no input, so neither can take the mouse from the dialog, and the
   overlay draws them on top, which is where the dialogs they replace were.
7. **Options family** (L, landed). Main options, display with its timed
   rollback, game controls in their three templates, keyboard with the hotkey
   capture control, the display-mode confirmation, the in-game options, and
   abort and surrender, one file each under `code/ui/`. The drivers in
   `mainopt.cpp`, `goptions.cpp`, `gamedlg.cpp` and `options.cpp` read the
   selector and keep their tails for both views: the hub closes before a child
   opens, nothing in game controls applies until it is accepted (the scroll
   method stored as 2 still comes back as 1), the in-game options queue the
   same events for the other players, and the confirmation rolls back after
   600 ticks. Key capture reads the keyboard queue while the key box has the
   focus, since the shell hands a screen only the keys it names. The display
   screen offers the interface scales from `InterfaceSizes` in `mainopt.h` on
   the browser build. Intentionally changed: the keyboard lists are sorted, as
   their templates ask and Windows does, and the keyboard dialog no longer
   reopens with an empty command list after a category it remembered.

   Evidence, on the WebAssembly target through the harness: `SUN.INI` after the
   same game controls and display changes, and `KEYBOARD.INI` after the same
   assignment, are identical to the legacy view's apart from `LegacyDialogs`;
   every button of both hubs, including save, load and delete in game with only
   one screen up at a time; abort answering 1, 3 and 2 for its three buttons and
   2 for Enter and Escape, and the real Abort restarting the mission; the
   confirmation's four answers and its timeout; and side-by-sides at 1280x800
   and letterboxed at 800x600. Not run: the WOL and network variants of the
   in-game options and game settings, the Windows display mode list, the Sound
   button without an audio device, a multiplayer game running under these
   screens, and the supported Visual Studio 2022 Win32 target.
8. **Main menu family** (M, landed). `Main_Menu`, `Select_MPlayer_Game` and
   `Select_Game_Type_Dialog` read the selector and describe their buttons to one menu
   screen in `code/ui/uimenus.cpp`: which button answers what, which are disabled (Load
   Mission when `Offer_Load` says so, Internet and World Domination always), and whether
   Enter and Escape end the menu as IDOK and IDCANCEL did. Each has its own document;
   the multiplayer one serves both templates, picking the expansion's while Firestorm is
   installed, as the driver does. The main menu keeps its loop's keys (Ctrl+V for the
   version screen with the menu hidden, Ctrl+Alt+C for the credits, the cheat codes)
   through one function both views call. `Choose_Campaign` hands the campaigns
   `Campaign_Available` allows to `code/ui/uicampaign.cpp`, which lists them, shows the
   difficulty's name beside its slider, and writes the campaign and
   `Options.Difficulty` only on OK. The `NewMenuClass` drivers keep their loops. The
   rules file choice had a dialog procedure and no template, so it never opened and the
   first rules file always won; the procedure is gone and the first file is taken
   directly.

   Evidence, on the WebAssembly target through the harness: the campaign screen from
   the graphic menu's New Campaign, with Nod and Hard picked, starts `NOD1A.MAP`; the
   plain main menu, the game type and the expansion's multiplayer menu opened through
   their drivers, with Escape and Enter ignored by the main menu, Ctrl+V showing the
   version screen and returning, and Exit Game closing it; and side-by-sides against the
   legacy dialogs that agree on placement, wallpaper, skins, disabled buttons and text
   positions to within two pixels. Not run: the base game's multiplayer template, which
   only shows without Firestorm; the plain menu as the game shows it when `GMENU.MIX` is
   missing; the credits key; and the supported Visual Studio 2022 Win32 target.
9. **Load, save, delete** (M, landed as one change). `LoadOptionsClass::Dialog`
   reads the selector in each of its three cases, and one presenter in
   `code/ui/uimission.cpp` serves all three screens, which share the list and
   the driver; each has its own document. The list is built as `Fill_List`
   builds it (the empty slot on save stamped with the current time, newest
   first up to `Scan_Limit`, the same date and time formats), the accept button
   is enabled only while the list has rows, a double click loads, and the save
   field opens with the caller's description and the caret at its end, takes a
   row's description when a row is picked, and stops at 79 characters. The save
   flow keeps its order: the empty-description box, the slot's own file or
   `Pick_Filename`, the overwrite question with No as its default, the error box
   on failure, and `Save_Confirmation`. Delete asks with No as its default and
   stays open until the list is empty. The browser build's Export and Import
   were drawn out of `Transfer_Command` into `Export_Saved_Game` and
   `Import_Saved_Game` so the screens can reach them; `Transfer_Command` calls
   the same two. File names, descriptions and the calls that write and read a
   save are unchanged.

   One behavior is intentionally changed: Enter on an empty load list opened
   from the main menu closed the legacy dialog as a success, which started a
   game with nothing loaded and left the page unresponsive. Enter now honours
   the accept button's disabled state and does nothing. Right-clicking to clear
   the selection and moving through the list with the arrow keys are not
   reproduced.

   Evidence, on the WebAssembly target through the harness: an in-game save
   and load through the proven sequence log `SAVING GAME [SAVE0000.SAV -
   Reinforce Phoenix Base harness] - Complete` and `LOADING GAME [SAVE0000.SAV]
   - Complete`, with no Win32 window up while the screens were, and the leading
   space typed into the description arriving; the empty-description box, a
   refused and an accepted overwrite, Export handing the page the file, a
   double-click load, a delete refused and accepted down to an empty list, and
   a load from the main menu after aborting a mission, at 1280x800 and at an
   800x600 frame letterboxed into 1024x768; and a side-by-side against the
   legacy dialogs that agrees on the backdrop, labels, list, rows, ellipses,
   buttons and field. Not run: Import's file picker, load and save failure, a
   full disk, the multiplayer load list, a list longer than its rows, Tab,
   paste, non-ASCII text, and the supported Visual Studio 2022 Win32 target.
10. **Skirmish and map selection** (M, two changes; skirmish has landed).
    `Skirmish_Mode_Dialog` reads the selector, and the rest of its driver runs
    unchanged for both views: the preview deleted, the settings written, the
    menu redrawn on cancel. `code/ui/uiskirmish.cpp` makes the dialog's
    initialization calls in its order (side and colour lists, the first
    scenario selected, the player and computer lists emptied, the preview
    rebuilt) and writes on accept what the dialog wrote, in the same order,
    after the waypoint check; cancel keeps only the name, side and colour.
    Short Game forces Bases on and clearing Bases clears Short Game, as the
    check boxes did. The track bars keep their ranges and steps, credits
    rounded down to 250, and show their values in the numbered trough. The map
    button runs `Scenario_Dialog` with the screen hidden. The preview reaches
    the document as `surface:skirmish-preview`, letterboxed as `Blit_Preview`
    placed it. One fix: a random-map pick with no preview at all read through a
    null pointer, and now falls back to the normal preview.

    Evidence, on the WebAssembly target through the harness: a skirmish
    started from the screen as Nod in red against two computer players with
    4750 credits logs the chosen map, houses and unit count and shows the
    credits and colour in game; the too-small-map box; the map picker hidden
    and restored, Escape there taking its cancel branch; cancel keeping the
    name, side and colour in `SUN.INI`; at 1280x800 and letterboxed at 800x600
    in 1024x768; and a side-by-side against the legacy dialog that agrees on
    layout, frames, troughs, skins and row pitch. Not run: Create Random Map,
    a session ending under the screen, and the supported Visual Studio 2022
    Win32 target. A combo box opens on a click anywhere on it, where the
    legacy one opened only on its arrow.
11. **Network lobbies** (L, two changes, landed). Host, guest, game list, the
    `WS_` stack, and `netshare.cpp` as one family; then disconnect, desync, and
    reconnect. Packets unchanged.

    The disconnect box (`code/ui/uidisconnect.cpp`) and the desync boxes
    (`code/ui/uidesync.cpp`, one document for the host's decision and the others'
    wait) are modeless, as their dialogs were: `Wait_For_Players` and
    `DesyncDialogClass::Run` open them, update them from the pass that updated the
    dialog, and close them. A box queues what its buttons ask for, and the driver
    drains it at the point its dialog procedure's command took effect, before the
    kick tally and the decision checks, so nothing that sends a packet or decides
    what the lockstep waits on moved. The bars, the countdown, the chat and player
    lists, the delayed Quit, and host promotion are updated by the same helpers,
    which branch on the box. `DesyncDialogClass` keeps whether its box is open
    in `Open`. The host icon is `key:wolhost.pcx`, the picture with the magenta
    the dialogs keyed it on left out.

    Evidence, on the WebAssembly target through the harness with two browsers in a
    LAN game through the relay: the disconnect box when one side closed, its kick
    buttons (a proposal for oneself refused; a kick of the other player sent and
    voted, the box closed and the game resumed, with log lines identical to the
    legacy dialog's), Escape returning to the lobby and Enter ignored; the desync
    boxes raised with `-DESYNCTEST=300`, Load Game disabled without saves, the
    waiting side's Quit enabled after about ten seconds, chat both ways, and the
    host's Continue closing both. Not run: the load flow and its countdown, host
    promotion, the Quit flows, a short frame, and the supported Visual Studio 2022
    Win32 target. The relay refused the engine's larger frames until its limit was
    raised to the 2048 byte receive buffer.

    The lobby screens are in `code/ui/uilobby.cpp`: the game list
    (`ui/gamelist.rml`), the host's and the guest's setup (one document,
    `ui/gameopts.rml`), the lobby's message box (`ui/netmsgbox.rml`), and the map
    picker (`ui/mapselect.rml`). The drivers in `netdlg2.cpp` and `netshare.cpp`
    keep their loops, their packets and their handlers. A screen takes a slot on
    the `WS_` stack in `windlg.cpp` under the identifier of the template it
    replaces (`code/ui/uiwsstack.h`), and a driver reads and writes each control
    by the identifier the template gave it, so every query the stack answers
    (the top screen, a screen found by its template, a wait on one) answers as it
    did for the dialog. A control reports what its owner-draw control reported,
    including the changes a dialog made itself, because the lobby's handlers act
    on those. The dialog procedures take message names of the engine's own
    (`code/lobbymsg.h`), which keep the Win32 numbers.

    A follow-up removed the lobby's window paths, so the drivers reach only the
    screens, and fixed Enter in the chat field: the view base took it as an
    accept, so the line was never sent. The screen now catches it first and hands
    the driver the text with the line break the owner-draw edit added.

    Evidence, on the WebAssembly target through the harness: a two-browser LAN
    game through the relay, from the game list and the host's and the guest's
    setup to `game`; and single-browser runs of the chat, the colour and side
    choices, and the map picker. Not run: the supported Visual Studio 2022 Win32
    target. Its side of the change was syntax-checked with clang against
    MinGW-w64 headers and has not been compiled with MSVC.
12. **Map generator and WDT** (L; the map generator has landed).
    `Do_Random_Map_Dialog` reads the selector and shares its tail with the
    dialog, so the preview written to `RandMap.img` and the seeder cleanup
    serve both. One document covers `IDD_MAPGEN`, `IDD_MAPGEN_FS` and
    `IDD_MAPGEN_WDT`, with one placement rule per control and template taken
    from their dialog units. The presenter's `Describe` and `Adopt` move the
    seed as `MapSeedClass::Set_Settings` and `Get_Settings` move it through the
    controls: the same ranges, a slider with no range shown disabled over 0 to
    100, the WDT territory's limits and its `UserMod` enabling, the environment
    and time boxes sorted by name, the hidden seed carried unchanged, four
    players under WDT, and the blue Tiberium test kept as it was. The track bars
    show their value in the numbered trough, as these did. Load, Save and
    Delete Map open the seed file screens with this document hidden, so the two
    are never up together. The generator repaints the preview at each stage;
    `Generate_Random_Map` is handed no window by the screen and asks it to
    repaint instead, and the preview reaches the document as
    `surface:mapgen-preview`, fitted to its frame.

    Evidence, on the WebAssembly target through the harness, raised through
    `Do_Random_Map_Dialog`: the screen opens with the Tiberian Sun layout, and a
    side-by-side against the legacy dialog agrees on every control, value and
    disabled state; Surprise Me repositions every setting; Preview Map runs the
    generator with the progress box over the screen and leaves the new map in
    the preview frame; Save Map hides the screen behind the seed file dialog and
    Cancel there brings it back. Not run: the Firestorm and WDT layouts, OK
    leading into a game, Load and Delete with seed files present, and the
    supported Visual Studio 2022 Win32 target. The WDT screens that are not the
    generator are not migrated.
13. **Retire OwnerDraw** (M, landed). Delete `ownrdraw.cpp`, `windlg.cpp`, the
    modeless dialog list, the dialog templates, the kill switch, and the
    coexistence assertions. String tables stay.

    First, every screen of steps 3 to 10 and 12 lost its dialog procedure and
    legacy branch, so a screen that cannot be prepared now answers as a dialog that could
    not be created did, and the rules file choice, which had no template, is gone. The
    wait box is `UIWaitBoxClass`, which its callers hold for the length of the work, and a
    caller waiting under it services the game with `UI_Service_Game`. The progress box
    keeps `ProgressScreenClass`'s interface and falls back to the full-screen presentation
    when it cannot open. Text drawn from the glyph sheets into game surfaces (the credits,
    the mission briefing, the MSEngine button captions) goes through `sheettext.cpp`,
    and the dialog artwork that the briefing's buttons use is loaded by
    `Cache_Dialog_Artwork` in `srfcache.cpp`, so neither needs OwnerDraw. The credits drew
    with GDI Arial on Windows and with the glyph sheet on the page; they now use the sheet on
    every target.

    Once step 11 had landed, the rest went: `ownrdraw.cpp` and `ownrdraw.h`, `winfix.cpp`
    and `winfix.h`, the modeless dialog list in `msgloop.cpp`, `msgroute.cpp`, the
    `[Video] LegacyDialogs` switch and `UI_Use_Rml`, `Blit_Preview`, `DSurface`'s GDI
    device context and `Is_GDI_Backed`, and the Win32 substitute's window manager,
    controls and GDI. `windlg.cpp` stays, reduced to the lobby's screen stack on
    `WSScreenHandle`. The dialog templates went with `Language.dll`, which nothing read
    any more; the strings stay, compiled in from `code/languagestrings.cpp`. Screens
    answer with `DialogResultType` from `code/dialogresult.h`, and the keyboard screen
    names hotkeys through `code/keyname.cpp`. A modal screen now frees the pointer from
    the game's capture while it is up, as the dialogs did (`UI_Begin_Modal`,
    `UI_End_Modal`), which fixed a pointer hidden over the pause menu during a scripted
    sequence. [The platform layer](PLATFORM.md#7-the-win32-substitute-removed) records the
    rest of the substitute's removal. Not run: the supported Visual Studio 2022 Win32
    target. The Windows side was syntax-checked with clang against MinGW-w64 headers and
    has not been compiled with MSVC.
14. **Sidebar** (M, then L). The model and view split with the gadget view;
    later the RmlUi view over the whole column and its selection key.

ImGui overlays (S each) can follow step 2: frame benchmarks first, then what
a developer needs next. GadgetClass screens, MSEngine screens, and the
credits are unscheduled.

## Validation and evidence

A `tests/uishell` CTest target links RmlUi core, FreeType, `uiscreen.h`, the
string table, and the screen presenters with a recording render interface
and a null system interface. It runs without game assets:

- Load every shipped document and fail on a parse error or a property
  outside the declared profile.
- Bind a presenter, drive it with `Context::ProcessMouseButtonDown` on a
  known element, and assert the queued intent and result; drive the same
  actions through the legacy adapter and assert the same ordered service
  calls.
- Reject stale intents after close, suspension, scenario generation change,
  and catalog removal.
- Scan shipped documents for `[[TXT_*]]` names and check each exists in the
  generated table.
- Round-trip the coordinate mapping at integer and fractional scales, with
  letterboxing, resize, outside input, and captured release.

Runtime evidence stays per pull request, as `CONTRIBUTING.md` requires: the
screen exercised in single player, skirmish, and a two-instance LAN game
where it can appear during play, at a native and a scaled resolution, with
repeated open and close, focus loss and return, keyboard-only navigation,
session end while open, and no leakage of wheel, edge scroll, or shortcuts
underneath. Editable screens add non-ASCII input, paste, and composition.
Capture paths that read software surfaces omit the overlay; capture after
composition or state the limitation. No performance target is asserted
before measurement; idle CPU, update time, submission cost, and texture and
geometry memory are recorded on an agreed baseline before defaults change.

## Documentation

- This page owns the architecture and is updated as steps land.
- `docs/BUILDING.md` lists the new submodules.
- `THIRD_PARTY_NOTICES.md` and the packaging license copy gain the three
  projects.
- The manual gains a systems page for the UI files (where they live, the
  override order, the string reference syntax) and a change record per
  migrated screen. The sidebar page changes when its view key lands.

## Open decisions

- The document and binding versioning rules for mods, fixed with the first
  supported override package.
