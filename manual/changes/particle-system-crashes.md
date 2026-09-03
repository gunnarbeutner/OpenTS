---
title: Keep particle systems from reading through missing objects and particles
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

Three particle system paths no longer read through a pointer that is not there. A smoke
system with no source object, whether it rises from a building or its source has been
destroyed, tests for the source before asking what kind of object it is. A spark or railgun
system whose type names no particle to hold spawns nothing instead of using the particle it
failed to create. A system being destroyed takes every particle it holds with it; the
teardown loop skipped every second particle and read past the last one, so half of them
leaked. Where systems sit and what they give off are unchanged.
