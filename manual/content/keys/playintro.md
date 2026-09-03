---
key: PlayIntro
summary: Controls the one-time `EVA` startup movie.
when_omitted:
  kind: value
  value: "yes"
---

When enabled, this setting selects the first-time startup path. The game writes
`PlayIntro=no` back to `sun.ini` before playing `EVA` with the build-selected movie extension, so a later start skips
that movie unless the setting is enabled again.

[`FROMINSTALL`](/using/command-line/from-install/) selects the same path without
reading this setting. The other startup movies are not gated by `PlayIntro`.
