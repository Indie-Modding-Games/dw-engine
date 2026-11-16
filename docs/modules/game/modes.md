# modes

The game modes (`dwe::game::modes`, `game` layer): which games the player can start. A mode joins a way of
playing with the ruleset it starts with; the game brings only one, **Original story**, and a mod adds its own with
a manifest. It depends on `core` and `assets`; it draws nothing.

The original has no modes: its new game is always the same story. Modes are there so that a mod can bring, for
example, a battle arena without the story (card #111).

## The manifest

Each mode is a `modes/<id>/` folder of a pack with a `mode.yaml` (schema `content/schemas/mode.schema.json`,
which `dwx validate` checks):

```yaml
id: original                          # the folder's name
name: modes.original_name             # localization key for the name
description: modes.original_description
ruleset: original                     # the ruleset it starts with
rulesets: [original, oculto, oculto_mega, todas, todas_oculto, todas_oculto_mega]   # the ones it admits; includes ruleset
order: 0                              # position in the list (then the id)
```

All the keys are required and no others are accepted. A pack on top replaces a mode by giving the same folder,
like any asset.

## Usage

```cpp
auto catalog = dwe::game::modes::ModeCatalog::load(files);   // the modes of the mounted packs
if (catalog.asksMode()) { /* more than one: the new game asks */ }
const dwe::game::modes::GameMode* mode = catalog.defaultMode();   // original, or the first
```

- **Catalog**: `ModeCatalog::load` reads each `modes/<folder>/mode`, checks that each of the mode's rulesets exists
  (`rules/<id>/ruleset`) and leaves out, with its reason in the log, the mode that cannot be read: a broken mod does not stop
  the game from starting. It sorts by `order` and then by id.
- **New game**: the title screen (`apps/game/src/title_screen.cpp`, `NewGameModes`) shows the
  `ui/new_game_mode` list only if there is more than one mode; with one, which is what the game brings, nothing changes. Then,
  the rules screen (`ui/new_game_rules`) appears only for a mode that offers the original's rules and its hidden
  content (`original` and `oculto` in `rulesets`); another mode starts with its `ruleset`. The test routes
  that do not ask for rules do not ask for a mode either: they play the original story with the command line's
  `--ruleset`.
- **Saving**: the session saves the mode in the `mode` section (`id`); a game saved before modes does not have it
  and is loaded as `original` (`session::ModeSave`). `GameSession::mode()` returns it.

## Pending

- A mode still does not choose which systems run (an arena without a field, for example): the session always mounts those of the
  story. It waits for the arena mode's design (#118).
- The list shows the first six modes; it does not paginate.
- There is no command-line option to choose a mode (`--mode`).
