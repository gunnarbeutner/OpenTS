---
title: Patrol without a target in reach
category: fix
release: 0.2.0
targets: []
credit: [Gunnar Beutner]
---

A patrolling infantry unit, vehicle or aircraft no longer dereferences a missing target.
The patrol mission scans the area for the greatest threat while it looks for a target,
engages one, and returns to its patrol cell, and it used the scan's answer without checking
that anything had been found; an aircraft with ammunition took the same path. A
straight-flying projectile fired at something more than 200 leptons above or below the
weapon also no longer reads a structure's height through a target that is not there. A
patrol that finds a target behaves as before.
