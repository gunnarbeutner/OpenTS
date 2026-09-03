---
key: ScreenWidth
scope: client-settings-2
label: Width the display opens at
see_also: [ScreenHeight, WindowWidth, Fullscreen]
when_omitted:
  kind: computed
  note: Both dimensions become 640 by 480 when either is left out.
---

This is the earlier of the two reads of the assignment, made before the main window exists. Either dimension left out or written as `-1` replaces both with 640 by 480, so a width alone is never enough to size the screen. The main window is then created at the resulting size, and the game reports a video error and stops if the renderer cannot start.

The size also fixes the layout the game is drawn into: the sidebar takes 168 pixels of the width for each step of [`UIScale`](/keys/uiscale/), and the tactical view is given what is left.

The WebAssembly build renders into the browser window rather than into a resolution anyone chose, and the size read here is replaced by the window's own before the display is brought up. It is honoured there only when the page asks for it, with `?display=scaled`; `?display=WIDTHxHEIGHT` overrides it with the size the page named.
