---
title: Skip or choose the startup movies
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:no-intro
  effect: added
- type: command
  id: launch:play-movie
  effect: added
credit: [Gunnar Beutner]
---

`-NOINTRO` skips the startup sequence's movies and goes straight to the main menu.
`-PLAYMOVIE=<name>` plays the named movie in place of the startup sequence and then
continues to the menu; the name has no extension, as in `-PLAYMOVIE=INTR0`. Without either
option the startup sequence is unchanged.
