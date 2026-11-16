# Port coverage

Which decomp functions the engine already has, system by system, to close the gaps before the player finds them while
playing. Each function in the decomp index is in one of four states:

- **Ported:** the engine code does it. It almost always names it in a comment or in a log (`[port:...]`); if not, a
  manual entry says where it is.
- **Partial:** the engine does a part; the note says what is missing.
- **Missing:** the engine does not do it.
- **Not applicable:** the engine replaces it with its own thing and it does not need to be ported: graphics, sound,
  disc and memory of the original hardware, boxes and cursors of its interface, reading of script operands (which
  become Lua) and the copies of the battle code carried by the tournament, versus and second gym overlays.

## How it is done

`scripts/port_coverage.py` reads the decomp's function index (`functions.tsv`, outside the repository; by default
`../decomp-index/functions.tsv`, or `--index`) and looks for each name in the engine code (`packages/`, `apps/`,
`tests/`, `content/packs/`). What only extraction and the tools name (`apps/dwx`, `packages/formats/`) counts as
missing: they name the original's functions to find their tables. A citation that says something is not yet done
("not drawn yet", "not ported", `TODO`) leaves the function as partial. Citations by line only (`partner.c:906`) are
not used: the index does not have static functions and the line often falls in the neighboring one.

After that it applies `docs/reference/port-coverage.toml`: the system of each decomp file, the "not applicable" rules
and the manual decisions per function (state, note, place in the engine and what it does). It writes the part of this
page that goes between the two marks; the rest is edited by hand.

```sh
python3 scripts/port_coverage.py            # rewrites the generated part
python3 scripts/port_coverage.py --check    # exits with 1 if the page is not up to date
python3 scripts/port_coverage.py --list     # one row per function, for review
```

Limits: that the engine names a function does not prove that it does all of it, and a function that it does without
naming it shows up as missing until a manual entry corrects it. The manual entries for fishing, death and rebirth,
evolution, stones, videos, tournaments and versus come from reading the body of each function; they should be
reviewed when porting those systems.

## Gaps the player will notice

With the whole story playable (2026-11-06), what is left to port is little. Ordered by how soon it appears in a
normal game:

| # | Gap | Decomp | Engine |
|---:|---|---|---|
| 1 | The two-player versus is already playable, online and in the Arena ([versus](../modules/game/versus.md)), but without the title's combat demo or part of the original's scene (the 60 `vs/` functions that are missing and 4 half done). It goes to phase 5 | `vs/` | `packages/game/versus` |

## Other gaps that are not a function of the index

- **Script slides against characters:** `wouldTouchCharacter` (the engine's, `apps/game/src/story_stage.cpp`) only
  looks at the player and the companion, so a script slide can go through NPCs
  ([field](../modules/game/field.md#positions)).
- **Companion scenes without a scene of their own:** the companion's routine 5 (the opening one, which only the
  debug-room script calls) ends at once; a routine screen that the app does not have closes at once and the log says
  so (`apps/game/src/story_scene_glue.cpp`).
- **31-day year when training:** the original advances the day with a 31-day year after training; the engine uses the
  clock, with 30. Decided that way ([partner](../modules/game/partner.md#parity-with-the-original)).

<!-- port-coverage:begin (scripts/port_coverage.py writes this part) -->

## Summary by system

Only the functions to port count (ported + partial + missing); those tied to the console are left out. Coverage = ported + half of partial, over the functions to port.

| System | Decomp files | To port | Ported | Partial | Missing | Coverage |
|---|---|---:|---:|---:|---:|---:|
| [Battle](#battle) | `btl/`, `main/` (9 files) | 155 | 155 | 0 | 0 | 100 % |
| [Battle effects](#battle-effects) | `btl/`, `main/` (6 files) | 240 | 240 | 0 | 0 | 100 % |
| [Field: movement, collision and camera](#field-movement-collision-and-camera) | `main/` (5 files) | 82 | 82 | 0 | 0 | 100 % |
| [NPCs and map objects](#npcs-and-map-objects) | `main/` (8 files) | 80 | 80 | 0 | 0 | 100 % |
| [Partner: AI and care](#partner-ai-and-care) | `main/partner.c`, `main/partner_impl.c` | 49 | 49 | 0 | 0 | 100 % |
| [Script and interpreter](#script-and-interpreter) | `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c` | 97 | 97 | 0 | 0 | 100 % |
| [Menus and interface](#menus-and-interface) | `main/` (8 files) | 79 | 79 | 0 | 0 | 100 % |
| [Items and shops](#items-and-shops) | `main/item.c`, `shop/script_menu.c` | 96 | 96 | 0 | 0 | 100 % |
| [Evolution](#evolution) | `evl/evl.c`, `main/evl.c`, `main/evolution.c` | 29 | 29 | 0 | 0 | 100 % |
| [Training](#training) | `trn/`, `trn2/` (11 files) | 38 | 37 | 1 | 0 | 99 % |
| [Fishing](#fishing) | `fish/fish.c`, `fish/fish_model.c`, `main/fish.c` | 95 | 95 | 0 | 0 | 100 % |
| [Death and rebirth](#death-and-rebirth) | `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c` | 40 | 40 | 0 | 0 | 100 % |
| [Saving and loading](#saving-and-loading) | `main/main_menu.c` | 12 | 12 | 0 | 0 | 100 % |
| [Tournaments](#tournaments) | `dget/`, `main/`, `std/` (6 files) | 113 | 113 | 0 | 0 | 100 % |
| [Versus and demo](#versus-and-demo) | `main/`, `vs/` (10 files) | 105 | 41 | 4 | 60 | 41 % |
| [Curling minigame](#curling-minigame) | `kar/kar.c`, `main/kar.c` | 44 | 44 | 0 | 0 | 100 % |
| [Audio](#audio) | `main/sound.c`, `main/sound_async.c` | 21 | 21 | 0 | 0 | 100 % |
| [Videos and ending](#videos-and-ending) | `eab/eab.c`, `endi/endi.c`, `mov/mov.c` | 30 | 30 | 0 | 0 | 100 % |
| [Core and platform](#core-and-platform) | `main/` (11 files) | 51 | 51 | 0 | 0 | 100 % |
| **Total** | 1456 functions to port | 1456 | 1391 | 5 | 60 | 96 % |

## Battle

The normal fight: commands, AI, damage, status effects, HUD, start and end, learning moves.

Decomp files: `btl/battle_hud.c`, `btl/battle_main.c`, `btl/battle_setup.c`, `btl/command_menu.c`, `btl/command_shout.c`, `main/battle_tick.c`, `main/battle_ui.c`, `main/btl.c`, `main/entity_text.c`.

Ported 155, partial 0, missing 0.

## Battle effects

The effect machine of the moves (EFE scripts): projectiles, hits, camera, particles, flashes.

Decomp files: `btl/battle_effect.c`, `main/buff_model.c`, `main/efe.c`, `main/efe_table.c`, `main/effect_common.c`, `main/particle.c`.

Ported 240, partial 0, missing 0.

## Field: movement, collision and camera

The player on the map, the collision grid, warps, the partner following and the camera.

Decomp files: `main/aabb.c`, `main/map.c`, `main/map_collision.c`, `main/tamer.c`, `main/vecmath.c`.

Ported 82, partial 0, missing 0.

## NPCs and map objects

The characters on the map and their walking AI, chests, objects on the ground and scenery with logic.

Decomp files: `main/anim.c`, `main/bubble.c`, `main/butterfly.c`, `main/drop_shadow.c`, `main/map_object.c`, `main/model.c`, `main/overworld.c`, `main/world_object.c`.

Ported 80, partial 0, missing 0.

## Partner: AI and care

Hunger, sleep, bathroom, sickness, happiness and discipline, poop, natural death, walking and commands.

Decomp files: `main/partner.c`, `main/partner_impl.c`.

Ported 49, partial 0, missing 0.

## Script and interpreter

The event script machine: commands, values, text boxes and choices.

Decomp files: `main/script_common.c`, `main/script_interp.c`, `main/script_textbox.c`, `main/script_value.c`.

Ported 97, partial 0, missing 0.

## Menus and interface

Title, game menu, partner sheet, bag, interface boxes, fades and text.

Decomp files: `main/fade.c`, `main/font.c`, `main/game_menu.c`, `main/inventory.c`, `main/main_menu.c`, `main/new_game.c`, `main/sjis.c`, `main/ui.c`.

Ported 79, partial 0, missing 0.

## Items and shops

Using items, food and chips, shops, cards, the wardrobe and the script menus of the shops overlay.

Decomp files: `main/item.c`, `shop/script_menu.c`.

Ported 96, partial 0, missing 0.

## Evolution

Choosing the destination and the evolution scene.

Decomp files: `evl/evl.c`, `main/evl.c`, `main/evolution.c`.

Ported 29, partial 0, missing 0.

## Training

The gym: slot machine, courses and rewards.

Decomp files: `trn/trn_brain.c`, `trn/trn_hud.c`, `trn/trn_off.c`, `trn/trn_reward.c`, `trn/trn_slots.c`, `trn/trn_speed.c`, `trn2/trn2_def_map108.c`, `trn2/trn2_hp_map107.c`, `trn2/trn2_hud.c`, `trn2/trn2_reward.c`, `trn2/trn2_slots.c`.

Ported 37, partial 1, missing 0.

| Function | Decomp | Status | What it does | Engine / note |
|---|---|---|---|---|
| `TRN_advanceTrainingTime` | `trn/trn_reward.c:721` | partial | Advances the time and day after a training session. | `content/packs/game/rules/original/care.yaml:229` · the original uses a 31-day year when training; the engine, the clock's (30). Decided this way (partner.md:188) |

## Fishing

The fishing minigame.

Decomp files: `fish/fish.c`, `fish/fish_model.c`, `main/fish.c`.

Ported 95, partial 0, missing 0.

## Death and rebirth

The death scene, the rebirth scene and the egg box.

Decomp files: `doo2/doo2.c`, `dooa/dooa.c`, `murd/murd.c`.

Ported 40, partial 0, missing 0.

## Saving and loading

The save on the memory card, the saving machine and the registration for versus mode.

Decomp files: `main/main_menu.c`.

Ported 12, partial 0, missing 0.

## Tournaments

Sign-up, calendar and the cups overlay, which brings its own battle engine.

Decomp files: `dget/dget.c`, `main/tournament.c`, `std/std_effect.c`, `std/std_hud.c`, `std/std_main.c`, `std/std_setup.c`.

Ported 113, partial 0, missing 0.

## Versus and demo

The two-player versus mode and the battle demo of the title.

Decomp files: `main/sound.c`, `vs/vs_camera.c`, `vs/vs_combat.c`, `vs/vs_demo.c`, `vs/vs_effect.c`, `vs/vs_hud.c`, `vs/vs_intro.c`, `vs/vs_main.c`, `vs/vs_scene.c`, `vs/vs_select.c`.

Ported 41, partial 4, missing 60.

| Function | Decomp | Status | What it does | Engine / note |
|---|---|---|---|---|
| `VSLoadSounds` | `main/sound.c:710` | missing | Loads the versus mode sounds. | `apps/game/src/versus_arena.cpp:200` |
| `VS_setCameraOrbit` | `vs/vs_camera.c:179` | missing (cited in docs) | Versus camera that orbits according to the distance between fighters. | — |
| `VS_setCameraYXZ` | `vs/vs_camera.c:221` | missing (cited in docs) | Versus camera that frames both fighters. | — |
| `VS_setViewpointRotationFromEntity` | `vs/vs_camera.c:241` | missing (cited in docs) | Versus camera that turns with the focused fighter. | — |
| `VS_setCameraLookAtEntity` | `vs/vs_camera.c:252` | missing (cited in docs) | Versus camera that looks at the focused fighter. | — |
| `VS_setCameraSimple` | `vs/vs_camera.c:288` | missing (cited in docs) | Versus camera that turns slowly around the arena. | — |
| `VS_setVSPhase` | `vs/vs_camera.c:309` | missing (cited in docs) | Activates the versus camera control with the given mode. | — |
| `VS_tickVSPhase` | `vs/vs_camera.c:316` | missing (cited in docs) | Chooses the active versus camera mode every frame. | — |
| `VS_setRandomViewpoint` | `vs/vs_camera.c:386` | missing (cited in docs) | Randomly chooses a preset viewpoint in the versus. | — |
| `VS_setViewpointFromBone` | `vs/vs_camera.c:425` | missing (cited in docs) | Places the versus camera relative to a fighter's bone. | — |
| `VS_setCameraToEntity` | `vs/vs_camera.c:454` | missing (cited in docs) | Fixed versus camera relative to the focused fighter. | — |
| `VS_updateCameraLerp` | `vs/vs_camera.c:485` | missing (cited in docs) | Interpolates the versus introduction camera. | — |
| `VS_tickCameraChase` | `vs/vs_camera.c:557` | missing (cited in docs) | Camera that chases the fighter in the versus introduction. | — |
| `VS_tickCameraIntro` | `vs/vs_camera.c:658` | missing (cited in docs) | Advances the versus introduction's camera sequence. | — |
| `VS_startCameraIntro` | `vs/vs_camera.c:683` | missing (cited in docs) | Starts the camera pass between the versus fighters. | — |
| `VS_applyEntityViewpoint` | `vs/vs_camera.c:755` | missing (cited in docs) | Versus camera anchored to the fighter's view. | — |
| `VS__combatInit` | `vs/vs_combat.c:280` | partial | Prepares the versus match: damage, clock, markers and both fighters' records. | `packages/game/battle/src/rules.cpp:1` · without the player markers or the 3D HUD records; online versus is shown as a panel |
| `VS_setupDemoCombat` | `vs/vs_combat.c:449` | missing (cited in docs) | Prepares the demo's automatic match. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_checkDemoEndCondition` | `vs/vs_combat.c:552` | missing (cited in docs) | Checks whether the demo ends by KO, time or a button. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_cleanupDemoCombat` | `vs/vs_combat.c:750` | missing (cited in docs) | Closes the demo match and frees its objects. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS__handlePause` | `vs/vs_combat.c:1233` | missing (cited in docs) | Handles the versus pause by either player. | — |
| `VS__addTimeoutWindow` | `vs/vs_combat.c:1505` | missing (cited in docs) | Shows the versus time-out notice. | — |
| `VS__tickDigimonStun` | `vs/vs_combat.c:1622` | missing (cited in docs) | Keeps the fighter stunned with its animation. | — |
| `VS__tickDigimonSenile` | `vs/vs_combat.c:1684` | missing (cited in docs) | Keeps the fighter disobedient until its counter runs out. | — |
| `VS__entityGetMoveWithHighestDistance` | `vs/vs_combat.c:2000` | missing (cited in docs) | Finds the fighter's longest-range move. | — |
| `VS__tickDigimonAttackingLogic` | `vs/vs_combat.c:2136` | missing (cited in docs) | The fighter's attack logic in the versus: collision and move use. | — |
| `VS__getBaseDistance` | `vs/vs_combat.c:2269` | missing (cited in docs) | Computes the base combat distance according to both fighters' size. | — |
| `VS__hasAffordableMoves2` | `vs/vs_combat.c:2292` | missing (cited in docs) | Tells whether the fighter has moves it can pay for with its MP. | — |
| `VS__initializePlayerMarker` | `vs/vs_combat.c:2337` | missing (cited in docs) | Adds the marker that tells apart fighters of the same species. | — |
| `VS__clearFighterDataTables` | `vs/vs_combat.c:2343` | missing (cited in docs) | Empties the internal tables of a fighter's record. | — |
| `VS_runDemoCombat` | `vs/vs_combat.c:2429` | missing (cited in docs) | Runs the demo match with music and no menus. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_setupDemoBattle` | `vs/vs_demo.c:107` | missing (cited in docs) | Sets up the demo: arena, two random fighters and their sounds. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_cleanupDemoBattle` | `vs/vs_demo.c:133` | missing (cited in docs) | Removes the fighters and the arena when the demo ends. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_loadDemoAssets` | `vs/vs_demo.c:155` | missing (cited in docs) | Randomly picks the demo's arena and music and prepares effects. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_loadDemoFighters` | `vs/vs_demo.c:180` | missing (cited in docs) | Randomly picks the demo's two Digimon. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_randomizeDemoFighter` | `vs/vs_demo.c:195` | missing (cited in docs) | Generates random stats and moves for a demo fighter. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_setupTrialBattle` | `vs/vs_demo.c:348` | missing (cited in docs) | Sets up the trial versus with three random Digimon per side. | — |
| `VS_randomizeTrialDigimonList` | `vs/vs_demo.c:373` | missing (cited in docs) | Fills the random Digimon list of one side of the trial versus. | — |
| `VS_randomizeTrialDigimon` | `vs/vs_demo.c:383` | missing (cited in docs) | Generates random stats and moves for a trial versus Digimon. | — |
| `VS_playDemo` | `vs/vs_demo.c:477` | missing (cited in docs) | Plays the title screen's automatic combat demo. | title demo: only the trial version's main calls it; this version's main plays the movie |
| `VS_initializeTrialBattle` | `vs/vs_demo.c:486` | missing (cited in docs) | Starts the trial versus with both players' lists. | — |
| `VS_addCommandMenu` | `vs/vs_hud.c:449` | missing (cited in docs) | Shows a versus player's command menu. | — |
| `VS_tickTargetCursor` | `vs/vs_hud.c:652` | missing (cited in docs) | Moves the versus player's target cursor. | — |
| `VS_addVersusModelScene` | `vs/vs_hud.c:1349` | missing (cited in docs) | Starts the versus sign animation. | — |
| `VS_tickVersusModelScene` | `vs/vs_hud.c:1355` | missing (cited in docs) | Animates the versus sign. | — |
| `VS_isVersusModelSceneFinished` | `vs/vs_hud.c:1401` | missing (cited in docs) | Tells whether the versus sign has finished. | — |
| `VS_loadStageModels` | `vs/vs_hud.c:1410` | missing | Computes the curves of the result signs and loads their models. | `apps/game/src/arena_letters.hpp:27` |
| `VS_addResultModelScene` | `vs/vs_hud.c:1445` | missing | Shows the victory sign of the winning versus player. | `apps/game/src/versus_arena.cpp:281` |
| `VS_setVersusModelSceneTimer` | `vs/vs_hud.c:1549` | missing | Sets the timer of the versus sign scene. | `apps/game/src/versus_arena.cpp:46` |
| `VS__setPostIntroPosition` | `vs/vs_intro.c:256` | missing (cited in docs) | Leaves the fighter in its final position after the versus introduction. | — |
| `VS__tickIntroName` | `vs/vs_intro.c:540` | missing (cited in docs) | Shows the fighter's name during the versus introduction. | — |
| `VS_startAttackAnimation` | `vs/vs_main.c:553` | missing (cited in docs) | Starts a move's animation and particles. | — |
| `VS_resolveAttack` | `vs/vs_main.c:562` | missing (cited in docs) | Resolves the hits of the moves in flight. | — |
| `VS_startFighterMove` | `vs/vs_main.c:797` | missing (cited in docs) | Makes the fighter start executing a move. | — |
| `VS_selectMoveTarget` | `vs/vs_main.c:856` | missing (cited in docs) | Chooses the target of the fighter's move in the versus. | — |
| `VS_applyMoveResult` | `vs/vs_main.c:1151` | missing (cited in docs) | Applies the damage and effects of a move that hits in the versus. | — |
| `VS_queueRandomMove` | `vs/vs_main.c:1735` | missing (cited in docs) | Randomly chooses move and target for the fighter's AI. | — |
| `VS_loadVSAssets` | `vs/vs_scene.c:1147` | partial | Prepares the versus arena: music per stage, camera, lights and effects. | `apps/game/src/versus_arena.cpp:1` · arena 0 with its camera, its lights and the effects; without the music per stage |
| `VS_playVersusIntroSequence` | `vs/vs_scene.c:1558` | missing | Plays the versus's opening introduction. | `apps/game/src/versus_arena.cpp:45` |
| `VS_tickVersusIntro` | `vs/vs_scene.c:1618` | missing | Advances the versus introduction by states. | `apps/game/src/versus_flow.cpp:447` |
| `VS_initializeCamera` | `vs/vs_scene.c:1851` | partial (cited in docs) | Places the versus arena's initial camera. | `apps/game/src/versus_arena.cpp:1` · the cup's camera (game::tournament::MatchCamera); not measured against the versus one |
| `VS__createPressStartToBeginBox` | `vs/vs_select.c:409` | missing (cited in docs) | Shows the prompt to press Start to begin. | — |
| `VS__initialize` | `vs/vs_select.c:442` | partial (cited in docs) | Versus setup flow: mode, map and Digimon selection. | `apps/game/src/versus_flow.cpp:1` · without arena choice (VS__tickSelectMap) |
| `VS__tickSelectMap` | `vs/vs_select.c:1166` | missing | Versus stage selection screen. | `apps/game/src/versus_flow.cpp:389` |

## Curling minigame

The curling minigame on ice.

Decomp files: `kar/kar.c`, `main/kar.c`.

Ported 44, partial 0, missing 0.

## Audio

Music, sound effects and loading them.

Decomp files: `main/sound.c`, `main/sound_async.c`.

Ported 21, partial 0, missing 0.

## Videos and ending

The video player, the credits and the ending animation.

Decomp files: `eab/eab.c`, `endi/endi.c`, `mov/mov.c`.

Ported 30, partial 0, missing 0.

## Core and platform

Startup, main loop, clock, randomness, math, files, memory and low-level graphics.

Decomp files: `main/_psstart.c`, `main/clock.c`, `main/file.c`, `main/file_queue.c`, `main/graphics.c`, `main/line.c`, `main/main.c`, `main/math.c`, `main/rng.c`, `main/utils.c`, `main/utils2.c`.

Ported 51, partial 0, missing 0.

## Outside the counts

982 decomp functions are tied to the console (drawing, memory, disc reading, hardware sound, internal tables, debugging, copies of the battle code in other overlays) and do not count in any figure. `python3 scripts/port_coverage.py --list` gives them with their reason (state `no-aplica`).

<!-- port-coverage:end -->
