# Game modes

A mode is a way of playing that the player chooses when starting a new game, with the ruleset it starts with. The game
ships two, the [game variants](../architecture/game-variants.md) **Classic Enhanced** and **Folder Mode**; a mod adds
another with a `modes/<id>/` folder and its `mode.yaml`. With only one mode mounted the game asks nothing; with two or
more, the new game shows the list before the rules. The saved game stores its mode. The module's details are in [modes](../modules/game/modes.md).

## The manifest

`modes/arena/mode.yaml` of a `my_mod` mod:

```yaml
id: arena                         # same as the folder name
name: my_mod.arena_name           # keys in localization/<language>/my_mod.yaml
description: my_mod.arena_description
ruleset: my_mod_arena             # the ruleset it starts with (rules/my_mod_arena/ruleset.yaml)
rulesets: [my_mod_arena]          # the ones the mode accepts; includes ruleset
order: 10                         # position in the list: Classic Enhanced is 0, Folder Mode 1
options:                          # optional: options its games fix at one value
  difficulty: normal
```

All keys but `options` are required and no others are accepted. The schema is `content/schemas/mode.schema.json`; the `game` pack
already declares it for `modes/*/mode`, so `dwx validate` also checks those of mods.

- **Ruleset.** Each ruleset in `rulesets` must exist in the mounted packs; if not, the mode is left out with the
  reason in the log. A mod ruleset usually extends the original and changes only its own parts
  ([rulesets](../architecture/rulesets.md)).
- **Rules.** The new game's rules screen (hidden content and Mega step) only shows for a mode that accepts `original`
  and `oculto`; any other mode starts directly with its `ruleset`.
- **Fixed options.** `options` maps option names (the settings' keys) to the value the mode's games play with, whatever
  the player chose; the options page shows them locked. Only the options a game applies from its own settings can be
  fixed (the rules and play options of the game tab); another one, or a value the option cannot take, is left out with
  the reason in the log ([fixed options](../architecture/game-variants.md#fixed-options-while-playing)).
- **Replace.** A pack on top with the same folder (`modes/folder/mode.yaml`) replaces the mode, like any asset; so a
  mod can, for example, admit its own ruleset in Folder Mode.
- **Saving.** The saved game stores the mode id; a game from before the variants (mode `original`, or none) loads as
  `folder`.

## What cannot be done yet

- A mode does not choose which systems run: the game always mounts those of the story (field, battle, care…). A mode
  without a field, like a pure arena, waits for the arena mode's design.
- The list shows the first six modes (by `order`).
