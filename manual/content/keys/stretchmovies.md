---
key: StretchMovies
summary: Scales a full screen movie to fit the display instead of playing it at its own size.
see_also: [ScreenWidth, ScreenHeight]
when_omitted:
  kind: value
  value: "no"
---

The flag reaches the full screen movie player alone, and only where that player's caller also asks for stretching. A movie played into a fixed rectangle — the sidebar's, and the one a graphical menu plays its backdrop into — never consults the flag and keeps that rectangle either way, because the artwork drawn over such a movie is placed against that rectangle at its own size.

A stretched movie keeps its shape. It grows by whichever of the two axes runs out first and sits centered in what is left over, so a 640 by 400 movie on a 1920 by 1080 display plays at 1728 by 1080 with a black band down each side. Where the two shapes match, as at 1280 by 800, the movie reaches every edge. The flag takes effect on every display; there is no display that refuses it.

The screen is cleared ahead of any full screen movie that will not cover the display, stretched or not, whether or not its caller asked for a clear. The bands around such a movie are therefore black rather than whatever the display last held.

The picture is resampled smoothly as it grows, which takes out the fine speckle that the movies' sixteen bit color carries and softens the edges of the compression's own blocks. A movie left at its own size is copied rather than resampled and looks exactly as it always did.

The browser build omits it as `yes` rather than `no`, because the frame there is the browser window rather than a resolution anyone picked. Writing the key still decides it there as it does anywhere.

The display options screen carries the same switch and stores it as the screen is accepted; leaving the options screen behind it writes the setting back to `sun.ini`.
