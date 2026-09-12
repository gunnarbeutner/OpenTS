---
title: Play MP4 movies in the browser build
category: feature
release: 0.2.0
targets:
- type: format
  id: mp4-video
  effect: added
- type: command
  id: fixed:skip-vqa
  effect: changed
credit: [Gunnar Beutner]
---

A WebAssembly build configured with `OPENTS_MOVIE_FORMAT=MP4` plays `.MP4` movies carrying
H.264 video and AAC audio, decoded by the browser and drawn into the engine's own surfaces,
so full screen movies, radar transmissions, menu animations and World Domination Tour
backdrops keep their placement. Such a build asks only for `.MP4` members and the default
VQA build only for `.VQA`; pairing a build with the other asset set skips its movies as
missing files. [MP4 video](/formats/mp4-video/) owns the naming, where a movie may be
served from, and what happens when a browser will not start a movie with sound. The MIX
archive format is unchanged.
