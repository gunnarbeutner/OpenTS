---
title: Skip mission briefings
category: feature
release: 0.2.0
targets:
- type: command
  id: launch:no-briefing
  effect: added
credit: [Gunnar Beutner]
---

`-NOBRIEFING` starts every mission without its intro, briefing and action movies and
without the mission restatement, which is what an unattended `-SCENARIO=` launch needs to
reach the map. Without it a mission starts as before.
