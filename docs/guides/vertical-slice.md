# Playing the vertical slice and passing its autotests

The vertical slice is the playable field: File City and
the old man's house (`twna01` and `room08`), with the player, the partner that follows them, the pause menu and the
minimap. Everything comes from the packs by asset id: the `base` pack that `dwx extract` pulls from the user's disc
and the repository's `game` pack (`content/packs/game`: field rules, warps, character models and UI).

## What you need

- A build: `scripts/dev.sh build` (the `linux-clang-debug` preset by default, with the Vulkan validation layer) or
  `PRESET=linux-clang-release scripts/dev.sh build` to measure performance.
- The extracted content: a folder with the `base` pack (`dwx extract`, see [extract.md](../formats/extract.md)). The
  game mounts the packs from `--content` and those from its `packs/` folder; if none is the `game` pack, it adds the
  repository's (`content/packs`), so the extracted folder is enough.

## Playing

```sh
scripts/dev.sh run --content <folder with the base pack>
```

The game starts where `field/warps` in the `game` pack says (`start`: `twna01`, exit 1). Options:

| Option | What it does |
|---|---|
| `--content <folder>` | The game's packs (`content` by default) |
| `--mods <folder>` | More packs, as mods; it can be repeated. The VFS stacks them by layer and priority. A third-party pack goes only in a mods folder or in `--mods`, never in the content folder or its `packs/`: everything there is treated as the engine's own content |
| `--dev-assets` | Also uses the local assets from the `dev-assets.yaml` file, with a notice on screen while it is active; for development only ([development assets](../architecture/development-assets.md)) |
| `--ruleset <id>` | The ruleset it is played with, `rules/<id>/` in the packs (`original` by default, see [rulesets](../architecture/rulesets.md)) |
| `--story-stage <id>\|all` | The [story lock](../modules/apps/session.md) stage that is played, instead of the ruleset's `active_stage`; `all` opens everything (development and tests) |
| `--size <width>x<height>` | Window size (1280x720 by default) |
| `--fullscreen` | Starts in full screen |
| `--no-vsync` | Presents each frame as soon as it is ready (to measure fps) |
| `--start <map>[,<exit>]` | Starts on another map and exit |
| `--new-game-chain original\|seeded` | How a new game starts, instead of the ruleset's `story_scenes.new_game.chain` chain: `original` runs the opening from `twna01` exit 9, `seeded` starts in the free field with the state from `data/new_game_story` ([game](../modules/apps/game.md)). An autotest uses `seeded` unless its route says `new_game` |
| `--time day\|dusk\|night` | Sets the game clock to 8:00, 16:00 or 20:00 (by default, the game's time: 8:00 from the `clock` section in a new one) |
| `--new-game` | Starts a new game (which is what it does without `--load`): the partner, the stats and the Tamer level from the ruleset's `new_game` section |
| `--load <slot>` | Loads a saved game, `slot_01` (or an old autosave, `autosave_01`) from the saves folder, and starts where it was saved (except with `--start`) |
| `--clock-speed <x>` | The game clock's speed: 1 the original's (one game minute per real second), 60 one game hour per real second |
| `--seed <n>` | The seed for the random numbers of a new game (the partner and the systems: battles, items, evolution), so that a route plays the same every time. Without it, the game takes one from the clock and writes it to the log (`game: new game, seed <n>`), so a game can be repeated. With `--load` it changes nothing: the loaded game continues with the numbers it saved |
| `--original-rolls <state>` | For the oracle only: battles roll with the original's generator (`rand()`) from that 32-bit state (decimal or `0x…`), the one the original had when its fight began, so the fighters' first decisions are the same on both sides. Without it, the engine rolls with its own generator |
| `--trace <file>` | Writes one JSON line per logic tick with what the game simulates (player, partner, NPCs, fighters), to compare it with a trace from the original ([state trace](state-trace.md)). Without it nothing is written |
| `--control <socket>` or `--control tcp:<port>` | Opens the debugging channel on that Unix socket or on that TCP port of 127.0.0.1: a tool reads the state and moves the running game ([remote control](remote-control.md)). With `--control-paused`, the logic waits stopped before its first tick |
| `--saves <folder>` | Where it reads and saves games (by default the user's data folder). An autotest only saves with it |
| `--reports <folder>` | Where it saves the [bug reports](#reporting-a-bug-f12) (by default `reports` in the user's data folder) |
| `--text-font modern\|original` | The font of the text with the original look (the map name), instead of the one chosen in the main menu or the packs'; for autotests and captures, it is not saved (see [UI](../architecture/ui.md#original-look)) |
| `--ui-skin <id>` | The interface look (`original`, the default, `modern`, or one from a mod), instead of the one chosen in the menus or the packs'; for autotests and captures, it is not saved (see [UI](../architecture/ui.md#skins)) |
| `--autotest <route.yaml>` | Runs a route and exits with 0 if it passes (see below) |
| `--shots <folder>` | Where it saves an autotest's captures |
| `--lang en\|es` | This run's language, for autotests; it is not saved. Without it, the game uses the one chosen in the main menu or, the first time, the system's ([localization](../architecture/localization.md)) |
| `--explore` | Starts with [free exploration](../architecture/script-warps.md#free-exploration) turned on for this run, without saving it; without it, the game uses the pause menu's setting (off by default) |
| `--skip-steps <type>[,<type>...]` | Skips the steps of those types (`fullscreen` on an X without a window manager); the report says so, and skipping `walk_to` or `wait` leaves the run partial (it exits with 2) |

The development views are still available: `--test-scene`, `--map <id>`, `--model-viewer`, `--ui-demo`,
`--check-content <folder>` and `--dump-signs <map>[,<map>...] --shots <folder>` (the images of a map with a grid for
writing the [signs](signs.md)).

### Controls

| | Keyboard and mouse | Controller |
|---|---|---|
| Move (relative to the camera; with the "Walk with: Keyboard/pad" option, the default) | WASD or arrows | Left stick or D-pad |
| Walk instead of run | Shift (hold) | West button (X on Xbox, square) or RB/R1 (hold) |
| Go to a point (with "Walk with: Mouse"; then the keys and the controller do not move the player) | Left click on the ground; holding and moving drags it | |
| Forget the point | Ctrl or right click | East (B, circle) |
| Menu | Tab or the HUD's "Menu" button | North (Y, triangle) |
| Pause ("Pause" box, everything freezes) | P or Pause | Start |
| Accept / go back in the menu | Enter, Space or click / Ctrl, Backspace or right click | South / East |
| Debug console | F1 or `º` | |
| Map debug layer (collision, exits, NPCs; [remote control](remote-control.md#map-debug-layer)) | F3 (action `debug_map_overlay`) | |
| Save a bug report | F12 (action `report_bug`) | |

Running is the norm, as in the original (there R1 made you walk). The button prompts in the HUD and the menu change
to the last device used. The menu stops the field while it is open.

The logic runs at a fixed 60 ticks per second; the drawing interpolates between ticks and the animations advance per
frame. As in the original, the game clock advances one minute per second and the background changes palette at 6:00,
16:00 and 20:00, and when crossing a door the screen darkens by subtracting a gray for one second while the player
keeps walking, with the new map's name in the center in the second half, and comes back in another second
([field sheet](../reference/field.md)).

## Reporting a bug (F12)

During a game, **F12** (the `report_bug` action, which can be remapped like the others) saves a report in
`<user data>/reports/<YYYYMMDD-HHMMSS>-<map>/` (on Linux `~/.local/share/indie-modding-games/dwe/reports/`, or the
`--reports` folder), writes the folder's path to the log and shows "Bug report saved" for a few seconds. The normal HUD
shows nothing of this. In the folder:

| File | What it is |
|---|---|
| `save.yaml` | The game at that moment, with the usual save system. In the middle of a battle, a script, a map change or a scene (care, evolution, sleep, gym, shop...) it cannot be saved: then it is the last snapshot of the free field, which the game takes every 5 s while it can, and `state.yaml` says so (`save.kind: field_snapshot`, `seconds_ago` and `reason`) |
| `screenshot.png` | The capture of the next frame, without the notice |
| `state.yaml` | The commit it was built from (`git describe --dirty`), the map, the game's hour and day, what is on screen (field, battle and its phase, running script and its sections, dialogue box, menu, map change), the position and turn of the player, the partner and each NPC (script id, species, model, whether a script holds or moves it), the battle's fighters with HP, MP and position, the partner's species and state, and the `field.overlaps` counter |
| `log.txt` | The last 2000 lines of the log. A slow frame writes its line, but in a streak of them only one per second, with how many were silenced and the worst (`slow_frame_log.hpp`), so that the rest of the log fits |
| `route.yaml` | An autotest route that loads `save.yaml` (`load_save`) and waits on that map |

To reproduce it, from the repository root:

```bash
build/linux-clang-debug/apps/game/dwe-game --content build/studio-content \
  --autotest ~/.local/share/indie-modding-games/dwe/reports/<folder>/route.yaml --shots /tmp/repro
```

The route ends with a capture; add after the wait the steps that show the bug and it becomes a test.

## Autotests

An autotest runs a route written in YAML with the same events as a player: the keys and the mouse come in as platform
events and the controller is a virtual SDL controller, so they go through the action map, the UI and the field for
real. While it runs, the game discards the desktop's real mouse and keyboard (the pointer of whoever is using the
computer, or the one resting over the window, would move the pointer under a click or a held press), so the desktop
can keep being used; a real controller does get through. The slice's routes are in `tests/autotest/`:

- `route-keyboard.yaml`, `route-gamepad.yaml` and `route-mouse.yaml`: File City → old man's house → File City with
  each device; they open the menu with that device, turn the minimap off ("No") and on again ("Yes"), stop behind a
  File City depth layer, change the window to 800x600, 1280x720, 1920x1080, 2560x1080 and full screen, and take
  captures. With the original look, the minimap and the options are among the tools on the player page (Tab on it;
  North on the controller), like the original's menu, which only has its six entries; the routes that open the
  minimap or the options (`route-keyboard`, `route-gamepad`, `route-mouse`, `route-language`, `route-field-hud` and
  the three for free exploration) reach them that way with that look (`when` steps with `ui_skin`) and through the
  menu's first page with the modern one. The mouse still has no way to open those tools, so the mouse routes press
  Tab for that and click on the rest. `route-mouse` also clicks on the tabs of the player page (original look) and of
  the partner page (Status and Techniques) and checks that each click shows its page.
- `route-mouse-opening.yaml`: the whole opening with the mouse (`new_game_scene: true`): it answers the old man's
  questions with clicks on the options (and checks that the pointer over one moves the cursor and that a click outside
  chooses nothing), types the two names with clicks on the letters and on OK and goes through the pages up to File
  City.
- `route-mouse-dialogue.yaml` (with `--new-game --new-game-chain seeded --start trai00`): talks to `species_j` in the
  gym, chooses the second option with a click and checks that the player does not start walking when it ends.
- `route-walk-control.yaml`: the "Walk with" option: with the keyboard (the default) a click on the ground does not
  move the player; it changes it to mouse in the pause menu and checks that the arrows no longer move them and a click
  does.
- `route-battle-mouse-no-walk.yaml`: with the mouse, a click during a battle does not leave a walk order queued: when
  the battle closes the player is still where they were.
- `route-fade.yaml`: enters the old man's house with the mouse and takes a capture in the middle of the fade, with the
  map name in view in the game's language (`--lang`), and another at the end.
- `route-field-pickup-mouse.yaml`: the notice for an item on the ground is answered with the mouse: `drop_item` leaves
  the Meat at the player's feet, the notice is shown, a `click: {element: "field_item_notice#box"}` on its box closes
  it and the item goes into the bag (the notice's box also answers to the click, like the keyboard's and controller's
  `confirm`).
- `route-language.yaml`: starts at the main menu (`main_menu: true`), changes the language in its options and puts it
  back, enters the field and opens the pause menu, with captures of each screen. It is run once per language, each
  with its own captures folder:

```sh
for lang in en es; do
    scripts/dev.sh run --content <folder> --lang $lang --autotest tests/autotest/route-language.yaml \
        --shots <captures>/$lang
done
```

- `route-field-hud.yaml`: the HUD's field state ([contract](../architecture/ui.md#field-status)): the time and day
  of a new game, the clock that advances, the ruleset's happiness and discipline ranges, that the partner finds
  nothing special about the starting map (`hud.partner.area` at 0), the hunger that appears with the accelerated
  clock, the panel that leaves with the pause menu and returns when it closes, and captures at 800x600, 1280x720 and
  2560x1080 and with the larger interface scale. It is run in both languages, like `route-language.yaml`. With it,
  `route-skin-original.yaml`, `route-scripted-loop.yaml` and the three for free exploration, the route step is 25
  routes (with the `route-save-load` pair): 50 runs on the extracted content, plus the 4 on the cooked `.pak`, 54 in
  total.
- `route-loop-keyboard.yaml`: the raise, train, fight and evolve loop with the keyboard, from the main menu: raises the partner (scolds it,
  waits for it to be hungry and gives it its favorite food from the bag, takes it to the potty and puts it to bed),
  trains it, fights with the debug trigger and, with the clock accelerated, takes care of it in stretches until it
  evolves. Each step checks its effect on the partner's state; it takes captures of each stage. It takes about two
  minutes with lavapipe. It is run in both languages, like `route-language.yaml`.
- `route-save-load.yaml` and `route-save-load-check.yaml`: saving and loading a game, in two runs. The first starts a
  new game from the main menu, puts three items in the bag, enters the old man's house and crosses the room, sets the
  clock forward to one in the afternoon and stops it, and saves in `slot_01` with the debug command
  `save("slot_01")` (it only saves if `--saves` names a folder). The second starts with `--load slot_01` from that
  same folder and checks that the game is as it was saved: the map and the player's position, the partner's name and
  age, the day and the time, the money and the items in the bag. It does not check the story, which is also saved. It
  is a smoke route: the short gate runs it in `en`, and the daily one in both languages. The second never runs alone;
  by hand:

```sh
saves=<captures>/saves
scripts/dev.sh run --content <folder> --lang en --saves $saves --autotest tests/autotest/route-save-load.yaml \
    --shots <captures>/save-load
scripts/dev.sh run --content <folder> --lang en --saves $saves --load slot_01 \
    --autotest tests/autotest/route-save-load-check.yaml --shots <captures>/save-load-check
```

  Saving from the game's menu is checked by `route-save-actions.yaml`, and sleeping and saving by
  `route-sleep-save.yaml`.
- `route-sleep-save.yaml`: sleeping and saving ([save-and-load.md](../architecture/save-and-load.md#36-sleep)).
  It starts a new game in `slot_01` from the main menu, accelerates the clock until the partner is sleepy
  (`pause_menu.sleep_enabled`), chooses "Sleep" in the pause menu and "Save and go to sleep" in the sleep menu, and
  waits for `save.last_result` = `saved` without any question, because the slot belongs to this same game; then it
  checks that the partner turned a day older. It needs `--saves` with an empty folder; the gate gives it one of its
  own (`saves_routes`). It is run in both languages.
- `route-dev-assets.yaml`: checks that the notice for the [development assets](../architecture/development-assets.md)
  is shown in the main menu and in the field. It is the only route that needs `--dev-assets` and a `dev-assets.yaml`
  in the current folder; it is run in both languages.
- `route-skins.yaml`: starts with the original look (the default), from the main menu's options switches live to the
  test one (the synthetic pack `tests/mods/ui_skin/test_contrast`), goes through the field, the pause menu, the
  partner's menu and a battle with it, and switches to DevVago and to the modern one from the pause menu's options,
  with captures of each screen. It needs `--mods tests/mods/ui_skin` and is run in both languages, like
  `route-language.yaml`.
- `route-skin-original.yaml`: the original look on each screen it draws, with a capture of each one: the title and its
  menu, the slot list and the question about playing without saving (it needs `--saves` with an empty folder of its
  own, which the gate gives it), the field's clock and bars, the pause menu, the player page and its tools' options,
  the partner's sheet (its menu entry) and the bag (the items one, with an item's options and the orders), a battle
  that is fled from (without final boxes, like the original), and the hunger bubble over the partner at 1920x1080.
  The saves folder is still empty after the battle and the new days (`saves.files: "0"`): playing without saving
  writes nothing. It is run in both languages.
- `route-text-fit.yaml`: that each text fits in its box (`expect_text_fits` after each capture) on the screens of the
  original look from the field: the pause menu and its pages (player, tree, medals, cards, tools, options, minimap),
  the partner's sheet, techniques and bag with an item's options, the save machine and a battle with its final boxes,
  at 1280x720 and again the pages with the most text at 1920x1080. It is run in both languages. To look at the texts
  of another route without touching it, add `text_fit: true` to it.
- `route-save-indicator.yaml`: the [save notice](../modules/apps/game.md#save-notice) from a new game with the
  keyboard: the console save, the autosave on entering the old man's house, the save machine and a save with the pause
  menu open at 1280x720, and another at 1920x1080 and at 1024x768; each capture checks that the notice does not cover
  any window (`expect_clear`) and that the texts fit (`text_fit`). At the end it turns the option off in the pause menu
  and checks that a save no longer shows it. It needs `--saves` with an empty folder. It is run in both languages.
- `route-minimap-clear.yaml`: the [minimap under the windows](../modules/apps/game.md#minimap-under-the-windows) from
  a new game with the keyboard: the minimap always visible and large, and the pause menu and the partner page at
  1280x720, 1920x1080 and 1024x768 without covering anything (`expect_clear`), with the minimap back when they close.
  Made for the modern look (`--ui-skin modern`); it also passes with the original. It is run in both languages.
- `route-options-tabs.yaml`: the [in-game options](../modules/apps/game.md#in-game-options) by tabs from a new
  game with the keyboard, at 1280x720: each tab with E and with a click, a row with focus and its description, a list
  and a volume changed in both directions and the "Reset to defaults" question, with all the texts inside
  (`expect_text_fits`). It is run with both looks (`--ui-skin original`, `modern`) and in both languages.
- `route-minimap-enemies.yaml`: the [enemies on the minimap](../modules/apps/game.md#enemies-on-the-minimap) on the
  fog's second map (`--start mist03 --story-stage all`): the dots on arrival, with the player among the three wild
  ones and moving, and then the option turned off in the pause menu (`expect_setting`) and the minimap without dots.
  It is run with both looks (`--ui-skin original`, `modern`).
- `route-digital-time.yaml`: the [time in digits](../modules/apps/game.md#time-in-digits) under the HUD clock from a
  new game with the keyboard: 08:00 at 1280x720, 1920x1080 and 1024x768, the night (20:00), the pause menu open (the
  clock leaves with its box) and the option turned off from the menu. Each capture with the box checks that it does
  not cover anything of the HUD or of another document (`expect_clear: hud#field-time`) and that the texts fit
  (`text_fit`).
  It is run in both languages and with both looks (`--ui-skin original` and `modern`).
- `route-all-species.yaml`: [every species as a partner](../modules/game/extra-evolutions.md), on by default: a new
  game from the main menu with the rules the screen brings, the tree with 110 boxes (the original's 61 and 49 new) and
  a new line through the natural check: `tane` to `otam` (offense the highest, no mistakes, weight 5) and `otam` to
  `geco`, with values that no target of the original takes. It captures the rules and the two forms in the field. It
  is run in both languages, with `--saves` in an empty folder. The routes that go through the rules screen to play
  with others (`route-hidden-content*`, `route-mega-step`, `route-canonical-stages`) first turn off "Every species as
  a partner" (`menu_select: rules-all`).
- `route-hidden-content.yaml`: the [hidden content](../modules/apps/game.md#hidden-content) from the main menu with
  the keyboard: the new game's rules screen (with the original's rules and with the hidden content turned on), the
  tree with 65 boxes and the four new forms through the natural check: `species_k` from `species_l`, `species_m` from
  `species_n`, `species_o` from `species_p` and `species_q` from `species_r` (`story_set` for the doors, `evolve` for
  the source form, `partner_set` for the values and `clock_speed` up to the check; for `species_q` the clock first
  runs to 19:00, `hud.clock.hour`, and the check falls at night). It captures each new partner in the field and on its
  sheet, and the tree with the new boxes in the original look (`species_k` and `species_m` on page 1, `species_o` and
  `species_q`, which do not fit in the Ultimate box, on page 2, with the detail of `species_o`), with `text_fit`. After
  each evolution it waits for its scene to end (the `evolution.active` setting, `"true"` while it runs) and confirms
  the Tamer level notice if it appears, so it also works on a slow machine. It is run in both languages, with
  `--saves` in an empty folder.
- `route-hidden-content-off.yaml`: the same with the original's rules: the tree stays at 61 boxes and the same
  `species_l` evolves into the form the original chooses. It is run in both languages, with `--saves` in an empty
  folder.
- `route-mega-step.yaml`: the Ultimate → Mega step ([evolution](../modules/game/evolution.md#ultimate--mega-step)): a
  new game with the hidden content and the Mega step turned on, the tree with 66 boxes, `species_o` from `species_s`
  and `species_t` from `species_u` through an Ultimate's check (120 h). It captures the rules screen, the two Megas in
  the field (`species_t` at 0.6) and `species_t`'s sheet; it makes `species_q` evolve (`evolve`, so that its box
  opens) and captures the tree in the original look: the two Megas in the Mega box, to the right of the Ultimate one on
  the same page, and `species_q`, which does not fit in the Ultimate box, on page 2 (Down from the last Fresh I box,
  its detail and Up to come back); then it turns on the canonical stages and captures the one-page tree with the four
  Megas and the detail of `species_t`, with `text_fit`. It is run in both languages and both looks, with `--saves` in
  an empty folder.
- `route-canonical-stages.yaml`: the [canonical stages](../modules/apps/game.md#canonical-stages) in a new game with
  the hidden content: it turns the option on (the options list scrolls to the new row), captures the one-page tree
  with the six boxes (the Mega box to the right of the Ultimate one), opens the detail of `species_v` and that of
  `species_w` (marked as raised with `story_set`), turns it off and captures the original's tree with arrows and page
  number and the detail of `species_w`, goes down with Down from `species_s` to page 2 (`species_o` and `species_q`,
  which do not fit in the Ultimate box, in the same column) and comes back with Up to page 1. The tree is only in the
  original skin. It is run in both languages and both skins, with `--saves` in an empty folder.
- `route-city-tab.yaml`: the [City tab](../modules/apps/screens.md#city-tab-a-dwe-improvement) from a new game with
  seven species joined to the city (`story_set`): it opens the player page with the keyboard, goes to the tab, puts
  the cursor on one that lives in the city, on one that is missing and on a joined Champion, at 1280x720 and again at
  1920x1080 and at 1024x768, with `text_fit`. It is run in both languages and, in addition, with `--ui-skin modern`
  (the "City" entry of the player page).
- `route-evolution-requirements.yaml`: the [evolution requirements](../modules/apps/game.md#evolution-requirements)
  in the partner's chart (`species_a`, as in a new game) with the keyboard: the cursor over its undiscovered ("???")
  and discovered evolutions, the original view, the weight moved with large berries and the chart at 1280x720,
  1920x1080 and 1024x768 (`text_fit`, `expect_clear`); at the end it turns the option off and on. It needs `--saves`
  with an empty folder (or a copy of a game with `species_a` and `--load`). It is run in both languages, with the
  original look: the chart is a page of its own and with `--ui-skin modern` the route fails at its first step.
- `route-status-extras.yaml`: the partner's [sheet extras](../modules/apps/screens.md#sheet-extras) from a new
  game with the keyboard: two trainings (tiredness) and the fast clock up to a care mistake, the sheet with the option
  on at 1280x720 and 1920x1080, again with the widest values (HP and MP 9999 / 9999, discipline at 100%, a
  three-digit evolution counter) at 1280x720 and 1024x768, the option turned off from the pause menu and the sheet
  again like the original's, with `text_fit`. It is run in both languages and with both looks (`--ui-skin original`
  and `modern`).
- `route-scripted-loop.yaml`: a lap through five zones from a new game with the keyboard: File City (`twna01`) → the
  old man's house, `room08` (the map's exit table) → File City (script) → `trai00` (table) → `mayo00` (table), where
  it plays the forest's first battle and the recruitment of `species_a` → `twnb01` (the map script's warp, on stepping
  on the tile) → File City through another entrance (script), which after the recruitment is `twna02`. It checks both
  map-change paths, the table and the [script warps](../architecture/script-warps.md), and that the partner follows
  the player at each arrival; it takes a capture in each zone. It is run in both languages, like
  `route-language.yaml`.
- `route-free-exploration.yaml`: [free exploration](../architecture/script-warps.md#free-exploration) with the
  keyboard, from a new game: it turns it on in the pause menu's options and goes through the doors that the story
  still has closed toward two zones that a new game does not reach (`mayo02_2` and `mayo08b`), with the destination
  selector and with a way back. It checks that the selector starts at the story's place, that it turns pages and that
  Escape leaves the player on the map. In `mayo00` it first plays the forest's first battle, like a new game, and its
  walks hold off with `encounters` the wild `species_x` that arrives whenever the time wants; beforehand it heals the
  partner with items (and scolds it if it refuses them), because that battle does not let you flee and a lost one
  sends it home. It ends on reaching `mayo08b`: its wild encounter does not let you flee and a new game's partner
  loses it, which sends it to the old man's house, as in the original. It is run in both languages.
- `route-free-exploration-mouse.yaml` and `route-free-exploration-gamepad.yaml`: the switch and the selector with the
  mouse (a click on the second page) and with the controller (East stays, South takes the story's place). They are
  run in both languages.

All the routes run with the original look, the default, unless they ask for another. The two for looks (and the
original look in all of them) need content extracted with `dwx` of extractor version 2 or later, which brings the
original look's images; with an earlier one the game starts with the modern one and those two routes fail at their
first step.

- `route-mod-map.yaml`: with the example mod mounted ([`tests/mods/example_mod`](../modules/tests/mods.md)), it
  starts on the map the mod adds by its id (`example_mod:meadow`), with no code of its own, and leaves through its exit
  to File City. It is run in both languages:

```sh
for lang in en es; do
    scripts/dev.sh run --content <folder> --mods tests/mods/example_mod --lang $lang \
        --autotest tests/autotest/route-mod-map.yaml --shots <captures>/$lang
done
```
- `route-game-modes.yaml`: with a second mode mounted ([`tests/mods/game_mode`](../modules/tests/mods.md)), the new
  game asks for the mode; "Original story" is followed by the rules screen and "Practice" starts without it. It needs
  an empty saves folder. It is run in both languages:

```sh
for lang in en es; do
    scripts/dev.sh run --content <folder> --mods tests/mods/game_mode --saves <empty folder> --lang $lang \
        --autotest tests/autotest/route-game-modes.yaml --shots <captures>/$lang
done
```
- `route-stage2.yaml` and `route-stage2-jungle.yaml`: Stage 2 of the story, with the lock as it comes out (the
  ruleset's active stage is 2: the jungle and the tunnel open, stage 3 and later closed) and a seeded game that starts
  on the map of each half. The Drill Tunnel one (`--start tunn06`) evolves the partner to Champion, talks to the miner,
  sees the rock roll (trigger 120), follows on to tunn07 and tunn08, where the screen shakes and the battle with the
  fire monster starts, wins it with `battle_win` and ends with trigger 209. The jungle one (`--start trop04`) bumps
  into `species_x`, who goes back to the city (204), passes through trop01 and trop05 (in trop05 it places the player
  next to the exit, so as not to fight the wild ones on the way) and enters Amida's forest (alert, 137) and goes up
  its central path:

```sh
scripts/dev.sh run --content <folder> --start tunn06 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop04 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-jungle.yaml --shots <captures>/stage2
```

- The Stage 2 recruitments, one route each, with the same lock and the seeded game; each one checks the recruitment
  trigger with `story.trigger.<n>`. `route-stage2-coelamon.yaml` (`--start trop00`) crosses the tiles of the jungle's
  broken bridge (trigger 185), goes back through mayo08a, mayo02 and mayo04b to mayo05 and steps on the shore, where
  `species_y` rises from the river and joins (249).
  `route-stage2-drimogemon.yaml` (`--start tunn09`) talks to `species_z` in the mine (trigger 353), lets a day pass
  taking care of the partner with the fast clock (variable 30 counts the days) and talks to it again: it joins (238).
  `route-stage2-piximon.yaml` (`--start trop01 --seed 4`) runs into `species_aa`, which only appears in the jungle with
  a roll of 3 in 100 on entering (seed 4 gives it; another chance roll before entering can change it), accepts its
  challenge, wins the fight and `species_aa` joins (255).
  `route-stage2-dirt.yaml` (`--start tunn04`) does the tunnel's dirt job: beats tunn04's digger (trigger 140), accepts
  the miner's job in tunn05, loads the minecart onto the pile (a tile from 80 to 109: it runs on confirming), empties
  it in tunn01 and gets paid (variable 18 to 1). Five days later the miners finish on their own (variable 18 to 10)
  and the way to tunn06 is left open.
  `route-stage2-centarumon.yaml` (`--start trop06`) crosses the stair lottery forest: the entry tile draws lots for
  which of the three endings is the good one (variable 116), the route leaves by the lane that leads to it (a `when`
  block per ending), takes each junction it finds and `species_ab` joins at the end (236). Its trip to koda00 is left
  closed by the stage 2 lock and the player stays in trop06. The three endings recruit the same (sections 0x44, 0x45
  and 0x46, the same scene with another lane); which one comes out depends on the seed and on the frames the player
  spends on the tile, so it can change with the engine: today seed 1 leads to section 0x44, 5 to 0x45 and 2 to 0x46;
  all three recruit.
  `route-stage2-amida-trap.yaml` (`--start trop06`) goes up the west lane passing its first junction (trigger 161,
  section 0x37) and goes back to the start of the lane (section 0x34), which finds the 161 set and springs the trap:
  the target mark blinks over the player's node 10 (`spawn_sprite_at_entity`, sprites 44 and 45), the arrows fly and
  the end of the trap clears the lane's triggers (158 to 173).
  `route-stage2-vegiemon-rumor.yaml` (`--start twna03`) talks to `species_ac` in `species_ad`'s field, who tells about
  the strange plant in the jungle and sets trigger 86; it does it with `species_ad` in the city (246), without
  `species_ae` (225), from 7 to 18 h and with 10 prosperity points (variable 1), which the route writes with
  `story_set` because the seeded game does not have them. `route-stage2-vegiemon.yaml` (`--start trop03`) starts from
  86 (written) and from the Rain Plant in the bag (`give_item`; in the story it is the fruit the field gives on day
  15), steps on the sprout, says yes to using items and chooses the plant in routine 0's selector (variable 254 =
  121): it rains on the sprout and `species_ae` joins (225). `route-stage2-lock.yaml` (`--start trop03`) steps on the
  lock's border in the jungle: the exit from trop03 to gcan01 (stage 4) and that from trop02 to dgha01 (stage 7) do
  not happen, the lock's message appears and the player stays on the jungle map.

```sh
scripts/dev.sh run --content <folder> --start trop00 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-coelamon.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start tunn09 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-drimogemon.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop01 --lang en --seed 4 \
    --autotest tests/autotest/route-stage2-piximon.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start tunn04 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-dirt.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop06 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-centarumon.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop06 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-amida-trap.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start twna03 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-vegiemon-rumor.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop03 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-vegiemon.yaml --shots <captures>/stage2
scripts/dev.sh run --content <folder> --start trop03 --lang en --seed 1 \
    --autotest tests/autotest/route-stage2-lock.yaml --shots <captures>/stage2
```

- The Stage 4 routes (the Great Canyon and the fortress), with `--story-stage stage_4` (the ruleset's active stage is
  still another) and the seeded game; each piece starts on its map and writes with `story_set` the triggers of the
  previous pieces. `route-stage4-yuramon.yaml` (`--start twna03`) talks to `species_af` in the city: with 6 prosperity
  points (variable 1, written) it asks for help (trigger 124); with the canyon bridge done (103, written) it tells
  about the bandits (197). `route-stage4-canyon.yaml` (`--start trop03`) enters gcan01, crosses the bridge (103), goes
  through gcan09 to gcan08_1, where the bandits spring the ambush (with 197): it wins the battle with `battle_win`
  (198 and 155), follows on to gcan03 and steps on its exit to Freezeland (stage 5), which the lock leaves closed.
  `route-stage4-birdramon.yaml` (`--start gcan03`) goes up with the elevator (Press UP) to gcan10, beats `species_e`
  (221), who takes the player to the city, and in the transport house (room12) opens the destination list with more
  birds written and 1800 Bits: the destinations the lock closes show in red, cannot be chosen and do not charge; it
  flies to gcan03. `route-stage4-fortress.yaml` (`--start gcan03`) goes down with the elevator (Press DOWN) to gcan06
  and gcan05, whose guard steps aside after the ambush (200), crosses ogre00 to ogre03 (the guards that reach it fight
  and are won with `battle_win`), beats `species_ag` on the throne (175 and 176), goes down by the freight elevator to
  gcan02 and follows on to the bridge (gcan04), where a voice asks for help (182), and steps on its closed exit to
  Freezeland. `route-stage4-shellmon.yaml` (`--start gcan02`) writes what lets you go down with the elevator from the
  top of the canyon to the fortress and come back on foot (180 out, 181; the throne's freight elevator would set 180
  again), follows on to the other bridge (gcan04_2), where `species_ah` joins (235), and its exit to Freezeland is also
  left closed. `route-stage4-monochromon.yaml` (`--start gcan11`) confirms at `species_ai`'s counter (script 49,
  section 0x51), says it will try and serves the 20 customers with the keys (scripts 199 to 202; `wait` with
  `confirm` and `answer`: always the first option); before the last one's sale it writes the tally almost to zero
  (`story_set("tally 2")`), so the ending pays the minimum whatever happens in the draws: "It could have been better",
  500 Bits, the clock 8 hours later and the jump to exit 2 of gcan09, without the 247. It enters again and
  `species_ai` offers the job once more ("You want to work?"); on the second one, the first customer becomes the 20th
  and the tally is written above 3072 (variable 243 of 12): that customer and the ending run through the script (the
  test, "`species_ai` joins the city!", 2000 Bits, 2 prosperity points and the 247), and the shop is left empty. It
  saves in `slot_01`; `route-stage4-monochromon-city.yaml` (`--load slot_01 --start room06` with the same `--saves`
  folder) finds it in the city's item shop (variable 25 to 3) and, on talking to it, opens its shop (68). That route plays the original job (`shop_job_mode("original")`);
  `route-stage4-monochromon-improved.yaml` (`--start gcan11 --seed 1`) plays the improved one: the rules panel opens by
  itself at the first customer, the first one buys at the regular price (click on row 6 of the counter, +90), the
  second is dismissed, and the third becomes the 20th with the tally above the bonus (3700): when it is dismissed, the
  ending pays as the original does on winning (the 247) and gives the Steel Heart.
  `route-stage4-lock.yaml` (`--start gcan01`) steps on the exit from gcan01 to dgha01 (stage 7): the lock's notice
  appears and the player stays in gcan01; then it goes back to the jungle through trop03.

```sh
scripts/dev.sh run --content <folder> --start twna03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-yuramon.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start trop03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-canyon.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-birdramon.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan03 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-fortress.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan02 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-shellmon.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan01 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-lock.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan11 --story-stage stage_4 --lang en --seed 1 --saves <captures>/saves \
    --autotest tests/autotest/route-stage4-monochromon.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --start gcan11 --story-stage stage_4 --lang en --seed 1 \
    --autotest tests/autotest/route-stage4-monochromon-improved.yaml --shots <captures>/stage4
scripts/dev.sh run --content <folder> --load slot_01 --start room06 --story-stage stage_4 --lang en \
    --saves <captures>/saves --autotest tests/autotest/route-stage4-monochromon-city.yaml --shots <captures>/stage4
```

- The Stage 3 routes (Mt. Panorama, Gear Savanna, `species_aj`'s cave, Trash Mountain and the Arena lobby) are played
  with `--story-stage stage_3` while the ruleset's active stage is earlier, with the seeded game and one route per
  stretch; the battles on the way end won at the start (`encounters: win`). `route-stage3-panorama.yaml`
  (`--start mayo02_2`) goes up through miha00, crosses miha05, miha01 and miha02 (its way down is tile 51 of script
  21, which leads to miha04a) and goes down to gias00. `route-stage3-savanna.yaml` (`--start gias00`) crosses gias01 up
  to `species_ak` (gias02): the route puts it in front of the player (`place`) and touches it three times (variable
  33; on the third it joins, 218); it continues through gias03 to gias08, whose exit to stic01 (stage 7), which the
  route reaches from the north path (`place`, so as not to brush the exit to Trash Mountain), is left closed with the
  lock's notice, and enters Trash Mountain (gomi01, gomi02). `route-stage3-leomon-tablet.yaml` (`--start leom02`) goes
  down to the cave (leom01), passes tile 51 (the player looks at the cave, 134) and takes the tablet from tile 52
  (Leomonstone, 135). `route-stage3-leomon.yaml` (`--start gias07`) sees `species_al`'s rescue in gias07 (196), puts
  the Leomonstone in the bag and enters gias09, where `species_aj` reads the tablet and joins (248).
  `route-stage3-keychain.yaml` (`--start leom02`) writes that 248 (the seeded game has not recruited `species_aj`),
  goes down to leom01, where the header then brings out the keychain one (up to its chat, 333), and talks to it (script
  109, section 5): the first of the bag's five chats (20 slots, 47; variable 21 to 1).
  `route-stage3-arena.yaml` (`--start room13`) confirms at the Arena lobby's counter (tiles 81): the welcome (91) and,
  the second time, the registration, which opens the schedule (routine 18); the player is free again and leaves to the
  city through the door (tiles 52). `route-stage3-tournaments.yaml` (`--start room13`, en and es) plays two cups: it
  registers in today's Grade C, at 10:00 steps on the stadium door (tile 54, routine 6), sees the grade card and the
  bracket, and loses the first battle (`battle_lose`): participation prize, no cup won (the partner is left with a
  fifth of its HP and the route heals it with an item). Then it registers in tomorrow's Grade D, spends the night,
  wins the three battles (`battle_win`) with the bracket before each one and the champion's climb at the end, and
  checks the won-cup trigger (15), the 3 victories (variable 255) and the prize money in the bag.

```sh
scripts/dev.sh run --content <folder> --story-stage stage_3 --start mayo02_2 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-panorama.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start gias00 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-savanna.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start leom02 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-leomon-tablet.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start gias07 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-leomon.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start leom02 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-keychain.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start room13 --lang en --seed 1 \
    --autotest tests/autotest/route-stage3-arena.yaml --shots <captures>/stage3
scripts/dev.sh run --content <folder> --story-stage stage_3 --start room13 --lang es --seed 1 \
    --autotest tests/autotest/route-stage3-tournaments.yaml --shots <captures>/stage3
```

- `route-stage5*.yaml`: Stage 5 of the story (Freezeland and `species_am`) with `--story-stage stage_5` and the seeded
  game. Freezeland's wild ones beat the seeded partner, so the walks end their battles at once (`encounters: win`),
  and each route writes with `story_set` the triggers of earlier stages that it needs. The path the original forces
  is played in two stretches: `route-stage5.yaml` (`--start gcan04`) leaves the Great Canyon for frzl01, crosses
  frzl02, frzl03, frzl06, frzl12 and frzl13 up to frzl16's beach, talks to `species_am` (tile 0x51 of script 130, with
  the Ogre Fortress's 175), says yes to the trip (352) and enters ogre10's cave with its scene (section 0x3c, "a voice
  behind"). `route-stage5-whamon.yaml` (`--start ogre10,0`, the cave's inner exit) enters ogre11, where `species_ag`,
  its two `species_an` and `species_ao` fight (script 137); the victory sets 150 and goes back to ogre10 with section
  6 of script 136: `species_ag` blows up the rocks and flees, `species_am` joins (224) and takes the player to frzl16.
  With 150, `route-stage5-ogremon.yaml` (`--start tunn02`) goes back to the stage 2 Drill Tunnel: the ambush of section
  0x33 of script 26 comes out, is won and `species_ag` joins (234).
  `route-stage5-angemon.yaml` (`--start icsa01`) steps in front of the statue (324), the partner moves the pedestal
  (routine 50, 323), goes down through the maze (icsa02, icsa07, icsa04, icsa05) to icsa06, where the light (routines
  45 and 46) sets 322, and goes back to icsa01 (its entry repeats routines 49 and 50); in icsa05 it puts the player
  next to the exits (`place`), because `species_c` knocks down the seeded partner. The awakening of `species_ap` is
  played by `route-angemon.yaml` (`--start icsa02`, with 324 and 323 written): the climb from icsa02 to icsa01 (tiles
  of section 0x33 of script 94) leads to exit 1 of icsa01 and, with the light's 322, section 5 of script 93 runs on
  arrival (`warp_to` 97, 1, 5): "Oh? About that statue...", the statue wakes up as `species_ap`, goes south and joins
  the city (220). Without 322 the same climb runs nothing and the statue stays still. For the player: after icsa06's
  light, go back to icsa01 by icsa02's stairs (not by icsa05's exit). Section 0x34 of icsa01 also jumps to section 5,
  but only with 220 already set, so in the original it does nothing. `route-stage5-curling.yaml`
  (`--start frzl15,2`) talks to `species_l` (357), plays the curling game in frzl17 confirming each shot and goes back
  to frzl15; if it wins, `species_l` joins (257). Lock border: `route-stage5-lock.yaml` (`--start frzl08,1`) steps on
  the exit from frzl08 to mist07 (stage 7) and `route-stage5-dock-lock.yaml` (`--start twna02`, with 224 written)
  chooses Factorial Town (fact01, stage 6) at File City's dock; both are left closed with the notice and the player
  stays on the map.

```sh
for r in "route-stage5 gcan04" "route-stage5-whamon ogre10,0" "route-stage5-ogremon tunn02" \
         "route-stage5-angemon icsa01" "route-angemon icsa02" "route-stage5-curling frzl15,2" "route-stage5-lock frzl08,1" \
         "route-stage5-dock-lock twna02"; do
    set -- $r
    scripts/dev.sh run --content <folder> --start $2 --story-stage stage_5 --lang en --seed 1 \
        --autotest tests/autotest/$1.yaml --shots <captures>/stage5
done
```

- `route-stage6-*.yaml`: Stage 6 (Factorial Town) with `--story-stage stage_6`, in links that are resumed with
  `--start`; each one writes with `story_set` the triggers of the previous ones. The walks win the battles on the way
  with `encounters: win` (the guards by day and `species_aq` at night). `route-stage6-arrival.yaml`
  (`--start twna02`) boards `species_am` on the shore (tile 55, trigger 224), chooses Factorial Town, goes down to the
  sewer (340) and talks to `species_ar` (342). `route-stage6-guard.yaml` (`--start fact03`) talks to fact04's guard
  (347), to `species_as` in fact06 (343) and again to the guard for the shift (331); it waits in fact03 with the fast
  clock until 11:56, when fact04 has no guards, goes on to fact09 (332) and fact07, goes down with the elevator (Enter
  on tile 81) to the machine room (fact08b) and beats `species_at`: the light comes back (344).
  `route-stage6-numemon.yaml` (`--start fact06`) hears `species_as` (341), goes down to the sewer, beats `species_ar`,
  who joins (211), and goes back to `species_as` (328). `route-stage6-andromon-city.yaml` (`--start mayo02`) crosses
  the bridge from mayo08b to twnb13 (329). `route-stage6-andromon.yaml` (`--start fact06`) talks to `species_as`, who
  joins (330, 240), and beats `species_at` in fact08a (241). `route-stage6-metalmamemon.yaml`
  (`--start fact02 --seed 4`) runs into `species_au` (it comes out with a 3% chance on entering fact02; seed 4 brings
  it out), says yes to the challenge and beats it (227). `route-stage6-lock.yaml` (`--start twna02`) steps on the
  border: tile 56 from twna02 to mgen01 (stage 8) shows the notice and the player stays in twna02, and with trigger
  220 tile 52 leads to room10.

```sh
scripts/dev.sh run --content <folder> --start twna02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-arrival.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start fact03 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-guard.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start fact06 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-numemon.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start mayo02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-andromon-city.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start fact06 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-andromon.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start fact02 --lang en --seed 4 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-metalmamemon.yaml --shots <captures>/stage6
scripts/dev.sh run --content <folder> --start twna02 --lang en --seed 1 --story-stage stage_6 \
    --autotest tests/autotest/route-stage6-lock.yaml --shots <captures>/stage6
```

- `route-stage7-*.yaml`: Stage 7 (the optional route, in any order) with `--story-stage stage_7`. It has no mandatory
  event: its recruits raise the prosperity and, with 50 or more, `species_i` in the city (trigger 205) and a partner
  that is no longer one of the small forms, the old man's chat in his grown-up house opens Mt. Infinity (354).
  `route-stage7-mountain.yaml` (`--start twna02`) writes that, evolves the partner, talks to the old man (354) and tests
  the lock: the back room (room11) and tile 56 to mgen01 belong to Stage 8 and show the notice.
  `route-stage7-lock.yaml` (`--start trop02`) enters the mansion through the jungle (dgha01, closed in Stage 2) and in
  the cemetery (dgha02) steps on tiles 51 toward the Back Dimension (mgen11, stage 9, with variable 16 at 2): notice
  and the player stays in dgha02; without it, the "black force" scene makes them go back.
  `route-stage7-mist.yaml` (`--start mist02`) crosses the fog forest: its tiles send at random to the next clearing,
  back or in a circle, and the tenth time they always lead to `species_av`'s clearing (mist05), whose encounter (147)
  and story clear the fog (148, routine 37); it follows on to Toy Town.
  `route-stage7-toy-cards.yaml` (`--start omoc01`) gives with `story_set` the three cards the Toy Town collector asks
  for, who keeps them (`take_card`) and gives an item. `route-stage7-fishing.yaml` (`--start mayo06`) takes out the
  rod at the lake (trigger 45 and Meat as bait), casts and puts it away with Ctrl: fishing is not in the beta, but it
  breaks nothing. `route-stage7-stic.yaml` (`--start stic01`): until trigger 319 the swamp's center (stic02) is closed
  and Volume Villa cannot be reached; `species_aw` (one time in three it comes out, the route goes in and out until it
  sees it with `field.npc.9`) fights, the `species_ax` take the player to gkyo02, their boss opens the swamp (319) and
  the fog's straight path (114), and the route goes back through gkyo01 and the open swamp to mist01.
  `route-stage7-betl`, `-saib` and `-koda` go through the exits of Beetle Land, Overdell and the koda forest, there and
  back, winning the battles on the way.

```sh
scripts/dev.sh run --content <folder> --start twna02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-stage7-mountain.yaml --shots <captures>/stage7
scripts/dev.sh run --content <folder> --start trop02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-stage7-lock.yaml --shots <captures>/stage7
scripts/dev.sh run --content <folder> --start mist02 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-stage7-mist.yaml --shots <captures>/stage7
scripts/dev.sh run --content <folder> --start omoc01 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-stage7-toy-cards.yaml --shots <captures>/stage7
scripts/dev.sh run --content <folder> --start mayo06 --lang en --seed 1 --story-stage stage_7 \
    --autotest tests/autotest/route-stage7-fishing.yaml --shots <captures>/stage7
for r in betl:betl01 stic:stic01 saib:saib01 koda:koda00; do
    scripts/dev.sh run --content <folder> --start ${r#*:} --lang en --seed 1 --story-stage stage_7 \
        --autotest tests/autotest/route-stage7-${r%%:*}.yaml --shots <captures>/stage7
done
```

- The Stage 8 routes (Mt. Infinity and the ending) and Stage 9 routes (the post-game) go in pieces chained by a saves
  folder (`--saves`): each piece saves in `slot_01` and the next loads it (`--load slot_01`).
  `route-stage8-jijimon.yaml` (`--start room08`) writes the recruits of stages 1 to 7 (77 prosperity points; saving
  counts them again, like the original), gives the partner the chips from the merit shop and the Ultimate form, talks
  to the old man (section 0x33 of script 210, trigger 354), beats `species_ay` on leaving the house (`battle_win` if
  it takes long), sees the room11 scene and saves in the city. `route-stage8-climb.yaml` crosses the door (tile 58),
  goes up from mgen01 to mgen10, mgen98 and mgen99 on foot (running at a few fps overshoots the way-down exit next to
  some arrivals) with the wild ones won at the start; it beats the bosses of mgen06 (321) and mgen98 (326), answers
  `species_u` with the first reply (hurry: no battle, the critical path) and saves in mgen99.
  `route-stage8-final.yaml` (with `movies: true`) beats the final boss (trigger 50), reaches tend02 (with 50 points or
  more), says yes to the end-of-game save, sees the credits and goes back to the cover. `route-stage9-egg.yaml` and
  `route-stage9-back.yaml` load that finished game: the egg and `species_az` at the summit (256), and the Back
  Dimension from frzl05 through mgen11, mgen12 and mgen13 up to icsa08. `route-stage9-back-mansion.yaml`
  (`--start dgha02`, variable 16 = 2) and `route-stage9-back-canyon.yaml` (`--start gcan06`, variable 16 = 3) enter
  through the other two doors (the canyon one also sets trigger 200: without it, gcan05's guardian is still in the way
  to tile 51), cross mgen11 to mgen13 and leave for chka01 or ogre02, whose arrival section (0x3c) is the battle with
  the ending's boss: won, it sets 202, variable 16 goes back to 0 and the player ends up in the city.
  `route-stage9-back-again.yaml` (`--start room08`) continues from there: the old man gives the prize (215) and,
  twenty days later (variable 28), the city draws another door (script 162: clears 202 and 215 and sets variable 16 to
  1, 2 or 3) for the second battle; it sets trigger 203 so that the old man's door leads to the already grown-up city
  (twna02 to twna13), where that draw is. `route-stage8-lock.yaml` (`--start frzl05`) steps on the entrance to the Back
  Dimension with stage 8: the script's map change does not happen, the notice appears and the player stays in frzl05
  (the tile does not launch its section again until the player leaves it).

```sh
scripts/dev.sh run --content <folder> --story-stage stage_8 --start room08 --saves <saves> --lang en --seed 1 \
    --autotest tests/autotest/route-stage8-jijimon.yaml --shots <captures>/stage8
scripts/dev.sh run --content <folder> --story-stage stage_8 --saves <saves> --load slot_01 --lang en --seed 1 \
    --autotest tests/autotest/route-stage8-climb.yaml --shots <captures>/stage8
scripts/dev.sh run --content <folder> --story-stage stage_8 --saves <saves> --load slot_01 --lang en --seed 1 \
    --autotest tests/autotest/route-stage8-final.yaml --shots <captures>/stage8
scripts/dev.sh run --content <folder> --story-stage stage_8 --start frzl05 --lang en --seed 1 \
    --autotest tests/autotest/route-stage8-lock.yaml --shots <captures>/stage8
scripts/dev.sh run --content <folder> --story-stage stage_9 --saves <copy of saves> --load slot_01 \
    --start mgen99 --lang en --seed 1 --autotest tests/autotest/route-stage9-egg.yaml --shots <captures>/stage9
scripts/dev.sh run --content <folder> --story-stage stage_9 --saves <copy of saves> --load slot_01 \
    --start frzl05 --lang en --seed 1 --autotest tests/autotest/route-stage9-back.yaml --shots <captures>/stage9
scripts/dev.sh run --content <folder> --story-stage stage_9 --start dgha02 --lang en --seed 1 \
    --autotest tests/autotest/route-stage9-back-mansion.yaml --shots <captures>/stage9
scripts/dev.sh run --content <folder> --story-stage stage_9 --start gcan06 --lang en --seed 1 \
    --autotest tests/autotest/route-stage9-back-canyon.yaml --shots <captures>/stage9
scripts/dev.sh run --content <folder> --story-stage stage_9 --start room08 --lang en --seed 1 \
    --autotest tests/autotest/route-stage9-back-again.yaml --shots <captures>/stage9
```

#### The complete chain

The `tests/autotest/route-full-NN-*.yaml` routes play the whole story in order, from the cover to the credits, with
the lock as it is (`active_stage`). Each stretch starts from the game the previous one saved in `slot_01` (the first,
at the cover with a new game) and lasts under 20 minutes:

| Stretch | What it plays |
|---|---|
| 01 | Cover, new game, the opening, `species_a` (203) and the broken bridge fight (mayo08b) |
| 02 | Back to the gym (trai00): 32 sessions with care after each one |
| 03 | `species_y`'s afternoon stroll, `species_x` (204), `species_y`'s bridge (249) and `species_ab` (236) |
| 04 | The tunnel: the digger (140), the mine job (variables 17 and 18) and five days of care |
| 05 | The rock in tunn06 (120), `species_b` (209) and `species_z` (238); it saves in tunn09 |
| 06 | The partner's death from old age, the rebirth (the name stays), the gym and a Champion (`species_ar`) |
| 07 | **Shortcut**: the old man (87), `species_i` (205), the rest of the recruits written, `species_ay` (354) and twna08 |
| 08 | The climb up Mt. Infinity to mgen99 |
| 09 | The final boss, the end-of-game save, the credits and the cover |

The chain's partner dies of old age around day 12 (the tunnel's five days and `species_z`'s three), so stretch 06
plays the death and the rebirth for real: the old man rejects a partner of Rookie level or lower.

`scripts/story-chain.sh` runs them in order, stops at the first stretch that fails and writes one line per stretch
(`full-NN PASS 4m49s ...` or `full-NN FAIL ...: <reason>`). A stretch that fails is repeated once before it is
counted as failed (`CHAIN_RETRIES`), because with the machine loaded the routes sometimes arrive late. `--from NN`
starts at stretch NN with the game the previous one left in `<output>/saves/<NN-1>/`:

```sh
DISPLAY=:23 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
    scripts/story-chain.sh --content <folder> --out <output> [--from 03] [--to 05]
```

With the GPU (`nvidia_icd.json` instead of `lvp_icd.json`) each stretch goes faster, but the driver leaves a few KiB
unfreed on exit and the `ci` build's `LeakSanitizer` closes the game with an error even if the route passes: add
`ASAN_OPTIONS=detect_leaks=0`.

Shortcuts the chain declares (what a real game would earn by playing more): the wild ones on the way are won at the
start (`encounters: win`); the bosses the partner does not beat in time end up won after a while (`win_after`); the
food, medicines and chips are in the bag instead of being bought; time passes quickly (`clock_speed`) to wait for the
afternoon or the tunnel's days; and stretch 07 writes in two parts the recruits of stages 4 to 7 (first `species_aj`,
`species_ak` and `species_ah` for the 15 points of the old man's first scene; then the rest up to 77 prosperity),
which do not have chained stretches yet: `species_aj`'s cave asks for 45 prosperity.

An autotest starts in the field, without the main menu, unless the route says `main_menu: true`. It neither reads nor
saves the user's settings. Since it has neither the opening's questions nor the movies, its new game starts in the
seeded chain (File City's free field) unless the route says `new_game: original`, like `route-stage1`. The questions
and the name box only appear if the route says `new_game_scene: true`. With `device: mouse`, `wait`'s `confirm: true`
turns the text box's pages with a click on the box instead of with Enter.

```sh
scripts/dev.sh run --content <folder> --autotest tests/autotest/route-keyboard.yaml --shots <captures folder>
```

The game closes completely (scene, UI, renderer and device) before judging the run, so that what is reported on
destruction counts. It exits with 0 if all the steps are met, the log has no `[error]` line (UI script errors,
resources that fail to load...) and, with the validation layer active (debug build), the layer has said nothing;
otherwise with 1 and the reason (`autotest: FAIL: ...`). It exits with 2 (`PASS (partial: skipped ...)`) if
everything is met but `--skip-steps` skipped `walk_to` or `wait` steps, which are the ones that move the player and
check the field. The log always says the layer's state: `on` (and how many messages), `unavailable` (requested but not
installed) or `off` (release); without the layer messages are not counted. It also says how many `[error]` lines
there were, the time of each step, each map change (read and total, in ms) and the frame times (`all` with all the
frames, `steady` without those that are slow on purpose: map load, resize, capture): frames, average fps, minimums and
99th percentile. The routes check that the partner follows the player inside each map (`partner_within` after a
`walk_to`), not only on entering, where it always appears close.

A window cannot be larger than the desktop's usable part: on a 1920x1080 screen with panels, the `resize` steps to
1920x1080 and 2560x1080 are skipped with a notice (`resize to ... skipped: the desktop's usable area is ...`) and the
report lists them. To cover them, an Xvfb with software Vulkan (lavapipe) is good for checking functionality, not
performance. Without a window manager there is no full screen: if in the middle of its `timeout` the window has
changed neither mode nor size, the `fullscreen` step skips itself with a notice (`fullscreen on skipped: the window
manager did not apply it`), and `--skip-steps fullscreen` skips it without waiting:

```sh
Xvfb :23 -screen 0 2560x1440x24 -nolisten tcp &
DISPLAY=:23 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json scripts/dev.sh run ... --autotest ... \
    --skip-steps fullscreen
```

### Routes on the cooked package

The routes above run on the extracted content (PNG, WAV, glTF), but a player loads the cooked package (KTX2, Opus)
packaged in a `.pak`. If `dwx cook` converts something the game needs to read in another way, the extracted content
does not notice: that is how the map backgrounds and the font were lost, which were cooked to KTX2 and no longer
loaded. That is why the gate cooks the extraction and runs some routes on the result.

It cooks, packages and checks inside the container. It needs a build of the preset; with a preset that does not build
for Linux (`windows-cross`) use the `dwx` from the `ci` build:

```sh
DWE_CONTENT=<extracted folder> DWE_COOKED_DIR=<output> PRESET=ci scripts/dev.sh cook-content
```

It reads `DWE_CONTENT/base` and writes to `DWE_COOKED_DIR` (`build/cooked-content` by default; outside the repository
it is mounted in the container at the same path). Before writing anything it rejects an output that is inside, or that
contains, `DWE_CONTENT`, the disc folder, `DWE_DECOMP`, `~/code/assets` or the team's protected folders
(`_work/content*`, `_work/p2/content`, `_work/p2/scratch-routes`, `_work/p3-inventory`, `_work/saves-play`,
`_work/mouse-route`, `_work/validate-runtime`, `_work/viz` and `build/verify-p1` of the main checkout), also through
symbolic links, and the repository's own folder. If git does not tell it where the main checkout is, it does not cook:
without it, it does not know which folders to protect. It writes:

- `folder/base`: the cooked pack (`dwx cook`). **By hand, cooking is incremental:** the second time it only cooks what
  changed in the extraction and keeps the rest. If you change `dwx` (a cooker, `PngOnlyImages`), delete
  `DWE_COOKED_DIR` first, or the result may be from a previous cooking.
- `archive/base.pak`: that pack in a file (`dwx pack`). It is deleted before cooking, so a cooking that fails does not
  leave an old `.pak`.

Then it passes `dwx validate --engine` over the cooked pack and over the repository's `game` pack mounted on the `.pak`
(this also checks the `.pak`); both have to end in `0 problems`. It takes about 8 minutes with the `ci` preset the
first time and about 5 afterward.

`check-full` does this step by itself (`cook-content.log`) when you give it `DWE_CONTENT` and the preset builds for
Linux; with `windows-cross` it skips it and says so. It does not use `DWE_COOKED_DIR`: it cooks from scratch in
`build/gate-cooked`, which it empties first, so that nothing from a previous cooking hides a failure. Then it runs
these routes on the `.pak`, in both languages, in the same Xvfb as the others (`<cooked>` is `build/gate-cooked` after
`check-full`, or your `DWE_COOKED_DIR`):

```sh
for lang in en es; do
    scripts/dev.sh run --content <cooked>/archive --mods tests/mods/example_mod --lang $lang \
        --autotest tests/autotest/route-mod-map.yaml --skip-steps fullscreen
    scripts/dev.sh run --content <cooked>/archive --text-font original --lang $lang \
        --autotest tests/autotest/route-fade.yaml --skip-steps fullscreen
done
```

Between the two they cover what the game reads from its PNG (`assets::PngOnlyImages`) and the rest of the cooked
package:

- `route-mod-map`: a mod's map (in PNG, uncooked) on the cooked base, and its exit to File City: the backgrounds and
  texture pages of `twna01` from the `.pak`, and the player's and the partner's models.
- `route-fade` with `--text-font original`: the backgrounds and texture pages of `twna01` and `room08`, with the
  palette of the time, and the map name during the fade drawn with the bitmap font (in `es`, with the characters it
  lacks taken from the modern one).

Besides `autotest: PASS`, 0 validation layer messages and 0 `[error]` lines, each one's log has to say
`ui: original bitmap font font/main: ...`: if the font atlas does not load, the game only warns (`[warning]`) and
continues with the modern one, so the route would pass anyway.

On Windows there is no container: the same steps with `dwx` from the Windows build
(`build\windows-clang-debug\apps\dwx\dwx.exe cook <extracted folder>\base -o <output>\folder\base`, then `pack` and
`validate`), and the game with `--content <output>\archive`.

### The gate's runs

A gate runs, on the exact tip, in Xvfb with lavapipe and with `--skip-steps fullscreen`:

- [ ] All the `tests/autotest/route-*.yaml` routes on the extracted content, in `en` and in `es`. `route-dev-assets` with `--dev-assets`, `route-mod-map` with `--mods tests/mods/example_mod`, `route-game-modes` with `--mods tests/mods/game_mode`,
  `route-skins` with `--mods tests/mods/ui_skin` and the `route-save-load` → `route-save-load-check` pair with a
  saves folder of their own.
- [ ] `check-full` with `DWE_CONTENT` (`cook-content` step, from scratch in `build/gate-cooked`) with `0 problems` in
  both checks.
- [ ] `route-mod-map` and `route-fade` on the cooked package, in `en` and in `es`: 4 runs.

Each run ends in `autotest: PASS`, with the validation layer at 0 messages and
`autotest: 0 [error] lines in the log`; those on the cooked package, also with the bitmap font loaded.

`scripts/gate.sh --tier daily` runs all this for you: the check, the Windows build and all the runs, one after
another or on several Xvfb displays at once. The short one (`scripts/gate.sh`) runs the smoke routes and the ones you
ask for. Both are optional (what is needed to merge is the pull request's CI) and are in
[passing the gate](gate.md). If you add a route that needs options of its own (`--mods`, `--dev-assets`), add them
also in `route_job` of `scripts/gate/route_jobs.sh`.

### Route format

```yaml
name: keyboard            # prefix of the captures
device: keyboard          # keyboard | gamepad | mouse: that of the steps that do not say another
gamepad: xbox             # family of the virtual controller (xbox, playstation, nintendo...)
timeout: 600              # seconds for the whole route
main_menu: false          # true: starts at the main menu instead of in the field
boot_screen: false        # true (with main_menu): the cover starts at "Press Start"
language_first: false     # true (with boot_screen): first, the language box, as when starting the game
skin_first: false         # true (with boot_screen): after the language box (or without it, at the start), the look box, as on every game start
new_game_rules: false     # true (with main_menu): the new game goes through the rules screen (otherwise it starts with the original's)
new_game: seeded          # original: the new game runs the opening (seeded by default, the free field)
new_game_scene: false     # true (with main_menu): the new game goes through the old man's questions and names
walk_control: keys        # keys | mouse: what moves the player (mouse by default if device is mouse, otherwise keys)
names: {player: Ana, partner: Rex}  # the new game's names, as if the name box had asked for them
load_save: save.yaml      # starts by loading that game (relative to the route), without main_menu or new_game
movies: false             # true: the game's movies play as without a route (the ending credits)
text_fit: false           # true: each capture also checks that the texts fit (like expect_text_fits: all)
steps:
  - wait: {map: twna01, seconds: 1}
  - walk_to: {trigger: 112}
  - screenshot: room08
```

| Step | What it does |
|---|---|
| `wait: {seconds, map, partner_within, player_near: [x, z], radius, menu_open, map_name, shown, hidden, partner_has, setting, partner_model, timeout}` | Waits the seconds and until the conditions are met (all those given). `map_name: <id>` waits for a map change to show that map's name, and then it does not look at the others. `shown: <document>` and `hidden: <document>` wait for a UI document (`battle`, `partner_menu`) to be seen or stop being seen. `partner_has: <key>` waits for the partner to have that state, by the key from the status page (`partner_menu.condition.sleepy`, `.hungry`, `.needs_toilet`, `.sick`...). `setting: {<name>: <value>}` waits for a setting to have that value (`battle.phase: end`). `partner_model: <id>` waits for the partner to be drawn with that model (`digimon/species_b/model`). `ticks: <n>` waits those logic ticks (1/60 s each) instead of clock seconds |
| `wait: {confirm, answer, win_after, ...}` | In addition, `confirm: true` presses confirm while waiting, every second, in the story text box (not in a chooser, whose cursor is visible: that one the route answers, except with `answer: true`, which chooses the entry under the cursor, for a long run of the same choice like the customers of the shop job; neither with the improved job's counter or rules open on top, which the route presses) and in a battle's final box, which plays itself. `win_after: <s>` (with `confirm`) ends with `battle_win` a battle that has been on screen that long (and not before its opening ends), for the bosses that the partner of a written game does not beat |
| `hold: {keys: [<key>...], ticks}` | Holds those keys down exactly that many logic ticks (`keys: []`, none) and releases them afterward; consecutive `hold` steps chain without gaps, tick by tick, even if the frames are slow. To repeat an input to the tick, as in the [state trace](state-trace.md) |
| `walk_to: {trigger \| point: [x, z], radius, walk, stop_on_warp, stop_on_scene, stop_on_closed, encounters, device, timeout}` | Walks with the device to the cells with that trigger (and the map change it fires) or to the point; it plans with the map's collision and avoids the other warps. `walk: true` walks instead of running. `stop_on_warp: true` ends when the map change starts, without waiting for the new map. `stop_on_scene: true` ends as soon as a story script holds the player on the way (the scene of a section tile) and leaves the scene running for the next steps. `stop_on_closed: true` ends when the story lock leaves the exit closed and shows its notice (the `dialog` dialog): the player stays on the map. If it steps on an item on the ground, it confirms its notice as the player would and continues. `encounters: true` holds out an encounter on the way, which arrives whenever the time wants (a wild monster approaches, talks and a battle starts): while a script holds the player or the battle is visible, it stops walking, turns the text box's pages with confirm (Enter, or South with the controller) every second and lets the battle end on its own, also pressing confirm on its final box, which writes each line with one press (a script's does not let you flee, as in the original; if it is lost, the player ends up in the old man's house and the route will see it); then it plans again from where the player ended up. That time does not count toward the step's `timeout`, but does toward the route's. With `encounters: win` the encounter's battle ends won as soon as it starts (like `battle_win`), so that the route's partner does not lose on the way. A destination chooser from another tile on the way (the path steps on the tile the player stayed on again) is rejected with cancel and the walk continues. With the keyboard or the controller the walk steers once per drawn frame, so a slow frame (the software renderer draws few per second) runs at most 4 logic ticks (`Autotest::ticksThisFrame`, `SteeredTicks`) instead of its whole quarter second with the previous frame's keys: before, a run overshot the path's curve and stepped on a warp next to it (mgen02, the arrival from below next to the way down: 2 of 2 runs went back to mgen01; with the cap 0 of 6). The game's ticks stay fixed at 60 per second; only the route's clock goes slower |
| `menu: open \| close` | Opens or closes the menu with the device (mouse: the HUD's "Menu" button and right click) |
| `menu_select: <id>` | Focuses and presses an entry of the open menu: the main one if visible (`play`, `language`), otherwise the pause one (`open-minimap`, `minimap-mode`...), otherwise the partner's menu (also the bag that the battle opens: `slot-0`..., `opt-use`), the battle screen (`command-0`..., `open-bag`), the free exploration selector or, on the cover, the dialog (`yes`, `no`, `ok`) and the slot list (`slot-1`..., `no-save`, `back`). With the keyboard or the controller it moves the focus down, up, right and left until it arrives; with the mouse it clicks the center of the entry |
| `script: <lua>` | Runs a line of the game's scripts, like the console; the debug commands are further below |
| `script_if: {script, <wait conditions>}` | Runs the script only if all the conditions are already met (those of `wait` except `map_name` and `seconds`); otherwise it goes on without waiting and says so in the log |
| `expect_setting: {<name>: <value>, timeout}` | Checks a setting published to the UI (`minimap: "off"`) or from the partner's state; `field.overlaps` is how many times two characters (or one and a wall) overlapped for more than a second with the player free (`expect_setting: {field.overlaps: 0}`); `saves.files` is how many files there are in the `--saves` folder (0 without it); `has_class.<document>#<element>.<class>` (`"true"` or `"false"`) says whether that element has that class, like the tab that shows a page (`has_class.partner_menu#tab-tech.selected`); `story.trigger.<n>` (`"true"` or `"false"`) and `story.var.<n>` read the story's triggers and variables, as the original keeps them in its script state (`wait: {setting: {story.trigger.249: "true"}}` waits for `species_y` to join); `field.npc.<id>` (`"true"` or `"false"`) says whether the NPC with that script id is on the map now (the swamp's `species_aw` only comes out sometimes). If it is not met, the failure says the value it read |
| `expect_range: {<name>: [min, max], timeout}` | Checks that the setting is a number between those two, both included (2 s by default); otherwise, the failure says the value it read. The routes of the [training check](check-training.md) use it with the `gym.session.*` settings |
| `remember: <name>` | Saves the value of a setting for a later `expect_change` |
| `expect_change: {<name>: up \| down \| changed, timeout}` | Checks that the setting has gone up, down or changed since the saved value (2 s by default) |
| `repeat: {times, steps}` | Repeats those steps in place that many times |
| `when: {<wait conditions>, steps}` | Does those steps only if all the conditions are already met (like `script_if`); otherwise it skips them and says so in the log. It is for what the game draws lots for when played (`when: {setting: {story.var.116: "1"}, steps: [...]}`) |
| `screenshot: <name>` | Saves `<route>-<NN>-<name>-<width>x<height>.png` in `--shots` |
| `expect_text_fits: <document> \| all` | Checks that each text visible in that UI document (or in all) fits: inside the drawn box that surrounds it, what clips it and the window, without crossing the edge of another box or covering another text, with one original pixel (3dp) of margin. What an opaque later box covers does not count, nor does a box that is opening or closing. It waits up to 1 s for the box to settle in place; if it still does not fit, it writes each text to the log (`in <document>, '<text>' (<element>) is out of <box> by <n> dp`) and the route continues, but ends up failing |
| `expect_clear: <document>` | Checks that nothing visible of that document (text, box, image) covers what another shown document draws (`Ui::drawnRects`, without the veils that cover half the window or more), like the save notice. It waits up to 1 s; if it still overlaps, it writes the first pair of rectangles to the log and the route ends up failing, like `expect_text_fits`. With `<document>#<element>` it checks only that element, which has to be visible, against the rest of its document and of the others (`Ui::drawnRectsAround`, without the boxes that surround it entirely, its panel), like the HUD's time in digits (`hud#field-time`) |
| `resize: [width, height]` | Changes the window and waits for the render to have that size |
| `fullscreen: true \| false` | Full screen |
| `press`, `release`, `tap: {key \| button}`, `axis: {axis, value}`, `click: [x, z]` | Loose controls: keys, virtual controller buttons and axes, a click on a point of the ground |
| `click: {element: "<document>#<id>"}`, `click: {window: [x, y]}`, `hover: {element \| window}` | Click on the center of an already laid-out UI element (`story_textbox#row-2`, `naming_box#cell-1-4`, `naming_box#button-ok`) or on a point of the window, in pixels; `hover` only takes the pointer there, without pressing. They do not need the field, so they work in the opening. With `walk_control: keys`, `click: [x, z]` (the ground) does not click: it gives the order to go there directly, as a route shortcut like `place` |

Coordinates are in the engine's world (x, z on the ground; 100 units per collision cell).

The partner's settings are read live, with the partner's menu open or not: `partner.name` (the key of its name, the
same in any language), `partner.age`, `partner.weight`, `partner.lives`, `partner.hp`, `partner.max_hp`,
`partner.mp`, `partner.max_mp`, `partner.stat.<0-3>.value` (offense, defense, speed and brains, in the order of the
status page), `partner.meter.<0-2>.value` (happiness, discipline and virus) and `partner.condition.count` with
`partner.condition.<n>.key`. The bag publishes what happened with the last item in `bag.result`.

Debug commands of the scripts, which also work in the console. They do the same as the player's action and add no
rules; they are shortcuts for routes and debugging:

| Command | What it does |
|---|---|
| `battle("")` | Starts a battle against the first NPC on the map that can fight; `battle("2,3")`, against those NPCs of the map (their number in the map's NPC list). With `story:` in front (`battle("story:2")`) it is a story battle: you cannot flee and the enemies do not spend MP |
| `place("<who> <x> <z> <rotation>")` | Puts the player (`player`), the partner (`partner`) or the NPC with that script id (`npc:6`) at that point on the ground and with that rotation, in the original's axes and units (z into the screen, 4096 per revolution); it keeps its height. To start a comparison with the original in the same place ([state trace](state-trace.md)) |
| `battle_win("")` | Ends the current battle with the enemies defeated: they fall as with a final blow and the victory, the items and the stats gained follow |
| `battle_lose("")` | The opposite: the partner falls as with a final blow and the battle is lost (to test a lost tournament) |
| `battle_time_up("win")` | In a cup battle, time runs out now: with `"win"` the rival loses HP until it has taken 200 more damage than the partner (the partner wins), with `"lose"` the other way round and with `""` they tie (to test the time-up notice) |
| `give_item("<id> <amount>")` | Puts items in the bag (`give_item("meat 3")`) |
| `story_set("trigger <n> on\|off")`, `story_set("var <n> <value>")`, `story_set("money <bits>")`, `story_set("card <n> <copies>")`, `story_set("tally <amount>")` | Writes a story trigger or variable, as the original keeps them in its script state, without running any script (`story_set("var 1 10")`: ten prosperity points), the money the player carries (from 0 to 999999), the shops' tally (from 0 to 32767: what was earned in the canyon shop job, which routines 31 and 56 add and copy to variables 243 and 244), or gives copies of a card like `give_card` (`story_set("card 19 1")`). To start a route at a point of the story that the seeded game does not reach |
| `evolve("<species>")` | Makes the partner evolve to that form like a map script (`evolve("species_b")`); the field draws it with its model |
| `use_item("<id>")` | Uses one from the bag on the partner, like "Use" in the bag; the partner may refuse it |
| `drop_item("<id>")` | Drops that item on the ground at the player's feet, like the drop of a defeated enemy (`drop_item("meat")`) |
| `toilet("")` | Takes the partner to the potty |
| `sleep("")` | Puts it to bed, if it is sleepy |
| `scold("")` | Scolds it, like "Scold" in the care tab |
| `train("<place> <course>")` | A training session (`train("trn brain")`) |
| `partner_set("<value> <number>")` | Writes a partner value, from 0 to 99999: `hp`, `mp`, `offense`, `defense`, `speed`, `brains` (the base stats, HP and MP in their units), `weight`, `care` (care mistakes), `battles`, `hours` (the evolution counter) and `discipline` (discipline, within the mood's limits) (`partner_set("hours 71")`); `poop` gives it the urge to go to the potty with the frames it holds out before doing it on the floor (`partner_set("poop 1")`). To leave the partner just before an evolution check |
| `learn_move("<technique> [slot]")` | The partner learns that technique (and those that come with it) as in brains training and, with a slot from 0 to 3, equips it there (`learn_move("meltdown 0")`), to see a technique it does not know yet |
| `clock_speed("<multiplier>")` | Accelerates the clock: 1 is one game minute per real second, 600 is ten hours |
| `hd_scenery("on"\|"off")` | Turns the high-definition scenery on or off, like its options row (and saves it the same) |
| `battle_speed("normal"\|"key"\|"fast")` | Changes the battle speed, like its "Quick battles" options row (and saves it the same) |
| `shop_job_mode("improved"\|"original")` | Switches Monochromon's shop job, like its options row (and saves it the same); the stage 4 route plays the original |
| `model_characters("on"\|"off")` | Turns the 3D characters on or off for the game (it does not save it) |
| `save("slot_NN")` | Saves the game in that manual slot (`save("slot_01")`); the old autosaves (`autosave_NN`) are never written. An autotest only saves with `--saves` |
