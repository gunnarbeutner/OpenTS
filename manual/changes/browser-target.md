---
title: Run the engine in a browser
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:profile-capture
  effect: added
credit: [Gunnar Beutner]
---

The engine runs in a web browser as a WebAssembly build. The target is unsupported and in
progress; the repository's `docs/BUILDING.md` owns how it is built, what it needs, and what
has been observed of it. Windows builds are unchanged.

A player opens a page. The game fills the browser window and changes resolution with it,
its text and dialogs are built into the program rather than read from `Language.dll`, and
its game data is fetched over HTTP as it is asked for and kept in the browser's own storage,
so a later visit reads what it already holds. The page shows a **Tap for sound** control
until the browser has the gesture it needs to make a sound, and a session whose
initialization fails reports why with the message the Windows build shows.

`-PGOCAPTURE` makes a browser run fetch archives only as they are read, which is how a
fetch profile of a session is recorded for later visits to load from.
