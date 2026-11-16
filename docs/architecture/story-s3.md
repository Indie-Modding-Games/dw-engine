# Story S3: port note

Status: **done, in `main`** (it landed after S2 4b). Parent design: [story interpreter](story-interpreter.md) (§2 "The runtime", §3 "State", §6 "S2 cut 4:
decisions").

Scope of S3 (BACKLOG HIST-S3): loading the map through `MAPHEAD`, `set_script` and `0xFE`, the step-on and confirm
tiles, `warp_to` and `teleport` with their continuations, and the `story` save section. It is a port: each behavior
comes from the decomp function (`~/code/dw_decomp/src/main`) that the table cites, translated to C++26 inside dwe's
architecture.

## Function → module

| Behavior | Original (`file:line`) | Destination in dwe |
|---|---|---|
| Starting a section: pointer, owner of dialogue `0xFD`, save variable 0, clear the 22 movement slots, "not frozen" mark | `script_interp.c:2244 callScriptSection` | `StoryRunner::startSection`: variable 0's value and the "frozen" state become runner state |
| Loading a map: the `MAPHEAD` section with the map's number runs **immediately** | `script_interp.c:2235 runMapHeadScript`, `map.c:5331 changeMap` (5397) | `StoryAdapter::mapEntered(map, arrivalSection)` → `StoryRunner::request({0, map}, Now)`, called from `apps/game` (`story_field_link.cpp`) on every map change (walking, scripted, on load) |
| `set_script` (0xFB): sets the map's script, pushes the "map loaded" continuation and continues the section | `script_interp.c:2833 scriptInstructionFBtoFF` (0xFB) | `StoryFlow::setScript` command in `packages/game/story`; the runner keeps the current map's script (`CURRENT_SCRIPT_ID`). The app already loaded the map: if the command's map is not the one just entered, an error is logged (on the disc, 255 of 255 match) |
| End of section: pop continuations; type 3 → runs `0xFE` of the map's script if it exists; type 4 → runs the saved section on the next frame, or with `0xFF` the end of `MAPHEAD`, which keeps popping; empty stack → end of script | `script_interp.c:2684 returnFromScriptFile`, `2181 setMapHeadActive` | `StoryRunner::endScript` becomes "pop": `std::vector<Continuation>` with `MapLoaded` and `RunSection{optional<section>}`. The stack is still not emptied between scripts (an original quirk) |
| A section failing (Lua error, budget) | — (does not exist in the original) | ends as `end_section`: pops and runs the continuations (§5 of the parent design) |
| At the end of the script: close boxes, restore variable 0 and, if it was frozen, give back control, camera and clock | `script_interp.c:390 tickScript` (634–642) | `StoryRunner` → port `FieldControlPort::release()`;  |
| Freeze: control, camera and clock off; variable 0 = 3; **always removes trigger 44** | `script_interp.c:2726 scriptPauseGame` | `StoryRunner::freeze()` → `FieldControlPort::freeze()`;  |
| `warp_to` (0x4B): freezes, decides whether the name is shown, pushes `RunSection(arrival)` and cuts the section | `script_interp.c:1402` (in `1297 scriptInstruction46to58`), `860 consumeMapChangeShowName` | wait command that closes the coroutine: new wait `MapChangeWait{screen, exit, arrival}` in `section_host.hpp`. The name rule (variable 6: 0 = "yes except in an event section", 1 yes, 2 no; then it is set to 0) in `packages/game/story` |
| `teleport` (0x58): screen and exit from variables `n` and `n+1`, with no arrival section | `script_interp.c:1534` | same as `warp_to`, `arrival = none` |
| `section_on_exit` (0x7B): pushes `RunSection(s)` and restores variable 0 | `script_interp.c:2151` (in `scriptInstruction64to7E`) | `StoryFlow::sectionOnExit` command |
| Scripted map change: fade of 20, name on frame 10, `changeMap` on 20, entry fade of 20 that **overlaps** with the arrival section; if there is no script anymore, control is given back; then `checkMapInteraction` | `script_interp.c:405–412`, `625–630`; `map.c:5277 scriptTickChangeMap` | `Field::startScriptedMapChange(MapChange, showName)` reuses the walking warp's fade without the exit step; the app enters the map as with any other change and calls `mapEntered`; the runner waits in a "changing map" state until that call |
| Tiles 51–79: the section with the same number **on every frame** that the player is on top and there is no script; 80–109: when pressing confirm; 120 with the partner's condition 8: `MAPHEAD 0x4E2` | `tamer.c:660 checkMapInteraction` (684–712), called in `tamer.c:588` | `Field::cellSections()` reports the sections the player's cell asks for (level, not edge) only with the player moving freely; `StoryAdapter` (`FrameCells`) requests them once per original frame with `NextFrame`; busy = lost (`RequestOutcome::Dropped`) |
| Walking warp: 20 + `changeMap` + 20, then `checkMapInteraction` | `tamer.c:1367 tamerTickChangeMap` | already ported in `Field`; S3 only added the call to `mapEntered` |
| New game: `MAPHEAD 0xCC` | `main.c:3455` | `story_scenes.new_game.chain` (`rules/<id>/story_scenes.yaml`): `original` runs `MAPHEAD 0xCC` and the opening; `seeded` starts with `data/new_game_story` and the start of `field/warps` |
| Loading a game: `runMapHeadScript(saved map)` with no continuation and then positions | `main.c:3465`, `3483`, `3791 initializeLoadedMap` | the session enters the saved place and calls `mapEntered`; the coroutine is not saved |
| Saving: 0x17C bytes of `ScriptState` (35 variables), variable 0 saved as 0 if it is 3 | `main_menu.c:5071 writeSavegame` (5080, 5120), `5016 loadSavegame` (5058) | done by S2 4b. S3 added Q10 of the parent design: with a section running, `StoryAdapter` saves the state from **before** that section (a copy when it starts) |
| Previous screen and exit (`PREVIOUS_SCREEN`, its per-map settings, saved in the game) | `map.c:5331 changeMap` (5336–5394), `main_menu.c:5048–5051`, `5110–5113` | `screen_history` (`packages/game/story`) and the ruleset's `previous_screen` table |
| Global entries (`0x4DD` item 22, `0x4DE` death, `0x4D9`/`0x4DA` tournament, `0x4E4` fishing); talking (`checkMapInteraction` 667–681, `overworld.c:1318`, `1333`) | `item.c:2263`, `partner_impl.c:1912`, `1943`, `tournament.c:97`, `116`, `script_common.c:3059` | each one with the system that requests it (see [story interpreter](story-interpreter.md), §6) |

New ports in `packages/game/story` (implemented by `packages/session`; `packages/game/story` still does not include
`packages/game/*` or `packages/engine/script`): `FieldControlPort` (freezing and releasing control, camera and clock)
and `MapChangePort` (starting a scripted map change). The map change constants (20, 10, 20) are already field rules
(`FieldRules`); the runner does not duplicate them.

## Names published in `story`

They were published in S3, each in the stretch in which it works (MOD-VISIBLE rule): `set_script`, `section_on_exit`,
`warp_to`, `teleport`. They are neutral and the emitter writes them that way (`warp_to`/`teleport` as
`return story.…`, `set_script` as a call that continues). `LuaApiVersion` went up one minor version.
`story.request` is not published (it is for mods: phase 5).

## CONTRACT CHANGES

1. **`story` Lua API:** four new names, `LuaApiVersion` +1 minor (additive).
2. **`field/script_warps`:** when the story carries the map's script it stops being the warps' mechanism and stays
   as a static graph (free exploration, minimap, reachability analysis; `Field::staticWarps`). The schema and the
   YAML do not change (contract with `dw1fe` intact).
3. **Saving:** the shape of the `story` section does not change (the coroutine is not saved). Its meaning changes with
   a section running (state from before the section, Q10). On load the `MAPHEAD` chain runs.
4. **Internal interface** `SectionHost::Wait` gains `MapChangeWait`: it is not a contract with mods or with data.
