---
title: Magnify the in-game interface on its own
category: feature
release: 0.2.0
targets:
- type: key
  id: UIScale
  effect: added
credit:
- Gunnar Beutner
---

The sidebar can be drawn larger without shrinking the battlefield. [`UIScale`](/keys/uiscale/)
in `sun.ini` sets how many screen pixels each pixel of the interface artwork becomes, and
left out it follows the screen. The playfield keeps the screen's resolution and gives up the
width the wider sidebar takes. A saved game does not carry the scale. In the browser build,
display options offers this size in place of a list of resolutions.

The graphic shell is magnified separately. The game chooser, both main menus, and the loading
and title screens fill the largest rectangle of their own shape the window holds, and stay
filled while a settings window is over them. The score, briefing, map selection and similar
screens keep the size they were drawn at.
