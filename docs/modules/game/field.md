# field

The field logic (`dwe::game::field`, `game` layer): the player moves around a map by intents, collides with the
original's collision grid, steps on triggers and warps, and the partner follows. What the original did and where
the engine differs is in the [field sheet](../../reference/field.md).

It draws nothing and reads no input devices or files: the app gives it the input and the `MapDocument`s (from
[`assets`](../engine/assets.md#maps)) and reads the state. Player and partner are entities of the `scene::World`
([`scene`](../engine/scene.md)) with the components the app draws: `Transform`, `ModelRef`,
`AnimationIntent` and `PlayerTag` or `PartnerTag`.

## Usage

```cpp
dwe::scene::World world;
auto rules = dwe::game::field::loadFieldRules(vfs);   // field/rules
auto warps = dwe::game::field::loadFieldWarps(vfs);   // field/warps
dwe::game::field::Field field{world, *rules, warps->scriptedWarps};
field.enterMap(*dwe::assets::loadMapDocument(vfs, warps->startMap), warps->startExit);

// Every fixed tick:
field.setInput({.stick = stick, .run = run});          // or .moveTarget = ground point when clicking
field.tick(1.0F / 60.0F);
if (auto change = field.takeMapChange()) {
    field.enterMap(*dwe::assets::loadMapDocument(vfs, change->map), change->exit);
}
```

- **Input** (`FieldInput`): `stick` is a direction relative to the map camera (x to the right of the screen,
  y toward the back), with length 0 to 1; `run` runs; `moveTarget` is the click to go to, which follows an A* path
  through the grid until it arrives, until the stick is used or until `cancelTarget`. A click while something holds
  the player (a script, care, a chooser, the map change or its fade) plans nothing, and any pending target is
  dropped (`dropMoveTarget`): once released, the player does not start walking on their own. `PlaySession` also drops it on every tick
  in which the field is not driving the player (a screen like the gym, a menu, combat). The path also starts from a point closer
  to a wall than the character's probe (the arrival at mgen12 from mgen11): if no point around is
  reachable in a straight line, the ones that can be stepped on are valid.
- **State**: `mapId()`, `player()` and `partner()` (position, yaw and movement), `collision()` for the minimap,
  `path()` and `cameraForward()` for debugging. Every tick, the `AnimationIntent` of the two entities says which animation
  to play (idle, walk, run) and at what speed, and `Transform` keeps the previous position for interpolation.
- **Changes**: `takeMapChange()` gives the stepped-on warp once (`MapChange`: map, exit and trigger, and the arrival
  section if it is the `warp_to` of a script that took over the field, `arrivalSection`); while it is pending nothing
  moves. `triggerSteps()` gives the non-warp triggers stepped on in the last tick, for the
  scripts later on.
- **Exit filter**: `setExitFilter(filter)` decides whether a player map change (stepped-on warp, one-tile script
  warp, free-exploration choice) may happen; it is asked before the fade and before writing
  triggers. A closed one leaves the player in place and `takeClosedExit()` gives it once. It is used by the story
  lock ([session](../apps/session.md)); without a filter everything passes.
- **Fade and name**: the map change counts in frames of the original. `fadeLevel()` says how much the
  fade removes from the screen (0 nothing, 1 black), with the curve of `fade.c` (`fadeOutShade`, `fadeInShade` and
  `shadeDarkness` in `rules.hpp`): the app subtracts it over the scene (`render::ScreenFade`), it does not blend it with black.
  `shadeNow()` gives the shade the screen holds (`FADE_PROGRESS`) and `setShade()` changes it when another fade draws it:
  `WhiteFade` (`white_fade.hpp`) is the white fade of routines 45 and 46 (`fade.c: fadeToWhite`,
  `fadeFromWhite`), which raises `160 / (frames + 1)` per frame from that shade up to 160 and comes back from 160 minus
  one step, draws its frames and is removed on the next one (with 40 it ends at 40, not at 0).
- **Final boss arrival** (`boss_arrival.hpp`, `BossArrival`, EAB overlay in frames of the original): routine 40
  (`EAB_startBuildup`) removes the boss from the map and starts the buildup, which adds a ring every 6 frames (`EAB_tickBuildup`;
  it lives 18, so there are 3 at a time) and never ends on its own; `bossRingScale` gives its size (from 0x10b8 to 0x614 wide and
  from 0x15c to 0xd98 tall, with `_sin` of 512 per turn). `effectLerp` is the original's `lerp` with its wrap-around passed
  `t1`. Unmeasured assumption (there is no saved state near the boss): the new ring counts one frame in the same one in
  which it is born, as if its object went behind the buildup's in the object table.
  The arrival (partner routine 8, `EAB_tick` and `EAB_tickSpawn`) starts with 9 dark flashes (`flashes`:
  `EFECreateFlash` in mode 0x20, the first half of the brightness subtracted from the screen, in order 0x22, from scale 0 to
  5120 and from gray 0x14 to 0xff in 0x32 frames, at the points of `EAB_D_8006170C`, read from the original's module) and
  counts from the next frame: from 45 the
  screen is subtracted to black (`blackout`); at 50 the boss comes back with animation 0x21, the hum stops and sound 2 plays,
  the boss switches to `vabId` 5 (`bossSoundSlot`: bank 4 of its clips plays from slot 5, the one routine 51 loaded;
  `FieldScene::setNpcSoundSlot`),
  the buildup is removed and only the boss is seen (`othersHidden`, until 319) with the camera turning (`orbit`, from 50 to 318:
  from 0x71c to 0x171c of angle until 290, from 0xdac to 0xbb8 of distance and from 0x258 to 0x32 of height until 320);
  the black returns to 0 at 82; every frame adds an arrival ring (it lives 9) and every 9 of 18 a green particle
  (it lives 86, grows from 8 to 0x9c4); at 210 animation 0x33; at 320 everything comes back, the particles are removed and sound
  3 plays; the rings continue until 329 and at 340 the arrival ends (`arrivalOver`). `solidShares` gives how much of the boss is
  drawn solid (from 0 to 16 until 290), which the engine does not use yet. Assumption: unmeasured in the original.
  `mapNameShown()` gives the map whose name is shown, from frame 10 of the exit fade until entering the new
  map; the app looks it up in the map table and the HUD puts it in front of the fade. The exit fade continues
  from the shade the last one left (`FADE_PROGRESS`), also if it cuts a half-done entry fade.
  `startPlay()` is the start of the game in its first map (`main.c: main: fadeFromBlack(0x28)`): 40
  frames (`start_fade_in_frames`) without holding anyone, so the tiles run from the first one. It holds
  the same for a new game (after `initializeMap`) and for a loaded one (after `initializeLoadedMap`, which puts
  both in their saved place with `enterMapAt`, without a fade of its own).
- **Script warps** (`ScriptWarpSetup`): those of `field/script_warps` with `GuardFacts` (the readers of
  triggers, bag, money, stats and partner conditions) and a `TriggerWriter` that receives the
  `sets_flags` of the taken warp; afterward the field decides the map's tiles again (`refreshScriptWarps`).
  `keepsOff(trigger)` says which tiles the click-to-go avoids (not the warp tile the player is standing on).
  See [script warps](../../architecture/script-warps.md).
- **NPCs that see the player** (`FieldNpcs`, with `npc.tracking` in `field/rules`): depending on the `followMode` of their
  record, an NPC looks at the player, runs to where it saw them and returns, or follows them while they are in its radius
  (`overworld.c: NPCEntityTickOverworld`). While it reacts it does not follow its route. If an NPC with `autotalk` bumps into
  the player, the contact (`contacts()`) launches its script section, which is the one that starts combat. The original
  has no random encounters: all enemies in the field are map NPCs. While a script holds the field,
  the NPCs do not tick, but their `Transform` starts every tick the same (`FieldNpcs::beginTick`): so an NPC that the
  script walks around is drawn where it is, not between that point and where it was when the script began.
- **Care scenes** (`holdForCare`): they hold the player without stopping game time, like state 6 of the
  tamer in the original. There is no input and no tiles, and the partner does not follow the player. `walkPartnerToRange(range)`
  takes the partner to that distance from the player: it comes closer if it is farther and moves away if it is closer
  (`partner.c: partnerTickFeedItem`, `checkEatDistance`). On arrival, the two look at each other. If it gets stuck, it stops where
  it is. `runPartnerUp()` makes the partner run toward the player up to its running distance, as before a poop
  in the field (`partner.c: tickWildPoop`, `getPartnerTamerCloseness > 0`); if it is already there, it does not move.
- **When a script or a chat ends** (`holdForScript(false)`, PC adaptation of 2026-10-28): the original leaves the
  partner where the script left it or where it was when talking (`script_interp.c: scriptPauseGame`, `tickScript` does not
  return anyone to their place; `partner.c:971`, the closeness to the player, keeps it still closer than 2.5 radii), so
  it could end up stuck to the player, inside the NPC it was talking to or between the camera and the player, covering them.
  Here, while the player stays still, the partner steps aside by walking (`Field::makeRoomForPlayer`): first
  sideways if it covers the player from the camera, and then outward up to halfway between touching the player (player's
  radius + its own) and its stopping distance; afterward it goes back to following like the original. If the player starts walking,
  it follows as usual; if it has no room, it stays. The NPC does not move during the chat and keeps looking at the player when
  it ends, as in the original (`overworld.c: tickWaypointWait` does not turn it). Also, when following the trail (the engine's
  own) the partner no longer gets into the player: the trail ends on touching them (like `partner.c:906` at the end of a
  step).
- **Free exploration** (`setExplore`, off by default): a script warp takes you to any of the places of
  its tile (`explorationWarps`). With several, or with just one on a step tile that the story has closed
  (the blizzard strips of the snowy maps cross the path), the field stops and `destinationChoice()` gives the
  map, the tile, the places and the vanilla one; `chooseDestination(index)` takes one and
  `chooseDestination(std::nullopt)` stays. Staying on a step tile does not ask again on it
  until the next map: crossing the strip back and forth works as in vanilla, without a selector.

## Positions

Where the characters (player, partner and NPCs) end up relative to each other and to the walls.

- **Overlap watch** (engine's own, for debugging and for test routes): on every tick in which the
  player moves freely (no script, no care, no map change or fade holding them), `Field` looks for
  pairs of characters whose collision squares overlap deeply and for the player or the partner with the center on a
  blocked tile (an NPC in a wall is where the original has it: see "Release rule"). An overlap lasting more than a second is logged once per episode
  (`[positions] overlap a=player b=partner ticks=61 on mayo03`) and counts in `overlapEpisodes()`; the route reads it with
  `expect_setting: {field.overlaps: 0}`. "Deep" means overlapping more than half the sum of the half-sides on both
  axes (`Field::overlaps`): the original's squares (`map.c: entityCheckEntityCollision`, half-side = the species'
  radius) are wide, and a partner standing diagonally 2.5 radii from the player (`partner.c:971` measures a
  circle) already overlaps them at the corner without it showing. The one-second margin lets the player walk over the
  partner (crossing it takes half a second of walking).
- **Release rule** (Documented improvement: the original has nothing like it; it leaves each one where whoever held
  them left them). Everything that holds the characters and releases them calls `Field::releaseHandoff(reason)`: the end of a
  script or a chat (`holdForScript(false)`, also the gym's), the end of a care scene
  (`holdForCare(false)`), the end of a battle (`FieldScene::releaseFighters`, also a fixed battle's, and
  after the ones who flee have walked out), entering a map through a warp or on load (`enterMapAt`). An NPC
  placed on the map (`FieldNpcs::spawn`) or released by a script (`hold(id, false)`, `holdAll(false)`) requests it on its
  own. The release:
  - forgets the partner's trail, its stuck time and the player's last cell
    (`PartnerFollow::forgetTrail`, `partnerStuckSeconds_`, `lastPlayerCell_`): what the partner was following before is no
    longer valid;
  - on the next tick without a hold, `separateActors` looks at the partner and the player: the partner with its center in
    a blocked cell or deeply overlapping another (`Field::overlaps`), and the player only if in a wall, go
    to the nearest free spot (rings every 25 units up to six cells, first in the direction opposite to
    what it overlaps) where it fits on the grid (`fitsOnGrid`), touches no one, is not a warp and can be reached in a straight line without a
    wall (unless it is leaving a wall), and is placed at once (`scene::place`, without interpolating the jump). If there is no
    room, it stays and it is logged (`[positions] release ... no free spot`). NPCs that fought start their route
    again (`restartPath`, like `clearMapAITable`).
  - **NPCs never move** (Same: the original does not move an NPC that is not walking along its route, reaction or a
    script). They stay where the map, their route or a script put them even if they are in a blocked cell or overlap
    someone; the others walk around them. Measured in DuckStation: `species_bg` in its bed in mayo02 (section 6 of its script, the
    cell is a wall) stays at (143, -1000) with orientation 1024 when the chat opens, during it (pose 29) and 300
    frames after closing it (pose 28, set by the script). Before, the release moved it to (-64, -819) on entering the
    map and to (22, -725) after each chat; 81 NPCs out of 989 spawn in a wall (beds, shop counters such as the
    bank's `species_a` in room02, which came out in front of the counter). An NPC on top of the player stays there; the player
    walks away.
  - After a script or a chat it waits for the partner to finish moving aside on foot (`makeRoomForPlayer`, the
    soft exit); the jump only comes if it still overlaps someone afterwards.
- **Original frames** (Same, `tamer.c: tamerTickWalkingState`, `partner.c: partnerTickWalking`,
  `anim.c: startAnimation`): the original's logic runs at 20 frames per second and the characters advance with the
  root momentum of their animation. `takeFrames` counts the 1/20 s frames that fall in each tick and
  `movePlayer`/`movePartnerFrames` take one step per frame: the player a whole one (50 running, 20 walking), the
  partner the one from its animation (see "Partner following"). Changing animation (starting, stopping, going from walking to running) spends a frame without advancing, like
  `startAnimation` setting the momentum to zero; direction changes immediately (`setTamerDirection`). When the
  stick is released the player stops on the next frame, without the extra step. Pressing while stopped starts a frame
  in that same tick (Documented improvement: the original waits for the next frame of its clock, up to 3 vblanks). The
  partner decides to walk, run or stop based only on distance (`getPartnerTamerCloseness`: it runs beyond 3.5 R,
  walks at 2.5 R or more), also while following the trail.
- **Partner following** (Same, `map.c: partnerTickCollision`, `collisionGrace`, `entityCheckCollision`,
  `partner.c: partnerTickWalking`, `updateConditionAnimation`, `anim.c: tickAnimation`, `applyRootMomentum`):
  `game::field::PartnerFollow` ports it with the original's integers. Each frame, after the player: the trail
  (ring of 30 cells, `isFiveTileWidePathBlocked`; when starting one, `initializePartnerWaypoint` takes the player's
  current cell as the last one seen), it looks at the player or at the center of the oldest cell of the trail
  (`_atan` with its 512 table, `core::original::atan4096`), tries the forward step of its animation
  (`entityCheckCollision`: first the map, which returns 10 and does **not** count, then the player and the NPCs with the
  front edges of their square); against the player or an NPC it recovers the heading it had and goes through
  `collisionGrace`. Then it picks the animation by distance (going from walking to running, or from still to walking, finishes
  the current turn; from running to walking it changes at once) and advances with the root momentum of that animation
  rotated by its heading (`rsin`/`rcos`), with the unit fraction in 1/32768 (`locX >> 15`). The animations come from the
  partner's model (`battleClipsOf`, the same one battle uses); without them (tests) it walks and runs at the
  speeds from `field/rules`. When stopping it waits 1 to 5 loops of its idle animation (`randomLimit(5) + 1`)
  and does its gesture. The `Transform` takes the integer position and heading after each frame; if something else moved it (script,
  care, `place`, moving aside), following starts from there.
- **Collision with deflection** (Same, `tamer.c: checkMapInteraction`, `map.c: entityCheckCollision`, `collisionGrace`):
  before advancing, the whole step is tested at the advanced position (`stepCollides`, the probe of
  `checkMapCollision`). If it collides, `collisionGrace` goes through the four directions of `COLLISION_GRACE_ROTATION`
  according to the heading: the first free one turns the character up to 0x50 toward it and the step loses the component that pushes
  against the ones that were blocked before; if none is free, it turns 32 and does not advance. For the player the grid, the
  NPCs and the partner count (in the field the original counts it here even though it passes through it in the earlier test); the partner
  does it in `PartnerFollow` (above). Against a wall head-on
  the player turns 0x50 and slides. Afterwards the step is applied whole, without testing the grid again
  (`anim.c: applyRootMomentum`): next to a wall's edge the player keeps going along the turned heading until all
  the exits are blocked, without sliding along it (#217).
- **Placing** (`placeAt`, the `place` script command): puts the character exactly where asked, without requesting the
  release rule.
- **Interpolation** (engine feature: the original draws each character at its spot of each frame, with nothing in between).
  The logic runs at 60 ticks per second and drawing interpolates between the previous and current position of each
  `Transform`. The player and the partner, which advance in frame jumps, are drawn sliding through the whole
  frame (`Field::drawnPose`, used by `FieldScene` and the camera): from the pre-step position to the post-step one in
  1/20 s, without stutter even if the step falls in a single tick. Each game tick starts with `Field::settle` (`FieldScene::settleActors`, at the start of
  `PlaySession::tick`), which sets previous = current for the player, partner and NPCs; `Field::tick` also always does it,
  even if only the fade of a map change or the wait for the new map is running. That way, in the ticks where the field does not
  run (battle for those not fighting, evolution, sleep, fishing, menus, item notice) nobody is drawn sliding
  from where an old tick left them, and the frame's sliding ends. If the field tick runs after `settle`,
  the sliding continues: before, `settle` turned it off on every tick and the player and partner were drawn sliding a third
  of the step, then the rest at once and two still ticks (the "jerky movement" the user saw on
  2026-10-31). Its clock advances once per field tick. It is an optional improvement (`setSmoothMotion`, setting
  `smooth_motion`, on by default): off, everyone is drawn where their tick leaves them, like the original. A script jump is drawn at once: `placeByScript` by more than half a cell and
  `turnByScript` by more than an eighth of a turn also set the previous one equal; short steps (the gym, the end of
  a slide) are still interpolated in their tick. `playerFacePartner` and the looking at each other at the end of a care walk
  (`entityLookAtLocation`) turn at once, like the original.
- **Partner size** (Same): the field measures it with its species' radius from the species table, like the
  original (`map.c: entityCheckEntityCollision`, `partner.c:971`), not with the fixed one from `field/rules` (which remains for
  tests and for when there is no table). `FieldScene::showPartnerSpecies` passes it (`Field::setPartnerRadius`) on
  load, on evolving, on rebirth and on changing partner; with it change the grid collision, the one NPCs
  see and the follow distances (running beyond 3.5 radii, walking beyond 2.5). If it grows, it requests the release
  rule, in case it now overlaps someone.
- **NPCs after a script** (Same): on releasing the script (`holdForScript(false)`) the NPCs it moved stop sliding
  (their `ScriptedActor` loses its velocity, which otherwise moved them again on the next hold), and the one that no longer
  has a script animation (`setActiveAnim` to 0) goes back to its own animations; the one that has it keeps it, like
  `NPC_ACTIVE_ANIM` in the original until a script removes it.
- **Path with more segments than points** (Same, `overworld.c: tickWaypointWait`, `NPCEntityTickWaypointWalk`,
  `map.c: loadMap`): a map can give fewer waypoints than an NPC's path has segments (dgha02, script
  id 7). The original reads the gaps that `loadMap` left at zero: the segment waits 0 frames or walks to the map's
  origin. Same here (`waypointOf`); before, the NPC stood still with an error in the log.
- **Off-screen NPC** (Same, `overworld.c: NPCEntityTickOverworld`, `map.c: entityIsOffScreen`,
  `utils2.c: isInvisible`, `map.c: entityCheckCollision`): the original computes at the end of each NPC's tick whether it is seen
  (`isOnScreen`, no corner of its radius-and-height box in the window) and collision skips those that are not seen.
  Here `FieldScene::followNpcs` computes it at the end of each tick with the camera (`FieldScene::onScreen`) and
  `FieldNpcs::setOnScreen` takes them out of all collisions and touches (`collides`) until they are seen again; the trace
  still lists them. Measured case: `species_s` waits 1000 units above its tree's door in mayo04a (296, -1000,
  1865) until the story drops it. In the original its `isOnScreen` is 0 and the player goes up with up from
  (288, 1548) to the door (306, 2198, heading 2128); before, the engine stopped it at z = 1600 against `species_s`'s
  square, now it reaches (331, 2200, heading 2128). Assumption: the engine's window is wider than the original's 320 x 240,
  so an NPC at the edge may be seen (and collide) here and not there.
- **Spawn spots**: the partner, when entering through a warp (at `spawnDistance` from the player) and when placing itself next to the
  player when it gets stuck or drifts away (`placePartnerNearPlayer`), tests the grid and also that it does not deeply overlap
  anyone; if not, it tries the next spot. An NPC appears where its record says and, if it overlaps someone, the release
  rule moves it aside on the next tick.

Status: **Same** (does the same as the decomp), **Documented improvement** (the engine's, on purpose), **Assumption** (the
decomp is not clear) or **Gap** (missing).

| What | Original | Engine | Test | Status |
|---|---|---|---|---|
| Separate the partner left overlapping another or in a wall, and the player in a wall, on releasing them (NPCs do not move: Same, measured with `species_bg` in mayo02) | nothing: each stays where whoever held it left it | `releaseHandoff`, `separateActors` | `field_tests`: "the release rule ..."; routes in `_work/routes-e2/positions` | Documented improvement |
| Overlap watch | — | `watchOverlaps`, `field.overlaps` | `field_tests`: "the overlap watch ..." | Documented improvement (debugging) |
| On load, an NPC on top of the player | `main.c:3821`: takes the player to the map entrance if it overlaps certain NPCs | the release rule moves the NPC aside; the player does not move | `field_tests`: "an NPC put on the player ..." | Assumption (preferring not to move the player) |
| Item dropped by a defeated enemy | `item.c: spawnDroppedItems`: falls at the enemy's (x, 0, z) when the final box closes, without looking at collision (the battle's collision, `map.c: entityCheckEntityCollision`, keeps it out of walls) | `FieldItems::drop` and `dropCell` with the map's collision (`FieldItemsAdapter::dropItem`, from `BattleScreen::close`): in a free cell it stays where the enemy was; if its cell is closed (a pushed enemy or a step that slipped into the wall) it moves to the center of the nearest free cell, ring by ring up to 8 cells; script items (`spawnItem`) stay in the cell the script gives | `field_items_tests`: "item.c spawnDroppedItems: a drop on a wall lies on the nearest free cell" | Documented improvement: the original does not need it if the enemy never enters the wall; not measured in the original (#164) |
| Partner size in the field | the species' radius | `setPartnerRadius` | `field_tests`: "the partner's size follows its species ..." | Same |
| On arriving through a warp the player is left at the arrival point, turned to its side and still (animation 0) | `tamer.c: setupTamerOnWarp` (from `loadMapEntities`) | `Field::enterMapAt` (`scene::place`, `AnimationIntent` at rest) | — | Same |
| While partner care holds it (state 6), the player stands still with animation 0 | `tamer.c: tamerTickIdle` (`tamerSetState(6)` in `partner.c`) | `Field::tickCare` | `field_tests`: "a care scene holds the player and walks the partner to a range, toward or away" | Same |
| Script commands that move, turn or remove a character look it up by its id: 0xFD is the player, 0xFC the partner, any other id the map NPC that has it | `tamer.c: getEntityFromScriptId` | `StoryStage::entityOf` (`PlayerScriptId`, `PartnerScriptId`) | — | Same |
| NPC after a script | `NPC_ACTIVE_ANIM` goes on until a script removes it | `holdForScript(false)` removes the velocity and the `ScriptedActor` without an animation | `field_tests`: "a script let go ..." | Same |
| Step per frame (20 per second) and a start-up frame with no advance | `tamer.c: tamerTickWalkingState`, `anim.c: startAnimation`/`applyRootMomentum` | `takeFrames`, `movePlayer`, `stepPlayer`, `movePartnerFrames` | `field_tests`: "the player steps on the original's frames ..."; oracle `field_walk_seen` | Same |
| Pressing while standing still | waits for the next frame of its clock (0 to 3 vblanks) | the frame starts on that tick | `field_tests`: "the player steps on the original's frames ..." | Documented improvement (less latency) |
| Player speed per frame | the root momentum changes from one frame to another | the average from `field/rules` | oracle `field_walk_seen` | Assumption (on a diagonal it drifts by 1 unit) |
| Partner following: trail, gaze, collision, animation and step by the root momentum, in integers | `map.c: partnerTickCollision`, `partner.c: partnerTickWalking`, `anim.c: tickAnimation` | `PartnerFollow`, `Field::movePartner` | `partner_follow_tests`: "the partner follows the player frame by frame ..." (60 frames of an integer model that reproduces the original's trace); oracle `field_walk_seen`: before up to 147 units of position and 308 of heading, now 2 to 5 units (the offset of the initial gesture, which the dwe side places within ±1 frame) and the same frame on every animation change | Same |
| Root motion of the animations a script sets on the player, the partner or an NPC: every 1/20 s frame the clip moves it by its root's step, turned to where it faces, with no collision test, until it ends or another starts. During a scripted walk (`entity_walk_to`) the glide replaces the root; during a scripted move (`entity_move_to`, `entity_move_to_axis`) it is added to it. When the script lets go, the player and the partner stop moving by the clip | `script_interp.c: scriptStartAnimation` (0xFD the player, 0xFC the partner, otherwise `scriptNPCStartAnimation`) → `anim.c: startAnimation` (`animFlag` = 1); the player's tick in the field, `partner.c: partnerTickOverworld` and `overworld.c: NPCEntityTickOverworld` → `anim.c: tickAnimation`, `applyRootMomentum`; `tickEntityWalkTo` versus `tickEntityMoveTo`/`tickEntityMoveToAxis` | `Field::moveByClip` and `applyClipRootMotion`, `clipRootStep` (`field_script.cpp`), `CharacterClipMotions` (`FieldScene` gives it to the player and the NPCs with `battleClipsOf`, to the partner with `setPartnerClips`), `ScriptGlide` and `story::Glide`; the sleeping scene uses the same path (`FieldScene::playPartnerClip(clip, true)`) | `field_tests`: "an NPC playing a script's clip moves by the clip's root motion", "the player and the partner playing a script's clip move by the clip's root motion", "a care scene's moving clip carries the partner until it stops" | Same. Measured in the original (2026-10-30): NPC, `e1-cp28`, `species_bg` in its bed in mayo02: animations 30 and 8 take it from (143, −1000) to (−284, −1000); dwe (−281, −1000). Partner, `mayo04a_etemon_tree_door` with variable 1 at 60 (prosperity) and cell 0x33 of script 4 (`species_s` falls from the tree): animation 40 takes `species_a` from (208, 1974) to (208, 1593), −381 in z facing 2048, before the battle; dwe did not move it before, now from (243, 1978) to (243, 1595), −383. Player, the same scene with the player's animation 15 changed to 17 in the script's RAM: from (306, 1848) to (306, 2245) when the battle starts (frame 33 of 49); dwe (with the Lua changed the same way) from (306, 1898) to (306, 2282). `species_s` falls with its animation 29 while `entity_move_to_axis` lowers it: the original leaves it at (49, 1618) from (296, 1865); dwe, with the step added, at (79, 1648) (without adding it, it left it at (256, 1825)). Sleeping: animation 9 carries the partner 70 units, as before |
| The partner while a script holds the field (a talk, a scene) | `script_interp.c: scriptPauseGame` → `overworld.c: setMovementEnabled(1, 1)` → partner state 11; `partner.c: partnerTickIdle` starts its animation 0 once and runs neither the following nor the mood gestures until the script lets it go or sets another | `Field::holdForScript` calls `PartnerFollow::standHeld` and `partnerFollowClip` gives that animation 0 for as long as the hold lasts; `FieldScene` draws it like the following's (the animation the script sets still takes precedence) | `field_tests`: "a script's hold stands the partner on its idle clip until it lets go" | Same. Measured (2026-11-01, `04_jijimon_house_interior`, talking to `species_bl` while `species_a` came running in): state 11 and animation 0 at once, the same and in the same place 400 frames later with a direction pressed. Before, dwe kept the gesture timer running during the talk (7 "happy" gestures in the opening talk with the old man, 1 in a short talk); now none |
| The partner and the walls while following | `partnerTickCollision` does nothing if `entityCheckCollision` gives 10 (the map): only the trail pushes it aside | same: a partner shut in walks through the wall toward the player | `field_tests`: "a partner in a closed pocket walks through its wall ..." | Same |
| Partner off screen | `isTileOffScreen`: puts it on the trail's cell if it is not seen | puts it next to the player if it has gone a second without advancing | `field_tests`: "a partner that cannot step for a second ..." | Gap (the off-screen trail is not used yet) |
| Partner very far away (angry, with the butterfly, walks while the player runs: `partner.c: partnerTickWalking`) | `getClosestTileOffScreen`: brings it to the first cell off screen on the player's straight line toward it; it is never seen jumping | when farther than `teleport_distance` and out of view (`FieldScene::offScreen`: the 320 x 240 screen with a 40-pixel margin, in fractions of the window), to the first cell out of view and closer on that line; if still visible, it keeps walking. Before, it put it behind the player, in view (card #170). Measured in dwe: running with the partner angry the distance goes past 2000 and comes back from 2002 units, out of view | `field_tests`: "a partner left too far comes back from the first cell out of view toward it" | Same (without a camera, in the tests, it stays behind the player) |
| Wait before the gesture | `randomLimit(5) + 1` loops of the idle animation, with the original's generator | same, with the engine's generator (`gestureRandom_`) | oracle `field_walk_seen` (the original did not do the gesture in the 46 frames standing still) | Assumption (another random sequence) |
| Idle, step and gesture by state (card #56) | `partner.c: setPartnerIdle` (1 tired or sick, otherwise 0), `setPartnerSlowWalking` (3 or 2), `updateConditionAnimation` (injury or sickness 0xE, hunger 0xF, bathroom 0x10, sleep 0x11, tiredness 0x12, anger 0x13; with no condition, 5 happy or 7 sad) | `PartnerFollow::setIdle`, `setSlowWalking`, `updateConditionAnimation` with `FieldScene::partnerMood` | Measured on 2026-11-03 with the flags written by hand: in the original tired idle 1 and walking 3, sick gesture 14 in a loop, angry 19; in dwe, the same tiredness gives idle 1, walking 3 and gesture 18, sick 14 and angry 19 | Same |
| NPC off screen outside the collision | `isInvisible` (`isOnScreen` 0) in `entityCheckCollision`; `species_s` in mayo04a: 0, the player reaches the door (306, 2198) | `FieldNpcs::setOnScreen`, `collides`, `FieldScene::followNpcs` | `field_tests`: "an NPC off screen is out of the collision ..."; before, the player stopped at z = 1600, now reaches (331, 2200) | Same |
| Partner collision with an NPC | `entityCheckEntityCollision` with the NPCs' squares | `FieldNpcs::blocks` in `npcBlocks` | — | Assumption |
| Collision with a detour of up to 0x50 | `map.c: collisionGrace`, `COLLISION_GRACE_ROTATION` | `collisionGrace`, `walkForward`, `stepCollides` | `field_tests`: "against a wall straight ahead ..."; oracle `field_walk` | Same |
| Step after the detour next to a wall's edge (#217) | `anim.c: applyRootMomentum` moves by the turned heading minus the blocked exits and no longer tests the map; measured in trop01 at (2788, −1494), the original's axes, with initial turn 0xEEB: right 30 frames turns to 0xA50 and goes (+78, +61); left (−283, −323); down (+318, −319); up does not move (turns to 0x650) | `walkForward` adds the step (before `moveOnGrid`, which clipped it by axes and slid along the wall: right (+355, 0)); now right (+79, +62), left (−283, −322), down (+318, −318), up nothing | `field_tests`: "a step aside of a wall's edge goes on along the turned facing, not along the wall" | Same |
| Partner against an NPC | `map.c: partnerTickCollision` | `PartnerFollow::collisionGrace` | oracle `field_walk` | Same |
| Partner walking or stopping | `partner.c: getPartnerTamerCloseness`, only the distance | same, also when following the trail | `field_tests`: following | Same |
| Partner heading with the player in front | recovers the one it had (`partnerTickCollision`) | same | oracle `field_walk_seen` | Same |
| Script `place` | puts the character in its spot | same, with no release rule | `field_tests`: "a placed partner stays ..." | Same |
| Drawing the frame step | no interpolation | `drawnPose` glides the step over 1/20 s, also with `settle` at the start of each of the game's ticks | `field_tests`: "... drawn gliding over a frame's ticks", "the game's ticks, each starting with settle, keep the frame's glide going". Measured in dwe (release, lavapipe at 320x180, running in mayo01_2): before, 36% of the draws with the player moving left it standing still (coefficient of variation of the drawn speed 1.40); after, 4% (those of starting and changing map) and 0.32 | Optional improvement (`smooth_motion`, on by default) |
| Smooth motion off | draws each frame in its spot | `drawnPose` without gliding or interpolating | `field_tests`: "with smooth motion off the characters are drawn where their tick left them" | Same (with the improvement off) |
| Interpolation without a field tick and script jumps | no interpolation | `settle`, `placeByScript`/`turnByScript` with a jump | `field_tests`: "settling ...", "a script's jump ..." | Documented improvement |
| The player walks through the partner | `tamer.c: checkMapInteraction` passes the partner as `source` to `entityCheckCollision`, which skips it (`other != source`) | the player walks through it; the watchdog logs it if the player stays on top | `control` route | Same |
| Trigger cell under the player on an edge | `map_collision.c: getTileTrigger` (`getModelTile`): truncates toward 0 and subtracts one column when x < 0 and one row when z > 0, so on a cell's edge it reads the neighbor away from 0. Exit 1 of frzl17 leaves the player at (-400, 1400), the corner of four cells: it reads (45, 35), the curling one, and the game starts on arrival (measured in the original: entering through that exit shows the "Start Game" question without moving) | `Field::checkTriggers`, `Field::checkConfirm` and the trigger remembered on entering read `assets::originalCellAt`; before, `collisionCellAt` (floor) read (46, 36) and you had to leave the cell and come back | `field_tests`: "a step cell is the one the original reads, the neighbour away from 0 on an edge" | Same |
| Script slides against walls and NPCs; `wouldTouchCharacter` without NPCs | — | unchanged; the release rule fixes the outcome | — | Gap |
| The player's way back: the cell it entered the map by and, while the straight line to it is blocked, the cells where it lost sight of the previous one (up to 30); used by the tamer's run when a battle starts | `map.c: initializeTamerWaypoints` (in `loadMap`), `tickTamerWaypoints` (at the start of `tamerTickWalkingState`), `isLinearPathBlocked`, `addTamerWaypoint`, `clearTamerWaypoints` | `PlayerTrail` (`Field::playerTrail`) | `player_trail_tests`: "a line of cells is blocked ...", "the way back keeps the cell ..." | Same; the original reads the past ring of 30 cells, here it stops at the end |
| Footsteps and other sounds of the field clips | `anim.c: tickAnimation` (0x4000): `playSound(bank, sound)` on the tick of each instruction if the entity is visible (`isOnScreen`: the player always; partner and NPCs by `entityIsOffScreen(320, 240)`); bank 4 = the species' bank. Measured running in twna01/twnb01: player `playSound(1, 27)` on ticks 2 and 8 of the run (anim 3), one every 6 frames (18 vblanks); partner `playSound(1, 12)` on ticks 3 and 9 of its own (anim 4); a walking NPC, `playSound(1, 10)`; they stop sounding when they stop. Does not depend on the ground | `clipSounds`/`AnimationPlayer::takeSounds` (engine), `FieldScene::playClipSounds` and `onScreen` (app) with the `sound/sl` bank of slot 1; before, field clips played no sound at all | `animation_tests`: "a clip plays its sounds on the ticks it leaves, ...", "the player hands over the sounds ..."; `assets_tests`: "a clip keeps the sounds its stream plays ..."; manual route: run 2/8, walk 3/13, partner 3/9 (no audio on the test bench: judged by the calls) | Same |
| Clip sounds with the species' banks, in battle and in the care scenes | `anim.c: tickAnimation` (0x4000): the clip's bank 4 is the entity's `stats.current.vabId` (partner 4, battle enemy i 4 + i, `btl.c: loadBattleData`; an NPC that never fought has 0, and one that fought keeps its slot); 3 is always slot 3. `sound.c: loadPartnerSounds`: slot 3 = `VLALL` entry of the partner's type (15 outside 1–65), slot 4 = its `VBALL`; `readVBALLSection(i + 4, type)` loads the enemies'. In battle `isOnScreen` is not recomputed (`battle_tick.c`): each fighter keeps the one from its last field tick; in the care scenes (`partner.c: partnerTickOverworld`, states 3–9 and 15) it is. Measured in `11_battle_agumon`: partner `playSound(4, 0x37)` and enemy (`species_a`, `vabId` 5) `playSound(5, 0x37)` from `tickAnimation`, plus footsteps `playSound(1, 12)`/`(1, 2)`; in `01_field_outside_jijimon_house`: partner `vabId` 4, NPC `vabId` 0 | `SessionSound` (slots 3–7 by each `bank_meta`'s `used_by`, `attachPartner`, `useEnemyBanks`), `BattleScreen` (loads the enemies and gives each figure its slot), `FieldScene::playClipSounds` (the actor's own slot, visibility saved when the battle takes hold of it) and `animatePartnerClip` no longer discards the sounds | `dwe_session_tests`: "slots 3 and 4 hold the partner's species banks, 5 to 7 the last battle's enemies'"; manual route at :91 (calls in the log) | Same; Assumption: an NPC just created on entering a map starts with slot 0 (the original keeps the `vabId` of the NPC slot across maps) |
| New game start: twna01 is not visible | `main.c: main`: `fadeFromBlack(0x28)` with the player free; in the same frame (vblank 32224, `FADE_IN_CURRENT` 1, `FADE_PROGRESS` 137) cell 54 calls section 0x36, `warp_to` does `fadeToBlack(20)` from that tone and the screen stays black until the "File City" caption and topn01, where the player is already lying down (anim 18) when seen | `Field::startPlay` (40 frames, no hold), `shadeNow`/`leavingFrom_`: the exit continues from the half-done fade's tone; before, the 20-frame fade held the player, the cell jumped on tick 59 with twna01 already lit and everyone was seen standing (topn01 on tick 113; now on 59) | `field_tests`: "play starts with a long fade in ...", "a fade out started during a fade in ..."; captures in `_work/routes-e2/npc-and-opening/` | Same (the exit tone is 160 instead of 144: the two blacks) |
| Start of a loaded game | `main.c: main`, cases 1 and 2 of `MAIN_STATE`: `initializeLoadedMap` and then the same `fadeFromBlack(0x28)` as the new game: the saved map appears from black in 40 frames, with the player free | `FieldScene::create` calls `startPlay` also when it starts at the saved place; before, loading showed all at once, without a fade | `field_tests`: "a loaded game starts with the same long fade in, at its saved place"; captures in `_work/tmp/p2-shots/ui-load-fade-*.png` | Same |
| The fade in continues during a battle | `fade.c: renderFadeIn` is a draw object (`addObject(4005)`) that advances one step per drawn frame, also inside `BTL_battleMain`: a battle that starts before the fade ends (console, or a script right after a map change) sees the screen come back just like in the field | `Field::tickFadeIn`, which `PlaySession` calls on every tick with the battle active; before, the field did not advance and the battle stayed almost black (mean brightness 9 of 255 in twna01 versus 114 for the same battle without a fade; does not depend on Xvfb or the time of day) | `field_tests`: "the fade in runs on while a battle holds the field"; before/after captures in `_work/tmp/w15-arena/dark-compare.png` | Same |
| Care walk to a point (the bathroom, eating poop) | `tickEntityWalkTo` ends on stepping onto the point's cell (`getModelTile`), not on the point; in partner states 6 and 9 (`tickPartnerToilet`, `partnerTickEatShit`) the clip carries it without `partnerTickCollision` or NPC collision | `tickCareWalk`/`stepCareWalk`: ends on the point's cell (`originalCellAt`) or 1 unit from it, without colliding with the map or the NPCs (the walk to a distance does collide) | `field_tests`: "a care walk to a point goes through walls and ends on the point's cell" | Same. Measured (2026-10-31, twna01 bathroom): `species_a` moves on to the second point at (−2115, −1939), already on the cell of (−2122, −1930), and enters the stall up to (−2251, −1266) |
| The partner leaving the bathroom (care walk animation) | `partner.c: tickPartnerToilet`: substate 0 starts the run (4) and substates 1 and 2 continue with it; 3 starts the walk (2) when animation 10 ends and `handleToilet`; 4 reaches the point and releases the script with the walk set, and `partnerTickWalking` (state 1) continues from that animation without stopping | `Field::walkPartnerTo` sets the movement at once; on reaching a point the step keeps the movement; `holdForCare(false)` with the partner walking or running continues into following with animation 2 or 4 at the frame it reached (`playPartnerFrom`); `FieldScene` does not restart the animation it already draws when following takes it over | `field_tests`: "a care walk walks from its first frame, and let go on its point the partner walks on" | Same. Measured (2026-11-01, twna01 bathroom): in the original, animation 10 → 2 without passing through 0, it stays on 2 when returning to state 1 at (−2120, −2024) and only goes to 0 when stopping at (−2133, −2178). Before, dwe inserted animation 0 one frame after 10, again when releasing it, and then ran (4) for 0.25 s before walking; now 10 → 2 → 0, stopped at (−2123, −2179) |
| The camera after a battle won: it pans back onto the player in 10 frames and the section waits for it, so the field follows on with the player centred (the player ran back in the battle's opening); a fixed fight (trigger 1) and a flight do not pan | `script_interp.c:1949`: `SCRIPT_MOVEMENT[10]` type 7 to entity 0xFD, speed 10, `WAIT_FOR_ENTITY` 10; `map.c: tickCameraMoveToEntity`, `tickCameraMovement` | `StoryBattles` (`story_battle.cpp`) asks `startCameraReturn` and waits for `cameraReturned`; the session (`story_scenes.cpp`) fills the camera's slot of `ScriptMotion` (`cameraToCharacter`, `cameraMoving`) | a victory goes on once the camera has panned back onto the player; a fixed fight won leaves the camera where it is; the camera's pan to the player starts where the player stands and holds its slot | Same. Measured in DuckStation in `icsa05` against Icemon: the player runs back from (0, 200) to (19, −1100), at the bottom edge of the screen, and after the result boxes `tickCameraMoveToEntity` writes `CAMERA_TARGET` and the camera recentres (drawing offset y 307 → 189). Before the fix dwe left the camera where the battle had it, with the player at the bottom edge (tester report, Ice Sanctuary). Seen in dwe after the fix at the same spot (`_work/routes-e2/w97-ice-camera/`) |

## Data

- `field/rules` (`content/packs/game/field/rules.yaml`, [schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/field_rules.schema.json)):
  radii, speeds and turning of the player and the partner, the following distances, the trail and the limits for
  unsticking, the stick's dead zone, the map change (frames, fade tones and name frame),
  the times of day, and how NPCs move and how they react to the player (`npc`).

The field has no clock of its own: `gameTimeRan()` says whether the last tick ran game time (it does not run it during
a map change, `tamer.c: stopGameTime`/`startGameTime`), and whoever runs the loop advances `ruleset::GameClock` with
it. In the game the session does it ([packages/session](../apps/session.md)).
- `field/warps` (`content/packs/game/field/warps.yaml`, [schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/field_warps.schema.json)): the
  starting map and exit, and the warps that a script did in the original, each with its test.

- `field/triggers` (`content/packs/game/field/triggers.yaml`, [schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/field_triggers.schema.json)):
  what the player's cell does according to its collision value: step on (51–79), confirm (80–109), exits (110–119),
  bathroom (120) and the blocking bit (0x80, fixed by the map format). `loadFieldRules` reads it into
  `FieldRules::triggers` (`trigger_bands.hpp`); without the file, the original's. `warpViaOf(value, bands)` and
  `TriggerBands::exitOf` are the two questions the field asks; the app uses them for the bathroom and arrival by warp.
  `parseTriggerBands` rejects bands that overlap, fall outside 1–127 or have more than 10 exits.

All three can be changed from a mod pack with the same id.

## Tests

`grid_tests.cpp` (collision with fixed trajectories, sliding, not walking through walls, paths), `field_tests.cpp`
(movement, warps with their fade and their name frame by frame, following, unsticking and place after a partner
script; the release rule, the overlap watchdog, interpolation, the partner's size and the NPCs after a
script) and
`rules_tests.cpp` (reading the YAML files, including those of the `game` pack, and the fade curve),
`trigger_bands_tests.cpp` (the `game` pack's bands are the original's, value by value, and the errors) use test maps. `route_tests.cpp` walks the phase 2 route in the extracted content if
`DWE_CONTENT` points to it: File City (`twna01`) → old man's house (`room08`) → File City, with the stick and with clicks.
Each leg starts when the fade in releases the player (a click while it holds the player is discarded), and the stick
points at the next point of the path as soon as a step reaches the current one: the player advances the run of one frame
(50 units) at once.
Each piece also has its own: `partner_follow_tests.cpp`, `player_trail_tests.cpp`, `field_items_tests.cpp`,
`script_warps_tests.cpp`, `boss_arrival_tests.cpp`, `white_fade_tests.cpp` and `reachability_tests.cpp` (slow: that a new
game reaches its maps and comes back).
