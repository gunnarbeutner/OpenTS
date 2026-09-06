---
format_id: mp4-video
title: MP4 video
summary: Stores browser-playable H.264 video and MPEG AAC audio for a WebAssembly movie build.
kind: binary
extensions:
  - .MP4
role: video
related:
  - { type: format, id: mix }
  - { type: format, id: vqa }
source_files:
  - CMakeLists.txt
  - code/movieformat.cpp
  - code/movies.cpp
  - code/mp4.cpp
---

An MP4 movie carries H.264 video and MPEG AAC audio in an ISO base media file. It is used only by a WebAssembly build configured with `OPENTS_MOVIE_FORMAT=MP4`; VQA remains the default and the Win32 player. The MP4 player covers the same callers: full-screen movies, radar transmissions, menu backdrops, menu-item movies, and World Domination Tour map backdrops.

## Names and archives

The build selects one four-character extension for every movie name. An MP4 build asks only for `.MP4`, even where inherited code supplies a `.VQA` name, and a VQA build asks only for `.VQA`. It never probes the other extension. A game-data image made for one format therefore passes over every movie when run with the other build, instead of handing one container to the wrong player.

The member still lives in an ordinary [MIX archive](/formats/mix/). The archive format and member lookup are unchanged; only the member name and its contents differ. A loose `.MP4` is also accepted because this player reads through the ordinary file layer.

Names registered in `[Movies]` remain extensionless. The campaign introductions are `INTR0.MP4` and `INTR1.MP4`; when the numbered name is absent, `INTRO.MP4` is tried. Startup and score movies keep their existing base names with `.MP4` in place of `.VQA`.

## Browser playback

A movie read out of an archive is copied into memory and exposed to the page as a `video/mp4` Blob; a movie the browser build's `manifest.json` names as a file of its own is handed to the page by URL, so the browser fetches and caches it itself. Either way the browser demuxes the container, decodes H.264 and AAC, and keeps their clocks synchronized. Each available video frame is drawn to an off-screen canvas, converted to RGB565, and copied into the same engine surface the VQA player uses. Placement, stretching, clipping, menu composition, and radar composition therefore stay in the engine rather than moving into the page.

The media element carries `playsinline`, so an iPhone does not replace the game with its native full-screen player. Safari, Chrome, and Firefox may block script-started media with sound. When that happens the movie starts muted, so the picture runs from the first frame, and the page's sound control asks separately for the gesture that turns the AAC track back on. Where even a muted start is refused, the game waits behind a **Tap to play movie** control placed over the page.

A browser that cannot decode the tracks rejects the movie. No software H.264 or AAC decoder is linked into the engine, so the asset pipeline must produce profiles supported by the browsers in the deployment.

## Memory use

This player does not stream the MP4 member into a Media Source buffer. Opening a movie temporarily holds the whole compressed member in WebAssembly memory while copying it into the Blob, and the Blob then remains until the movie handle is destroyed. This leaves MIX and ISO reading unchanged, at the cost of memory proportional to the largest compressed movie.
