---
title: The movie escape key at both player loops
category: internal
release: 0.2.0
targets:
- type: command
  id: fixed:skip-vqa
  effect: changed
credit: [Gunnar Beutner]
---

The Escape key that stops a movie is now catalogued at the VQA player's own loop as well
as at the skip handler, so the control's sites name every place the key is read. The key
and its behaviour are unchanged.
