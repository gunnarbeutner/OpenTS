---
title: A menu item is picked when the button is let go over it
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

An item on a graphic shell menu, which covers the game chooser, both main menus, and the
side choice in the World Domination Tour, is chosen when the button is let go over the item
it was pressed on. It used to be chosen the moment the button went down, so there was no
way to back out, and on a touch screen the choice was made under the finger. Sliding off
the item takes the highlight away and picks nothing, and sliding back on picks it again. A
plain click or tap is unchanged, and so is the keyboard: Return picks whatever the pointer
is over and an item's shortcut key picks it outright.
