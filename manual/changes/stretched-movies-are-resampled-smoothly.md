---
title: Resample a stretched movie smoothly
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A full screen movie stretched past its own size is resampled with a smooth filter instead of
pixel doubled, which takes out the speckle of the movies' sixteen bit dither and softens the
compression's block edges. [`StretchMovies`](/keys/stretchmovies/) now takes effect on every
display and fits the movie in both directions; it used to rely on a Windows drawing call, and
without one the movie played at its own size in a corner of the screen. Graphic menu pages are
resampled with a sharper filter that keeps their lettering. A movie played at its own size is
copied and unchanged.
