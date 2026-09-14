---
title: Draw the mission briefing at the window's own resolution
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

The mission restatement, the page that prints the briefing before a mission with no briefing
film and that the in-game Briefing button brings back, was drawn at 640 by 400 and magnified
to the window, so its text was soft at every size above that. It is now laid out and drawn at
the window's resolution over the same plate: the text is typeset rather than stretched, and it
reflows when the window changes. The briefing still prints a line at a time with its sound,
still pauses with a "-- More --" control between pages, and still offers Resume Mission and,
where the scenario has a briefing film, Video. Space and escape do what they did: they turn the
page while one is waiting, and resume the mission once the briefing has finished. The text is
set in the interface face the game ships rather than the menu font, whose letters are pictures
and exist at one size only.
