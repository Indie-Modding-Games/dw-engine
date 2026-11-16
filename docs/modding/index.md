# Modding

A mod is a **pack**: a folder with a `pack.yaml` and its files, which the game mounts on top of its own. With a pack
you can replace any asset (a menu, a texture, a text), add entries to the tables (a species, a map, an item), change
the rules with a ruleset, add a game mode or an interface skin and react to what happens in the game with Lua
scripts. None of this touches the engine: the game starts even if a mod is broken, and leaves it out with the reason
in the log.

The game ships nothing from the original disc: each player extracts their own ([play](../getting-started/play.md)).
A mod must not hand out data, images or texts from the original either; it brings only its own
([asset policy](../adr/0011-asset-legal-policy.md)).

## Pages

| Page | What it covers |
|---|---|
| [Your first mod](first-mod.md) | Step by step: a pack with a script that reacts to the game |
| [Packs and ids](packs.md) | The `pack.yaml`, the layers, the asset and content ids, replacing and adding |
| [Lua API](lua-api.md) | The entry script, a mod's environment, the game events and the published functions |
| [Game modes](modes.md) | Add a mode with its `mode.yaml` |
| [Skins](skins.md) | Change the interface style with your own skin |
| [Interfaces](ui-addons.md) | Install or make a community interface that changes the game's screens |
| [Interface API](ui-api.md) | What each screen reads from the game and sends to it: settings, commands and options |
| [Story API](story.md) | The event scripts, their patches per section and the capacity of triggers and variables |
| [Edit a map](maps.md) and [add one](new-maps.md) | The mapper |

## What to know first

- **API version.** A mod declares in its `pack.yaml` the Lua API version it is written for (`api: "1.x"` or
  `api: "1.4"`). It is required in the `mods` and `user` layers: a mod with scripts that does not declare it is left
  out (a data-only one is still mounted, with a warning).
- **Ids.** Everything is named by id, never by number: assets by their path (`ui/hud/document`) and what a pack adds
  by `<pack id>:<name>` ([packs and ids](packs.md)).
- **Saved games.** A saved game stores its ruleset and its mode. If it is loaded without the mod that added something
  (triggers, variables, species), what no longer fits is discarded with a warning in the log and the game goes on.
- **Budgets.** Each Lua call has an instruction and time budget; a runaway script is stopped without hanging the game
  ([script](../modules/engine/script.md#a-scripts-budgets-for-mod-authors)).
- **Checking.** `dwx validate` checks a pack (its YAML schemas, API, layer) without starting the game
  ([dwx validate](../architecture/assets-pipeline.md#dwx-validate)).
