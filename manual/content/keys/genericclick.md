---
key: GenericClick
summary: Sound acknowledging a repair or sell click on a structure, and a click on a dialog control.
see_also: [GenericBeep, ScoldSound, SellSound]
when_omitted:
  kind: value
  value: none
---

```ini title="rules.ini"
[AudioVisual]
GenericClick=BUTTON1 ; a sound ID registered in SOUND.INI
```

Two very different sets of consumers share the setting.

## In the game world

Clicking the repair cursor on a structure plays it at that structure's position, whichever way the toggle went — on for a damaged structure, and off again. Turning repair on for a structure already at full strength is the exception: that request still turns repair on, but it takes [`ScoldSound`](/keys/scoldsound/) instead and puts up no wrench. Clicking the sell cursor on a structure with build-up artwork plays it at full volume rather than from the structure's position, again for both directions of the toggle.

Both are gated on the structure belonging to a player-controlled house, so the same actions taken by a computer house — auto-repair, an AI selling off a structure, a trigger action — are silent. The sidebar itself never plays it.

## In dialogs

The menus and dialogs play it without a position when the player presses a button or moves a track bar to another step. The sound options' volume bars are silent. Pressing a check box or a drop-down list, or picking an item from the drop-down, plays it too. Clicking an entry in a list plays it whether or not the selection changes, and in the save, load, and delete lists a click anywhere in the list does. The mission restatement screen's "More" button plays it on the frame it first draws pressed.
