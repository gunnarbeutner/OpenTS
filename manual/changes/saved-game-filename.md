---
title: Save a game under the name the save dialog picked for it
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Saving into an empty slot writes the file under the `SAVE####.SAV` name the dialog chose.
The name lived in a buffer that had gone out of scope before the save ran, so the game
could be written under whatever was left there; a file named that way is not found by the
search for saved games, which is why Load Game stayed unavailable after a save.

The description, house, scenario and version a save carries are also written at their true
length. They were measured in characters of the wrong width and stored short and
unterminated. A save written that way still loads, and the file format is unchanged in
either direction.
