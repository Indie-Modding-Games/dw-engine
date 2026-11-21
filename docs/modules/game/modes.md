# modes

The game modes (`dwe::game::modes`, `game` layer): which games the player can start. A mode joins a way of
playing with the rulesets it admits and, optionally, the options its games fix; the game brings two, the
[game variants](../../architecture/game-variants.md) **Classic Enhanced** and **Folder Mode**, and a mod adds its own
with a manifest. It depends on `core` and `assets`; it draws nothing.

The original has no modes: its new game is always the same story. Modes are there so that the player picks the
original's rules or the expanded game, and so that a mod can bring, for example, a battle arena without the story
(card #111).

## The manifest

Each mode is a `modes/<id>/` folder of a pack with a `mode.yaml` (schema `content/schemas/mode.schema.json`,
which `dwx validate` checks):

```yaml
id: classic                           # the folder's name
name: modes.classic_name              # localization key for the name
description: modes.classic_description
ruleset: original                     # the ruleset it starts with
rulesets: [original]                  # the ones it admits; includes ruleset
order: 0                              # position in the list (then the id)
options:                              # optional: option name -> the value its games fix
  difficulty: normal
  rare_items: false
```

All the keys but `options` are required and no others are accepted. `options` is read into `GameMode::options`
(`OptionPin`: option and value as text); the game applies it (`apps/game/src/mode_options.hpp`). A pack on top replaces a mode by giving the same folder,
like any asset.

## Usage

```cpp
auto catalog = dwe::game::modes::ModeCatalog::load(files);   // the modes of the mounted packs
if (catalog.asksMode()) { /* more than one: the new game asks */ }
const dwe::game::modes::GameMode* mode = catalog.defaultMode();   // folder, or the first
```

- **Catalog**: `ModeCatalog::load` reads each `modes/<folder>/mode`, checks that each of the mode's rulesets exists
  (`rules/<id>/ruleset`) and leaves out, with its reason in the log, the mode that cannot be read: a broken mod does not stop
  the game from starting. It sorts by `order` and then by id.
- **New game**: the title screen (`apps/game/src/title_screen.cpp`, `NewGameModes`) shows the
  `ui/new_game_mode` list only if there is more than one mode; with one (a mod that replaces both), nothing is asked. Then,
  the rules screen (`ui/new_game_rules`) appears only for a mode that offers the original's rules and its hidden
  content (`original` and `oculto` in `rulesets`); another mode starts with its `ruleset`. The test routes
  that do not ask for rules do not ask for a mode either: they play Folder Mode (`DefaultMode`) with the command
  line's `--ruleset`.
- **Saving**: the session saves the mode in the `mode` section (`id`) and in the header's summary (format 4); a game
  saved before modes does not have it, and one saved before the variants has `original`: both load as `folder`
  (`currentModeId`, `session::ModeSave`). `GameSession::mode()` returns it.

## Pending

- A mode still does not choose which systems run (an arena without a field, for example): the session always mounts those of the
  story. It waits for the arena mode's design (#118).
- The list shows the first six modes; it does not paginate.
- There is no command-line option to choose a mode (`--mode`).
