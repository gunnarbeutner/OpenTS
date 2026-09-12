---
title: Stop shipping Language.dll
category: internal
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

OpenTS read its text from `Language.dll`, which the release zip carried beside `Game.exe`. The text is now compiled into the executable: packages no longer carry the library, a run directory does not need it, and a copy beside the executable, including a localized or edited one, is ignored.
