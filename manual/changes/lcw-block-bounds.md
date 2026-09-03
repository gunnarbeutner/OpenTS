---
title: Stop an LCW block that does not decompress to the size it claims
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A compressed block in map or saved game data whose contents run out early, or whose codes
reach outside the block, ends the data rather than driving the decompressor past the
buffers on either side. The blocks before the damage are kept and nothing after it is read,
so a truncated or tampered file can no longer overrun a buffer. Well formed data reads back
unchanged.
