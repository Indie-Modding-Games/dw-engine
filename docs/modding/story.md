# Story API

The original game's event scripts reach the engine as Lua modules that `dwx extract` writes in `story/<n>.lua`
([format](../formats/scripts.md#lua)). Each script command is a call to the `story` table. This page says how those
functions are named and the keys of their texts, which is what a mod sees and can replace.

> Status: the game publishes `story` in its story VM and `LuaApiVersion` is **1.4.0**: a mod uses it from its entry
> script and its events ([Lua API](lua-api.md)), and the triggers and variables hold as many as the ruleset says
> ([capacity](#capacity)).
>
> **CONTRACT CHANGE (2026-10-23).** `story.play_sound(slot, sound)` plays the slot's sound (0 and 1, the system
> banks; 8, the map's) and goes on without waiting; `story.set_bgm(source)` sets the source's music (it restarts it
> even if it is the same; `0xFF` stops it) and `story.stop_bgm(operand)` stops it
> ([audio](../architecture/audio.md#64-effects-in-detail)). Before they were markers that made no sound; with
> them `LuaApiVersion` goes from 1.2.0 to **1.3.0**.

## Names

A `story` function is named like the command in the listing (`story.show_textbox`, `story.play_sound`), except those in
this table, which the API renames with the engine's vocabulary (species, partner, NPC, variable). The YAML listing
(`script/dg/<n>.yaml`) keeps the original names: it is the contract with `dw1fe` and does not change.

| Listing command | `story` function | What it does |
|---|---|---|
| `store_digimon_type` | `store_partner_species` | Stores the partner's species in a variable |
| `store_digimon_value` | `store_species_type` | Stores in a variable the type (`type` field of `data/digimon`) of the species held in another |
| `load_digimon` | `load_species_model` | Loads a species' model |
| `set_digimon` | `spawn_npc` | The NPC of a map slot appears, if it is of that species |
| `call_digimon_routine` | `call_partner_routine` | Runs a fixed routine of the partner |
| `unload_digimon` | `unload_species_model` | Unloads a species' model |
| `set_pstat` | `set_var` | Gives a value to a story variable |
| `add_to_pstat` | `add_to_var` | Adds to a story variable |
| `reduce_pstat` | `reduce_var` | Subtracts from a story variable |
| `copy_pstat` | `copy_var` | Copies a story variable into another |

The operands are renamed the same way: the `digimon` operand is called `species` and the `pstat` operand `var`, in the
Lua comments.

The entities that are not NPCs are `story` constants: `story.PLAYER` (the player's character, `tamer` in the listing)
and `story.PARTNER` (the partner).

The table lives in a single place, `packages/formats/src/script/story_api_names.cpp`, and the words that no generated
name may contain, in `ForbiddenNameWords` (`packages/formats/include/dwe/formats/script/story_api_names.hpp`). A test
goes through all the commands and their operands, and the converter and disc tests look at each generated module.

## Text keys

A `show_textbox` or a `set_selection` does not carry its text in the Lua: it carries a key, and the text is in
`text/<language>/story_<n>.yaml`.

- The key is `story_<n>.text.<offset>`: `<n>` is the script id with three digits (`000` for `MAPHEAD.SCN`, which the
  game loads as script 0; `001`–`221` for those of `DG.SCN`, the same number as the `script_id` of a map's NPCs) and
  `<offset>` that of the command in the listing, with four hexadecimal digits. Example: `story_049.text.04c0`.
- In the text file the key goes without the file prefix: `text.04c0: "..."`.
- `dwx extract` writes the English (`text/en/`). A language without the key uses the English.

## Patches per section

To change an event you do not need to copy the whole `story/<n>.lua`. A pack brings `story/patches/<name>.lua` (the
name is free) that returns the script it patches and its sections:

```lua
-- story/patches/open_door.lua of a mod
return {
  script = 49,
  sections = {
    -- Replaces section 0x51 of script 49.
    [0x51] = function()
      story.set_trigger(203)
      return story.ENDED
    end,
    -- Wraps 0x52: does something first and lets the original go on.
    [0x52] = function(original)
      story.set_var(5, 1)
      return original()
    end,
  },
}
```

- Each function receives `original`, the section it replaces (or `nil` if the script does not have it: a patch can
  also add new sections). The sections the patch does not name remain those of the script.
- `return original()` lets the original run whole, waits and jumps included. To do something **after**, keep what it
  returns and return it at the end: `local r = original(); story.set_var(5, 2); return r`. That way a
  `return story.ENDED` from the original still ends the script.
- A section returns `story.ENDED` (ends the script) or nothing (returns to whoever called it), like those of the
  script.
- The patch runs in its pack's environment (the pack's sandbox and memory quota) with the same `story` API.
- If two packs patch the same section, the one higher in the stack wins (layer and priority of the `pack.yaml`) and
  the log says so. `original` is always the script's section, not that of the patch that lost.
- A patch that does not load (Lua that does not compile, no `script`, a section outside 0–65535) is left out with an
  error in the log and the game uses the script's section. `dwx validate` checks that the Lua compiles.
- The section numbers are those of the script: in the generated Lua they appear as `[0x0051] = routines.at_...`.
- With the game open and the [control channel](../guides/remote-control.md#live-map-editing), `reload_map`
  makes the modules and the patches be read again from the packs at the next section that runs: a new patch or an
  edited `story/<n>.lua` applies without restarting, and the triggers, the variables and the session stay the same.
  It is not reloaded while a section is running.
- `dwx script-warps` ([script warps](../architecture/script-warps.md#from-the-lua)) applies the patches the same way
  as the game before extracting the map changes, so a patch's `story.warp_to` reaches `field/script_warps`. The walk
  only follows what it can read: `original()` as in a subroutine (`return original()`, or
  `if original() == ENDED then return ENDED end` and carry on), not stored in a variable (`local r = original()`). A
  patch like that runs fine in the game, but `dwx script-warps` fails and names the patch's file and its line.
- A patch does not see the module's `routines`: it runs in its own environment. If a section needs shared code from the
  script (the `return routines.at_0100()` of the generated Lua), the patch brings its copy as a private routine: at
  the top, `local routines = {}` and then `function routines.<name>() ... end` (without parameters and without
  `original`). Inside the patch, `routines.<name>` is always its own; one the patch does not define is an error of
  `dwx script-warps` with its line (in the game it would fail when run). The sections the patch does not touch keep
  using the module's. This is how the map editor saves an edited section:

  ```lua
  local routines = {}

  function routines.at_0100()
    story.set_var(5, 2)
    return story.ENDED
  end

  return {
    script = 900,
    sections = {
      [0x51] = function(original)
        story.set_trigger(73)
        return routines.at_0100()
      end,
    },
  }
  ```

## Edit events with the mapper

The [map editor](../modules/apps/mapper.md#events) writes these patches for you:

1. Open the content and the map, choose the mod pack in "File → Target" and turn on the **Events** layer (key 0). The
   "Events" panel lists what the map does on entering, on stepping or pressing on each trigger cell and on talking
   to each NPC, with the section that runs and the patch that provides it, if any.
2. A click on an event (in the panel or on the canvas) opens its section. In "Structure", choose a line and change its
   fields: a dialog's text, a trigger's number, an `if`'s condition. The palette adds commands and the "Lua" tab
   lets you write the section by hand. Ctrl+Z undoes.
3. Ctrl+S saves `story/patches/<pack>_<script>.lua` with the changed sections (and the routines they use, as private
   routines) and the text lines in the pack's `text/<language>`. If the pack already had a patch for that script,
   that one is edited.
4. With the game open and connected ([Live](../modules/apps/mapper.md#live)), "Test" puts the game's story in the
   state the section needs, reloads the map, takes the player to the event and shows what changed.

If the change adds or moves a `story.warp_to`, extract `field/script_warps` again with `dwx script-warps` (the editor
does it by itself if it knows where `dwx` is, in "File → Settings…"). Before publishing, run `dwx validate` on the mod.

## Edit scenes with the timeline

A scene (the player walks, the camera follows, someone talks) is better edited in time than line by line. In the
section editor, "Timeline" opens the [section as tracks](../modules/apps/mapper.md#sequences): one per character, the
camera, the texts, the sound, the waits and the rest of the state.

1. Move the cursor along the ruler to see each one on the map where they would be at that frame (20 per second).
   Striped blocks are estimates: the editor does not know how long the player takes to read a text or how long an
   animation lasts.
2. Change the scene: drag the diamond at the end of a movement on the map to change where it goes, the edge of a
   `delay` to lengthen it, "Before" and "After" to change the order, or add commands at the cursor (right click on the
   map: "Walk here", "Camera here"). Remember that walking does not wait: if what follows has to wait for it to
   arrive, a `wait_for_entity` is needed.
3. Ctrl+Z undoes each step; Ctrl+S saves the section in the pack's patch, like any other edit.
4. With the game connected, "Play" starts the saved section and "Play from here" starts it at the chosen command, with
   everyone placed where they would be, saving nothing. Pause and "+1 frame" to go slowly; "Follow the game" takes the
   cursor to the command it is at. If the drift between the game and the model is large, look at the estimated block
   from before.

## Names of triggers and variables

The story works with numbers (triggers 0–799, variables 0–255). `story/names.yaml` of the `game` pack gives a label to
those the engine uses for something (medals, items on the player's page, music…), so that the tools show them by name
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/story_names.schema.json)):

```yaml
triggers:
  364: medal_00
variables:
  245: music_mode
```

A mod can bring its own `story/names.yaml` (it wins by ID, whole). The names change nothing in the game.

## Capacity

The original has 800 triggers (0–799) and 256 one-byte variables (0–255), and marks each species as "raised" with the
trigger `0x200 + number`, only for the first 63. A ruleset extends all that in its `story` section, with `capacity`
(the original adds nothing):

```yaml
capacity:
  extra_triggers: 64           # triggers 800–863
  extra_variables: 4           # variables 256–259
  extra_variable_max: 100000   # the highest value of an extra variable (the original's stay at variable_max)
  extra_raised:                # "raised" for the species from the 63rd on
    first_trigger: 800         # species 63 uses 800, 64 uses 801…
    species: 32                # how many species it covers; must fit in the extra triggers
```

- What is added is numbered after the original's, so the original's numbers do not change. The story commands
  (`set_var`, `add_to_var`, `copy_var`…) limit each variable to its range: 0–`variable_max` for the original's and
  0–`extra_variable_max` for the extra ones.
- Limits: 65,536 extra triggers and 4,096 extra variables. `extra_raised` must stay within the extra triggers; a
  species outside all the ranges is still marked separately, as before.
- **Saved games.** The save's `story` section is at version 2: `triggers` holds all the triggers (bytes in
  hexadecimal) and `extra_variables` the extra variables. A version 1 game loads unchanged. If it is loaded with a
  smaller ruleset (without the mod that enlarged the capacity), what does not fit is discarded, with a warning in the
  log when something was set.
