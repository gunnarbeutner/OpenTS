---
title: Carry the scenario file in the save game
category: feature
release: 0.2.0
targets:
- type: format
  id: save-games
  effect: changed
- type: system
  id: campaign-progression
  effect: changed
- type: format
  id: spawn-ini
  effect: changed
credit:
- ZivDero
---

A save now carries the scenario file it was played from, and a restart or the replay after a
loss reads the mission from that copy rather than from disk. A mission resumed through the
CnCNet client can be restarted again; the client replaces `spawnmap.ini` with a stub on
resume, and the restart read the stub and crashed. A file edited on disk during a mission no
longer changes a restart; a fresh start picks it up.

Saves from earlier development snapshots of this cycle no longer load, because the scenario
record grew by the file.
