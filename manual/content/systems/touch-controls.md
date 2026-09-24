---
title: Touch controls
summary: "Reads a finger as a mouse button in the browser build: one finger is the left button, holding it is the right button, two fingers move the view, and a scroll moves whatever it is over. Over the battlefield one finger carries the view instead, and resting one selects."
category: interface-controls
keys: []
related:
  - type: system
    id: sidebar
  - type: using
    id: build-and-run
---

The browser build reads a touch screen as well as a mouse, and a device with both — an iPad
with a mouse attached — answers to either at any moment. Nothing here changes what a mouse
does. Which button does what, edge scrolling, and hovering are the same on every target.

## The gestures

Two rules decide every gesture off the battlefield. One finger is the left button; two
fingers are the view.

| Gesture | What it is |
| --- | --- |
| Tap | A left click where the finger landed |
| Press and hold | A right click where the finger landed, in a mission |
| Drag one finger | The left button held and carried |
| Drag two fingers | The map panned one for one under them |
| Tap with two fingers | A right click, kept as a second way to reach it |

A hold becomes the right click after roughly half a second, and it happens while the finger is
still down, so what it cancels goes away under the finger rather than after it is lifted. A
finger that travels more than about ten screen pixels before then is a drag instead, and can
no longer become either a tap or a hold.

Over the tactical view the last two rows of that table read differently, because a single
contact is all a stylus has and a band drawn with one covers the ground it is choosing from:

| Gesture over the battlefield | What it is |
| --- | --- |
| Drag one finger | The map panned one for one under it |
| Press and hold | A selection grown from where the finger rests |

Everywhere else the engine already knows what a button means — the sidebar, the tab bar, a
dialog and a menu — so a gesture is handed over as the button it stands for and the engine
answers as it would to a mouse. That is what makes one finger dragged across a dialog's
slider a slider drag, and across a cameo nothing at all.

## Selecting by resting a finger

A rest over the battlefield tints the ground around it and takes what stands there when the
finger lifts. How long the rest takes to arm depends on what it is over: about a fifth of a
second with something selectable under it, about a third with something within five cells,
and about half a second with nothing within reach at all. That last case cannot select
anything, so it only clears the selection, and the finger is left free to carry the view
away rather than committed to a selection it cannot make.

Once armed, the tinted ground widens on its own for as long as the finger stays down, at
about two and a half cells a second, so a large group is a longer rest rather than a longer
reach. Moving the finger takes the size over: from then on the reach is the ground between
where the rest began and where the finger is, and it stops growing by itself. Holding shift
adds what the rest takes to the selection instead of replacing it.

The tint marks whole cells, and a unit is taken when the cell it stands on is one of them.

## A finger that rests on a control

A control that a mouse can hold down shows itself pressed for as long as the button is down,
and that is how a player checks what they are about to trigger. A finger gets the same: it
presses a dialog's button as it lands, the button draws itself pressed for as long as the
finger stays on it, and the lift is what triggers it. Sliding off the button before lifting
raises it again and triggers nothing, exactly as dragging a mouse off it does.

A shell screen's menu item has no pressed picture of its own — it lights the item the pointer
is over — and it lights under a resting finger from the moment the finger lands. It answers
the lift in the same way a dialog's button does: sliding off the item puts its light out and
lifting there picks nothing, and sliding back on lights it again and picks it.

Nothing the game draws for itself works that way, because there a press is already the action:
a sidebar cameo starts building on the press, and the battlefield anchors a selection band on
it. Those get the click whole, when the finger lifts.

## What a hold reaches

The right button is the game's cancel, and it cancels in one order wherever it is used: a
building waiting to be placed first, then repair mode, sell mode, power mode, superweapon
targeting and waypoint mode. With nothing left to cancel it deselects everything. A hold
over the battlefield selects rather than cancels, so a touch player backs out of what they
are doing with the two-finger tap, and deselects with either that or the bar below.

Holding a finger on a sidebar cameo is the same right click the desktop uses there: it puts
production on hold, and holding again abandons it and refunds the money.

A hold is the right button only where a right button means something, which is a mission on
screen and not a control that took the press as the finger landed. On a shell screen, and on
a dialog's own controls, a hold is a tap that is taking its time: resting there and lifting
does what tapping does, rather than leaving the control with nothing to answer.

## The bar in the corner

A row of square buttons sits in the bottom left during a mission. It is the way to the
control groups for a player who has no number keys, and every button on it does what a key
already does.

| Button | What it does |
| --- | --- |
| Base | Goes to the base, and back where it came from when pressed again |
| A numbered group | Selects it; pressed again, takes the view to it |
| A numbered group, held | Stores the selection as that group |
| Add | Adds the selection to that group |
| A dashed slot | Stores the selection as a group that is free |
| Clear | Deselects everything |

The bar only offers what would change something. Adding is offered for a group that does not
already hold the whole selection, and storing is withheld while the selection is the whole of
some group, because a unit carries one group and storing it elsewhere would empty that one
without saying so. Going back from the base counts as going back from anywhere within about
half a screen of where the jump landed, so looking around the base does not cost the way back
but leaving it does.

## Nothing hovers

A mouse leaves a pointer wherever it stops, and a good deal of the interface reads that
resting position rather than a click: the tooltip that appears over terrain or a cameo, the
map scrolling when the pointer rests against an edge, and the placement cursor following the
pointer while a structure waits to be put down. A finger leaves nothing behind, so on touch
none of those fire, and the position of the last tap is never mistaken for somewhere the
player is pointing.

Placing a structure works differently for that reason. There is no pointer to carry the
outline about, so the first tap moves it to the tapped spot and the second tap puts the
building there. The player sees where it will land, and whether the game will allow it,
before it lands, and a hold cancels the placement outright.

## Movies and typing

A movie has no use for a button, so any touch during one stops the movie, exactly as the
escape key does. A movie the mission does not allow to be skipped still cannot be.

The hall of fame asks for a name at the end of a mission and ends only when Return is typed.
On a device whose only keyboard is drawn on its screen, the page asks for that keyboard while
the game is waiting for the name and puts it away afterwards. A phone or tablet will not raise
its keyboard except in answer to a gesture, so if it does not appear by itself, one tap
anywhere brings it up.

A text field in a dialog — the name in skirmish and multiplayer setup is the one most players
meet — asks for the same keyboard, for as long as the field holds the focus. Tapping the field
is what gives it the focus, and the keyboard goes away when the focus moves to another control
or the dialog closes. The same rule about a gesture applies: if the keyboard does not appear
with the tap that reached the field, the next one brings it up.

What the keyboard produces goes into the field whichever way it reports it. A keyboard that
corrects or completes a word delivers the result without a key press behind it, and those
characters reach the field as typed ones do.

## Scrolling

A trackpad's two-finger swipe is not a touch at all — a browser reports it as a scroll, the
same kind of event a wheel produces — so it is read where the pointer is rather than as a
gesture. Over the battlefield a scroll moves the map, in the direction and by the distance the
fingers or the wheel travelled, sideways as well as up and down. Anywhere else it moves the
sidebar's build lists one row per notch's worth, which is what a wheel has always done here.

The map takes the travel whole rather than a notch at a time, so the ground keeps up with the
fingers. There is no momentum of the game's own; what a device sends after the fingers lift is
carried like any other scroll, because a page is not told which events those are.

## Panning during a scripted sequence

Two fingers move the map directly rather than through the input the engine drops while it is
driving the camera, so the pan is suppressed for as long as the game has taken control of the
player — a scripted sequence in a mission, or an open in-game dialog. A mouse cannot scroll
then either.
