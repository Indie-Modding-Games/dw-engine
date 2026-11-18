# Lua API

What a mod can do from Lua: its entry script, the game events it listens to and the functions the game publishes to
it. How it is built inside (sandbox, budgets, function and event registry) is in
[script](../modules/engine/script.md).

> **Version 1.4.0 (2026-11-07).** The mods' entry script, the game events and their versioned tables (below) arrive,
> and `api` becomes required in the packs of the `mods` and `user` layers. Nothing that already existed changes: a mod
> written for 1.3 keeps working if it declares `api: "1.x"` or `api: "1.3"`.

## The entry script

If a pack brings `scripts/main.lua`, the game runs it once per game, when starting a new one or loading one, in the
pack's environment and before launching `game.new_game` or `game.loaded`. It is the place to register the event
handlers:

```lua
-- scripts/main.lua of my_mod
local days = 0

hooks.on("clock.new_day", function(t)
  days = days + 1
  print("day " .. t.day .. " of year " .. t.year .. "; I have counted " .. days)
end)
```

- Entry scripts run in stack order, from the lowest pack to the highest.
- Each pack's environment is its own: its globals are strict (everything with `local`), `print` writes to the log as
  `mod <pack>: ...` and the API tables are read-only ([environments](../modules/engine/script.md#mod-environments)).
- If the script fails, the error goes to the log with the pack's name and the game goes on.
- What the script keeps in Lua variables does not pass into the saved game: that is what the story triggers and
  variables are for ([capacity](story.md#capacity)).

## Game events

`hooks.on(event, function)` registers a handler. Each handler receives **a table** with the event's fields and
`version`, the table's version: a new field raises the API's minor version and does not change `version`; removing or
changing one raises both. Each handler receives its own copy of the table, so changing it does not affect the others.
An event the game does not publish is an error of `hooks.on`.

| Event | When |
|---|---|
| `field.map_entered` | The player is on a map: when the game starts (`previous` is nil) and after each map change |
| `field.map_left` | The player leaves a map for another (`next`), right before the new one's `field.map_entered` |
| `battle.started` | A battle starts. `kind`: `normal`, `tournament`, `arena` or `versus`; `enemies`, how many opponents; `first_enemy`, the species id of the first; `scripted`, whether the story opens it |
| `battle.ended` | A battle ends and its rewards were already given. `outcome`: `victory`, `fled` or `defeat`; `recruit`, the resident who joins the town, if there is one |
| `evolution.evolved` | The partner evolves from one species (`from`) to another (`to`), by id |
| `clock.new_day` | A new day starts (the partner's midnight) |
| `inventory.item_used` | The player uses an item on the partner; `context` is `field` (in battle it is not launched yet) |
| `game.new_game` | A new game starts with the partner `partner` |
| `game.loaded` | A game was loaded and the game goes on |

A pack's handlers have one budget per event and another per frame; one that fails goes to the log and the others go on
([budgets](../modules/engine/script.md#a-scripts-budgets-for-mod-authors)).

## Published functions

The game publishes in the story VM the `story` table, the one for event scripts: a mod uses it with the same names
([story API](story.md)). The ones that wait (show a text, move a character) can only be called from a story section,
which the engine advances frame by frame; from an event handler, the call fails. Reading and changing triggers and
variables (`story.trigger`, `story.set_trigger`, `story.var`, `story.set_var`…) works anywhere:

```lua
hooks.on("battle.ended", function(t)
  if t.won then
    -- 300: an extra variable that the mod's ruleset adds (story.md, "Capacity")
    story.set_var(300, story.var(300) + 1)
  end
end)
```

## Generated reference

This part is written by the test `the Lua API page lists every game event and story function the game publishes`
(`packages/session/tests/game_hooks_tests.cpp`) from the code, and it fails if it does not match: with
`DWE_WRITE_API_DOC=1` it rewrites it.

<!-- api-lua:generated -->
API version: **1.4.0**. Each event's table: version 1.

| Event | Table fields, besides `version` |
|---|---|
| `field.map_entered` | `map` (string), `previous` (string or nil) |
| `field.map_left` | `map` (string), `next` (string) |
| `battle.started` | `kind` (string), `map` (string), `enemies` (integer), `first_enemy` (string or nil), `scripted` (boolean) |
| `battle.ended` | `kind` (string), `outcome` (string), `won` (boolean), `recruit` (string or nil) |
| `evolution.evolved` | `from` (string), `to` (string) |
| `clock.new_day` | `year` (integer), `day` (integer) |
| `inventory.item_used` | `item` (string), `context` (string) |
| `game.new_game` | `partner` (string) |
| `game.loaded` | only `version` |

`story` functions (53):

- `story.ENDED`
- `story.PARTNER`
- `story.PLAYER`
- `story.add_to_var`
- `story.cancel_closes_choice`
- `story.copy_var`
- `story.delay`
- `story.entity_look_at_entity`
- `story.entity_move_to`
- `story.entity_move_to_axis`
- `story.entity_move_to_axis_with_camera`
- `story.entity_move_to_entity`
- `story.entity_move_to_entity_with_camera`
- `story.entity_move_to_with_camera`
- `story.entity_set_rotation`
- `story.entity_walk_to`
- `story.entity_walk_to_entity`
- `story.entity_walk_to_entity_with_camera`
- `story.entity_walk_to_with_camera`
- `story.fadeout_hud`
- `story.jump_to_file`
- `story.load_species_model`
- `story.move_camera_to`
- `story.move_camera_to_entity`
- `story.move_object_to`
- `story.play_animation`
- `story.play_sound`
- `story.reduce_var`
- `story.reset_entity_origin`
- `story.rotate_3d_object`
- `story.section_on_exit`
- `story.set_autotalk`
- `story.set_bgm`
- `story.set_dialog_owner`
- `story.set_script`
- `story.set_selection`
- `story.set_textbox_mode`
- `story.set_textbox_origin`
- `story.set_textbox_size`
- `story.set_trigger`
- `story.set_var`
- `story.show_textbox`
- `story.spawn_npc`
- `story.stop_bgm`
- `story.store_random`
- `story.teleport`
- `story.trigger`
- `story.unload_entity`
- `story.unload_species_model`
- `story.unset_trigger`
- `story.var`
- `story.wait_for_entity`
- `story.warp_to`

<!-- /api-lua:generated -->
