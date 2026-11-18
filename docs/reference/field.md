# Reference sheet: field

How the player and the partner moved around the field maps in the original game, according to the decomp, and what
`packages/game/field` does with it. It is step 1 of the [design cycle](../conventions/design-cycle.md).

Marks: **(verified)**, read in the decomp's code; **(measured)**, computed from the disc data with the code's rules;
**(assumption)**, our own interpretation pending confirmation. Original units: 100 is one collision cell, 4096 one
full turn. For the axes and conversions see [world space](../architecture/world-space.md) and
[maps](../formats/maps.md#in-the-engine).

## What it does

The player moves with the d-pad in eight directions relative to the map camera. By default it runs; with a button held
it walks. The map's collision grid stops it at walls and lets it slide when going diagonally. Stepping on certain
cells makes something happen: a script, the bathroom or a warp to another map. The partner follows it at a certain
distance, runs if it falls behind and, if it loses sight of the player, follows its trail of cells.

## Rules

### Pace

- The game loop waits three vertical retraces per frame (`gameLoop`, `VSync(3)` in `main.c`): about **20 frames per
  second** in NTSC **(verified)**; that `VSync(3)` is three retraces is the console library's behavior
  **(assumption, almost certain)**. All the per-frame speeds in this sheet are at that pace. The engine expresses
  everything per second and runs at a fixed tick (60 Hz in the app).

### Player movement

- **Direction** (`tamer.c: tamerTickWalkingState`) **(verified)**: `getMapRotation` gives the camera heading (from the
  reference to the viewpoint) and it is rounded to multiples of 512 (45°) with `rot / 0x200` and a round up if the
  remainder is 0x100 or more; with negative headings the division truncates toward zero and the rounding is not
  symmetric. Up adds 0x800, down 0, left 0x400, right 0xC00 and the diagonals the intermediate values; up wins over
  down and left over right. The turn is instantaneous (`setTamerDirection`).
- **Running and walking** **(verified)**: with the d-pad pressed, animation 2 (walk) if R1 or the cancel button is
  held, and otherwise 3 (run); without the d-pad, 0 (idle). Running is the normal state.
- **Speed**: there is no constant. The player advances with the root motion of its animation (`anim.c:
  tickAnimation`, `applyRootMomentum`), rotated by its orientation **(verified)**. With the player model's data
  (`BOYS.MMD`): animation 3 (run) advances 50 per frame and 2 (walk) 20; 0 (idle) nothing **(measured)**. At 20 per
  second: **1000 and 400 per second**.
- Against a wall the animation continues at its speed: the collision cancels the advance, not the animation
  **(verified)**.
- The first frame after changing animation does not advance (`anim.c: startAnimation` zeroes the momentum)
  **(verified)**; the engine reproduces it.

### Collision

- The grid is 100 × 100 cells of 100 units; 0 free, bit `0x80` blocked, the rest triggers (`map_collision.c`)
  **(verified)**.
- Before moving, the game tests the whole step (`map.c: entityCheckCollision`, `checkMapCollision`): it places the
  character one step ahead (`entityMoveForward`) and, according to the sign of the step on each axis, looks at the
  line of cells at a **radius** ahead on that axis, across **half a radius** on each side (`checkMapCollisionX/Z`)
  **(verified)**. The player's radius is 100 (`DIGIMON_DATA[0].radius`) **(measured)**.
- If it collides (`map.c: collisionGrace`), it tests the four cardinal directions in a fixed order that depends on its
  turn (`COLLISION_GRACE_ROTATION`). Each blocked candidate cancels that component of the movement; on the first free
  one, the character deviates toward it up to 0x50 in that frame. Since `setTamerDirection` sets the d-pad turn again
  every frame, the deviation does not accumulate: head-on against a wall the player advances sideways at 12% of its
  speed, and only stops if all four are blocked **(verified in the code; not seen in the game)**.
- The first test excludes the partner; `collisionGrace` does count it among the candidates **(verified)**. With an NPC
  it stops.
- Since the lateral probe measures a radius (100) wide, it always touches two rows of cells except at exact positions:
  a corridor one cell wide cannot be walked and one two cells wide can, along the line between the two **(measured
  with the code's rules)**. `getModelTile` computes the column as x / 100 + 50 and the row as 50 - z / 100 with C
  division (which truncates toward zero) and then subtracts one from the column if x < 0 and from the row if z > 0.
  It gives the same as `floor` except at exact edges: an x that is a negative multiple of 100 falls one column further
  left, and a z equal to 0 or a negative multiple of 100, one row further. `checkMapCollisionX/Z` compute the row in
  another way (49 - z / 100, plus one if z < 0), which only differs from `getModelTile` at z = 0. The engine uses
  `floor` in all cases.

### Triggers and warps

- Every frame in normal state (`TAMER_STATE` 0) the cell under the player's center is looked at
  (`tamer.c: checkMapInteraction`, `getTileTrigger`) **(verified)**:
  - 51–79: calls the section with that number of the map's script, if no other script is running;
  - 80–109: the same, but on pressing confirm;
  - 110–119: warp through exit `value - 110` of the map: goes to map `targetMap` and appears at exit `targetExit`;
  - 120: a script (section 0x4E2) if the partner has bit 8 of its condition (the bathroom, **assumption**).
  - In the engine these ranges (and bit 0x80 of the cells that block the way) are data:
    `content/packs/game/field/triggers.yaml` (`TriggerBands`, `packages/game/field`). The `game` pack's file carries
    the original's and a test checks that every value 0–127 means the same; `dwx`, the minimap and the world map still
    read the original's (`core/collision_triggers.hpp`).
  - With `TAMER_STATE` other than 0 the function returns before looking at any cell (`tamer.c:685`; only
    `tamerTickWalkingState` calls it, state 0 of `tamerTickOverworld`, `tamer.c:455`), and sections 51–109 also
    require that no script is running (`IS_SCRIPT_PAUSED` 1). In the engine this is equivalent to the field step
    getting to look at the cells: no fade-in in progress and not leaving the map. **It is equivalent only because** the
    engine does not carry those other states inside the field (6 stopped, 7 pick up item, 8 train, 11 fish, 14 chest,
    15–20 opening, ending and loss of lives); with a script running the story discards the section, like the original.
    If the field ever carries them, they must cut this check. The cell values (51, 80, 110 and 10 exits) are in a
    single place, `core/collision_triggers.hpp`.
  - **Gap (arrival):** when the fade-in ends the original calls `checkMapInteraction` again (`tamer.c:
    tamerTickChangeMap`, `tamer.c:1391`), and every frame in state 0 it calls it again on the cell it is in (not only
    on stepping on it). The engine fires a cell only on entering it and not the spawn one, which gives the same as long
    as no spawn falls on a trigger cell (**assumption**, not measured on the disc).
  - **S4:** in the original, confirming in front of an NPC talks to it before looking at the cell. The engine does the
    same: if confirm touches an NPC (`Field::touchNpcs`), the section of the 80–109 cell for that frame is lost.
- **The map change** (`tamer.c: tamerTickChangeMap`, `fade.c`) **(verified)**: the clock stops (`stopGameTime`) and the
  camera stops following. `fadeToBlack(20)` puts a rectangle in front that **subtracts** a gray that rises 7 per
  frame; on frame 10 the map name appears and on 20 the new one is loaded (`changeMap`). Meanwhile, the player
  continues with animation 2 (walk) and without a collision test: it advances about 400 units. Afterwards,
  `fadeFromBlack(20)` removes the gray over another 20 frames and only then does the player return to the normal
  state, the partner to its own and the clock continues (`startGameTime`). Each fade lasts **one second**. Script
  warps do the same (`map.c: scriptTickChangeMap`).
- **The fade, frame by frame** (`fade.c: renderFadeOut`, `renderFadeIn`, `renderFade`) **(verified)**:
  - The rectangle covers the 320 × 240 on layer 1 of the ordering (in front of the map, the characters and the HUD) in
    semi-transparent mode 2 (background − rectangle). It is a textured polygon: its texel (page 896, 256, u 250, v 253,
    CLUT 256, 502: entry 4 of `ETCDAT/ETCTIM.BIN`, palette 6, index 0) is `0xFFFF`, white with the semi-transparency
    bit. The console multiplies the texel by the tone / 128, so what is subtracted is white × tone / 128: with a tone
    of 128 or more the screen is **entirely black**, not a gray of 140.
  - The outgoing tone rises `160 / (20 + 1) = 7` per frame up to 160. `fadeToBlack` does not set it to 0: it starts at
    what the last fade back left, which ends at `160 − 20 × 7 = 20`. So the outgoing one goes 27, 34… and the screen
    is black from its frame 16 (132). The new map is loaded on frame 20 (`FADE_OUT_CURRENT` reaches 20), which already
    draws the first tone of the way back.
  - The way back drops from 160 in steps of 7 (153, 146…): black until its frame 4 and clearing little by little down
    to 20 on its frame 20, on which the player already moves; on the next frame there is no rectangle anymore.
- **The map name** (`map.c: addMapNameObject`, `renderMapName`; `font.c: drawString`) **(verified)**: on frame 10 of
  the outgoing fade `MAP_NAME_PTR[MAP_ENTRIES[map].loadingName]` is written (in the base pack, `loading_name` of
  `data/maps.yaml` and `map_names` of `text/en/main.yaml`) and it is drawn on layer 0, in front of the fade;
  `changeMap` removes it, so it is seen for 10 frames (half a second) while the screen darkens. It goes in the center
  of the screen (the American version places it at `x = 12 − (length / 2) × 8`, `y = −6`; the names carry spaces on the
  sides up to 24 characters, so the text ends up centered to within a few pixels), with the game font (11 pixels
  high), in light gray (color 1 of the font's CLUT, `0x6B5A`), without a box and without a shadow. It appears on
  every warp in the map's table; on script ones, if the script asks for it (`consumeMapChangeShowName`: always in the
  trigger sections, not in the event ones, unless variable 6 says otherwise).
- There is no protection against firing the warp again on appearing: the spawn points in the data never fall on warp
  cells **(verified)**.
- **Appearance** (`tamer.c: loadMapEntities`, `partner_impl.c: setupPartnerOnWarp`) **(verified)**: the player appears
  at the exit's `spawn`, with its turn. The partner appears 200 away in the cardinal direction behind the player (by
  turn bands: `z + 200` if it faces −z, etc.), always, with the player's turn plus half a turn;
  `partnerTickCollision` makes it look at the player from the next frame.
- An entry in the warp table only fires from a 110–119 cell of its exit; those without a cell are just arrival points
  of other warps (`tamer.c: checkMapInteraction`, `TARGET_MAP = targetMap[trigger - 110]`) **(verified)**.
- Many exits are not in the table but in the map's event script (`warp_to`): the door of the old man's house (`room08`,
  trigger 51) runs section 51 of script 178, which takes the player to the door and does `warp_to` to `twna01` exit 2
  while trigger 203 is off **(verified in the extracted script)**. `dwx` extracts them all with their conditions
  ([script warps](../architecture/script-warps.md)).

### The partner

- It also moves with the root of its animations **(verified)**. With the initial partner (`species_a`): walk (2) 22.6
  per frame, slow walk (3) 14.9 and run (4) 43.5 **(measured)**: 452, 298 and 870 per second. It runs slower than the
  player.
- **Distance** (`partner.c`, the companion's closeness to the player) **(verified)**, with R its radius (120 in
  `species_a`): farther than 3.5 R it runs, between 2.5 R and 3.5 R it walks, closer it finishes the walk cycle and
  stops (or stops dead if it collides with the player).
- **Its state changes the pace** (`partner.c: partnerTickWalking`, `setPartnerSlowWalking`, `setPartnerIdle`)
  **(verified)**: unhappy (condition `0x10`) it walks instead of running; with condition bits 1, 2, `0x20` or `0x40`
  it walks slowly (animation 3) and when stopped it uses 1 instead of 0. From running to walking it changes at once;
  from walking to running it waits for the walk cycle to end.
- **Stopped** (`partner.c`, `updateConditionAnimation`) **(verified)**: it looks at the player; after 1 to 5 cycles of
  the idle animation (`randomLimit(5) + 1`) it does an emotion animation according to its state: happy (happiness 31 or
  more), sad (less than −30), sick or injured, needs to go to the bathroom, hungry, tired (tiredness 80 or more),
  sleepy or unhappy; when it ends it goes back to idle with another count. The counter is a signed 8-bit one and
  wraps at 127.
- **Heading** (`map.c: partnerTickCollision`) **(verified)**: it looks all at once every frame toward the player, or
  toward the oldest cell of its trail if it has one, and removes it on stepping on it.
- **Trail**: when the player changes cell, if the view between the two is blocked (`isFiveTileWidePathBlocked`: five
  lines of cells, shifted up to two cells to each side on the minor axis), the player's previous cell is noted (up to
  30), only if the last one in the trail is not visible either; if the view is clear, the trail is erased. Also, every
  frame, the first of the 2nd, 3rd and 4th newest cells (in that order) that is visible from the player makes the
  later ones redundant **(verified)**.
- **Collision**: the partner tests collision like the player and, if it collides, applies `collisionGrace` (outside
  combat) **(verified)**.
- **Getting stuck**: if it has a trail, its oldest cell is off screen (more than 200 pixels horizontally or 160
  vertically from the center, `isTileOffScreen`) and the partner is not visible either, it appears in that cell
  **(verified)**.
- On changing map the trail is erased (`map.c: loadMap`, `initializePartnerWaypoint`) and it is placed as on
  appearance **(verified)**.

### Camera

- `map.c: setupMap` centers the 320 × 240 window on the background and takes it to the player
  (`initializeDrawingOffsets`); after that `tickCameraFollowPlayer` moves it every frame by as much as the player has
  moved on screen, without smoothing or dead zone, and `updateDrawingOffsets` and `cameraIsAtEdge` limit it to the
  background, so the player is off-center only near the edges **(verified)**. It only moves with the player in normal
  state and with the d-pad pressed, so it stays still while the player leaves a map (`tamer.c: checkMapInteraction`
  calls `unsetCameraFollowPlayer` on stepping on a warp); scripts move it on their own. Nothing centers it again: after
  a battle or a script it follows by differences from where it was left, with the player off-center, until the next map
  (`FieldCameraFollow` in `apps/game`). Details in [maps](../formats/maps.md#in-the-engine). This is done by the map's
  render path, not by `packages/game/field`.

### Time of day

- **Clock** (`clock.c: tickGameClock`) **(verified)**: it counts frames and adds one minute every 20, that is, one game
  minute per second and one hour per minute. It only runs in the field, without fishing and with the clock running
  (it stops on map changes). The options' clock speed doubles it, halves it or stops it. A new game starts at 8:00.
- **Palette and lights** (`map.c: updateTimeOfDay`, when loading the map) **(verified)**: from 6:00 to 15:59 day, from
  16:00 to 19:59 dusk (the map's three lights with 7/10 of green and blue) and the rest night (5/10 of everything).
  Maps with bit `0x40` in their flags (`MAP_ENTRIES`) stay in daytime and with their lights.
- **Change** (`map.c: renderMap`, `tickDaytimeTransition`) **(verified)**: exactly at 6:00, 16:00 and 20:00, except on
  fixed-time maps, the palette fades over 25 frames in steps (1/6, 1/5, 1/4, 1/3 and 1/2 of the way, five frames each)
  and the lights change at the end. The fade waits while the game is not in the field (`GAME_STATE` other than 0); the
  field's pause menu does not change `GAME_STATE`, so a fade already started continues with the menu open. The hours
  and the lights are rules: `time_of_day` in `field/rules.yaml`.
- The menu's clock icon uses another cutoff: day until 16:59 (`clock.c: renderGameClock`).

## What the engine does (`packages/game/field`)

| Original | Engine | Why |
|---|---|---|
| 20 fps, animation root speed | Fixed tick (60 Hz); player and companion take a whole step for each 1/20 s frame (`Field::takeFrames`), at the average speed from `field/rules.yaml`, and are drawn sliding through the frame (`Field::drawnPose`) | Same positions as the original on every frame and smooth drawing; animations are scaled with `AnimationIntent::speedScale`. The speed is the average: the original's root momentum changes somewhat from one frame to another |
| Camera heading rounded to 45°, eight directions | Same (`Field::cameraForward`), but it rounds to the nearest also with negative headings; with a gamepad, analog direction over that heading | The asymmetry is an effect of integer division; analog gamepad |
| Instant turn, animation at normal speed against the wall | Same (`turn_speed: 0`) | |
| Whole-step collision test and cancellation with `collisionGrace` | Same: whole-step probe at the advanced position (`stepCollides`) and `collisionGrace`. The click to go still uses `moveOnGrid`'s X then Z step | |
| Deviation of up to 0x50 toward a free direction on colliding | Same (`collisionGrace` in `field.cpp`), for the player (grid, NPC and companion) and for the companion against an NPC | |
| The first frame of a new animation does not advance | Same: starting, stopping or changing pace spends a frame without advancing | Pressing while stopped starts the frame on that tick, without waiting for the 20 fps clock (less latency) |
| Cells truncating toward zero | `floor` | Same result except at exact edges; without depending on the sign |
| Trigger every frame on the cell | The sections of the step-on cells (51–79) are requested every frame, like the original; the table's warps (110–119) and the rest, on entering the cell (not while standing on it) | It avoids repeated warps even if some data changes |
| Map change: 20 + 20 frames, the player walks without collision, the clock and camera stopped | Same (`transition` in `field/rules.yaml`, `Field::fadeLevel`; the camera keeps its focus while the player leaves, in `FieldScene::update`) | |
| Fade subtracting gray (tone in steps of 7, white texel × tone / 128) and map name from frame 10 to 20 | Same (`transition` in `field/rules.yaml`, `Field::fadeLevel` and `Field::mapNameShown`): `render::ScreenFade` subtracts the gray over the scene, by original frames, and the HUD puts the name in front (`map_title_key`, in the player's language) | The HUD is RmlUi: it is drawn after the fade so the name stays in front |
| Map name with the game's bitmap font, in light gray (color 1 of its CLUT) | The font is a player setting (`text_font`): the original's bitmap one, read from `font/main.png` and `font/main_meta.yaml` of the base pack, or Noto Sans, the default; in both cases, in the original's gray (class `.original-look`, see [UI](../architecture/ui.md#original-look)). What the original's font does not have (ñ, á, ¿, ¡...) is drawn by Noto Sans on the same baseline | Design owner's decision, 2026-10-20 |
| No fixed HUD in the field: what is on screen (characters, status bars from `partner_impl.c: renderStatusBars`, windows) goes behind the fade rectangle (layer 1, `fade.c: renderFade`) and is darkened by subtracting the same gray from it; only the name goes in front (layer 0) | The engine's HUD (minimap, menu button) fades out with alpha at the fade's pace (`screen_fade`) instead of having the gray subtracted from it | Approved deviation: design owner's decision, 2026-10-20 |
| Name placed at `x = 12 − (length / 2) × 8` with padding spaces: centered to within a few pixels | Exactly centered, with no quirk option to imitate the offset | Design owner's decision, 2026-10-20 |
| Script warps (`warp_to` in the sections of the map script's cells) | `field/script_warps` of the base pack, which `dwx` extracts with the conditions of their path: on stepping on (51–79) or on confirming (80–109) the first one that is met with certainty is taken; the conditions (story triggers, items, money and partner) are decided with the game state; what cannot be decided (chance, choices, conversations, unstructured paths) is not taken and is logged once per map and cell; on taking it its `sets_flags` are applied, which are saved in the `story` section | When the story carries the map's script, it runs the sections of the cells, the NPCs, entry and transport, and `field/script_warps` only serves [free exploration](../architecture/script-warps.md#free-exploration) (`Field::staticWarps`); without the story, this graph takes its place and the warps on talking to an NPC (`via: talk`), entry and transport are not taken |
| New partner facing outward for one frame | Facing the player from the start, and only if it fits in the grid | Same visible result; it does not appear inside a wall |
| Trail: pruned every frame, from newest to oldest | Same | |
| The partner walks or stops only by distance | Same | Below 2.5 R it stops even if there is a wall in between, like the original |
| Appears on the trail if it is off screen | If it does not advance for `stuck_seconds` or moves farther than `teleport_distance` away, it appears behind the player | `packages/game/field` does not know what is on screen; it never gets stuck |
| Pace according to its state (unhappy, slow walk), finishes the walk cycle, emotions when stopped | Same (`PartnerFollow` in `packages/game/field`: it walks instead of running if unhappy, finishes the cycle's pass and chooses the stopped gestures with `happy_gesture_from` and `unhappy_gesture_below` from `field/rules.yaml`) | |
| Items on the ground (11) and chests (8): they are picked up from one cell away or less (the chest, from the cell that is 4 ahead of its own according to its turn: 0 four rows more, 0x800 four less, 0x400 four columns less, 0xc00 four more, `spawnChest`; its cell and the surrounding ones are closed); the item is offered once until it leaves its reach (`HAS_PICKED_UP_ITEM`); with the bag full it stays where it is; the chest asks for confirm and its trigger marks it empty. What a defeated enemy drops falls where it was when the final battle box closes (`spawnDroppedItems`). The notice shows the item's name over the tamer's words ("Woah!", "I can't hold anymore.") in the story's dialog box, in its place, and it is only answered by confirming, without a button (`renderItemPickupTextbox`); while the notice of a ground item shows, the player crouches down to it and stays that way until answering (animation 12, `tamerTickPickupItem`; `field_items.pickup_player_clip`); the notice holds only the player: the clock, the NPCs and the companion continue; with a chest, the player turns toward it with animation 0 and the companion stays still (`tamerTickTakeChest`: `partnerSetState(0xb)`); the box accepts confirmation from the fifth frame of opening (on a chest, the button held from the sixth) and a chest closes with a second confirmation ([details](../modules/apps/game.md#picking-up-an-item-from-the-ground-and-opening-a-chest)) | Same (`packages/game/field/field_items`, `session::FieldItemsAdapter`, `field_items` section of the ruleset; `field_item_notice` notice in the app). When loading a map, also the same one after a `warp_to` to it, they are erased (`FieldItems::loadMap`). As an improvement over the original, the notice box also responds to a mouse click, the same as to confirming with the keyboard or gamepad (`field_item_notice` puts `pointer-events` on its box and its `onclick` sends the same `field_item_notice_ok`; the original had no mouse) | Ground items are drawn like the original: their icon from `ETCDAT/ITEM.TIM` with its palette, 128 units, facing the camera and centered on its point (`renderOverworldItem`), over a 200 × 200 shadow on the floor from `ETCDAT/SPOT256.TIM`, subtracted (`renderDroppedItemShadow`); as in the original, the icon covers almost all of the shadow and only its edge is seen, a bit darker. The companion does not dodge ground items (the original only looks at them when picking up, `tamer.c: checkItemPickup`): if it follows the player to one, it stays on top. Chests are drawn with `ETCNA/TAKA.TMD` in the center of their cell, on the floor and turned (`renderChest`); on confirming, the drawer (the second object) comes out 20 per original frame up to 160 and then the notice appears; if the bag is full it goes back in at the same pace under the second notice, which is not answered until it is in (`tickOpenChestTray`, `tickCloseChestTray`). A chest already taken shows with the drawer out. Same (frames not measured in the original: read from the code). `spawnChest` looks incomplete in the decomp; the engine uses the first free slot (assumption). |
| Bathroom (cell 120): with the partner needing to go and without a script, section 0x4e2 of the map takes the partner to the bathroom | Same (`app::FieldToilet`): the player waits, the camera goes to the bathroom, the partner runs to the map's two bathroom points, does `handleToilet` and goes back to the first | Without `toilet_walk` in the ruleset or without bathroom points on the map, it does it where it is |
| Sleeping, praising and scolding: the partner approaches until it is less than 2.5 R away, the two look at each other, a sound plays and the animations continue | Same, with the player's and partner's animations (`app::PartnerWalkUp`, `app::PraiseScoldCall`, `app::SleepScreen`); praising and scolding are in the pause menu | |
| Camera: follows only with the d-pad; 320 × 240 window | Always follows the player (also with a click); the window is the screen's, from 320 × 240 upward | Click to go; any resolution and aspect ratio |
| Clock: one minute every 20 frames, from 8:00 | Same (`clock` section of the ruleset, `ruleset::GameClock`; the game advances it only on the ticks where `Field::gameTimeRan`); `--time` sets it to the `clock` start time or to the dusk or night one from `time_of_day` | |
| Clock speed from the options | Not a player option: `--clock-speed <x>` and the `clock_speed` development command set it | |
| Time fade in steps of 25 frames, lights at the end, stopped outside the field; the field's pause menu does not stop it (`tickDaytimeTransition` only waits for `GAME_STATE`, which the menu does not change) | Same (`render::timeOfDayFadeShown`; hours and lights in `time_of_day` of `field/rules.yaml`): with the menu open the clock stops, but a fade already started continues | |
| No click | Click to go: A* path through points every half cell (centers, edges and corners), in 8 directions, only through steps that the player walks with the original's collision even if it goes 20 units off, and without stepping on warps that are not the destination | PC game ([ADR 0013](../adr/0013-native-pc-game.md)); the follower never stays on an edge. When starting on a warp's cells (on arriving at a map on one), the path can leave through them: the warp of the cell the player is on does not fire until it steps on it again (`Field::keepsOff`), with or without free exploration (accepted by the coordinator, 2026-10-22) |

## Data

- `map/<id>/data`: collision, warps and camera of each map ([maps](../formats/maps.md)).
- `data/maps`: loading names. File City is name 43 and the old man's house is 48 **(verified)**.
- `field/script_warps` and `data/new_game_story` of the base pack, which `dwx` extracts (schemas `script_warps` and
  `new_game_story`).
- `content/packs/game/field/rules.yaml`, `warps.yaml` (only `start`) and `triggers.yaml` (the ranges of collision
  values; schemas `field_rules`, `field_warps` and `field_triggers` in `content/schemas/`).

## The phase 2 walkthrough

- **File City** is `twna01` (index 204, loading name 43): maps `twna01`–`twna13` and `twnb01`–`twnb24` are the city in
  its different phases; `twna01` is the one at the start, the only one whose NPCs are just the old man and his people,
  and the one the house script chooses with flag 203 off **(verified; that it is the first phase, reasonable
  assumption)**.
- **The old man's house** is `room08` (index 218, loading name 48): exit 2 of `twna01` (trigger 112) leads to its
  exit 0. `room10` and `room11` are also called that on loading; they are another version of the house with stairs
  **(assumption)**.
- **The way back**: `room08` has no warps in its table; its door (trigger 51) is the script warp to `twna01` exit 2,
  in front of the house.
- The field starts at `twna01`, exit 1 (the one coming from `trai00`, the city entrance), except with the story and
  `story_scenes.new_game.chain: original`: then it starts like the original (`map.c: initializeMap`), at `twna01`
  exit 9, whose cell runs the opening (section 0x36 of script 164: triggers 54, 55 and 75 and a jump to `topn01`,
  where script 193 wakes the player up at the old man's house).

## Map reach

It is measured by the disc test `the user's disc, extracted: a new game reaches its maps and back, and the scripts join all
but a few` (`packages/game/field/tests/reachability_tests.cpp`), from the field start (`twna01`) and only with what the
field fires: each map's table from its 110–119 cells and the script warps of the cells the map has, to maps with data
(241). Figures from the American disc **(verified, 2026-10-21; the same with version 5 of the extractor, which adds the
warps on talking and the transport choices, 2026-10-22)**:

| Which warps | Maps reached | With a way back to the start |
|---|---|---|
| Only the table (the engine before script warps) | 4 | 1 |
| Table + script warps that are met with the new-game triggers (vanilla, starting after the opening) | 5: `twna01`, `room08`, `trai00`, `mayo00`, `twnb01` | 5 |
| Table + any script warp, whatever its condition (the open graph, and that of [free exploration](../architecture/script-warps.md#free-exploration), which gives the same) | 236 | 233 |

- **When the cells are decided again:** on entering a map (also on loading a game, which enters the saved map with the
  game already read), on taking a script warp (by its `sets_flags`) and, in the game, on the frame in which something
  happens that can change what their conditions read: an order from the companion menu (items, food), giving or using
  an item, a care order (train, sleep...), an evolution by script, a partner event (conditions, evolution) or the end
  of a battle (loot, money). It is not checked every frame. Limit: a change that does not go through any of them (the
  debug console touching money or the bag, for example) is seen at the next of those events or on changing map.
- **Applying `sets_flags`:** the test takes the warps that vanilla can take and applies their `sets_flags` up to the
  fixed point; none is written and they are still 5.
- **Why so few in vanilla:** the exit from `mayo00` toward the rest of the world (and most of the city's phases) waits
  for trigger 203, which sets a conversation with an NPC in `mayo00` (script 105, section 6). The new-game triggers
  are all off (`data/new_game_story`: `triggers_set: []`) except 54, 55 and 75 that the opening sets (`opening`),
  which the field skips. In the game, the vanilla world is opened by the story (`packages/game/story`), which runs the
  conversations and saves its state; part (b) of the script warps on its own does not open it.
- **Free exploration exceptions** (241 − 8 = 233). The test fails if another appears, with its name:

| Map | Index | What happens to it | Proof |
|---|---|---|---|
| `mgen14` | 229 | Unused: nothing leads to it | `MAPHEAD` only loads script 180 for it. No `warp_to` or table entry names it and no script sets variable 247 (the `teleport` screen) to 229. The only map changes in the code are the table (`tamer.c: checkMapInteraction`), the script ones (`map.c: scriptTickChangeMap`) and death (`map.c: changeToDeathMap`), whose map is set by `dooa.c` and `murd.c` (`setDeathMap`) to 0xCD or 0xDA |
| `mgen15` | 230 | Unused | Same as `mgen14` (variable 247 never to 230) |
| `mgen16` | 231 | Unused | Same as `mgen14` (variable 247 never to 231) |
| `miha03` | 21 | No known path | Like `mgen14`: no `warp_to`, table entry or death names it. Script 49 (`gcan11`, offset 1334) sets variables 247 and 248 to 21 and 12, but they are the arguments of `call_routine 21` (`script_interp.c`, case 0x15: `resetMapObjectAnimation`), not a `teleport`. The transport (`call_routine 10`, `script_interp.c: tickBirdraTransport`) only goes to the maps in `script_common.c: BIRDRA_TRANSPORT_TARGETS` (38, 70, 79, 93, 105 and 119) |
| `yaka26` | 224 | Only by talking to an NPC | Its only entry warp is section 5 of script 63 in `chka01`, an NPC's. |
| `icsa08` | 104 | No way back | It is reached through script 187 (offset 92); no warp leaves it |
| `tend01` | 236 | No way back: the ending | It is reached from `mgen99` (script 184, section 51); it is the end of the game |
| `tend02` | 237 | No way back: the ending | Same as `tend01` |

## Where it is in the decomp

- `src/main/tamer.c`: `tamerTickWalkingState`, `getMapRotation`, `setTamerDirection`, `checkMapInteraction`,
  `loadMapEntities`, `tamerTickChangeMap`.
- `src/main/map_collision.c`: `getTileTrigger`, `checkMapCollisionX/Z`, `getModelTile`.
- `src/main/map.c`: `entityCheckCollision`, `checkMapCollision`, `collisionGrace`, `partnerTickCollision`,
  `isFiveTileWidePathBlocked`, `isTileOffScreen`, `loadMap`, `scriptTickChangeMap`; for the map's render path,
  `setupMap`, `initializeDrawingOffsets`, `tickCameraFollowPlayer`, `updateDrawingOffsets`, `cameraIsAtEdge`,
  `renderMap`, `updateTimeOfDay` and `tickDaytimeTransition`.
- `src/main/anim.c`: `startAnimation`, `tickAnimation`, `tickMomentum`, `applyRootMomentum`.
- `src/main/fade.c`: `fadeToBlack`, `fadeFromBlack`, `renderFade`.
- `src/main/clock.c`: `tickGameClock`, `renderGameClock`.
- `src/main/partner.c`: `partnerTickNormal`, `partnerTickWalking`, `getPartnerTamerCloseness`,
  `setPartnerSlowWalking`, `setPartnerIdle`, `updateConditionAnimation`.
- `src/main/partner_impl.c`: `setupPartnerOnWarp`.
- Scripts: `MAPHEAD` section 218 (`set_script 178`) and script 178, section 0x33.
- Map changes in the code: `map.c: changeMap`, `scriptTickChangeMap`, `setDeathMap`, `changeToDeathMap`; `dooa.c` and
  `murd.c` (`setDeathMap`); `main.c: newGameScene` (`MAPHEAD` section 0xCC).
