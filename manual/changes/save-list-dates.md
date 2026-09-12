---
title: List saved games with the same date and time on every build
category: feature
release: 0.2.0
targets:
- type: format
  id: save-games
  effect: changed
credit: [Gunnar Beutner]
---

The load and save lists show when a game was saved as a date such as `09/11/26` and a
24-hour time such as `18:10`, in local time, on every build. The Windows build showed the
short date and time of the player's regional settings, and the browser build those of the
browser's language, whose four-digit year was cut off in the date column.
