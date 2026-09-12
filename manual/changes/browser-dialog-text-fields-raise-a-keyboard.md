---
title: A dialog's text field raises a keyboard
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

On a device whose only keyboard is drawn on its screen, the browser build raises that
keyboard for a text field that takes the focus, such as the name in skirmish and
multiplayer setup, and for the hall of fame name a mission's score screen waits on, and
puts it away when the focus moves on or the wait ends. Neither could be typed before, so a
campaign ended at the score screen. Characters the keyboard reports without a key press
reach the field too.

A click made with Ctrl, Alt or Shift held also keeps its key however quickly the key is
released, so a forced attack, forced move or added selection no longer turns into a plain
move order. A target with a real keyboard is unchanged.
