# game

`dwe-game`, the game: it opens the window, mounts the packs, starts the game (`packages/session`), draws the field
(`FieldScene`) and the screens (`ui/*` of the `game` package, with the view models of `packages/screens`) and runs the
autotests. The options, the controls and how the routes are run are in the
[vertical slice guide](../../guides/vertical-slice.md); the UI and its commands, in
[ui.md](../../architecture/ui.md).

## Seed

`--seed <n>` gives the seed of a new game (`SessionDesc::seed`): the partner's random numbers and those of the
systems come from it, so a route with the same seed plays the same every time. Without the option, the game takes one from the
system clock and writes it to the log (`game: new game, seed <n>`) so that the game from a bug report can be repeated.
With `--load` the option changes nothing: the loaded game continues with the state of the random numbers it saved.

## The partner on the field

The field draws the partner with the model of its species: `digimon/<species>/model` of the base pack, with the
animations of `models/partner` of the `game` pack, which have the same ids on all partners
([models](../../architecture/models.md)). At the start, a loaded game draws it with the saved species; after that,
the game takes the partner's events every frame (`GameSession::takePartnerEvents`). The
[evolution scene](#the-evolution-scene) and the one for [death and rebirth](#death-and-rebirth) keep their own and
`showPartnerEvents` (`partner_events.cpp`) looks at the rest: `Evolved` (an evolution, the virus form or its return) and
`Reborn` load the model of the new species (`FieldScene::showPartnerSpecies`) and `GotSick` appears in the
[award notice](#award-notice) (`AwardScreen::noteSickness`). The others are not visible on screen and are only written
to the debug log. If the model of a species cannot be loaded, the partner keeps the one it had and the log
warns about it.

`evolve("<species>")`, in the console and in the `script` steps of the routes, makes the partner evolve like a map
script (`EvolutionAdapter::evolveByScript`, the same path the scripts use); it is a debugging aid. The route
`tests/autotest/route-evolve.yaml` uses it to check that the model changes (`wait: {partner_model: ...}`).

The collision radius is the species' own (the one from the species table): `showPartnerSpecies` passes it to the field
(`Field::setPartnerRadius`). Gap: the partner's walking and running speeds are still those of `field/rules`
(measured on the starting partner), not those of the root of the new species' animations.

## Giving an item on the field

Using an item from the bag on the field does not apply it right away: it plays the original's scene (`game_menu.c:
startFeedingItem`, `partner.c: partnerTickFeedItem`, in `feed_scene.cpp`). The menu closes and the player stands still
while time runs (`Field::holdForCare`). The partner walks to the distance at which it takes items: it comes closer if
it is far and moves away if it is close (`Field::walkPartnerToRange`, `checkEatDistance`). That distance is per species in
`field_items.feed.take_distance` (`ITEM_TAKE_DISTANCE`). The player holds out the item and the sound plays. After
frame 7, the partner eats it and the item is applied at the end, or it refuses it and the item stays in the bag. While
it lasts, death and evolution wait, because the player is not free. When the scene opens, the menu closes
(`startFeedingItem` sets `IS_IN_MENU = 0`). Care scenes wait for a clip to finish its first loop, also
if it loops (`FieldScene::partnerClipFinished`, `playerClipPlayedThrough`); with a looping clip, the refusal
never ended. Without the ruleset's `feed` block, the bag uses
the item right away, as before. The console's `use_item` also uses it right away. From when the player holds it out
until frame 11 of the eating animation (or until the end of the refusal), the item's icon is shown facing the camera
on node 9 of the player's model, without a shadow (`renderFeedingItem`, `addObject(0xfa7)`/`removeObject`;
`FieldScene::setHeldItem`, `feed_item.hpp`; capture `w11-missing/shots/feed-item-after.png`).

Differences from the original:

- The partner decides whether to take it when the item is chosen, not when it is held out: it is the same roll, a moment earlier.
- If the partner gets stuck against a wall, it stops where it is; the original keeps walking.

## Picking up an item from the ground and opening a chest

When stepping on an item on the ground, the player crouches to get it while the notice appears (`tamer.c: tamerTickPickupItem`,
state 0: `startAnimation(ENTITY_TABLE[0], 0xc)`). The animation is `field_items.pickup_player_clip` (12 in the
`original` ruleset) and `FieldItemsScreen::tick` sets it with `FieldScene::playPlayerSceneClip`. It plays once and stays on its
last frame until answered. On answering, the item goes into the bag (or stays, with a full bag, after the
second notice, without changing animation) and the player walks again on the next tick (state 4: `tamerSetState(0)`,
whose animation starts on the next frame). Without the key, the player just stands.

The notice only holds the player (player states 7 and 14; `Field::holdForItem`): the rest of the field goes on. The game
clock runs and the clock and the bars stay on screen (`tickGameClock`); the NPCs keep walking, but do not talk or
touch the player (their scripts wait for state 0); the partner's care (needs, poops, sleep) and the map
cells (the toilet) wait, as in the original (`tickConditions` and `checkMapInteraction` look at
`tamerGetState() == 0`). With an item on the ground, the partner stays in its normal state: it follows the player and shows
its bubble (`partnerTickNormal`). With a chest, it stands still with its animation 0 (`partnerSetState(0xb)`,
`partnerTickIdle`) and without a bubble until the notice is closed (`partnerSetState(1)`).

When confirming next to a chest, the player turns suddenly toward it and stands with animation 0
(`tamerTickTakeChest`, state 0: `startAnimation(ENTITY_TABLE[0], 0)` and `entityLookAtLocation`). From the next
frame the drawer comes out, and the box opens when it is out. The first confirmation puts the item in the bag and
marks the chest (`setTrigger`), but the notice stays; the second closes it (state 4). The original gives the item
on closing (`giveItem(..., 1)`); the engine gives it on the first, with no visible difference because nothing can happen between the
two. With a full bag, the drawer goes back in under the second notice, which is closed with another confirmation.

The box takes as long to accept the confirmation as in the original. It opens on the frame after stepping on the
item (state 1, `createAnimatedUIBox`), or when the drawer comes out, and counts the frames of `TAKE_ITEM_FRAME_COUNTER`
(`ItemNoticeCounter`): an item on the ground accepts a press from its fifth frame (`4 < TAKE_ITEM_FRAME_COUNTER`)
and the confirmation sound plays (`playSound(0, 3)`); a chest accepts the held button from the sixth (`5 <`, `POLLED_INPUT`) and
no sound plays. An earlier press is lost. The counter goes back to 0 with each confirmation and does not count while the
drawer moves. In the original skin, the box grows from the player in 5 frames with its gray outline and its sound, and when
closing it goes back to the player (`ui/common/original_box.lua`, like the field menu). Before, the notice stopped the whole field and the
clock, appeared whole on the same frame, accepted the confirmation at any moment and the chest closed with the
first; the player did not turn toward the chest and the partner kept doing its own thing.

Measured in the original (2026-11-06, an item dropped by a defeated enemy in mayo00, the field at 20 frames per
second): the player's state 7 starts animation 12 on the same frame the item is stepped on; it goes from frame 2
to 15 in 13 frames (about 0.65 s) and stays on 15. The box is created on the next frame, its outline grows 5
frames and the first accepted confirmation arrives 6 or 7 frames after stepping on the item (about 0.3 s). The
partner follows the player and the clock runs meanwhile. Measured in the engine (2026-11-07, game control, 60 ticks
per second): the box appears 3 ticks after stepping on the item, its outline grows until tick 18 and a press on
ticks 16 and 17 is lost, on 19 and 20 it is accepted. The same. The chest could not be measured in the original (there is no way to
reach one from the reference saves); its numbers come from the code. Tests:
`packages/session/tests/rules_tests.cpp` ("the original's player crouches with clip 12..."),
`packages/game/field/tests/field_tests.cpp` ("an item on the ground's notice holds the player while the partner
follows...", "a chest's notice stands the partner and turns the player to the chest"),
`apps/game/tests/item_notice_counter_tests.cpp` and the routes `tests/autotest/route-field-pickup-pose.yaml` and
`tests/autotest/route-field-chest.yaml`.

Differences from the original:

- While the box shrinks on closing (5 frames), the original already lets the player walk; in the engine the notice
  stays in front and keeps the keys for those 0.25 s. The field, the partner and the clock do keep going.
- Picking up poops by a partner that eats them (`tickPoopDetection`, in its normal state) waits for the
  notice of an item on the ground to close; in the original it goes on. It is a rare case and lasts less than a second.

## Sleeping

"Sleep" in the field menu plays the original's scene (`partner.c: partnerTickSleep`, in `sleep_screen.cpp` and
`sleep_steps.cpp`); saving and the fades are in [save and load](../../architecture/save-and-load.md#36-sleep).

Same: the clock stops and the player stands still (`Field::holdForCare`, player state 6). If the partner is not
already next to the player (closer than 2.5 radii, `getPartnerTamerCloseness` 2), it walks straight toward the player
(`Field::walkPartnerToRange`) and the two look at each other. Then `playSound(0, 15)` plays, the player does animation 8
(pats it) and the partner does 0; when the player's ends (31 frames, 89 vblanks measured) the partner
yawns with 11 (51 frames, 150 vblanks) and lies down with 9; the fade to black starts 5 frames before the
end of 9 (57 vblanks after it starts). Animation 9 moves the partner toward the player through its root motion
(`applyRootMomentum`: 70 units measured in the original and in dwe; `FieldScene::playPartnerClip(clip, true)`, which
moves it through `Field::moveByClip` like the scripts' animations). The field
goes on (the NPCs move) while they play their animations and during the two fades; it only waits with the sleep
menu. The fade-in shows the partner standing (animation 0) and the player in the last pose of 8, until
it regains control. The animation numbers are in `care.yaml` (`sleep.scene`). Measured with a `species_a` in
the city (partner state 3 written to RAM).

Same: no HUD during the scene. The original stops game time on the first frame of the scene
(`partnerTickSleep` state 0, `stopGameTime`) and does not start it again until the fade-in passes
34 (`startGameTime` with `FADE_IN_CURRENT` 35), and `clock.c: renderGameClock` shows the clock and the bars while
time is stopped (50 pixels per frame, from -135 to -220). Measured in twna01 at night (07_field_night): the menu had already
brought out the clock (-220); on choosing "Sleep" the menu starts time for one frame (-170) and the scene
stops it right away; the clock comes back in with the fade at 35. In dwe the field holds the player with time stopped
(`Field::holdForCare(true, CareClock::Stops)`), so `gameTimeRan` is false and the HUD (clock, "Tab Menu" and bars)
goes away from when "Sleep" is chosen until the player regains control; the original's stray frame is not
copied.

Same: the partner does not fall asleep on its own. When sleepy it makes its gesture (animation 0x11, `updateConditionAnimation`) and, if
the wake-up time arrives without sleeping, it is a neglect (`tickSleepMechanics`); in the original there is no bed or other
place to sleep than this menu.

Gap: the original, if the partner is off screen at the start, puts it on the off-screen cell closest to
the player (`tickPartnerWaypoints`); here it walks from where it is. If it gets stuck against something, the original
keeps walking against the wall and here it stops where it is and the scene goes on.

## The toilet

If the player steps on the toilet cell and the partner needs to go, the partner goes to the map's toilet (`FieldToilet`, in
`field_toilet.cpp`). It is section 0x4e2 of the original: `call_partner_routine(0)` and `partner.c: tickPartnerToilet`. The
player stands still while time runs. The camera goes to the toilet in 20 frames. The partner runs to the two points
of the toilet (`TOILET_DATA`, which the base pack brings in `maps.yaml`, `toilets`) and does the toilet animation there. When
it ends, the partner does its business (`handleToilet`), the camera comes back and the partner walks back to the first
point. Then the toilet sound plays. Without `field_items.toilet_walk`, or on a map with no toilet points, the partner
goes where it is, as before.

Same: the points of `TOILET_DATA` are in the original's axes, with z the opposite of the world
(`MapToilet::firstPoint`/`secondPoint`, `core::fromOriginalAxes`), and the partner runs to them without colliding with the
map (`Field::walkPartnerTo`: in state 6 the clip carries it without `partnerTickCollision`), so it enters the booth.
Before, z was not flipped: in twna02 the partner ran toward (−2122, −1930) instead of (−2122, 1930), got stuck at about
3800 units and the camera went to an empty place on the map, so the toilet animation was not seen. Measured in the
original (2026-10-31, `01_field_outside_jijimon_house`, twna01, the same toilet 8, condition 8 and the player on
cell (32, 75)): `species_a` runs from (−1650, −2400) and switches to the second point on stepping on the first one's cell,
at (−2115, −1939), about 45 vblanks later; it reaches (−2251, −1266), already inside the booth, and does animation 10 (51
frames) with the camera on top; then it walks back (animation 2) to the first point. dwe with the user's save
(twna02, `species_a`): the camera goes to the booth, the partner enters, does animation 10 on the toilet and comes back, with no
getting stuck (`dwe_assets_tests`: "a toilet's points turn the original's z the other way into world space"; `field_tests`:
"a care walk to a point goes through walls and ends on the point's cell").

### Extra toilets

An optional improvement, off by default, proposed by a tester so as not to always have to go back to the same toilet. The
`extra_toilets` setting (row "Extra toilets", "Baños extra" in Spanish, Game tab, saved in the user's
settings) reaches `game::field::Field::setExtraToiletsOn`. The booths come from `field/extra_toilets.yaml` of the game
pack (schema `field_extra_toilets.schema.json`). Each entry gives everything needed:

- **The drawn booth.** `FieldScene::showExtraToilets` places it with `setProps`, the same path as the story's
  objects: a 3D model in the map's scene, with depth in front of the characters and the background layers, and the
  map's light. The model is `props/toilet_booth`, ours: a low-poly wooden booth, open at the
  front, with the seat in view, a roof sloping backward and a plaque with a drawing of a toilet (no text). It is generated by `scripts/art/make_toilet_booth.py` (standard
  library only, always the same); it is in world space, +Y up, on 200 × 200 and with the front toward +Z.
- **The cells.** On entering the map (and on turning the option on), the free cells under the booth become
  solid and those of the strip in front (two cells deep, as wide as the booth) take the toilet's value
  (120, `field/triggers`). The walls, the warps and the other triggers stay as they are. On turning it off they become
  free again and the booth stops being drawn.
- **The toilet routine.** `FieldToilet` sees the cell; if it belongs to a booth (`Field::extraToiletAt`), the partner
  runs to the point right in front of the front (`ExtraToiletFront`, 200) and goes in to the center, instead of going to the map's
  points. That run does not collide with the map (`Field::walkPartnerTo`), so it enters even though the booth is solid.

The list has one booth: trai00 (the green gym), on the grass next to the fence, to the left of the sign, at
(−800, −3400) and open toward the camera and the path (3584). Checked live: it is seen from the map's camera, the
player collides with it and the partner that needs to go runs in front, enters and is hidden by the booth.

**Adding a booth.** One more line in `toilets` of `content/packs/game/field/extra_toilets.yaml` is enough (or in the same
file of a mod):

```yaml
  - {map: <map id>, position: [x, z], facing: <0-4095>}
```

- `position` is the center of the booth, in the original's axes (those of `place()` in the console and those of the map
  table). On a corner of cells (multiples of 100) it occupies exactly 2 × 2.
- `facing` is the original's Y rotation toward which the open front looks. With a quarter turn (0, 1024, 2048,
  3072) it fits the grid; diagonally, 512 for example, it also occupies four cells and leaves five in front.
- `model` (optional) changes the model of that booth. The one for all is the `model` key above.
- To find the spot, the `dwe` MCP (`start` with `scene`, then `place` and `screenshot`) or the console will do: the spot
  must be free on the grid and have the strip in front free.

Tests: "an extra toilet's booth is solid and its front takes the toilet trigger only while on" (`field_tests.cpp`);
"extra toilets read their booth's map, place, facing and model, and refuse a bad one", "an extra toilet's booth stands
on 2 x 2 cells and its trigger strip is in front" and "the game pack's field data is valid", which also loads each
model (`rules_tests.cpp`).

### Angemon's event zone

An optional improvement, on by default, asked for by a tester. In the original, Angemon's event on the Ice Sanctuary's
ground floor (icsa01, script 93, section 0x05: "Oh? About that statue...", Angemon joins the city) only plays as the
arrival section of the stairs up from the trapdoor passage: script 94's section 0x33 does `warp_to(97, 1, 5)` once
trigger 322 (Angemon's light, taken on the top floor) is set. Coming back by the floor-0 teleport (script 97, section
0x36, `warp_to(97, 2, 255)`) or through the front door from frzl05 runs no section, and the corridor band (cells of
value 52, rows 47-48, x -800..800, z 100..300; section 0x34) only plays the partner's discovery (323/324). So the
player has to go down the trapdoor and up again. Measured in the original (2026-11-17, `w97_icsa05_after_win` with
322, 323 and 324 set): the icsa05 teleport lands at (0, 0) on icsa01 with the partner at (0, 200), and walking through
the band starts nothing (trigger 220 stays clear). dwe matches that with the option off.

The `angemon_event_zone` setting (row "Angemon on the ground floor", "Angemon en la planta baja" in Spanish, Game tab,
saved in the user's settings) reaches `session::StoryScenes::setImprovement(StoryImprovement::AngemonZone)`. The story
patch `story/patches/angemon_zone_093.lua` of the game pack wraps section 0x34: it asks the engine's routine 261 (the
improvement's number in variable 254, the answer there: 1 on, 0 off) and, on, with 322 set and 220 clear, jumps to
section 0x05; otherwise it runs the original's section. The event itself is the original's, from wherever the player
crosses the band. Tests: "routine 261 tells a story patch whether a story improvement is on" and "the Angemon zone
patch plays the event on the corridor band once the light is freed" (`story_improvement_tests.cpp`). Checked live
(dwe, `icsa01,2` with 322, 323 and 324 set): the teleport lands at (0, 0) with the partner at (0, 200) as in the
original; with the option on, walking into the band plays the event to its end (trigger 220 set); with it off from the
Game tab, the band runs the original's section and nothing starts.

### Whamon in the water

An optional improvement, on by default, asked for after a tester report. In the original, Whamon (entity 5 of the Secret
Beach Cave, ogre10, script 136) spawns far back at (752, 4022), off camera, and only comes into the water at (1457,
2916) once trigger 224 (the Ogremon event) is set: the map's entry section (0xFE) slides it there, and the ride's
arrival sections (0x3C, the first trip from frzl16; 0x3D, from script 162) do the same when the player comes on its
back. Before 224 the player can still talk to it from the confirm cells by the shore (value 81, section 0x51: "Going
back to the city?") while it is not on screen. Measured in the original by w112c (2026-11-19, sheet
`_work/tmp/w112/sheets/whamon.png`): on the confirm cell with 224 clear, Whamon stays at (752, 4022) and the prompt
shows with no Whamon in view; dwe does the same with the option off.

The `whamon_in_water` setting (row "Whamon in the water", "Whamon en el agua" in Spanish, Game tab, saved in the user's
settings) reaches `session::StoryScenes::setImprovement(StoryImprovement::WhamonInWater)`. The story patch
`story/patches/whamon_water_136.lua` of the game pack wraps section 0xFE: with 224 clear it asks routine 261 for
improvement 2 and, on, the engine's routine 262 for the arrival section that runs after the entry
(`game::story::StoryRunner::arrivalAhead`, answered in variable 254, 0xFF none). On a walk in or a loaded save with the
option on, it turns Whamon to rotation 3584 (its head to the shore), waits a frame (a slide ordered on the same slot
would end the turn) and slides it toward (1500, 2800) in 8 frames, waiting for it; it keeps its idle clip. A slide can
stop up to one step past its point (tamer.c: tickEntityMoveTo; the original's 2-frame slide to (1457, 2916) after 224
leaves Whamon at (1809, 2363)), so the 8 frames keep that within (93, -152) of it. That spot is off the confirm cell
(885, 1561), outside the shore's walkable cells and clear of the green machine (objects 29 to 56 around (160, 2200)).
Then the original's section runs. After 224, on a ride in (0x3C, 0x3D) and with the option off, the section is the
original's, so the ride's arrival scenes and the spot after the event do not change; talking and the ride (section 0x51)
are the original's. Tests: "the map's entry section knows the arrival section that runs after it"
(`story_runner_tests.cpp`), "routine 261 tells a story patch whether a story improvement is on", "routine 262 tells a
map's entry section the arrival section that runs after it" and "the Whamon patch puts Whamon in the water before the
Ogremon event" (`story_improvement_tests.cpp`). Checked live (dwe, 2026-11-19, `ogre11,0` with 224 cleared, then
`warp("ogre10 0")`): routine 262 answers 255, Whamon ends at (1589, 2654) with rotation 3584, in the water by the shore,
and confirming on (885, 1561) shows "Going back to the city?" with Whamon in view; "I'm going back" warps as before. The
first ride from frzl16 (175 and 352 set) runs the entry with routine 262 answering 60, the patch does nothing and
section 0x3C plays as the original's (Whamon at (1465, 2906), rotation 0). Sheet:
`_work/tmp/w112/sheets/whamon-improvement.png`.

## Praise and scold

"Praise" and "Scold" in the field menu (Tab) play the original's scene (`partner.c: partnerTickPraiseScold`,
`tamer.c: tamerTickPraiseScold`, in `praise_scold_call.cpp`). The menu closes, the player stands still while
time runs and the partner comes over until it is at the player's side; the two look at each other. As with sleeping, the field holds the
player (`Field::holdForCare`) and, if the partner is not already at its side, walks it in a straight line to within
2.5 radii (`walkPartnerToRange`; `partner.c: partnerTickPraiseScold` states 0 and 1, `getPartnerTamerCloseness` equal
to 2). Before, the partner only followed the player, and following stops between 2.5 and 3.5 radii: a partner standing
still at that distance never arrived and the scene did not end. When praising, `playSound(0,
14)` plays, the player does animation 38 (39 if the partner is a Champion or higher, except `species_ba`, `species_au` and
`species_az`, which are short) and the partner does 11. When scolding, `playSound(0, 13)` plays, the player does animation 7 and
the partner 12, or 25 if the scolding comes right after a refusal or a poop on the ground
(`ITEM_SCOLD_FLAG`). When the player's animation ends, the praise or scolding counts (`handlePraiseScold`: discipline and
happiness), the player stands again and the scene ends when the partner's animation finishes its first
loop. The sounds of those animations play like those of any field clip (`FieldScene::playClipSounds`). The
animations are in `field_items.praise_scold`; without that block, the praise or scolding counts as soon as the partner
arrives, without animations, as before.

Measured in the original (2026-10-30, `species_a` next to the player in twna01, the field at 20 frames per second):

| Case | Player's gesture | Partner's response | Counts at | Scene | Change |
|---|---|---|---|---|---|
| Praise | 38, 45 frames | 11, 51 frames | partner's frame 46 | 50 frames | happiness +7, discipline -5 |
| Scold | 7, 17 frames | 12, 41 frames | frame 18 | 41 frames | happiness -10, discipline +2 |
| Scold after a refusal | 7, 17 frames | 25, 17 frames | frame 10 | 25 frames | discipline +8, happiness +3 |

In dwe the same animations with their sounds (11 sounds on its frames 12, 23 and 39; 12, on 10) and the same
order: the gesture, the praise or scolding when it ends and the finish with the response.

Differences from the original: the animations start on the same tick the partner arrives; in the original, the
player's starts one frame later (the partner puts the player in state 13 or 9 and the player handles it on its
next tick). A special evolution after the praise or scolding (`handleSpecialEvolutions(3)`) waits for the scene
to end, as in the original. The scolding after a refusal changes happiness with an uninitialized value in the
original (+3 in both measurements); dwe uses 0 (`care.yaml`, `boosted_scold_happiness`).

## Poops on the ground

`FieldPoop` (`field_poop.cpp`) places on the map the piles the partner leaves (`partner_impl.c: createPoopPile`):
with the field free, each pile pending in the session (`PoopAdapter`) goes to the cell in front of the partner according to
where it is facing, or to a random neighboring one if there is already one. Every frame it passes the map's piles to `FieldScene::setProps`: the
model of `poop_piles.look.model` at the center of its cell, at the partner's height and at the scale of its size between
`model_size` (`renderPoop`). Without the `poop_piles` section the piles are saved but not seen.

Color (Same, measured in `twna01` with a pile set through RAM in `WORLD_POOP`): the pile is brown and opaque
(texels 148,99,41 / 132,74,24 / 115,57,8 of palette (96, 501), gouraud shading near 1.0) with a shadow that
darkens the ground. `ETCNA/UNTI.TMD` marks its primitives semi-transparent in subtract mode, but the console only blends
texels with STP: the pile has none and the shadow does. The extractor resolves it (version 9, see
[per-texel semi-transparency](../../formats/models.md)); with an older `unti.glb` the pile comes out black. The giant poop
of `gomi02` (`ETCNA/TRY.TMD`) is not semi-transparent and does not change.

`FieldStoryProps` (`field_story_props.cpp`) draws what the story puts on the field besides the map and its characters
(`session/story_props.hpp`): the Drill Tunnel's cart in front of the partner, at its radius plus 100 and turned like it
(`map_object.c: renderDirtCartModel`), the dirt pile of tunn05 at the scale of its size and the rock of tunn06, all
with `FieldScene::setProps` (one set of places per model); it removes them on entering another map (`unloadMapParts`).
It carries the fade to white of routines 45 and 46 (`game::field::WhiteFade`) at 20 frames per second and passes it to
`render::ScreenFade::setWhiteness`; the tone comes from `Field`'s and goes back to it, because the original shares `FADE_PROGRESS`
between fades, and a map change removes the white (`fadeToBlack` deletes object 4005).
The final boss's arrival in routine 40 is passed to `BossArrivalScene` (`boss_arrival_scene.cpp`), which runs
`game::field::BossArrival` at 20 frames per second: it removes the boss from what is drawn (`FieldScene::setHiddenNpcs`, the
`isOnMap = 0` of `EAB_startBuildup`), draws the rings of `ETCHI/BOSS_EFE.TMD` turned half a turn where it is, at the
scale of each frame and tinted to 0x3c/0x80 (`EAB_setModelColor`, with the tint of `FieldScene::PropPlace`), and sets
the held hum of slot 8 (`playSound2(8, 0)`); on entering another map it removes everything.
Partner routine 8 (player state 18 in the original) is played by the same `BossArrivalScene` from `StorySceneGlue`:
the arrival sets a `FieldStage` with the screen subtracted to black (`EAB_renderFlash`, a subtractive quad in front of
everything), the scenery, the player and the partner hidden and only the boss (`FieldStage::shownNpc`) with its rings
(`shownProps`), the camera turning around the boss's root bone (`EAB_calculateCameraOrbit`, with the map's
projection) and the particles as four distant lines; it sets the boss's animations 0x21 and 0x33 like a `play_animation`
and sounds 1, 2 and 3 of slot 8. When it ends it gives the scenery back and the story goes on
(`StoryScenes::partnerSceneFinished`). Test with the route `_work/routes-e2/boss-arrival/route-mgen99.yaml` (new game
in mgen99 with all stages): load, arrival and battle against the boss.
It moves the shake of routine 34 (`screen_shake.hpp`: `ScreenShake`) at 20 frames per second, passes it to the camera
(`MapRenderer::setShake`) and draws its subtractive red square over the scene with `FieldScene::setStoryScreenPrims`
(in front of the map and the characters, behind the interface), and it carries a
map sprite to a character's node (`spawnSpriteAtEntity`, with `FieldScene::entityNodePoint` and
`MapLayers::spriteSize`). The mood bubble of routine 28 (`mood_bubble.hpp`: `MoodBubble`) grows and rises at 20
frames per second and is drawn with `FieldScene::setStorySprites`: an additive quad of the image
`map/<map>/texture4/000` at the species' condition point (`conditionFx` and `entityNodePoint`); with
extracted content without `condition_fx` it does not appear. As in the original (`translateConditionFXToEntity` reads
`CONDITION_FX_OFFSETS[getOriginalType(type)]`), a variant takes the point of its original type
(`condition_point.hpp`: `conditionPointRow`, with `effects.original_types` of the battle rules): two of the four
customers of the cannon shop (types 93 and 99, which the original maps to 57 and 25) have their own row at zero (node 0
with no offset, the model's root) and the bubble ended up inside the body. The same goes for the poison and the
confusion of enemies in battle and the partner's butterfly.

A poop on the field has its own scene (`partner.c: tickWildPoop`, state 7), with `poop_piles.wild`: the player stops and
looks at the partner, which runs toward them up to its running distance (`Field::runPartnerUp`), squats (`squat_clip`),
leaves the pile in front of them and does `after_clip`; then the player is free. The scene starts when the partner
has a poop waiting (`Partner::wildPoopDue`); the pile and its effects (care error, mood, weight) arrive when it is
left (`GameSession::leaveWildPile`, `handleWildPoop`), and the possible drop in tamer level at the end
(`GameSession::finishWildPoop`, substate 3), as in the original. Without `poop_piles.wild` everything arrives at once. The poop
of a night's sleep comes out without a scene (`handleSleeping`). With `poop_piles.eat`, a partner that eats poops (`care.toilet.poop_eaters`) looks, with the
field free, for a pile on the map with the player less than `small_reach` from its center in x and z (`large_reach` from
size `large_from`; `tickPoopDetection`), walks to it, does `eat_clip` and eats it (`handleEatingPoop`). Both
scenes let time run and close the pause menu.

## The evolution scene

`EvolutionScene` (`evolution_scene.cpp`) plays `evl.c: EVL_tickEvoSequence` frame by frame: the map, the player and the
others fade to black in 0x20 frames (`EVL_fadeClutBank0/1`, `FieldStage::sceneryDarkness`, which darkens the background,
the layers and the models), the camera turns around the partner and at frame 0x6A the burst starts. The effects
live in `EvolutionEffects` (`evolution_effects.cpp`, with no engine state, with tests) and are drawn as the
original's primitives over the scene (`ScenePrimDrawer`, `scene_prims.cpp`):

- Green squares on the ground at the partner's feet, nine of every 18 frames (`EVL_D_80063F28`, `EVL_spawnParticle`,
  `EVL_tickParticle`): they grow from 8 to 0x9C4, fade out in 0x56 frames and go behind everything.
- The bones break in the order drawn at the start (`EVL_initEvoSequence`, `rand() & 0xFFF` per bone): those
  of table `EVL_D_80063EFC` and then one per frame from 0x9B to 0x11B; bones without an object are skipped. Each break
  sounds (slot 8, effect 2 to 4), turns the bone to wireframe, throws its faces as shards for 0x1F frames
  (`EVL_buildShardSet`, `EVL_tickShardSet`), a ray from bone 1 that lasts until the change (`EVL_tickSpark`,
  `DEFEFF` palette 1, additive) and a flash (`EFECreateFlash`, mode 1).
- The body lightens from 0x70 to 0xCD with a random factor per channel (`ModelInstance::lift`).
- On the even frames from 0xD7 to 0x11B, large flashes over bone 1, in front or behind depending on the frame
  (`SPOT256`, `renderEFEFlash`; its sine is the one in `math.c`, 512 per turn).
- At 0x11C the model changes; the new form appears in wireframe and fills in from 0x126 to 0x14E (`renderWireframed`,
  `WIREFRAME_RNG_TABLE`); at 0x162 the field is restored.

Measured in the original (forced with the time on `twna01`) and compared frame by frame with dwe in the same phases.

- Same, measured in the original (2026-11-04, state `01_field_outside_jijimon_house` with `EVOLUTION_TARGET` 5): when
  the fade ends (frame 0x20 of state 0, 99 vblanks after `EVL_initEvoSequence`)
  `EVL_setOtherEntitiesVisible(0)` takes the player and the two NPCs off the map (the copy `EVL_D_80068938` stores 1 in
  entities 0, 6 and 8) and leaves the partner; in state 3 `EVL_setOtherEntitiesVisible(1)` restores them. dwe sets
  `FieldStage::sceneryHidden` on the same frame (`showsPlayer`, `showsNpcs`) and removes it with `stage_ = {}`
  (`EvolutionScene::restore`). The stage only masks the drawing, so whoever was hidden before the scene
  stays hidden afterward, as with the original's copy.
- Same (2026-11-08, a tester's report: "the evolution sound doesn't go away until I close the game"): at the
  burst (0x6A) effects 1 and 0 of slot 8 (bank 18) play, whose samples repeat in a loop until something
  stops them; the original cuts them at frame 0xD6 with `stopSound` right before the roar (effect 5, no loop). dwe
  played them without keeping their voices and never stopped them. Now the scene keeps every sound it plays
  (`HeldSceneSounds`, `scene_sounds.cpp`, the same one death uses), stops them at 0xD6 before the roar and stops whatever
  is left at the end or on skipping (`skip_evolution`: a skip before 0xD6 also cut the loop halfway in dwe).
  Tests: `scene_sounds_tests.cpp` ("the burst's looping sounds stop with stopSound…"); live, the log shows
  `thunkStopSoundMask` of both loops at 0xD6 and, on skipping, at frame 181.
- Same (2026-11-09, user report: "the evolution bubble doesn't appear in every case, not even with the
  cheats"): before the fade the partner shows a bubble (`main/evl.c: tickEvoSequenceLoading` calls
  `addConditionBubble(7)` at the end of its first 56-frame wait, when the `ready` clip starts; `bubble.c`:
  `CONDITION_ICON_EVOLVING`, `CONDITION_SPRITES[9]`, the four frames of `CONDITION_EVOLVING_FRAMES`). It appears with
  any cause (natural, item, script, virus, its return and `evolve` from the console or the development menu), because
  all of them go through state 13 (`partnerTickEvolving`). dwe never drew it: the HUD bubble only showed
  needs and was hidden with the field stopped, and the scene stops time. Measured in the original (state
  `01_field_outside_jijimon_house`, `EVOLUTION_TARGET` 5): `addConditionBubble(7)` from `tickEvoSequenceLoading` and
  `frameCount` 15 at 45 vblanks and 30 at 90 (one step per frame, at 20 per second, counting the frame it
  is born on); it lasts frames 1 to 51 (grows in the first 10, bursts at 51) and disappears before the fade. Now the
  scene counts those frames from the switch to `ready` (`EvolutionScene::bubbleFrame`, `evolving_bubble.hpp`), the
  game publishes them (`hud.partner.evolving`, with the point visible even if the field is stopped) at the height of the form
  still being drawn (`drawnSpecies`: the session already evolved the partner; `addConditionBubble` reads the entity's height
  without changing it), and the HUD puts the class `evolving` on the bubble instead of the needs. The original skin
  draws it with its sprites (`ow-evolving`, palette 2 of `FEEL_EF`); the modern one does not show the bubble, as with
  the needs. Tests: `evolving_bubble_tests.cpp`, `field_hud_view_tests.cpp` ("the evolving bubble's frame goes to
  the field HUD…"), `ui_tests` ("the evolving bubble shows over the partner…") and `evolution_adapter_tests.cpp` ("every
  way to evolve reports one evolution…"); live captures with `evolve` from the console and from the development menu.
- Same (2026-11-09): before the scene the partner walks up to the player (`partner.c: partnerTickEvolving`,
  substate 0: stops time, closes the menus, holds the player, who turns toward the partner, and sets clip 2,
  walk; substate 1: the partner turns toward the player every frame until `getPartnerTamerCloseness() > 0`, at
  3.5 radii or less, and then `getEvoSequenceState` leaves it still and the first 56-frame wait starts). There is no
  time limit. Measured in the original (state `01_field_outside_jijimon_house`, `EVOLUTION_TARGET` 5, partner at
  1196 from the player, radius 120): it walks with clip 2 about 22 per frame in a straight line, going through the hut between
  the two (only the clip's momentum moves it, `anim.c: tickAnimation`, with no collision test); the player does not
  move with the d-pad or open the menu, time stands still, the clock and the bars go away, and `getEvoSequenceState`
  arrives at a distance of 414 (≤ 420) at z −871 after 109 vblanks. If it was already at 3.5 radii or less, the scene starts on
  the first frame. dwe started the scene wherever the partner was. Now `EvolutionScene` has the phase
  `Approaching`: on its first tick (when another care scene has already released the field) it holds the field with time
  stopped (`holdForCare(true, CareClock::Stops)`), turns the player and sends the partner walking without colliding
  (`Field::walkPartnerUp`, the march of `runPartnerUp` with the walk clip and without collision); on arrival they look at each other and
  `Glowing` starts, so the bubble appears at the same 56 frames from the arrival (`partnerBubblePlaced`: the hold
  does not take its place away, unlike in the care scenes). When it ends it releases the field. In dwe, same state and same
  positions: it arrives at z −872 in about 100 ticks, with the player neither moving nor opening the menu with Tab. Difference: with a
  script holding the field (routines 2 and 10) the partner does not walk, the script moves it. Tests: `field_tests` ("an
  evolution walks the partner up to its running distance…", with a wall in between) and `evolving_bubble_tests` ("the
  evolving bubble keeps its place while the walk up holds the partner"); captures `evo-wait-*.png`.
- Same: at the start, `EVL_initShardSets`, `EVL_resetParticles` and `EVL_resetSparks` free the 30 shard
  groups, the 100 squares and the 16 rays; `EvolutionEffects::start` begins with empty lists and the same caps
  (test `a new evolution starts with none of the last one's objects and keeps the caps`). The alignment of the
  working memory does not apply.
- Same (2026-11-19, a tester's report: "when it evolves it doesn't play its own cry but the new form's"): the
  partner's clips play their voices from slot 3 (`anim.c: tickAnimation`, instruction 0x4000, sound/vlall).
  `main/evl.c: getEvoSequenceState` calls `loadVLALL` with the new form: its vball reaches slot 4 as the scene starts,
  but its vlall is only read, and `evl.c:539` (`waitForSoundBufferLoading(3)`) puts it in slot 3 at the swap (0x11C).
  Measured in the original (state `01_field_outside_jijimon_house`, Agumon with `EVOLUTION_TARGET` 5): no sound
  during the walk or the `ready` clip; at 0x6A the slot 8 effects 1 and 0 and the breaks (2 to 4); at frame 117 the
  burst clip (0xC) of Agumon plays `playSound(3, 0x32)` with Agumon's vlall still in slot 3 (no upload of slot 3
  until vblank 23642, frame 0x11C); at 0xD6 `stopSound` and the roar (slot 8, 5); in state 4 (frames 372 and 392)
  the `finish` clip (0xB) of Greymon plays `playSound(3, 0x31)` from Greymon's vlall. So the burst cry is the old
  form's and the one after the change the new form's. dwe evolves the partner in the session before the scene, and
  slots 3 and 4 followed it at once: the burst cry (sound 0x32) played from Greymon's vlall (`sound/vlall/002`
  instead of `001`). Now the scene keeps the old form's banks in slots 3 and 4 from its start
  (`GameSession::keepPartnerSounds`, `SessionSound::keepPartnerBank`), gives slot 4 back to the partner at
  `getEvoSequenceState` (the start of `Glowing`) and slot 3 when it draws the new form (`drawNewForm`, 0x11C), and
  lets both go at its end or on skipping. Every way to evolve (natural, Numemon and Sukamon, item, virus, the
  script's return, the console) goes through state 13 and this scene; death and rebirth (`dooa.c`) load the new
  vlall at the same moment the form changes, and battle cries do not use this. Test: `session_sound_tests.cpp`
  ("slots 3 and 4 keep the old form's banks while the evolution scene holds them"); live, the burst plays
  `slot=3 sound=50 bank=sound/vlall/001` and the `finish` clip `sound/vlall/002`.

Assumptions:

- Objects advance on the same frame they are born.
- The textured faces of the wireframe carry no light; the lightening is not applied to the shards.
- The order of the faces inside a bone is that of the groups of the exported model.
- The random numbers of the effects use their own seed, not the game's.
- The frames of the effects are constants in the code, not in the ruleset.

## Field effects

`FieldEffects` (`field_effects.cpp`, with no engine state, with tests) runs the field's own effects one original frame at a
time (the field's logic ticks, `transition.frameSeconds`) and `FieldScene` draws them with the same primitives as
evolution (`field_scene_fx.cpp`). The textures are those of `ETCDAT/ETCTIM.BIN` with their STP bit
(`stpSceneImage`): only texels with STP are added (`ScenePrim::stpMask`); the rest comes out opaque. Everything is cleared
on changing maps.

- Smoke and dust clouds (`efe.c: createCloudFX`, `tickCloudFX`, `renderCloudFX`): 14 frames, height, size, column
  and tone from their tables; up to 60. They are requested by the gym (`GymPorts::addCloud`, `TRN_createCloudFXLine`) and by each poop that
  comes out (`createPoopPile`).
- Gym sparks (`createParticleFX(0, 0, …)`, `tickParticleFX`, `renderParticleFX`): 0x13 frames, the flash
  (`SPOT256`, two) only on the second and 25 particles that spread out with the sine of 512 per turn; up to 4.
- Healing particles (`particle.c`): 44 frames; a spiral that descends from above the head (one particle per
  frame for 20) and, with large items, a ring of 4 per frame. Which items throw them and which carry a
  ring is in `inventory.yaml` (`effects.healing_particles`); medicine only when it cures the sickness.
  `InventoryAdapter::takeHealingParticles` passes them to `PlaySession`.
- Butterfly (`butterfly.c`): while the partner's only condition is being distracted and it is in its normal state;
  it flies in a circle around the `CONDITION_FX_OFFSETS` point of its species (species table: `condition_fx`, a model
  node and an offset) and flaps its wings every 2 frames.
- Smoke of a character that disappears (routine 38, `overworld.c: createNinjamonEffect`, `renderNinjamonEffect`):
  `ObjectSmoke` moves the first 41 objects of the map to the right and down with random steps, returns them to their place
  on passing x 160, puts them at entry 10 of the order table (`MapDepthLayers::setOrderValue`) and hides them at
  60 frames.
- Fog of Misty Trees and of two sewer screens (`overworld.c: renderMist`, which calls `renderMapOverlays` on
  screens 115-119, 121, 0xa3 and 0xdc): `MistLayers` (`field_mist.cpp`) and `FieldScene::addMist`. Two layers of the
  same image, the 256 x 200 texels of the 4-bit page at x 0x2c0 of VRAM (the map's second 4-bit image,
  or the only one in the sewer), stretched over the 320 x 240 screen and repeated: the back one (palette 0, tone
  0x96, B + F/4) moves with the camera and drifts 1 px to the left every two frames; the front one, mirrored
  (tone 0x50, B + F), moves 12/10 of the camera (truncated per frame, like the original) and drifts to the right; both
  rows follow only the camera's vertical movements. It goes in front of the map, the characters and their effects (entry
  20 of the order table) and behind the interface. The palette of the front layer is that of `MIST_CLUT_Y` (column /
  16: 1 in mist01-04 and 07, 5 in mist05) and the story gives it (`StoryScenes::mistOn`, `story_props.hpp:
  mistThisFrame`): trigger 0x94 leaves it at 8 / 12, colorless, and only the back layer remains; in the sewer both
  use palette 0 and trigger 0x155 removes the fog. Between two frames of the original the layers follow the camera
  so as not to shake against the background; in a wider window the fog reaches its edges and not the bars.

| Fog | Original (measured 2026-11-03, mist01 and mist05) | dwe | Status |
|---|---|---|---|
| Drift while still | `MIST_X_OFFSETS[0]` -51 and `MAIN_D_80134228[0]` +51 in 100 `PLAYTIME_FRAMES` frames; continues with the menu open | -1 / +1 every even frame (10 px/s) | Same |
| Camera | camera +160 x, -47 y in 21 frames: back layer -171 (-160 and the drift), front -177 with the drift (12/10 truncated), rows +47 | same calculation (`field_mist_tests.cpp`) | Same |
| Palettes | `MIST_CLUT_Y` 0x50 / 0x10 on arrival; 0xc0 / 0x80 a few frames after setting trigger 0x94 | 5 / 1 and 12 / 8 | Same |
| Order | covers the player and the partner; the clock, the bars and the menu stay on top | same (contact sheet in `_work/tmp/w19-fog/`) | Same |
| On entering a map | the previous camera (`CAMERA_X_PREVIOUS`) makes a phase jump; it wraps one step per frame | same, with dwe's camera | Assumption (not measured) |

- Reset of the map objects' animation (routine 21, `overworld.c: resetMapObjectAnimation`):
  `restartObjectAnimations` (`story_scene_glue.cpp`) zeroes the clock and the frame of the map's 2D objects from
  variable 247, as many as variable 248 says (`MapDepthLayers::restartAnimation`). The sparks of `species_ak`'s
  discharge (`gias02`, object 41) come out this way from their first frame.
- Numbers over the partner on the field (`entity_text.c: addEntityText`, `setCombatTextPosition`,
  `renderEntityText`): `FieldNumbers` (`field_numbers.cpp`) takes the damage `deal_damage` did
  (`StoryGameOrders::takePartnerDamage`) and puts it where the partner's feet were when it was added (on the field it does not
  follow it); it advances once per field frame (1/20 s) with the same bounce and the same tail as those of battle
  (`screens::DamageNumbers`), 31 frames. The `field_numbers` document uses the style of each skin's battle
  numbers. A battle that starts removes them.
- `deal_damage` (`script_interp.c`, `scriptPauseGame(0xff)` before the damage) freezes the world as soon as it does damage, not
  when the characters turn: this way the player's fall (animation 17) and the partner's (40) reach the end.

Measured in the original (2026-11-03, `species_ak`'s discharge in `gias02`, script 68, section 5, three times): each discharge
takes away 20 % of max life (800 → 640 → 480 → 320) and shows "160" over the partner from about 10 vblanks
later until 94; by 104 it is gone (31 field frames ≈ 93 vblanks). The sparks come out small at 10,
large at 18 and leave before 30. The player moves back about 400 units and the partner about 380, and both
fall; the number stays in view with the text box open. dwe: the number is seen until +94 and not at +106, the player
moves back 396 and the partner 383, and the three discharges give 640, 480 and 320. Same.

The sparks (object 41 of `gias02`, five frames of 1 frame each) follow the order of `renderMapOverlays`: the script
restarts them (routine 21), that frame is drawn the first cycle and only then counted, so it lasts one whole frame;
`MapDepthLayers::restartAnimation` also restarts the clock of 20 frames per second, which before could move to the second
frame on the same frame. Measured in dwe after the fix (2026-11-04, vblank by vblank from the key): the number and the
small spark at +6, the large one from +13 to +15, gone at +19; in the original the number and the small spark at +8..10, the
large one at +16..18 and gone at +28. From the number to the large spark, 7 versus 8: Same within one field frame.
Assumption: the whole scene starts about 2 to 4 vblanks earlier than in the original (the key reaches the script earlier, not the
sparks), and the text box opens earlier (+30 versus +44..64; depends on `species_ak`'s turn, not measured). The healing numbers with items
on the field (`item.c`, `addEntityText`) do not appear yet.

Measured in the original (2026-10-31): the gym's clouds and sparks (`TRN_tickDefenseTraining`, state
`trn_def_normal_bonus`) are created by a routine of the object list and `tickObjects` advances them the same frame, so
they are drawn for the first time in state 1 (the spark shows its flash on the second drawn frame and lasts one fewer);
a poop's cloud (`createPoopPile`) is drawn first in state 0. `FieldEffects::Birth` tells them apart. Same: particle
0 of the healing ring divides 512 by 0 (`512 / (i * 13 / 4)`); the original's code does not check the
divisor (`div` without `break`) and the processor's division gives -1 with a positive dividend, which is what is taken. Same: the
50 directions of the particles (`initializeParticleFX`) are those the original rolls at startup, measured in
`FX_PARTICLE_DATA` (the same in a field game and in a battle one).

Assumptions:

- The random numbers of the effects use their own seed, not the game's.
- A new species without `condition_fx` has no butterfly.
- The butterfly has not been measured on screen (the state of only being sulky could not be forced through RAM); its numbers are
  those of `renderButterfly`.

Ground items and chests on changing maps (#176): `map.c: changeMap` deletes loose items and chests
(`clearDroppedItems`, `unloadMap`) on loading the new map, before its header script, which places those of the new map.
`PlaySession` calls `FieldItems::enterMap` with the new map right after `FieldScene::finishTick`, at the same point.
Before, only `FieldItemsScreen::tick` did it, which does not run until the fade-in ends: during that fade
`FieldItemsScreen::show` kept drawing the previous map's items and chests at their coordinates, on top of the new
map. Test: `field_items_tests` "map.c changeMap: another map clears the last one's ground and chests, its own
stay"; in the frame-by-frame capture of room08 → twna01 with a Meat dropped in room08, it used to be seen over the grass
of twna01 for the 20 frames of the fade-in and disappeared on the last; now it does not appear.

Loading the same map again also deletes them (`changeMap` calls `clearDroppedItems` on every load). In the forest
of the centaur's test (trop06, script 18) the header rolls `store_random` seven times and places on seven cells one
of two or three items (50 % on the first three, 30 % on the other four: 2.7 on average) as long as trigger 236
(the centaur already in the city) is not set; routine 25, which it calls first, only fixes what returns in variable 0 at
the end. Each arrow it gives the partner, each "Missed!" and each lane failure return the player with
`warp_to screen=17`, which reloads trop06 and rolls again. Measured in the original (2026-11-07, entering trop06 from
`01_field_outside_jijimon_house` through RAM): first entry with 2 items (0 at (49, 53) and 16 at (67, 42)); after the
arrow and "Let's go back" `DROPPED_ITEMS` is empty and the header places 3 (0, 16 and 1 at (50, 34)); with trigger
236 set, none. dwe already placed them on entering by walking and on loading a save, but
`FieldItems::enterMap` only deleted on changing to another map: after each round the items of the previous roll
stayed and the new one was added (two on the same cell, until the 11 slots were full and the following rolls lost).
Now `PlaySession` calls `FieldItems::loadMap` with `FieldScene`'s load number (`mapSerial`), which deletes on
every load, that of the same map included; `enterMap` still does not delete for whoever only names the map. Test:
`field_items_tests` "map.c changeMap: a load of the same map clears the last load's ground and chests". Other maps with
`spawn_item` in their header checked in dwe: mayo01, trop01, miha03 and dgha01 roll and place their items and chests.

## The field behind the menu

On opening the game menu (Tab) while walking, the original puts the player and the partner in their rest animation, 0,
whichever they had (the player's walking state and `overworld.c: setPartnerIdling`), and keeps moving it
while the menu is open (`partner.c: partnerTickOverworld` with `IS_IN_MENU`, the player in its rest state,
6); the NPCs stand still
(`NPCEntityTickOverworld` returns immediately). `FieldClock` (`field_clock.hpp`) decides it every frame:
`MenuRest` with the menu (or the partner's sheet opened from it), `Battle` in a battle, `Frozen` with any other screen.

| Measurement (twna01, menu opened while running) | Original | dwe before | dwe now | Status |
|---|---|---|---|---|
| Player's clip | 3 (run) changes to 0 on opening | stayed frozen mid-stride | rest from its start | Same |
| Partner's clip | 4 changes to 0 (according to the decomp, also from a state clip) | frozen | rest (`FieldAnimation::Idle`) | Same |
| Clip frames with the menu open | player 2 to 12 and partner 2 to 6 in 60 refreshes | 0 | advance at 20 per second | Same |
| NPCs | still (`species_af` and `species_ac` without changing frame) | still | still | Same |

On closing the menu each one goes back to what it should do (the player's step or the partner's following). Test:
`field_clock_tests.cpp`; comparison in `_work/tmp/w21-trfield/contact_sheet.png`.

In a battle the field's characters stand, but the scenery keeps moving: the original draws the map with
`map.c: renderMap` every frame whatever `GAME_STATE` says, and its `overworld.c: renderMapOverlays` steps the animated
sprites' timers and `renderMist` drifts the mist with `PLAYTIME_FRAMES`. dwe used to stop them, and the weather and the
season's leaves drawn over them, for the whole battle. `sceneryMoves(FieldClock)` now keeps the map's sprites
(`MapDepthLayers`), the mist and the weather running with `Running` and `Battle`; Start's pause
(`utils2.c: handlePause`, which spins until the next press) and the pause menu still stop everything, in the field too.

| Measurement (mayo00, battle with Agumon) | Original | dwe before | dwe now | Status |
|---|---|---|---|---|
| Animated map sprite's timer | 189 to 193 in 10 refreshes (`GAME_STATE` 1), 164 to 168 as the battle starts | stopped | counts on | Same |
| `PLAYTIME_FRAMES` (mist drift) | 5070 to 5073 in 10 refreshes | mist stopped | drifts | Same |
| Same timer under Start's pause | stays at 17 for 32 refreshes | in the field the rain, the leaves and the clips went on | stopped | Same |
| Rain and leaves (improvement, no original) | none | still for the whole battle, intro banner included | fall | Improvement |

Test: `field_clock_tests.cpp` "a battle stands the characters but the map's sprites, mist and weather move on";
comparison in `_work/tmp/w112/sheets/battle-weather.png`.

## Menu lines

`renderHorizontalLine` (`script_common.c:2386`) draws three one-pixel rows: dark (0x020202), blue-gray
(0xa08769, red at the end: `#6987a0`) and dark. Measured in the original: rows 100-102 and 139-141 of the card shop's
quantity box, and 57-59 of the trade menu. Before, the trade and transport drew a dark one and
a light gray one (`#b0b0b0`), and the card shop only the bluish one; now all three (`card_shop`, `trade_menu`,
`bird_transport`) carry the three rows. Same.

## Opening the menus

Opening a menu stalled for a frame (#221). Measured with the measurement route (shop, save machine, game menu,
partner's sheet, items, player, options) on the RTX 2060, 1280x720, three alternating passes of each binary:
the CPU cost of the heaviest frame of each opening (ticks, update, interface and draw recording, not
counting presentation). The rest of the menu frames already fit comfortably (interface 1.3 to 1.9 ms on average).

| Opening (median of 3) | Before | Now | Cause |
|---|---|---|---|
| Partner's sheet, first time | 70 ms | 14 ms | the portrait (model and textures) was loaded on the sheet's first frame; now it follows the partner's form and is loaded when the game starts or on evolving |
| Game menu, first time | 22 ms | 12 ms | the player page arrived with the menu already visible and each changed setting redrew its tab (the City four times); now it is published right before showing it (`PlaySession::preparePauseMenu`) and the menu draws it once on opening |
| Partner's sheet, later | 13 ms | 9 ms | the two settings it sets on opening redrew the whole game menu it leaves (`pause_menu`: no longer) |
| Shop, first visit | 17 ms | 5 ms | each shop and service read the MAPHEAD listing (a quarter of an MB) on its first visit; now it is read on loading the game and once per content |

In addition the HUD redrew the minimap with every setting it did not know: the hundreds the partner's sheet publishes on
opening cost 5 to 7 ms; now it only does it with those it reads (`minimap`, `minimap_size`, `battle.active`). The
measurement route and the traces stay outside the repository (`_work/tmp/w38-ui/`).

## First frame of a loaded game

Loading a game stalled the first frames (#325): the status portrait and the characters the map brings
on the first tick (`FieldScene::followNpcs`) doubled their textures on the spot, while the background was doubled on its
thread. Now both use `ModelRenderer::setUpscaleInBackground`: smoothed textures at once and the doubled ones as
soon as they are ready, like the background. Measured with a route that loads the game from the first stretch of the chain (`--load`,
bridge) and opens the sheet, on the RTX 2060:

| First frame after `--load` | Before | Now |
|---|---|---|
| Portrait load, normal build (3 passes) | 60 to 70 ms | 8 to 12 ms |
| Slowest frame at start, normal build (3 passes) | 408 to 440 ms (ticks 342 to 374 ms) | 108 to 117 ms (ticks 40 to 47 ms) |
| Portrait load, `ci` build (ASan) | 2372 ms | 426 ms |
| Slowest frame at start, `ci` build (ASan) | 13871 ms (ticks 12612 ms) | 4989 ms (ticks 2281 ms) |

The doubled textures arrive about 40 to 150 ms later in the normal build (6 to 8 s with ASan); the sheet looks the same.

## Ending scene

`EndingScene` plays the last scene (`call_partner_routine 6`, which requests routine 53; in the original, the player's final tick
with `endi.c`). The logic is `EndingEffects`, at 20 frames per second: the player
is drawn with additive blending (`FieldStage::playerAdditive`) and its colors fall to black from frame 1 to 60
(`ENDI_fadeClut`) while one spark per frame comes out over a bone (80 in total); from 60 to 78 it shrinks to 0x999 and
grows to 0x1800 at 80. There sound 1 of routine 52's bank plays, the sparks leave the bones and rise together at
450 units per frame, 20 less each, until 103 (5290 in total). At 104 the sound stops and they disappear;
on the next frame the story goes on (`SOME_SCRIPT_SYNC_BIT`), routine 53 ends the game and EDR plays. The
sparks are drawn like the field's particles (`FieldEffects::setGlows`, `renderFXParticle`), with random color and size
on every frame.

- In the decomp: `ENDI_startParticles` and `ENDI_clearParticles` are `EndingEffects::start`; `ENDI_getParticleBase` and
  `ENDI_setParticleBase`, the rise (`rise_`); `ENDI_tickEndingParticles` is empty in the original (does not apply).
- Assumption: the order of the first frame (startup and the first advance go on different frames) and the numbers
  are taken from the decomp; they remain to be measured in the original (`_work/tmp/measure-queue.md`).
- Difference: the original removes the player's shadow at the start (`PLAYER_SHADOW_ENABLED = 0`); dwe keeps
  drawing it while the player has not completely faded out (`FieldStage::playerFade` greater than 0).

## Death and rebirth

`DeathScene` plays the lost life (`murd.c: MURD_tickScene`), death (`dooa.c: DOOA_tickDissolve`) and rebirth
(`DOOA_tickRebirth`). The map where the tamer wakes up is chosen when the scene starts (`map.c: setDeathMap`) and it is entered
halfway (`changeToDeathMap`); loading the map in the background does not exist here. Without a story, or if the story has no `story_scenes.death`, it starts with the partner's events
and holds the field. In the lost life, at the same time the partner is left alone in black, the lives box appears over it
(`MURD_createLivesBox`, document `ui/lives_box`, model `screens::LivesBox`): it grows from the partner, shows one
heart per life one starts with (`life.lives`), with the one just lost full, blinking and empty, and it is removed when
the fade to white ends (`MURD_removeLivesBox`). If it was the last, the box shrinks back toward the partner
(`MURD_animateLivesBoxOut`) and death goes on when it is gone. With it, the story requests each scene with `call_partner_routine` (9 and 7, lost life; 3, death;
4, rebirth) and `StorySceneGlue` passes it on: the field goes on and the script holds it. Death ends with the partner
already gone, in black. Then the section of the wake-up map runs: the texts, the egg box (`EggBoxScreen`,
document `ui/egg_box`) and routine 4, which gives the partner its new form and plays the rebirth. Left and right switch
the egg and confirm picks one; you cannot leave without choosing. What is missing: the overlay load.

- Same (2026-11-08, a tester's report: "when it dies the egg is chosen at random"): the egg box only appears after
  a natural death (section 0x47 of script 212: it asks which egg you want and opens routine 29). If the partner lost
  all its lives, section 0x49 opens no box: it says that because of the danger the egg did not inherit all the abilities (that
  line only appears in this case), lowers the player's level if it can, runs routine 47 (forget techniques) and is reborn; and
  `DOOA_getSequenceState` rolls the egg (`lives == 0`: `eggSlot = rand() % 4`). dwe does the same
  (`Partner::rebirth`: the box's egg only counts with lives; without them `life.rebirth.baby_choice` decides, at random).
  It is not a bug. Improvement (option, off by default): "Choose the egg after losing all lives"
  (`choose_egg_after_lives_lost`, [Quirks as options](#quirks-as-options)) turns off
  `life.rebirth.random_egg_without_lives`; then routine 4 of section 0x49 first opens the egg box
  (`rebirthOpensEggBox`, `StoryScenes::screenClosed` goes on with the rebirth) and counts the chosen egg. The texts, the
  level drop and the forgotten techniques of that section stay the same. Tests: `story_death_tests.cpp`,
  `life_tests.cpp` ("without the random egg…"), `rule_options_tests.cpp`.
- Fixed (2026-11-08): after a natural death, when the rebirth ended section 0x4DE ran again with the
  new partner and the lost life scene appeared with "survived with what little life it had left". The original
  calls the section once (`tickDeathCondition`); in dwe the `Died` event arrived a frame after requesting it and
  requested it again. Now a death event does not request it if its section already ran (`deathEventAsksForSection`, test
  `story_death_tests.cpp`).

- Same: with the original skin the box grows in 5 frames at 20 per second from the 16-pixel square at the center
  of the screen (`DOO2_openEggBox`: `createAnimatedUIBox(3, …)` from `MAIN_D_80134B98` {-8, -8, 16, 16}) and, on choosing,
  shrinks in another 5 back to it (`removeAnimatedUIBox(3, MAIN_D_80134BA4)`), with the gray outline 0x808080 of
  `renderUIBoxAnim` (`ui/common/original_box.lua`). While it grows no egg is moved or chosen (`DOO2_tickEggInput`
  waits for `DOO2_tickEggBox` to set `sparkleIndex` to 0); on opening the box sound does not play but its own
  (`stopSound`, `playSound(8, 4)`). The story goes on when the box has shrunk (`egg_box_closed`; the original returns 1
  with `UI_BOX_DATA[3].state` 0). With another skin the box opens and closes instantly.
- Same, measured in the original (2026-11-03, state `death_egg_box_open`): with the original skin the box is that of
  `UI_BOX_DATA[3]`, -84, -70, 168 x 80 from the center of the screen (`MAIN_D_80134B90`; x 76 to 244, y 50 to 130 of
  320 x 240), semi-transparent (`features` 2, color 0: `.original-box`), with no title or texts: the question belongs to the
  dialogue below. Inside, the 4 eggs of `ETCDAT/TAMA.TIM` in a row (`DOO2_renderEggIcons`): 32 x 32 centered at
  x -54, -18, 18 and 54 and y -30, the drawing `EGG_ICONS` {0, 2, 3, 1} (u = 96 + 32 × drawing) with the palette of row
  492 + egg, and the 40 x 40 cursor (u 96, v 32, row 496) over the chosen egg
  (`ui/skins/original/egg_box/style.rcss`). Only left and right move the egg (buttons 0x8000 and 0x2000). The
  document is `own-sounds`: its keys do not sound like a menu; only the box sounds the game sends play
  (open 8/4, move 0/2, choose 0/3) and the box's closing (0/1).
- A 4-pixel difference: in the original's capture the box's top edge is at line 46, not at 50
  of the rectangle; dwe puts it at 50.

The effects of the two scenes live in `DeathEffects` (`death_effects.cpp`, with no engine state, with tests) and are
drawn as primitives over the scene (`ScenePrimDrawer`), with the evolution's pieces (`addFlashPrims`,
`addWiredFaces`, `SortedPrim`):

- The iris (`MURD_renderIris`, `DOOA_renderIrisWindow`): the ring of `SPOT256` and four rectangles of its white texel,
  subtracted (`ScenePrimBlend::Subtractive`), close from 200 to 0 in 0x23 frames (0x21 in death) over the
  middle of the partner. When the ring already fits in the partner's box (`MURD_isDigimonLargerThanIris`), the
  partner is drawn on top.
- The death camera (`DOOA_updateCutsceneCamera`, `DOOA_getOrbitPosition`): from 0x23 to 0x55 it turns toward the
  partner from the old map's view; at 0x56 the partner moves to the origin of the wake-up map looking at the player and
  the camera ends at height × 5 + 1200, 400 + 2 × height above, at 0x638 of its turn, with projection distance
  1000; from 0x149 to 0x17B it goes back the same way to the map's view. In that second part the center of the image
  starts from the center of the screen (0xA0, 0x78) and not from the map's offset; on the story's map (0xCD)
  it starts from 0xBC, 28 pixels to the right (`DOOA_tickDissolve` phase 0, `wake_map.story_map_start_x`).
- From 0x69 to 0x8C the body turns to wireframe in waves (`DOOA_SHARD_WAVE_SCHEDULE`); each wave releases as shards the faces
  of the bones from the 2 that fall to it in `WIREFRAME_RNG_TABLE` and that `rand() & 3` leaves (`DOOA_spawnShardWave`,
  `DOOA_spawnBoneShards`); the shards fall to the ground (`DOOA_updateShards`).
- From 0xC0 to 0xD0 the partner squashes and stretches with `DOOA_DISSOLVE_SCALE_CURVE` and, past 300, jumps height + 100.
- From 0xD2 to 0x135, one flash per frame descends from above the head to the ground swaying 10 to each side
  (mode 0, 8 frames, gray 60 + `rand() % 100`); at 0x135 the shards start to blink. Same, measured in the
  original (2026-10-31, state `death_dooa_light_start`): 7 flashes alive at once (progress 1 to 7), depth
  2512 and projection distance 993 on the first, scale 84, 167, 240, 298, 336, 358 (half of 6 to 23 pixels);
  on screen, a 40-pixel halo and a white center of 20 on 320 × 240. dwe gives the same scales and, at 1280 × 720,
  122 and 64 pixels (41 and 21 of the original). Before it came out three times smaller: with the field hidden the new map
  of `changeToDeathMap` was not prepared and its camera stayed at 320 × 240, so the scene's primitives were
  drawn at scale 1 (`MapRenderer::setMap` now keeps the window size).
- From 0x185 to the end (`DOOA_tickDissolve` phase 0x66: `ENTITY_TABLE[2]->isOnMap = 1`) the first character of the wake-up
  map (the old man in his house) is seen again over black, and goes on during the texts, the egg box and
  the rebirth until everything returns at frame 152 (`life.yaml`: `death.host_frame`). Measured in the original
  (2026-11-03): with the box open only the old man (entity 2) is on the map; the player and the others are not.
- In the rebirth the shards rise in a spiral at frame 10 and close over the egg at 100 (`DOOA_setShardState`
  1 and 3). The egg of `TAMA.TMD` (the baby's, 6 pieces) appears in wireframe from frame 1 and fills in from 101 to 131;
  at 132 the shards are removed and until 152 there is one flash per frame over the egg and rays (`DOO2_renderSparkStreak`).
  At 152 the baby appears, hidden, and the map returns from black until 202 (`FieldStage::sceneryDarkness`). From 203 the egg
  breaks at frames 207, 217, 227, 237, 247 and 248 (`DOOA_SPARKLE_BONE_IDS`); each piece sounds (slot 8,
  effect 7) and its faces fly for 41 frames (`DOO2_buildShardSet`, `DOO2_tickShardSet`).
- The scene's sounds stop where the original calls `stopSound` (0x8C, 0xBF, the end and frame 10 of the
  rebirth).
- Same, measured in the original (2026-11-04, states `death_dooa_light_start` and `death_egg_box_open`): on switching to the
  wake-up map, `DOOA_setOtherEntitiesVisible(0)` and `DOOA_hideAllButPartner` leave everyone off the map except
  the partner (the copy `DOOA_SAVED_ENTITY_VISIBILITY` stores 1 in entities 0, 2, 3 and 4 of `room08`); at
  rebirth frame 152 `DOOA_setOtherEntitiesVisible(1)`, entity 2 and `DOOA_showPlayerAndPartner` return the
  player and entities 2 to 4, and the partner hides again under the egg. dwe sets `sceneryHidden` in the
  same step and, in `swap_frame`, `stage_ = {}` with `partnerHidden` (`FieldStage::showsPlayer`, `showsNpcs`,
  `showsPartner`; tests in `field_stage_tests.cpp`).

Assumptions:

- While the partner is drawn over the iris, the rest of the scene is no longer drawn (the original only draws
  the partner again on top): this way nothing closer than its back covers the ring.
- If the story plays the death, the baby and its egg are taken from the already reborn partner when the rebirth starts.
- The random numbers of the effects use their own seed, not the game's.

## The save machine

`SaveMachine` (`save_machine.cpp`) opens the machine in the old man's house when the script calls routine 48
(`StorySceneGlue::takeSaveMachine`; the script goes on when it closes) or with `save_machine("")`. `SaveMachineFlow`
(`save_machine_flow.cpp`) runs the menus of `main_menu.c: tickMainMenu` 0x3C to 0x46 without an interface: registering the
partner in one of the 39 slots (`session::BattleRegistry`), "Do you want to save?", the notice without a slot (it closes
by itself at the original's 0x79 frames) and the one for below Rookie. It saves with `GameSaves`, as when sleeping, and undoes the
registration if the save is not written. If it opens while a care scene holds the player
(`GameSession::savesWait`), the opening waits and the machine appears when the scene ends (a dwe safety rule,
[session](session.md#parity-with-the-original)). The `ui/save_machine` document draws the original's boxes where the original
draws them; the cursor stops at the ends and only sounds when it moves. Detail in
[save-and-load.md, 3.7](../../architecture/save-and-load.md#37-save-machine-and-end-of-the-game).

## Opening of a new game

A new game starts like the original (`main.c: main`, state 0): the old man's questions, the movie and the
first map. `NewGameInterview` ports `newGameScene` (`main.c`), `loadNewGameScene` and `tickNewGameJijimon`
(`new_game.c`) and `newGameStateMachine` (`shop/script_menu.c`), with the ruleset's `new_game_scene` section: the old man
walks in over black and, on turning, asks whether the player has a Digivice, then day or night (if they say yes)
or whether they have friends (if they say no); the first answer gives `species_a` and the second `species_an`. Then it asks for the two names
in the naming box (`NamingBox`, document `ui/naming_box`), with its confirmation, and fades to black. The
result goes in `PlayStart::choice` up to `GameSession::startNewGame`. The sentences are those of table 1238 of
MAPHEAD and are seen in the `ui/story_textbox` document, at the top of the screen as in the original
(`setupNewGameDialogueBox`, setting `story_textbox.top`). In the choices (yes/no, day/night) the first row is the
name just given, in blue, as in the original: empty before naming anyone, the player's after their name and
the partner's after its own, even though line 0x1C serves both (captures cp05-name-right-yesno,
cp06-partner-name-yesno). While the movie, the
interview or a naming box run, `menu` does not open the pause menu (there it skips the movie or takes the cursor to
OK), like the original, which has no pause on those screens. The movie is also skipped with `confirm` or
`cancel` (Enter or Space to confirm, Ctrl or Backspace to cancel): a PC improvement, the original only reads Start; the press that starts it (the last
answer of the interview) does not count. The same naming box serves routine 32 of
the scripts (renaming the player or the partner, `StoryNaming`). It does not run in an autotest, like the movies, unless
the route says `new_game_scene: true`.

Each line of the interview is `showNewGameDialogue` (the sentence in the box and, on closing it, the next state) and
each question `showNewGameSelection` (two options with the cursor on the first; the next state plus the chosen
row): `NewGameInterview::say` and `ask`.

After the "yes" to the player's name it goes straight to "What's your Digimon's name?" (`newGameStateMachine`: state
0x18, line 0x1D); "Good Name." (line 0x1F) only appears after the partner's name (state 0x1C).

Before the interview, the empty slot asks "Record new adventure?" with the cursor on "Yes", like the original
(`SaveDialog::startOnYes`, setting `dialog.start_on_yes`); the other slot questions start on "No".

With the story and `story_scenes.new_game.chain: original`, the field starts at `twna01` exit 9: the map's header
loads script 164 and the cell of that exit runs the opening, which leads to `topn01`, the waking up in the old
man's house, and from there to File City (all within stage 1 of the story lock). `seeded` is still available: the
state of `data/new_game_story` and the start of `field/warps`, without the opening. `--new-game-chain original|seeded`
chooses the chain of this run instead of the ruleset's (`StoryScenes::setNewGameChain`). An autotest, since it leaves out
the questions and the movies, starts in `seeded` (the free field of File City) unless the route says
`new_game: original` (today only `route-stage1`); `--new-game-chain` overrides both.

The naming box is the American one: two halves of 9 rows and 5 columns (capitals; lowercase and digits), with
"Name", Back and OK to its left and the name in a separate box below, with the yellow underline on the next
letter (the original skin places them like the original: `ui/skins/original/naming_box`; the original only says
"Name", not whose name it is, and that line is hidden), and the cursor moves as in `namingSelectionUp`, `namingSelectionDown`, `namingSelectionLeft` and `namingSelectionRight` (up and down
wrap around in each half; to the right of the last column it goes to the other half or to Back; from a button one enters
through the Z row). The directions repeat when held (at 6 frames and then every 2). Confirm writes
the letter (a space cannot be first); with the name full it rewrites the last one and the cursor goes to OK;
cancel deletes the last one, and with the name full deletes two, like the American version; menu goes to OK, `alt`
(the original's circle, which here is the gamepad's north button) to Back, and `extra` (its square, the gamepad's west)
deletes the whole name. On the keyboard `alt` and `extra` are letters of the `WASD` cluster (`E` and `Q`) that when naming
**type** instead of firing the action (`takeTyping` runs before `pressButtons`, `naming_box.cpp:137`), so
those two shortcuts only work on the gamepad; on the keyboard you reach "Back" with the grid's arrows. At the same time you
can type: the letters of the grid and the space are written (another key sounds rejected) and Enter accepts the
name if something has been typed since the cursor last moved. So, with a keyboard, the box's hints
only show Delete and OK: Space ("Write") writes a space and `Q` ("Clear") a "q", and the typing hint
of the name box already says how to write. The box is placed by its own document (`naming_box`, centered
at the top in any window), not the skin: the modern skin's panel is centered with a `#scrim` that this document
does not have. The player's name starts empty, as in the
American version. With the option [long names](#long-names) (on by default) it admits 12 letters instead of
6.

The sounds are the original's (`playSound`, bank 0, `sounds` section): cursor 2, confirm 3 (a page, a
choice, a letter, delete), rejection 0xB (cancelling a choice, because the scene has trigger 49 set; an empty
name; a space first). The scene has no music. The text appears in whole pages, as in the
original, which draws the hidden page one row per frame and shows it at once: there is no letter-by-letter text in
any frame of the game. While it is being drawn, the scene does not take presses.

The partner's own name comes from `PartnerView::nickname` (the game's names): in the dialogues, on the
status page of the partner menu (with the species on its own line, `game_menu.c: drawDigimonStatsStrings`;
the original skin puts it on the row below the name, like the original, with the statuses to its right)
and in the save slots (`nickname`, next to the species, `main_menu.c: drawMainMenuStrings`). Battle, the HUD
and the evolution and death scenes show the species or no name, like the original.

With the mouse (a PC improvement that changes no rules, no option; the original has no pointer): a click outside a
choice turns the page, like confirm. In the old man's choices, a click on an option chooses it and the pointer
passing over it moves the cursor to it without sound; a click outside the options chooses nothing. In the naming box,
a click on a letter writes it, on Back or OK presses them, and on "Name" or on an empty row only moves the cursor; the
pointer moving over the grid carries the cursor with it. The same goes for the story's dialogues
(`docs/modules/game/story.md`, "The pointer in dialogues"). A click the UI keeps (a text box, a
choice, the naming box) is never a walk command: `PlaySession::walksByPointer` only lets the click take the
player when the field moves it, it is free, it is not changing maps and no script or text box runs, and when
control is taken from it (a dialogue, a menu, the gym, battle, a scene, a map change) the destination it
carried is dropped (`Field::dropMoveTarget`): before, a click to turn a page of `species_j` left a destination that the player
followed when closing the dialogue, and the same when finishing a training. Conversely, a click on the ground far from the player
(more than two cells away, `MouseSteering::pressesInPlace`) only takes it there and does not count as confirm on the field:
otherwise, on leaving a confirm cell (the save machine next to the old man's door) each click made
its question again. Near it (on it, the machine or whoever is in front) the click is still confirm and talks or
uses what is there. Routes: `route-mouse-opening.yaml` (the whole opening
with the mouse) and `route-mouse-dialogue.yaml` (`species_j` in the gym, with `--start trai00`).

Differences from the original: on the gamepad, `alt` is on the north button and not on the circle, which here is cancel;
the letters are half width and not the fixed width of the original's font; the battle and arena
records, which in the original name the partner, do not exist yet.

Shutdown during the questions: when an autotest ends the game shuts down by hand and in order (the session, the UI, the
portraits, the renderer and finally the device) before giving the verdict. The questions scene
(`NewGameInterview`, with the old man's models) was not on that list, so a route that failed or ended
during the interview (or the naming box) destroyed the device with its memory still reserved and aborted with the
VMA assertion "Some allocations were not freed" (code 134, no FAIL line). Now it is released before the session.
Checked with routes that fail on purpose: in the interview (before 134, now 1 with its FAIL), on the title, in
battle and with the item menu open (1 with its FAIL, no assertion); on closing the window or with the control channel's `quit`
it already exited cleanly at those points, because locals are destroyed in reverse order.

## Battle opening

Every battle (wild, story or console) starts in `BattleScreen::start` with the `intro` phase: the
original's opening (`battle_setup.c: BTL_handleBattleIntro`), which `screens::BattleIntro` runs at 20 frames per
second, and which is published with `battle.intro.*` ([screens](screens.md#battle-opening)). First, 20 frames of the
anger mark over each enemy (`btl.c: handleBattleStart`, `setFleeBubble`), where `PlaySession` projects it:
the map NPC of that species on that cell, at its height plus how high the mark rises (`renderFleeBubble`). Then
the "BATTLE" banner (sound 0x10), which flies in, comes together and blinks, and its burst (sound 0x11); when the last
piece leaves the screen, the phase moves to `fight`. During the opening the document takes no orders, nor the bag,
nor the finishing-move charge. The pieces' seed is the session's battle count.

Differences from the original: the banner waits 60 frames where the original waits for the tamer to walk to
their place (the field is still in battle) and the flee marks of the other Digimon on the screen do not appear; the
anger mark does not tilt toward its enemy or change size with distance. An enemy with no NPC on the map
(the console's, if there is none of its species) carries no mark.

The HUD menu hint ("Tab Menu") goes with the field's clock and bars (`hud.shown`): it does not appear in battles,
scenes or menus, as in the original.

## Award notice

`AwardScreen` (`award_screen.cpp`) is `tamer.c: checkPendingAwards` and `tamerTickAwardSomething`: while the player
walks free (no script, menu, map change or gym course) it checks the medals (`MedalsAdapter::check`) and,
if a medal or a tamer level is pending, opens the story's dialogue box (`ui/award_notice`) with
the medal's three lines in color 7 ("Congratulations!", `main.tamer_messages.0005` to `0007`) or the level's
(up in color 7, down in 3), `award_sound` (0, 7) plays and the field and the clock wait until confirmed
(Enter / Space; cross on the gamepad), which clears both notices. Measured in the original: the box appears with 999999 bits in
twna01, the HUD hides while it is up and comes back on closing it (the same in dwe). The medals are the triggers 0x16c +
medal that the player page already reads (`StoryAdapter::hasMedal`).

Same in the original skin: the notice's box and the item notice's (`ui/field_item_notice`). Both are the
story's box (the original's cyan border) at x 30 to 292, and 162 to 221 (`tamer.c`, the player's states for the
award and for picking up an item: -130, 42, 262 x 59 from the center), with the fill `UI_BOX_COLORS` 0, black at half
(`createAnimatedUIBox` color 0, feature 2; measured: inside the box the field stays at 0.46 of its light), the 12-pixel
rows from y 165 (`renderAwardSomethingTextbox`, `renderItemPickupTextbox`: `renderString` at y 45 from the center)
and the confirm button of `script_textbox.c: renderUIBox`: 16 x 12 at (239, 43) from the box's corner, three
drawings of SYSTEM_W (u 0x4C + drawing x 16, v 0xB7, palette 12) that change every 4 field frames
(`TEXTBOX_OPEN_TIMER / 4 % 3`; measured in DuckStation: the counter rises 10 in 30 vblanks, 20 per second, and the button's
cross changes height every 12 vblanks): one turn every 0.6 s. The button is the glyph of the device in use
(`set_prompt("confirm", true)`, [docs/architecture/ui.md](../../architecture/ui.md), "Button glyphs"): with a
gamepad of the shapes family, the original's own button (the cross of SYSTEM_W); with the keyboard, the confirm
key (Space) drawn in the same style, 17 x 12, which grows to the left from the button's right edge; with
another gamepad, its button with its letter or its position; all three with the same animation. The story box's one while
it waits (`renderScriptDialogueBox` draws it with the same function and in the same place) is the same. Style:
`ui/skins/original/story_textbox/style.rcss`. The modern skin draws it as a small key or button in the box's
corner that breathes (opacity from 1 to 0.55 in 0.9 s), with the key's name or the button's drawing.

Same: the sickness notice. When the partner falls sick (`partner_impl.c: tickSicknessMechanics`, which puts the
tamer in state 0x14), the same box appears with its name in color 10 and "is Sick!" (`main.partner_impl_is_sick`)
in color 1, `award_sound` plays and it waits for confirmation (Enter / Space). `PlaySession` notes it on seeing `GotSick` in
the partner's events (`noteSickness`); if on the same frame there is a medal or a level, that one appears, as in the
original, where the medal's text overwrites the sickness's. Measured in the original (DuckStation, flag 0x40
set by hand with 11 hours of injury): "AAis Sick!" on the box's first line; in dwe "CODEX is Sick!". A difference
on purpose: the original glues the name to the phrase without a space; dwe puts one. The partner's events (getting sick,
dying, evolving) wait in the session until the game picks them up (`GameSession::takePartnerEvents`): before, they were
deleted at the start of each tick and lost on frames that run more than one tick.

## Free exploration

`free_exploration.cpp` runs [free exploration](../../architecture/script-warps.md#free-exploration):
it is turned on by the `free_exploration` setting (pause menu, saved in the user's settings) or `--explore` (only in
that run). When the field stops on a cell with several places (or on a step-on one closed by the story),
it opens `ui/destination_choice` with the places (`exploration_place_name.N`, `exploration_place_map.N`,
`exploration_places`, `exploration_story`) and passes to the field what is chosen (`choose_destination`) or staying
(`stay_here`, also with cancel). The selector is modal, so `settings` (Escape, F10, gamepad Back) does not open
the options underneath: `PlaySession::pressDestinationChoice` takes it as staying.

## Loop pace

The original runs one logic tick per image and waits three refreshes (`main.c: gameLoop`, `VSync(3)`): 20 ticks per
second. The clock adds one frame per tick and one minute every 20 (`clock.c: tickGameClock`), and animations advance
one frame per tick (`anim.c: tickAnimation`). The game runs logic at a fixed 60 ticks per second
(`core::FixedStep`, cap of 0.25 s per image) and each system counts its 1/20 s frames; what is drawn between
ticks (the characters' clips, the fades, the effects) advances with the time the step took
(`FixedStep::takenSeconds`), with the same cap as the ticks, never with the image's real time.

| Measurement (30 s) | Original (DuckStation) | dwe at 45 fps (vsync) | dwe at 100 fps | dwe at 280-300 fps (no vsync) | Status |
|---|---|---|---|---|---|
| Clock | 20 frames/s, 1.00 min/s | 19.9-20.0 frames/s | 20.0 | 20.0 | Same |
| Logic ticks | 20/s | 60/s (3 per original frame) | 60/s | 60/s | Same |
| Animation | 1 frame every 3 refreshes (20/s) | 1.00 s of clip per s (clips of 20 ticks/s) | 1.00 | 1.00 | Same |
| Player's run | 50 units per tick, 1000/s | ~995/s | same (fixed tick) | same | Same |
| Battle | 20 frames/s | 200 frames in 10 s | 200 in 10 s | 200 in 10 s | Same |
| After a 2 s stall (load, hitch) | logic and animation together | before: clips +2 s, logic +0.25 s; now both +0.25 s | | | Same |

The tests `apps/game/tests/frame_pace_tests.cpp` run the loop at 30, 60, 144 and 240 images per second, with
irregular images (0.5 to 40 ms) and with a 2 s stall, and check the same ticks, the same clock and the same
clip time. Gap: a character's clip is sampled per image, so what the logic reads from it (a clip that
already finished, in the care scenes) can arrive a tick earlier or later depending on the screen's pace.

## Smooth motion

An optional improvement, on by default: the `smooth_motion` setting (row "Smooth motion" of the pause menu's
options, saved in the user's settings) reaches `Field::setSmoothMotion` from `PlaySession`. On, the
characters and the camera are drawn gliding between the original's 1/20 s frames (`Field::drawnPose`, see
`docs/modules/game/field.md`, "Interpolation"); off, each is drawn where its tick leaves it, in frame
jumps like the original. The logic is the same in both cases.

In battle the original moves the fighters and redraws the techniques' effects once per frame (20 per
second, one in every three game ticks): drawn where each frame leaves them, they advance in jumps even if the field
glides. With smooth motion on, `battle_glide.hpp` glides them just like the field, one frame
behind: `PoseGlide` takes each fighter (position and turn by the short way) from where it was drawn to the place
of the new frame in 1/20 s (`FieldScene::holdFighter`, which advances with `settleActors` on each tick), and a jump of more
than 500 units in one frame is drawn at once; `blendEffects` glides the effects between the last two
frames (each sprite's point, each model object's translation, each particle's ends),
pairing each drawing with the one of the same order among those of its technique and its class; if a technique changed how many
it draws of a class, that frame is drawn as is. The shadows, the auras, the rings and the other status effects
that hang from a fighter (`statusAnchor`, `nodePoint`) follow its glided place; the battle's logic
(`BattleEffectBodies`, the flight probes) reads the frame's.

The fighters' animations advance by whole ticks (60 per second, `FieldScene::runFighters`): at 120 images
per second or more the body jumps from pose to pose. With smooth motion, `ClipTween` draws the clip at the time
between the last two ticks according to the loop's alpha, one tick behind like the field: only the time within
the clip that is playing now is blended, so a new clip, or the same one started again, is drawn from its start without
dragging anything from the previous one; a frozen clip stays still. The fighter's player keeps the pose and the time
of the tick, which is what the logic reads (`fighterPose`, `fighterPlayedThrough`). The sparks of hits (the
particles of each spark: spread and color; the dust clouds: height, size and tone) and the status effects (the
poison's bubbles, the objects that spin with confusion, the stun's signal when shrinking and the rings
that fall from it, the disc and the trails of the buffs, the objects' shines) change once per battle frame;
`FieldScene::setStatusVisuals` keeps those of the previous frame and they are drawn between the two, one frame
behind. Each thing is paired with the same one of the previous frame (same slot, same owner, one frame younger);
what has just appeared, ended or jumped is drawn as is, with no trails. The drawings that change image (the
spark's flash, the bubble's frame, the countdown's numbers) change on their frame. The finishing move's aura and the healing
rings stay as in the original (their tables change in object jumps). The fighters of the versus
arena are drawn the same way, with the player's setting (`VersusScreen::setSmoothMotion`). Off, everything is drawn
as before, like the original.

For captures between two ticks, the control channel accepts `pause {"alpha": 0.5}`: with the logic stopped, each image is
drawn that fraction of a tick after the last (see `docs/guides/remote-control.md`).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Fighters between frames | Drawn at each frame's place (20 per second) | `PoseGlide` glides the position and the turn during the frame, one frame behind; jumps of more than 500 units at once | `battle_glide_tests.cpp` ("a fighter glides over the battle's frame…", "a fighter's turn glides the short way…"); captures | Optional improvement (`smooth_motion`, on by default) |
| Techniques' effects between frames | Redrawn on each frame | `blendEffects`: sprites, model objects and particles paired by technique, class and order | `battle_glide_tests.cpp` ("the effects glide between two frames…", "an effect model glides its translation…") | Optional improvement (`smooth_motion`) |
| Fighters' animations between ticks | The body changes pose on each frame | `ClipTween`: the clip at the time between the last two ticks, one tick behind; a new or restarted clip from its start; a scene clip put on top without `follow` (the victory pose) is drawn with the player (`ClipTween::follows`) | `battle_glide_tests.cpp` ("a held fighter's clip is drawn between its last two ticks…", "a fighter's new clip…", "a clip played over a held fighter…"); captures | Optional improvement (`smooth_motion`) |
| Hit sparks and status effects between frames | Redrawn on each frame | Those of the previous frame paired by slot and owner (`sparkBehind`, `cloudBehind`, `betweenFrames`) | `battle_glide_tests.cpp` ("the status visuals' counts…", "a hit spark and a cloud…") | Optional improvement (`smooth_motion`) |
| The battle's logic with and without smooth motion | | Same sequence of ticks: the per-tick hash of the battle and of what it reads from the drawing is the same on and off | `battle_glide_tests.cpp` ("smooth motion changes only the drawing…") | Same |

## Chibi style

An optional improvement and prototype (the original does not have it): it draws a model with a big head, a short torso and
short limbs (`chibi_style.hpp`, `animation::buildChibiRig`, see `docs/modules/engine/animation.md`, "Chibi
style"), so that models that are not from the original game (those of a mod or an imported `.glb`) look like
its own. The original's models are already chibi: their proportions are the reference (`models/chibi_profile.yaml`) and
an external model that already falls within them stays as it is. The `chibi_style` setting has two values:

| Value | What is drawn chibi |
|---|---|
| `off` ("No") | Nothing |
| `external` ("External", default) | The models of a pack that is not the engine's and that are not yet chibi |

An old saved setting with `all` ("All") is read as `external`. It is changed in the "Chibi style" row of the
options (from the title or from the pause menu: Tab, "Options"; gamepad: Select), takes effect at once and is
saved in the user's settings; `--chibi` on the command line also passes the originals through the detector (to
debug; all come out "already chibi"). The origin comes from the pack that wins when resolving the model in the virtual
file system (`isOriginalModel`): a model is from the original if that pack is the engine's and is in the base or remaster layer;
one from the mods or user layer is external. `FieldScene::setChibiMode` applies it to the player, the partner, the field's
NPCs and the battle's fighters, which are the same field actors.

Each external model leaves its decision in the log (info level; the originals in debug): shape, whether it is already chibi or is
changed and with which factors, and each proportion that fell outside its range and how it ends up drawn.

Only the drawing changes: the animations are the same, the feet stay on the ground on every frame and the collisions,
speeds and sizes the logic reads are the original's. The effect points that come from a bone
(`nodePoint`, `bonePoint`) and the faces of the partner and the NPCs follow the chibi silhouette (`drawnJoints`). Each
model computes its shape on loading; a body the detection does not recognize stays as it is, and `models/chibi.yaml`
of the game pack corrects a species (turning it off, its shape, a bone's part or its factors).

`--model-viewer <species>/model --chibi [--viewer-yaw <degrees>] [--viewer-frame <units>]` shows a species
still and walking, original and chibi side by side, from the front (0) or the side (90); `--viewer-frame` fixes the framed
height to compare models at the same scale. The shadows and the
portraits do not follow the chibi silhouette. Isolated tests in `tests/chibi_style_tests.cpp` (a tall humanoid made of boxes in a mod pack is not chibi, it is
drawn within the profile with its feet on the ground, and one from the original does not change).

## Save notice

An optional improvement, on by default (the original does not autosave and says nothing when its save machine writes):
while the game saves, a small `species_an`, full-body and in profile, goes from side to side, walking or running,
next to "Saving" (key `save.saving`) in the bottom right corner. The `save_notice` setting
(row "Save notice" of the pause menu's options, saved in the user's settings) reaches
`SaveIndicator::setEnabled` from `PlaySession`; off, nothing is seen.

- **When.** `GameSession::savesWritten` counts each save the session writes: the autosave, the save
  machine, `save("slot_NN")`, the sleep menu's and the one for a new game's slot. `SaveIndicator` looks at it at the
  end of `PlaySession::update`, when everything of the frame has already been written, and with a new save it starts
  `SaveNotice`: whole for 1.5 s from the write and then it fades in 0.5 s; another save in that time starts it
  again. A save that waits for a care scene to end (`savesWait`) does not show it until it is written.
  The notice never delays the write: it only looks at the counter. An autosave that is skipped because the game has no
  slot and all have another game (`GameSession::autosavesSkipped`, see [Autosave](#autosave)) shows the
  same notice with another text and without the figure: "Not autosaved: every slot holds another adventure. Save from
  the menu." (`save.autosave_no_slot`, setting `save_indicator.text`).
- **Where.** The `ui/save_indicator` document is shown the whole game, transparent between saves (so it is
  always laid out and does not take focus from a menu). While it is seen, `SaveIndicator` takes the rectangles of what
  the other documents draw (`Ui::drawnRects`: texts, boxes and images; not the layers that cover half a window or more,
  like the save machine's veil) and raises it from its corner to the lowest free place with 6dp of margin
  (`liftClearOf`, setting `save_indicator.lift`, in pixels), for example above the happiness and
  discipline bars of the original skin. If no place on its side is free, it is not seen. It is only placed again when
  something falls on it, so as not to follow a sliding window.
- **The figure.** `SaveFigure` loads the model of `species_an` (the species `gabu`) from the user's extracted content
  and its walk (2) and run (4) clips, the partner's; if one is missing it uses the other and, with neither, 0.
  It uses them without root motion. It draws it in profile, whole and a bit taller than the text's capitals, in a
  texture of its own of 288x96 (`runtime/save-figure`) that the document paints as a 72x24dp strip, only on the frames
  in which the notice is seen. `LaneRunner` takes it along a lane across the strip and at each end it turns around in 0.2 s
  to face the camera. Each lap walks or runs at the partner's speed for that gait (452 and 870 per second): almost
  always it alternates, and now and then it repeats (never twice in a row). A hash of the lap number decides it, so
  it is always the same and does not touch the game's randomness. While turning it keeps its legs still; if the new lap changes
  clip, the turn blends the stride it ended on with the start of the new clip, with no jumps. Without the model
  (incomplete content) the `save_indicator.figure` setting is false and the notice shows only the text. In git there is nothing
  of the model or captures.
- **Look.** In the original skin it is one of the original's windows (`.original-box`) with the text in the original
  skin's font at 22dp (twice its 11-pixel cell); in the modern one, a dark pill.

The route `route-save-indicator.yaml` checks it with the console's save, the autosave of a map change, the save
machine and the pause menu open at 1280x720, and at 1920x1080 and 1024x768; each capture passes
`expect_clear: save_indicator` (it covers nothing of another document) and `text_fit`. At the end it turns the option off and checks that
a save does not show it. Tests: `the save notice holds ...`, `the figure trots its lane back and forth ...`, `the
lane's laps mostly alternate ...`, `the indicator goes just above the windows ...`, `the indicator shows for each save
written ...`, `a skipped autosave shows its own notice ...` and `the indicator moves up clear of a window ...`
(`apps/game/tests/save_indicator_tests.cpp`).

## Autosave

An optional improvement, on by default (the original does not autosave). When it autosaves is set by the ruleset (section
`save`); where, by the game: **the same slot as the player's saves**, the one that was loaded or last saved to, so
there are no separate autosave slots and "Continue" always loads the latest. The rules (new game with no slot → first empty one; never over another game; with none empty it is skipped and the notice says so) are in
[save.md](../../architecture/save.md#slots-autosave-and-backups).

| Where it is changed | Keyboard | Gamepad |
|---|---|---|
| In game (modern skin) | Tab, "Options", row "Autosave" (Yes/No, the last) | North button, "Options", "Autosave" |
| In game (original skin) | Tab, "Player", Tab, "Options", "Autosave" | North button, "Player", north button, "Options", "Autosave" |
| On the title | "Autosave" under "Chibi style" (modern); "Options", "AUTOSAVE" (original) | Same |

The row sends the `autosave` command (true or false); `changeAutosave` (`save_indicator.hpp`) publishes the `autosave`
setting and saves it in the user's settings (`autosave: false`), and `PlaySession` passes it to
`GameSession::setAutosaveEnabled`. Off, the game never autosaves (it discards a pending request) and no
notice appears; the player's saves (sleeping, save machine, `save("slot_NN")`) do not change.

In the title's lists (both skins) only the manual slots appear; "Continue" starts with the cursor on the
most recent saved game (`save.slots.newest`) and loads it with no further questions (there is no longer a "latest
autosave" question). The `autosave_NN.yaml` files of earlier versions stay in the list while they are on disk, as
"Old autosave N" ("CONTINUE OLD N" in the original skin, with "Old autosave N" in the box
below): they are loaded and deleted, never written, and on loading them the game continues in the manual slot they name.
Tests: `the autosave option is published ...` (`save_indicator_tests.cpp`), `the pause menu's autosave row ...`
(`graphics_options_tests.cpp`), `the main menu's autosave row ...` (`packages/engine/ui/tests/ui_tests.cpp`), `continue starts
on the newest save ...` (`save_flow_tests.cpp`) and the session's ([session](session.md)).

## Time in digits

An optional improvement, on by default (the original only has the dial clock): under the field HUD's clock, a small
box tells the game time in digits, 24-hour, "08:54". It is the game day's hour and minute, the
same ones that move the hand and the dot of the dial (`hud.clock.hour` and `hud.clock.minute`). The `digital_time` setting
(row "Digital time" of the pause menu's options, saved in the user's settings) arrives as a UI setting from `PlaySession`, and the HUD's script (`ui/hud/view.lua`) uses it to put
the class `digital` on `#field-clock`.

- **When it changes.** The text is only written when the minute changes (or the language): the HUD redraws its state with
  each `hud.revision`, which the game only raises when something changes, and the script keeps the last time written, so a
  frame with no changes builds no text.
- **Where.** It is part of the clock (`#field-time` inside `#field-clock`), so it is seen and hidden with it: the
  stopped field (menus, battles, scenes) removes it as the clock slides out, and it fades with the HUD on
  map changes. In the original skin it is a box 96dp wide with the frame of the original's windows
  (`.original-box`) and a black background, to read over any ground, centered under the dial, with the digits in
  white in the original skin's font at 22dp (double its cell; the bitmap font is only drawn at
  whole multiples and triple does not fit in the box). With non-integer scales (1920x1080 is x1.5) the frame's corners
  do not quite match its lines, as in all the original skin's boxes; in a box this low it shows more. In
  the modern one, which does not draw the dial, it is the big time of its panel, and when off the panel only says the day.

The route `route-digital-time.yaml` takes captures in the morning at 1280x720, 1920x1080 and 1024x768, at night, with the
pause menu and with the option off; each capture with the box passes `expect_clear: hud#field-time` (it covers nothing of the
HUD or another document, see [UI](../engine/ui.md)) and `text_fit`. Tests: `the field clock tells the game time in
digits as HH:MM while the option is on` (`packages/engine/ui/tests/ui_tests.cpp`), `one part of a document is checked against
the rest of it but the panel round it` (`packages/engine/ui/tests/text_fit_tests.cpp`) and the user settings
(`apps/game/tests/user_settings_tests.cpp`).

## Walking with keyboard or mouse

A dwe option (the original only has the gamepad): "Walk with" (`walk_control` in `settings.yaml`: `keys` or
`mouse`; `apps/game/src/walk_control.hpp`) says what moves the player on the field, and never both at once. By
default it is `keys`, like the original: WASD, the arrows and the gamepad move it, and a click on the ground does not take it
anywhere or drag it when held, so a stray click never sets the player walking. With `mouse`, a click
on the ground takes it there and holding drags it, and the keyboard and gamepad directions do not move it. The menus and
the dialogues accept keys and clicks with both (a click is still confirm, and near the player it talks or uses what
is in front). It is in the pause menu's options (row `walk-control`). Changing it drops the destination the
player had (`WalkControlChanger`, `Field::dropMoveTarget`), so it does not keep walking toward an earlier click. An
autotest does not read `settings.yaml`: it uses `mouse` in the mouse routes and `keys` in the others, or whatever the route's
`walk_control` says. Tests: `apps/game/tests/walk_control_tests.cpp` (both modes and the change, which drops the
destination); route `route-walk-control.yaml`.

## Minimap under the windows

A dwe rule (the original has no minimap): the HUD's minimap is never drawn on top of a window, with any
skin, window size and minimap size. Every frame, before laying out the UI, `MinimapCover`
(`apps/game/src/minimap_setting.hpp`) looks at the last layout: if something that another shown document draws
(`Ui::drawnRects`: the pause menu, the partner page, a merchant's or the card shop's window, a
summary box) covers the box of `#minimap`, it publishes `hud.minimap_covered` and the HUD's script puts the class
`covered`, which leaves it in place without drawing it (`visibility: hidden` in `ui/hud/structure.rcss`, common to all
skins). It comes back when nothing has covered it for 0.25 s (`MinimapCover::HoldSeconds`), so that a box
that closes with a transformation, which `drawnRects` does not count, does not uncover it while it shrinks. The windows do not move:
where they do not reach its corner (the modern pause menu at 1280x720 or 1920x1080 with the large minimap) the minimap is
still seen.

The route `route-minimap-clear.yaml` sets the minimap always visible and large and checks with `expect_clear` the pause
menu, the partner page and the minimap with the menu closed at 1280x720, 1920x1080 and 1024x768 (at 1024x768 the modern
menu covers it and the minimap hides); `route-bulk-cards.yaml` and `route-bulk-trade.yaml` (the latter with the large minimap
at 4:3) check the card shop and the trade the same way. Tests: `the minimap hides while a window lies over it
and shows a moment after it leaves` and `the HUD draws no minimap under a window, whatever the window`
(`apps/game/tests/minimap_setting_tests.cpp`).

## Bulk trades

An optional improvement, on by default: the `bulk_trades` setting (row "Bulk trades" of the pause menu's options,
saved in the user's settings) goes from `PlaySession::setBulkTrades` to `TradeScreen::setBulk` and to
`CardShop::setBulkPacks`. On, the item trade and the card shop's packs ask how many at
once; off, they go one at a time like the original. The rules and the equality test are in
[screens.md](screens.md#bulk-trades).

## Evolution requirements

An optional improvement, on by default (the original's evolution chart only draws the lines): in the
"Chart" tab of the player page, on opening a form (Enter) the cursor goes through its evolutions (up and down
arrows; left goes back to the original view with the source forms, right returns to the cursor). A panel on the
left says what the evolution under the cursor requires: the range of care mistakes, the weight's, each minimum stat
(HP and MP in their units, the original compares them divided by 10) or, for a Rookie target, that the partner's highest
stat be one of the preferred ones, and the extra conditions (previous form, discipline, happiness, battles,
techniques). A special evolution (no mistakes or weight) appears as "Special evolution"; a baby's, "Always".

In the partner's own tree each line carries its current value next to it, green if met and red if not, and a mark
(✓ or ✗) per group; below, "3 of 4 ready" and the check time ("Check 17/72 h": the counter and the hours of its
level; the Rookie's fallback with its own). The rule is the original's (`evolution.c: calculateRequirementScore`):
one point per group (care mistakes at least, or at most with the mark 0x10; weight within ±5; stats; any one
extra) and it passes with 3 of 4; among those that pass, the check's choice (the highest score at big baby,
the highest average of the stats asked at Rookie and Champion, with the preference for unraised forms), and a Rookie with no
candidate goes to `species_ar` from 96 hours. The line of the form the check would choose glows green and says "Next check
picks it"; the others dim. If the check would choose another form, it says so ("Next check: …").

A form the player has not yet discovered (the gray cell, no image: it is not among the raised ones,
`player.chart.raised`, the same mark the chart uses) appears as "???" and with nothing of its requirement, no points or
glow, in the partner's tree and in those of the other species; once discovered, it shows everything. If the check would choose
an undiscovered form, it only says "Next check: ???". `ChartRequirements` does not publish the partner's values
for those forms and gives their number as -2 in `evoreq.partner` (`encodeOutlook`, `shownTargets`).

All of it comes from `Evolution::outlook` and `Evolution::report` ([evolution.md](../game/evolution.md)), the same rules that
decide evolution: `ChartRequirements` (`chart_requirements.cpp`), while the pause menu is shown, publishes once
the requirements of each form (`evoreq.req.<species>`, "key:range|…") and the hours per level (`evoreq.hours`), and on
every frame in which something changes the partner's values (`evoreq.partner`, `evoreq.partner.<species>`) and raises
`evoreq.version`; the menu's logic (`pause_menu/logic.lua`) draws the panel and the cursor. The setting
`evolution_requirements` (row "Evolution requirements" of the pause menu's options, saved in the user's
settings) turned off leaves the original chart. The chart only exists in the original skin; the option's row is in
both.

The route `route-evolution-requirements.yaml` opens the partner's chart with the keyboard (in a new game every
target is "???"), moves the cursor, goes through the original view, discovers `species_i` and `species_bb` (triggers 0x200 + their
number), gives it big berries to move the weight (with a `species_bb` it ends with 3 of 4; with three the check chooses
`species_ai`, undiscovered: "Next check: ???") and opens it again at 1280x720, 1920x1080 and 1024x768 with `text_fit` and `expect_clear: pause_menu`; at the end it turns the
option off and on. Tests: `apps/game/tests/chart_requirements_tests.cpp` (ranges, rows and undiscovered forms), `packages/game/evolution/tests/report_tests.cpp`
(the report's points are those of the score in each mix of groups; the form `outlook` gives is the one `checkNatural`
chooses) and `the chart's outlook names the form the next check evolves the partner into`
(`packages/session/tests/evolution_adapter_tests.cpp`).

## Translated signs

An optional improvement, on by default (the American version left many map signs in Japanese): the
signs painted on the backgrounds are written again in the player's language (module [signs](../engine/signs.md)). The
`translated_signs` setting (row "Translated signs" of the pause menu's options,
saved in the user's settings) reaches `FieldScene::setTranslatedSigns` from `PlaySession`; off, the map
shows the original textures.

- **On entering a map.** `FieldScene::enterMap` keeps the map's images as they were read and paints the
  signs of the language before `MapRenderer::setMap`.
- **On changing the language or the option.** `finishTick` compares the key of what was painted (language, catalog revision and
  option) and, if it changed, uploads again the background and the sprites' textures (`MapRenderer::updateImages`) without
  reloading the map.
- **`--dump-signs`** (`sign_dump.cpp`) writes a map's images with a grid for writing overlays
  ([guide](../../guides/signs.md)).

## Status extras

An optional improvement, on by default: the `status_extras` setting (row "Status extras" of the pause menu's options,
saved in the user's settings) arrives as a UI setting from
`PlaySession` (`StatusExtrasSetting`). On, the partner's sheet shows what the original knows and hides:
tiredness, happiness, discipline and virus with a number, care mistakes, battles, hours awake, remaining life and
evolution counter; off, the sheet is the original's. `PartnerMenu` adds to the view the hours from which the partner's
level checks evolution (`EvolutionAdapter`, `Evolution::checkHoursFor`). Where each one goes in each
skin and where each value comes from: [screens.md](screens.md#sheet-extras).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Partner's sheet with more data | The status view (`game_menu.c`) draws name, age, weight, lives, HP, MP, stats, specialties, type, activity pattern, happiness, discipline and virus (without a number); tiredness, mistakes, battles, sleep schedule, remaining life and evolution counter exist in `PartnerPara` but are not shown | With the option: tiredness and the number of the care bars, mistakes and battles, hours awake today, remaining life and evolution counter with the hours of its level's check, in both skins and both languages; everything read from the partner's state and the evolution rules. Off, the original's sheet | `partner_status_view_tests.cpp` ("…tiredness and the care mistakes…", "…battles, the lifetime, the evolution counter and the waking hours"); `session_tests.cpp` ("the partner view carries the lifetime, the counters and the sleep schedule it lives by"); `report_tests.cpp` ("a form's check hours are its level's…"); `user_settings_tests`; route `route-status-extras.yaml`; before/after captures in both skins | Improvement (option) |

## Long names

An optional improvement, on by default (it changes no rule): the `long_names` setting (row "Long names"
of the pause menu's options, saved in the user's settings) arrives as a UI setting from `PlaySession` (`LongNamesSetting`). On, the naming box (the interview's and routine 32's)
admits up to 12 letters (`LongNameLength`, `naming_box.hpp`); off, the original's 6 (`naming.length` of
`rules/original/new_game_scene.yaml`, `initializeNamingBuffer`). Twelve is what fits in the original's name
box (149 pixels, one letter every 12), so the naming box does not change size in any skin. The change
applies from the next box that opens; a name already given stays as it is.

Where a name is drawn and what a long one does:

| Place | What it does |
|---|---|
| Naming box (`ui/naming_box`) | 12 cells; they fit in the name box of both skins |
| Dialogue boxes (`ui/story_textbox`, also the tournament chat) | A row that the name makes wider than the box is narrowed from its left edge (`ui/common/fit_text.lua`); the rows that fit are not touched, so with 6-letter names everything stays like the original |
| Player page (original skin) | With more than 6 letters the name box grows 12 pixels per letter (class `long-player-name`) |
| Partner's sheet | Its box already fits 12 letters (it grows for a longer species name) |
| Save list (`ui/save_slots`) | The line "name (species)" is narrowed if it does not fit |

Measured: test route with the player "WWWWWWWWWWWW" and the partner "Bartholomews" (the route key `names`), and the
naming box in the interview (the key `new_game_scene: true`); before/after captures
in both skins.

## Space after names

An optional improvement, on by default (it changes no rule): the `name_spacing` setting (row "Space after names"
of the options' Game tab, saved in the user's settings),
which arrives as a UI setting from `PlaySession` (`NameSpacingSetting`, `story_text_rows.hpp`).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Inserted name glued to the next word | `script_textbox.c: showTextbox` copies the name as is (codes 5, 6, 7, 8, 9, 0x11 and 0x12; `getSpeakerName`, `DIGIMON_NAME`, `MOVE_NAMES`, `ITEM_NAME`, `BGM_TRACK_NAMES`, `TOURNAMENT_NAMES`) and `drawString2` adds nothing; ten texts of the original carry no space after the name (MAPHEAD 0x5cbe, a technique's name and the next word; 0x5b94, 0x4ac4 after `{partner}`; five of the cards and two of script 132 after a species name). Measured: the script bytes in RAM (`MAPHEAD_SCRIPT_PTR` + 0x5cbe) are `08 F3` and then the first letter of the word, with no space; with `SCRIPT_POINTER` taken to that text in an open box (state `save_machine_intro`) the game draws the name and the word together | Each name that fills a tag (`StoryTextbox::pageText`, `TournamentScheduleScreen`) ends in the `NameEnd` mark; `appendPageRows` removes it and, with the option, puts a space if what follows (after the color tags) is a letter or a digit; before a sign, a space, a line break or the end of the page it puts nothing. Without the option, the text is glued as in the original | "a name the text runs into the next word gets a space with name spacing" (`story_text_rows_tests.cpp`) | Improvement (option) |

## Description in shops

Like the original: in a shop's list, Pause (P, or Start on the gamepad) on an item opens its description in a
one-line box under the list, with the look of the money box (`ui/shop_menu`, `description` phase of
`ShopScreen`); accept, cancel or Pause close it and return to the list with the same item. While the shop is
on the list or on the description, Pause does not open the pause menu (`ShopScreen::holdsPause`); the UI delivers Pause and the menu
button (Tab, the gamepad's top button) to the document as the same `menu` action, so `ShopScreen::press` tells them
apart: the menu button still goes back as before.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Item description in the shop | `tickItemMenu`: Start on a row calls `createItemMenuDescriptionBox` (a 264 × 22 box next to the row); `tickItemMenuDescriptionBox` closes it with accept, cancel or Start | `ShopScreen` moves to the `description` phase with the cursor's row (`shop_cursor`) and publishes `shop.described` and each row's description; the document shows the box in the bag's box's place | Route `route-shop-description.yaml` (in both skins: Pause opens and accept closes the box, which does not cover the list) | Same as the original |

## Shop extras

An optional improvement, off by default: the `shop_extras` setting (row "Shop extras" of the options'
Interface tab, saved in the user's settings). The documents read it as a UI
setting; `Game` changes it on the title and in game (`changeUiToggle`, `title_options.cpp`). On, the shop
shows under the list the description of the focused row, how many the player already has of each item on sale
(`ShopCounter::owned`, the sum of the bag's cells), and in the quantity box how many they have and the money they
will have left after the purchase or sale (`shop.amount.owned`, `shop.amount.money_after`). The money was already always in
view. Off, the original's shop. It covers item shops and recycling; the card and
merit shops do not. They are checked by the route `route-shop-description.yaml` (buys one and the row goes to `x1`) and
`bag_tests.cpp` ("the total of an item counts every stack…", `Inventory::totalOf`).

## Favorite food

Each species has a favorite food (`favoriteFood` in the species table). The original uses it when eating and when
refusing items, but does not show it anywhere: you have to guess it. dwe ports its effect and, with the optional
improvement `favorite_food` (row "Show favourite food", Interface tab,
**on by default**, saved in the user's settings), shows it:

- On the partner's sheet (both skins), a "Favourite food" row with the name from the item
  table (`partner.favorite_food`). Newborn species have item 0 as favorite, which is not a
  food: the row says "None". `InventoryAdapter::favoriteFood` only gives the favorite if it is a food
  (`item_use.meals`). In the original skin it goes in the free end of the "Profile" label's row (the profile rows
  are already full); in the modern one, under the weight, with the profile rows at 24dp so the conditions still
  end before the cards.
- In every list where an item is given or used (the field bag, the battle bag and the shop list), the
  favorite appears in gold and with a mark that does not depend on color: in the original skin the sheet's heart next
  to the icon, in the modern one a gold dot; in the shop, a gold border to the left of the row
  (`bag.slot.<i>.favorite`, `shop.row.<n>.favorite`).
- It follows the current partner: the partner menu publishes its settings every frame, so after an
  evolution the sheet and the bag already mark the new species' favorite.

Off, the sheet and the lists look like the original.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Eating the favorite: energy ×14/10, happiness +2 | `item.c: handleFood` | `applyFood` | `hunger_tests` | Same |
| When hungry, eating enough or the favorite makes the partner celebrate (clip 0xb) | `partner_impl.c: partnerHandleFoodFeed` (`startAnimation` 0xb) | `answerHunger` emits `EventKind::Cheered`; `FeedScene::cheer` plays the `partner_cheer` clip of `field_items.yaml` once (`FieldScene::playPartnerClipOnce`); not in battle | `hunger_tests`: "a hungry partner cheers when it eats enough or its favourite food" | Same |
| When not hungry, it refuses the favorite 20 % of the time; the mark lasts a single offer | `partner_impl.c: partnerWillRefuseItem` (clears `refusedFavFood` at the start) | `OriginalItemRefusal::refuses` | `hunger_tests`: "a partner that is not hungry refuses its favourite food 20 % of the times" | Same |
| An item with a discipline check that passes the roll still goes to the favorite's check | `partner_impl.c: partnerWillRefuseItem` | `OriginalItemRefusal::refuses` | `hunger_tests`: "an item past the discipline roll is still refused as an unwanted favourite" | Same |
| Favorite food row on the sheet | Does not exist | `screens::setFavoriteFood`, `#status-food-line` | `partner_status_view_tests`, `ui_tests`: "the status page names the partner's favourite food…"; route `route-favorite-food.yaml` (in both skins: the row, the text fits, it follows evolution and is hidden with the option off) | Improvement (option) |
| Favorite marked in the bag and the shop | Does not exist | `BagSlotView::favorite`, `ShopScreen::settings`, `bag_slot.lua`, `shop_menu/logic.lua` | `bag_view_tests`, `ui_tests`: "the bag marks the partner's favourite food…"; route `route-favorite-food.yaml` | Improvement (option) |

## Art fixes

An optional improvement, off by default: the `art_fixes` setting (row "Art fixes", Interface
tab). It gathers the drawings that the original takes from a wrong area of its texture; the original shows them wrong
just the same, so off they look as in it.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Happiness label on the partner's sheet (original skin) | The status view draws the label with columns that are not the word's (it comes out cut, the same in the original) | With the option, the `fixed` class of `#status-happiness-name` uses the sprite `status-happiness-name-fixed` (columns 214–254, rows 60–67, measured on the texture) and ends where the bar starts; without it, the original's drawing | Route `route-art-fixes.yaml` (with `--lang en --ui-skin original`, where the sheet uses the label of the texture) | Improvement (option) |

## Developer tools

The `dev_tools` setting (row "Developer tools" of the Interface tab) turns on the console
(F1 or `), the development menu (F2) and the map layer (F3). Off, those keys do nothing and, if the console or the
menu were open, they are closed. Its default value depends on the build (`dev_tools.hpp`): off in
packages (`DWE_PACKAGE`) and on in development builds; `--tuning` always turns them on.
"Reset values" leaves it off.

The development menu (`ui/dev_menu`, `DevMenuScreen`) is a modal menu over the game that only writes console lines:
giving items (1, 10 or 99), setting money, the clock's speed, stats, care
(happiness, discipline, tiredness, mistakes, sleeping, toilet), making evolve, learning every technique, traveling to an
already visited map and saving (with the pause menu's `quick_save`). The lists (items, species, maps) come from the player's tables with their name keys
(`dev_menu.<list>.count`, `.N.id`, `.N.name`). The evolve one is that of all species with a model
(`EvolutionAdapter::cheatTargets`, `EvolutionTables::cheatTargets`), in the table's order: the partner forms,
the special ones (virus, by item or event, the Megas a ruleset adds) and also the enemies, the bosses and
the characters, even if no evolution in the game reaches them. With the original's data there are 178 (all but the two
with no model). A species with no model is left out even with an evolution row of its own, and `evolve` refuses
it: WereGarurumon, a form cut from the disc, keeps its row but has no bones, no model and a placeholder move list (16
times Fire Tower: no finisher and nothing it learns); evolving into it left the status saying WereGarurumon while the field
kept drawing the previous form. The menu and `evolve` use `evolveByCheat`, which is only reached from the development tools; the
game's evolutions do not change. What a species with no row lacks is taken from a stand-in
(`assets::standInRow`): the first species with a row of the same name (the enemy copy of a partner form),
then of the same level and type, then of the same level and if not that of a Rookie. This is how these come out:

- The evolution's reference stats (`EvolutionTables::cheatReference`): those of the stand-in, loading the
  requested species.
- The attacks: those of its row in the species table. Enemy copies have none in the table (they fight with
  those of their map): these learn, equip and fight with the stand-in's list (`assets::listsMoves`,
  `BattleAdapter::partnerRow`), without changing the table the enemies read. A form whose first specialty does not
  teach any of its attacks (evolution would give it no technique) learns and equips the first of its list
  (`Evolution::evolveByCheat`).
- The care (`game/partner`, `SpeciesTable::addBorrowed`: hunger, sleep, toilet, weight, favorite food) and the placement
  of the status portrait (`PlaySession::raiseTable`): those of the stand-in's care row.
- The animations: the partner's model is no longer rejected if it lacks clips (`MissingClips::StandIn`,
  `partner_clip_fallbacks.hpp`). A clip of `models/partner.yaml` that the model lacks is replaced by the walk one
  (the movement ones) or by the rest one (the others), and a scene clip that is missing (eating, sleeping, evolution) plays the
  rest one and counts as finished. In battle, a missing clip leaves the one that was playing. Before that, a
  species with a recipe in `animations/partner_clips.yaml` (the character models) receives all its partner and
  battle clips ([partner clips](../engine/animation.md#partner-clips)), so it lacks none.

`Game` only runs a `dev_menu_run` line if the tools
are on and the command is one the menu offers (`devMenuLineAllowed`). For this the console gained:

- `warp("<map> [exit]")`: changes map like a script (`startScriptedMapChange`), with exit 0 if none is given.
- `partner_set("happiness <n>")` and `partner_set("tiredness <n>")`, from 0 to 100.
- `learn_all("")` (the menu's Techniques entry, which then says it is done): the partner learns every technique its
  current species can learn (`BattleAdapter::learnAllMoves`, `session::learnableMoves`): the moves of its species'
  list that the catalog marks learnable (below 0x3A, as `battleMoveLearning` and `TRN_tryLearnMove` teach them), so
  never its finisher nor a technique another species animates. Each is learned as `learnMove` teaches it (`dynamite_kick`
  brings its pair), into the same learned list, so they show in the technique list, can be equipped, are saved
  and stay through evolutions like any learned one. Running it again learns nothing new; there is no undo. With the
  original's data Agumon ends with 7 and Gabumon with 8 learned.

Tests: `dev_tools_tests.cpp` (which lines the menu accepts and how it publishes its lists), `tables_tests.cpp` and
`evolution_adapter_tests.cpp` (the evolve list has all species with a model and each one evolves),
`game_table_tests.cpp` (the order of the stand-ins), `species_tests.cpp` (the borrowed care),
`partner_clip_fallbacks_tests.cpp` (the stand-in clips), `battle_adapter_tests.cpp` (the borrowed attacks; `learn_all`:
the learnable set, once, equipped and saved, and the two starting partners on extracted content),
`dev_menu_tests.cpp` (the Techniques entry in every skin and language), `partner_set_command_tests.cpp`
and the route `route-dev-menu.yaml` (F2, money, an item, and with the option off F2 opens nothing).

## Recruit bars

An optional improvement, on by default (it changes no rule; the original does not show how much the player has left): while
a job or a game decides whether someone joins the city, a bar shows how much is missing. A single option
turns them all on or off: the `shop_job_bar` setting (it keeps the key it came in with; row "Recruit bar"
of the pause menu's options, saved in the user's settings), which arrives as a UI setting
from `PlaySession` (`RecruitBarSetting`, `recruit_bar.hpp`).

- **The cannon shop**: while the job lasts (gcan11, script 49 and the customers of scripts 199 to 202), a bar
  at the top center shows what was earned against the tally with which `species_ai` joins the city, with a mark where
  the better pay starts, and the customers served ("Customers 7/20").
- **The stones game**: during the match against the first rival (frzl17), two lines at the bottom, under the dialogue
  box and without covering the original's HUD: each one's points ("You 3 - 1 `species_l`"), the throws they have left,
  how it stands ("You're ahead: `species_l` joins the city", "You're behind by N", "Tie") and "Perfect score:
  medal" while it can be reached.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| The job's bar in the shop | Does not exist: the ending (section 0x52) pays by variable 243, the high byte of the tally that routine 56 leaves: up to 6, 500 bits; from 7 to 11, 1500; from 12, joins (2000 bits and trigger 247) | `ShopJobBar` (`shop_job_bar.hpp`) reads the rule `story_scenes.shop_job` (map, variable 123 of customers, 20 customers, `better_pay_from: 7`, `joins_from: 12`) and the tally (`StoryAdapter::shopTally`); the bar goes from 0 to 3072 (12 × 256, `ShopJobRules::tallyFrom`) with the mark at 1792 (7 × 256). It appears from the first customer (variable 123 rises on the shop's map) until the player leaves the map (the ending's jump); a tally left from an earlier job is not shown until the next one's first customer. It publishes `shop_job.*` and raises `shop_job.revision`; the HUD (`ui/hud/view.lua`, `#shop-job`) draws the fill, the mark and the texts (the customers, which do not go past 20 even if some customer counts twice when leaving, and the next mark on the right), with the classes `better` and `joins` on passing each mark, in both skins | `shop_job_bar_tests.cpp` ("the shop job's bar shows from the first customer…", "a count left over…", "…hidden with the option off…"); `the game pack's shop job joins from a tally of 3072…` (`story_props_tests.cpp`); mid-game capture in both skins | Improvement (option) |
| Improved shop job: counter, rules and bar | The original picks the price in menus ("Raise" rolls the markup at random) and the bar does not exist | `ShopHaggleScreen` (`shop_haggle_screen.hpp`) takes the `ShopHaggle` screen from routine 259: it publishes `shop_haggle.*` (`shopHaggleSettings`: customer, item, the 9 rows with what they add, the chance and the most likely face, the offers and the tally), shows `ui/shop_haggle` and with `shop_haggle_offer`, `shop_haggle_talk` or `shop_haggle_dismiss` plays the turn (`ShopJobAdapter::play`), removes the screen and the story goes on. `ShopJobRulesPanel` (`shop_job_rules_panel.hpp`) shows `ui/shop_job_rules` with `shop_job_rules` (the bar's **?** or the counter's menu button) and only at the first customer of each job; `shop_job_rules_close` closes it. In the improved mode the bar runs from 0 to the line's perfect score (`shop_job.top`) with the bonus mark and its icon (`shop_job.bonus`), and `shop_job.tier` gives the fill's class (`tier-lowest`…`tier-bonus`); in the original it runs from 0 to the goal. Option `shop_job_mode` (Game tab) | `shop_job_bar_tests.cpp` ("the improved job's bar runs to the line's perfect score…", "the original job's bar keeps its goal…"), `shop_haggle_screen_tests.cpp`, `ui_tests.cpp` ("the improved shop job's bar runs to the perfect score with the bonus mark") | Improvement (option) |
| The stones game's bar | Does not exist: script 206 (section 0x0a, on returning to frzl15) reads the result `KAR_finishMatch` leaves in variable 121: 0 loses or ties (no joining), 1 wins, 2 wins with 10 points or more (also the medal, trigger 372); with 1 or 2 `species_l` joins the city (trigger 257, prosperity +1). Only the first rival joins | `curlingRecruitOf` (`curling_recruit_bar.hpp`) reads the match (`Match::teams`: points and throws made); the perfect score stays within reach while the player's stones still in play (on the rack, in hand or on the ice) can add the 10 in the ring that is worth most (2), that is, while none has left. `CurlingScreen::showRecruitBar` publishes `curling_recruit.*` and raises `curling_recruit.revision`; the `curling_hud` document (`#recruit`) draws it at y 222 to 240 of the original, in both skins (the original one with its box). It appears only in the first rival's match and is removed when it ends | `curling_recruit_bar_tests.cpp` ("…shows only during a match, and not with the option off", "…follows the score and the throws left", "the perfect score shows while…"); mid-game captures in both skins | Improvement (option) |

## Bubble with several needs

An optional improvement, on by default (it changes no rule): the `need_bubble_stack` setting (row "Several needs"
of the options; saved in the user's settings) arrives as a UI setting from
`PlaySession` (`NeedBubbleStackSetting`) and the HUD gathers up to five needs in one bubble, the one that
hurts first coming first (`hud.need.<i>.urgency`, from `game::partner::needDeadlinesOf`;
[ui.md](../../architecture/ui.md#field-status)).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Bubble with two or more needs | `partner_impl.c: handleConditionBubble`: one at a time, by priority (sleep, tiredness, hunger, toilet, wound, sickness); at 50 frames it switches to the highest of the rest, so the two most urgent alternate and a third does not appear | Up to five in a row of a bubble widened with the original's pieces (50, 68, 86 or 104 pixels), ordered by the time left until each does lasting damage (toilet, hunger, sleep, wound, sickness; tiredness, with no deadline, last; ties by the original's priority), the most urgent on the left, and tucked into the window near an edge; with only one, the original's; off, the original's whole | `need_deadlines_tests.cpp`; `field_hud_view_tests.cpp`; `ui_tests`: "the need bubble shows several needs side by side ..."; `hud_fit_tests`; `user_settings_tests`; `in_game_options_tests`; captures with 1, 3 and 5 needs at 100 % and at 130 % | Improvement (option) |
| Bubble frames: it grows in 10, each need animates by the bubble's counter (the three "Z"s of sleep, the drop that falls for tiredness, the frames of food, toilet, wound and sickness), bursts at 51 and is not seen until 60 | `bubble.c: tickConditionBubble`, `renderConditionBubble` (`CONDITION_ICON_SLEEPY`, `CONDITION_TIRED_FRAME_U`/`_MY`, `CONDITION_HUNGRY_FRAMES`); `partner_impl.c: handleConditionBubble` (`CONDITION_BUBBLE_TIMER`). Measured in DuckStation: 180 vblanks per bubble, 150 on switching need; "Z" on frames 12, 17, 23 and 28 | `ui/hud/presenter.lua` counts the frames and `view.lua` publishes `frame-<n>`; the original skin draws it (`style.rcss`, `skin.yaml`). Before, only the first frame of each need appeared and the "Z" went down | `ui_tests`: "the need bubble plays the original's frames ..." | Same (without the 12-unit sway) |

## Round minimap

An optional improvement, on by default with both skins (it changes no rule; the original has no
minimap): the `minimap` setting (`on` or `off`; the `auto` of old settings is read as `on`) is changed in the row
"Show minimap" of the in-game options, in the "Minimap" page of the pause menu and
in the options from the title; it applies at once, without reloading the map, and is saved in the user's settings
(`MinimapChanger`, `apps/game/src/minimap_setting.hpp`). How it is drawn: [ui.md](../../architecture/ui.md#minimap).

`PlaySession` publishes `hud.minimap_allowed` (`minimapAllowed`): the minimap only appears while the field's time runs
(no menu, scene, chat, fade or map change, like the original's clock and bars:
`clock.c: renderGameClock`) and never in a battle, a training, fishing, the stones, the tournament screens,
an evolution, a death or the ending. It goes at the top right, away from the clock, the partner's bars and the menu
hint, and hides under any window (above, "Minimap under the windows").

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Round minimap on the HUD | Does not exist | A disc of the map around the player, with north (−Z) up, the map name above and the day and time below; the player at the center with their orientation, the partner where it is (on the edge if it is outside); with both skins and the same layout: the modern one with a glowing ring and pills; the original with the name and the time in its windows (cyan border with corners, black background and its font), the four-stripe ring of its borders and the N and the player in its yellow; a long name is split into centered lines. It is seen with `on` and outside battles, scenes and minigames | `minimap_setting_tests.cpp` ("the minimap shows only while the field runs…", "the options page's minimap row hides the HUD's minimap at once and saves the choice", modes and saving); `in_game_options_tests.cpp` ("the options page shows and hides the minimap with either look"); `ui_tests.cpp` ("the HUD shows the minimap unless it is off…", "…round the player, north-up"); `minimap_tests.cpp` and `minimap_disc_tests.cpp` (calculation of the disc and the ring); route `route-minimap-clear.yaml` | Improvement (option) |

## Enemies on the minimap

An optional improvement, on by default (it changes no rule; the original has no minimap): the
`minimap_enemies` setting (`true` or `false`) is changed in the row "Enemies on minimap"
of the in-game options, right below "Show minimap"; it applies at once and is saved in the user's
settings (key `minimap_enemies`). Off, the minimap stays as before: only the player and the partner.

What an enemy is: the original marks no character on the map as wild. A character fights because its
section of the map's script (the one with its script id that runs when touched or spoken to) reaches the command to start a
battle, which fights whoever was spoken to. `fightingSections` (`apps/game/src/minimap_enemies.hpp`) walks the listing
of the active script (`script/dg/<n>`, the one `StoryRunner::mapScript()` gives) from the start of each section following
jumps, conditions, choices, `switch` and calls, and the section fights if it reaches `start_battle`. The following
mode will not do: many wild ones only turn toward the player and some that chase only talk (across all
maps: mode 2, 102 that fight and 73 that do not; mode 12, 352 and 89). `MinimapEnemyFinder` reads the listing once per script.
`PlaySession::minimapEnemyMarkers` puts a dot for each character of the map that is on the field
(`NpcSet::positionOf`) and whose section fights, once per script id; on each frame it reads where it is, so the dot
follows the character and disappears when it is unloaded, leaves the map or falls in battle (battle removes it from the field).

Decisions: a story boss or a recruitable whose section can start a battle also carries a dot (the
dot says that touching or speaking to it can end in a fight); friends, the partner and characters that only talk,
do not. An enemy outside the disc's view is not painted (it is not pinned to the edge like the partner). Without a map script
(the story does not run) nothing is marked. A jump to another script (`jump_to_file`) is not followed. How it is drawn:
[ui.md](../../architecture/ui.md#minimap).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Enemies on the minimap | Does not exist | An orange-red dot with a dark border for each character on the map that can fight, where it is, under the player's arrow; with both skins. Measured in dwe: on the second map of the fog the three wild ones appear and move with them, one leaves the view; in the tunnel (two that chase but only talk) and in the town, none. Off, none | `minimap_enemies_tests.cpp` (sections that fight, friends out, live positions, off, listing reader); `ui_tests.cpp` ("the minimap marks the enemies in view round the player, under the player's arrow"); `minimap_tests.cpp` (the dot); `in_game_options_tests.cpp` ("the options page turns the enemies on the minimap on and off…"); `user_settings_tests`; route `route-minimap-enemies.yaml` | Improvement (option) |

## Items on the minimap

An optional improvement, on by default (it changes no rule; the original has no minimap): the setting
`minimap_items` (`true` or `false`) is changed in the row "Items on minimap" of the
Game tab of the in-game options, under "Island map" (the Interface tab, next to "Enemies on
minimap", already fills the page at 1280x720); it applies at once and is
saved in the user's settings (key `minimap_items`). Off, the minimap stays without them.

What an item is: one of those lying on the map's ground that is picked up on walking over it (`item.c:
spawnItem`, which the map's scripts place, and `spawnDroppedItems`, what a defeated enemy drops or what the
player throws away), the list `FieldItems::ground()` of the map the player is on. `PlaySession::minimapItemMarkers` puts a mark
for each one the field draws (`minimapItems`, `apps/game/src/minimap_items.hpp`: those that have an original icon,
like `FieldItemsScreen::show`), where it is; on each frame it is read again, so the mark goes away on picking it up and
those of a new map appear when its script places them. Chests carry no mark: they are part of the map's scenery and are
seen on it. The mark is a light green diamond with a dark border (`generated:minimap_item`, `buildMinimapDiamond`), of another
shape and another color than the enemies' dot so they are not confused, on a layer under the enemies'; one
outside the disc's view is not painted. How it is drawn: [ui.md](../../architecture/ui.md#minimap).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Items on the minimap | Does not exist | A light green diamond with a dark border for each item lying on the ground, where it is, under the enemies' dots and the player's arrow; with both skins. It goes away on picking it up. Off, none | `minimap_items_tests.cpp` (those drawn, empty slots out, off); `ui_tests.cpp` ("the minimap marks the items on the ground in view, under the enemies' dots"); `minimap_tests.cpp` (the diamond); `in_game_options_tests.cpp` ("the options page turns the items on the minimap on and off…"); `user_settings_tests` | Improvement (option) |

## Island map

An optional improvement, on by default (it changes no rule; the original has no island map): the M key
(L1 on the gamepad, the `world_map` action of `defaultActionMap`) opens a full-screen map of the whole island, where
the player walks free (no menu, script, fade or open window, like the pause menu). M or Ctrl (cancel) close it. While it is open the game stops (`PlaySession::paused`). The `world_map` setting (row "Island
map" of the options; saved in the user's settings) turns the key off; off, M still
closes an open map.

- **The drawing**: the original brings no image of the island, so the engine paints it on opening
  (`ui::paintWorldMap`, `dwe/ui/world_map.hpp`) from `ui/world_map/layout.yaml` of the game pack (schema
  `world_map_layout`): each area with its center, its radius and its color on a 1000 × 640 canvas, and the paths between
  areas. The land is the smooth union of the areas with a wavy, inked coast; each land point belongs to the nearest
  area (dotted borders), with shallows, waves in the sea and dashed paths. Unvisited areas are painted
  faded toward the parchment color. The image is registered as `generated:world_map/<n>` (`Ui::setGeneratedImage`)
  and only repainted if the known areas change.
- **The areas**: an area groups the names the original shows on entering a map (`MapEntry::loadingName`, the
  text `main.map_names.<n>`), so their names come out translated and follow a language change. Their place on the canvas
  is dwe's, by hand, consistent with the exits between maps.
- **Where the player is**: the area of their map, with a marker that pulses over its name, and above "You are in: place
  (area)".
- **Where it can go**: the places the exits of its map lead to: the walk jumps whose trigger cell
  (110 + exit) the map has (`checkMapInteraction`) and the map changes of its scripts on stepping or
  confirming (`field/script_warps`), without repeats and without its own place. They appear on a line under the map and their areas
  highlighted.
- **Visited**: the save section `map_visits` (`session::MapVisits`) keeps the maps the player has stepped on. An
  earlier save without the section loads with no visits: only the area where it is is known.
- **Skins**: the modern one, with its frame and its colors; the original, with its box (`.original-box`) and its font at 22dp in
  the title, the place, the exits and the legend. The area names use the small modern font in both
  skins, because the original's at 22dp would cover the island.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Island map (M) | Does not exist | `WorldMapScreen` (`world_map_screen.hpp`) publishes `world_map.*` and raises `world_map.revision`; the `world_map` document draws it. The pure logic (areas, known, marker, exits, key) is in `world_map.hpp` | `world_map_tests.cpp` of `apps/game` ("a map lies in the area…", "the player knows the areas…", "the marker stands on the centre…", "the exits are the walk warps…", "M opens the island map…", "the game pack's island layout…"); `world_map_tests.cpp` of `packages/engine/ui` (the painter); `map_visits_tests.cpp` (new and old save); `in_game_options_tests`; captures in both skins and two places with `text_fit` | Improvement (option) |

## HD scenery

An optional improvement, on by default (it changes no rule): the engine draws the field's prerendered background and
its depth layers with its own edge-following filter, at the window's resolution, instead of enlarging
each pixel of the original. The `hd_scenery` setting (row "HD scenery" of the
options, in game or from the title; saved in the user's
settings) applies at once; off, the original's exact pixels are seen. The why and how it works are in
[High definition in the engine](../../architecture/high-definition.md#high-definition-in-the-engine).

- **Where**: `hd_scenery_setting.hpp` (`startHdScenery`, `HdSceneryChanger`) reads and saves the setting; `PlaySession` passes
  it to the scene at the start and `Game` on changing it (`FieldScene::setHdScenery` → `render::MapRenderer::setHdScenery`).
  The console and the routes change it with `hd_scenery("on")` / `hd_scenery("off")`. Every frame, `Game` puts the scene's
  background pass (`FieldScene::hdSceneryScene`, nothing with the scenery hidden) in `FrameDesc::textureScenes`.
- **Collisions**: the option only changes how the map is sampled on the GPU; the field (`packages/game/field`) does not see it.
- **Options**: it is an entry of `ui/common/game_options.lua` in the "Graphics" tab (`tab = "graphics"`) of the
  [in-game options](#in-game-options).
- **Sharpness**: three more settings, also at their best by default, decide how much definition the image has
  (`graphics_options.hpp`; the why in [Sharpness](../../architecture/high-definition.md#sharpness)):
  `scenery_quality` (`upscaled`, the background enlarged once per map with the engine's upscaler and kept in the cache
  of `<user data>/cache/upscale`; `smooth`, the usual filter), `antialiasing` (`true`: the scene with 4
  samples per pixel, model outlines without jaggies) and `texture_filter` (`upscaled`, the models' textures
  enlarged on loading and filtered with mipmaps; `smooth`, only filtered; `nearest`, the original's texels).
  They are saved in the user's settings, `--graphics scenery=smooth|upscaled,aa=on|off,textures=nearest|smooth|upscaled`
  changes them for a game and the console and the routes with `scenery_quality("smooth")`, `antialiasing("off")` and
  `texture_filter("nearest")`. The "Graphics" tab has its rows, after "HD scenery":
  "Scenery quality" (Upscaled / Smooth, always "Improvement"), "Antialiasing" (Yes / No) and "Model
  textures" (Upscaled / Smooth / Pixels, "Like the original" on Pixels). `texture_filter` applies to all models:
  the field and battle (the fighters and the techniques' models belong to the field's `ModelRenderer`) and the
  3D figures in the menus (the partner and the player on the sheet, the save figure, the medal and the new
  game interview), which also share the upscaler's cache, and also the movies (with "Upscaled", the
  movie upscaling below).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| HD scenery | Does not exist: the background and the layers are seen at their resolution, enlarged pixel by pixel | `map.slang` (`sampleSmooth`, modes `SamplingSmooth` and `SamplingCutout`) filters the colors with the time's palette; `MapRenderer::resolveBackgroundColors` turns the background into colors when the palette changes; `render::MapCutouts` finds the layers that are cut-outs of the background so they take their color from the filtered background, with no seams | `hd_scenery_tests.cpp` ("HD scenery is on unless the player turned it off…", "HD scenery changes no collision, trigger, map change or camera of a walk"); `map_cutouts_tests.cpp` of `packages/engine/render`; before and after captures at 1280 × 720 and 1920 × 1080 in `twna01`, `trop01`, `leom01` and `frzl01`, by day and by night | Improvement (option) |
| Upscaled scenery (`scenery_quality`) | Does not exist | `render::upscaleScenery` doubles the background in each palette variant (its own edge-directed interpolation, blended dithering, soft adaptive sharpening) on a thread, with a disk cache (`render::UpscaleCache`); `map.slang` (`SamplingUpscaledPrefilter`) resamples it to the window with clamped Catmull-Rom, blends the variants with the fade's weights and takes each pixel's class from the nearest original texel | `scenery_upscale_tests.cpp` of `packages/engine/render` (edges, stepless diagonals, gradients, fades, classes, dithering, cache); `graphics_options_tests.cpp` ("the sharpness options start on their best…"); captures of the same frame original / HD before / new in seven maps | Improvement (option) |
| Antialiasing (`antialiasing`) | Does not exist | `Renderer::setMultisampling`: the scene pass with 4 samples (`GraphicsPipeline` with a multisampled variant), resolved into the window or into the light pass's texture | `pipeline_test.cpp` of `packages/engine/rhi`; captures | Improvement (option) |
| Model textures (`texture_filter`) | Square texels | `ModelRenderer::setTextureFilter`: filtering with mipmaps and anisotropy (`FlagSmooth` in `skinned_model.slang`), on the textures doubled by `upscaleRgba` (interpolated alpha, no dark fringe) | `scenery_upscale_tests.cpp` ("a model texture's cut-out edge takes no dark fringe…"); `pipeline_test.cpp` (mip levels); captures | Improvement (option) |
| Restored movies | The MDEC decodes with its 8-bit integer IDCT and repeats each frame's chroma in 2 × 2: blocks in gradients, halos on edges, bands in the dark | `dwx` decodes with the exact IDCT in sixteenths of a level (`decodePreciseFrame`), removes blocks and halos with MPEG-4's filters according to the frame's scale (`deblock`, `dering`) and writes 10 bits; `dwx cook` cooks 10-bit AV1; `video::toRgba` interpolates the chroma and uses the exact BT.601 ([video](../../formats/video.md#restoration)) | `mdec_tests.cpp` and `restore_tests.cpp` of `packages/formats`; `cook_tests.cpp` of `apps/dwx` ("a 10-bit movie cooks…", "pictures convert to RGB with full-range BT.601…"); captures `video-before-*` / `video-after-*` of OP1 | Improvement |
| Upscaled movies (`texture_filter`) | The 320 × 176 image enlarged | With "Upscaled", `MovieDrawer` enlarges the image to the window's pixels with edge-adaptive scaling (`movie_upscale.slang`) and sharpens it in the post pass with contrast-adaptive sharpening (`movie_sharpen.slang`); with "Smooth" or "Pixels", stretched with a linear filter as always ([movies](../../architecture/movies.md#scaling)) | captures `video-compare-*` (1:1 crops of the same frame before and after) | Improvement (option) |

## 3D characters

An optional improvement, on by default (it changes no rule): some maps draw characters as
prerendered drawings of their depth layers, not as models. With the `model_characters` setting (row "3D
characters" of the in-game options, "Graphics" tab; saved in the user's settings)
the field leaves out those drawings and puts in their place the species' model with its rest animation; off, the
original's drawings are seen. The console and the routes change it for the game with `model_characters("on")` /
`model_characters("off")`.

- **What the original does**: they are map objects (`overworld.c`: `loadMapObjects`, `calcMapObjectOrder`,
  `renderMapOverlays`, `buildMapOverlayPrim`), not part of the background. A long animation goes in two objects that take turns
  with blank frames and the shadow is another separate object. The scripts show and hide them with
  `set_obj_visibility` (`setMapObjectsFlag`).
- **Where they appear** (all are objects, none is painted into the background of the maps reviewed): the opening in front of
  the old man's house (`topn01`, six babies), the ending in the same place (`tend01`, `tend02`, nine), the two in
  the farm's planters (`twna01` to `twna13`, according to triggers 246 and 225), the two in the old man's house
  (`room08`) and the one at the market stall (`twnb02` to `twnb09`). The table is `field/sprite_characters.yaml` of the game
  pack (schema `field_sprite_characters.schema.json`).
- **Where**: `sprite_characters.hpp` reads the table and computes the place: the feet are the center of the shadow, the foot
  of the first drawing or a given pixel (`feet`); `MapCamera::pickGround` takes them to the ground, at height `height` (a
  planter, a bed), and the model faces the camera unless the table gives `facing` (the stall's shopkeeper, 3584) or
  `look_at`, a ground point in the original's units that it turns toward (`yawOfDirection`). `FieldScene` loads the
  model of each species on entering the map (`standInSpriteCharacters`) and draws it every frame with its rest
  (`drawSpriteCharacters`) while the first drawing is not hidden.
- **Pose**: a drawing that does not show the character standing carries `animation` (the original's animation number) and
  `frame` (the tick the drawing shows); the model takes that animation and stays still on that tick instead of the rest one.
  If the model lacks that animation it warns in the log and uses the rest one. The market stall's shopkeeper (`twnb02` to
  `twnb09`, the same instance at pixel 279, 86 in all variants, day and night) is a 48×48 drawing in
  three 12-tick frames lying face down on the rug, head toward the camera and to the right; its rest
  floats standing (body from 190 to 373 units above the ground), and before it was seen floating in front of the rug. Now it
  carries `feet: [305, 118]`, `facing: 3584`, `animation: 41`, `frame: 7` (the lying tick of its sudden fall).
  `MapDepthLayers::setStoodIn` takes the drawings out of the painting without touching the hidden mark the scripts set; the
  shadows are still drawn.
- **Semi-transparency**: `additive` draws the model added to what is behind with its colors by that factor
  (`ModelInstance::additive` and the tint), like sprites with additive semi-transparency: `species_bc` in the ending
  (modes 1 and 3), with 0.35. The other sprites of those maps are opaque (mode 4) or have mode 0 with no semi-transparent
  texels in view (`species_j`), and their model is drawn opaque; the `species_bc` in the old man's house uses the model
  as is, which is already transparent underneath.
- **Rules**: there is no new entity; collisions, chats, scripts and the minimap do not change. If a species'
  model is missing, its drawing stays.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| 3D characters | Prerendered drawings, map objects (`renderMapOverlays`, `buildMapOverlayPrim`), shown and hidden by the scripts (`setMapObjectsFlag`) | `field/sprite_characters.yaml`; `sprite_characters.hpp` (`placeOf`, `standInFor`); `FieldScene::drawSpriteCharacters` draws the species' model with its rest where the drawing was; `MapDepthLayers::setStoodIn` | `sprite_characters_tests.cpp` (table with `additive`, pose, feet, place on the ground, when it is seen, setting); `map_layers_tests.cpp` ("an instance stood in for is not drawn…"); `in_game_options_tests.cpp`; before and after captures in `topn01`, `tend01`, `tend02`, `twna02`, `room08` and `twnb02` to `twnb09` | Improvement (option) |
| Where the opening's babies look | Their drawings (`topn01`, objects 0 to 17) are turned toward the player who wakes up in the middle, lying at (266, −2330) when the scene starts (measured with the original: savestate `00_intro_arrival`; the left one is seen from the side looking right, the right one looking left, those below from behind). The script (193, section 0x36) turns none of them: they are not entities; it only turns entity 10 with `entity_look_at_entity` | All six carry `look_at: [266, -2330]` in `field/sprite_characters.yaml` and their models turn toward that point, like their drawings. Before they all looked at the camera | `sprite_characters_tests.cpp` ("a character with a point to look at faces it, not the camera", the pack's table); before/after captures and the original's | Same (with the option) |
| The stall shopkeeper's pose | A prerendered drawing (map object, instance at pixel 279, 86 of `twnb02` to `twnb09`, 48×48 frames, 12 ticks each, the same day and night): lying face down on the rug, head toward the camera and to the right, next to the cash register | `animation: 41`, `frame: 7`, `facing: 3584`, `feet: [305, 118]` in the eight entries of `field/sprite_characters.yaml`; the model stays still on that tick. Before it floated standing with its rest in front of the rug | `sprite_characters_tests.cpp` ("a sprite character holds the pose its drawing shows…", the pack's table); the original's captures, before and after | Same (with the option) |
| The opening celebration's sound | After the old man's first sentence the two entities that jump (10 and 12, clip 11) sound `playSound(3, 49)` from their animation (`tickAnimation`, instruction 0x4000), once per jump; the six drawn babies make no sound (they are map objects) and the script requests no sound (measured with the original: savestate `00_intro_arrival`, stop at `playSound`) | The same two calls, from the clip's sounds (`FieldScene::playClipSounds`, log `[port:anim] … playSound(3, 49)`); the babies' models make no sound, like their drawings | Opening route with the debug log | Same |

## Widescreen pictures

An optional improvement, on by default (it only changes the look): the `widescreen_pictures` setting (row "Widescreen
pictures" of the "Graphics" tab; saved in the user's settings). dwe has no
other aspect ratio option: the field and the battles already fill the window, so this option is the one that decides
how the original's full-screen images, made for a 4:3 screen, look. Off, they look as in it:
in its 4:3 frame, in the middle, over black.

The full-screen images the game shows (the modern skin draws none: its title is a plain
background):

| Image | Size | Rescaling | Before | With the option |
|---|---|---|---|---|
| Screen before the menu ("Press Start", TITLE256; `main.c: runLandingScreen`) | 320 × 256, rows 0–205 are drawn | Smoothed (`upscale: smooth`) with the "upscaled" texture filter | At 3 dp per pixel at the top center (960 × 618 dp); at the sides and below, black | Covers the window from the top: at 16:9, 4 dp per pixel (1280 dp wide) and rows 0–179 are seen; the "Press Start" line rises to image row 162 (the original's 168 would be stuck to the edge), under the logo |
| Main menu cover (TITLE2; `main_menu.c: renderInitialMenu`), also behind the language, the battle mode, the save slots, the new game rules and the options over the title | 320 × 240 | Smoothed (`upscale: smooth`) | Fit to the height (`contain`), black at the sides | Covers the window with the bottom edge fixed: at 16:9, 4 dp per pixel and rows 60–239 are seen (above there is only a grid); the boxes move away from the logo |
| Movies (OP1 at startup and after 600 frames on the title, OP2 in a new game, ED2 on loading after the ending, EDR in the ending; `mov.c`) | 320 × 176 video | Linear filtering (it is video, it does not go through the upscaler) | On the original's 4:3 screen fit to the window: at 1920 × 1080, 1440 × 792 with bars on all four sides | The image alone fit to the window with its proportion (`movieFrame`): at 1920 × 1080, 1920 × 1056 |

How each one is filled, and why:

- **Cover and screen before the menu: the image enlarged to cover the window.** It is scaled whole, the same on
  both axes, until it fills the width, and rows are left over: the cover loses them at the top, where there is only a grid (its
  logo is at the bottom right, rows 165–227, and stays whole with 12 rows of margin); the screen before the
  menu loses them at the bottom, where it is black (its logo is in rows 37–149). It is never cropped more than in a
  16:9 window (at least 180 of its 240 rows are seen): in a wider one (21:9) the image stays at that scale and each
  side goes on as a mirror of its edge columns (`mirror_sides` in `skin.yaml`), which darkens toward the window's
  edge. The mirrors do not reach the logo: the cover reflects on the right only the 26 columns the logo leaves
  free and on the left up to 100, so the logo goes a bit to the right; the other reflects 52 and 56.
  In a 4:3 window it looks like the original; in a narrower one the sides are cropped without touching the logo.
- **The boxes, in their place across the image's width and off the logo.** The menu, the language, the battle mode,
  the slots and the new game rules go to the original image's column (the menu at 72, the
  slots at 60), at their usual height in the window. If one covered the logo, it moves to the left and, those
  that can (the slots with their summary, the rules), upward, without leaving the window. The options over the
  title are those of the pause menu, with their own place.
- **Sharpness.** The upscaler doubles the image up to 2560 pixels wide (three times; before, twice up to 1280), so
  at 1920 × 1080 and at 2560 × 1440 each screen pixel comes from at least one smoothed texel, without stretching filtered
  texels.
- **Movies: the whole image, uncropped.** The video is already nearly 16:9 (320 × 176); the original shows it with
  bars above and below inside its 4:3 screen. With the option it is fit to the window: it fills the width of a 16:9
  with 12 pixels of bar above and below, and in a 4:3 window it stays the same as in the original.

The title documents (`main_menu`, `save_slots`, `new_game_rules`) have, first in `#scrim`, the elements
`#picture`, `#picture-fade-left` and `#picture-fade-right`, and load `ui/common/title_picture.lua`. With the option and the
original skin it puts the class `wide` on `#scrim`, computes every tick with the window's size the image's scale and place
(`title_picture.layout`), places those elements and moves the boxes each document passes to it; off,
the boxes go back to the place in their stylesheet. The skin draws `#picture` with the sheets `title-wide` and
`boot-wide` (the same images with the sides mirrored, [ui](../../architecture/ui.md#skins)) and the side
gradients. The movies read the setting at the start of each one (`MovieScreen::setFillWindow`).

Tests: `title_picture_tests.cpp` ("the title's picture covers the window, its logo whole and the menu off it", "a
window wider than 16:9 sees the picture's mirrored sides…", "a 4:3 window draws the title as the original…", "the
slot list and its summary move off the logo…", "the modern look's title draws no picture…"), "an image reference may
ask for mirrored sides…" and "mirrored sides carry the edge columns on…" (`image_reference_tests.cpp`), "mirrored
sides count the game's pixels…" (`image_replacement_tests.cpp`), "a sprite sheet may mirror its picture's sides…"
(`skin_tests.cpp`), "a movie sits on the original's 4:3 screen, or fills a wide window with the option"
(`movie_frame_tests.cpp`), "the widescreen pictures are the UI's option…" (`title_options_tests.cpp`); captures at
1920 × 1080, 2560 × 1440 and 2560 × 1080.

## In-game options

The options are changed without going back to the title: the pause menu's "Options" page opens with the game's three
actions — **"Save"** (quick save, `quick_save`), **"Return to title"** (`to_title`) and **"Quit game"**
(`quit`), in both skins; in the modern one they are also on the main page, and in the original it is the visible place
where the icon grid stays faithful — and goes on with the display ones (window mode,
scale, vertical sync), the skin, the language and the original-look text (`language`, `text_font`; the same page opens from the
title, see [From the title](#from-the-title)) and, below, the engine's own options.

| How it opens | Keyboard | Gamepad |
|---|---|---|
| Directly, also from the Start pause (it closes it) | Esc (or F10) | Select (Back) |
| From the menu (modern skin) | Tab, "Options" | North button, "Options" |
| From the menu (original skin) | Tab, "Player", Tab, "Options" | North button, "Player", north button, "Options" |

`settings` (`defaultActionMap`) opens the pause menu on its options page (`pause_menu.start_page`, which its
`on_show` reads); Back from there returns to the game, and with the menu open it closes it. On the keyboard it is `Esc` (or `F10`) — the
classic PC pause key, freed from `cancel`, which moved to `Ctrl` — and on the gamepad Select/Back. Where there is no pause
menu (title, movies, interview, naming box, gym) or with another menu open it does nothing. Under the
pause box the key of the device in use is read ("Esc Options" on keyboard, "Back Options" on gamepad)
(`pause_box/logic.lua`).

### Tabs

The page is divided into four tabs; each row appears in only one:

| Tab | Rows |
|---|---|
| Game | Walk with, Autosave, Island map, Items on minimap, Bulk trades, Long names, Evolution requirements, Canonical stages, Status extras, Rare items, Quick battles, Enemy life bars, Gym sessions, Battles without wait, Difficulty, Skip evolutions, Cancel closes choices, Fair sickness chance, Fair evolution average, Fair move forgetting, Card stock without repeats, Space after names, Free exploration |
| Graphics | Display, Vertical sync, Smooth motion, HD scenery, Scenery quality, Antialiasing, Model textures, 3D characters, Widescreen pictures, Enhanced lighting, Vivid colors, Shadows, Chibi style, Weather, Seasons |
| Sound | Music volume, Effects volume, Continuous music, Reverb |
| Interface | Interface style, Interface scale, Language, Original-look text, Translated signs, Show minimap, Enemies on minimap, Digital time, Save notice, Several needs, Recruit bar, Shop extras, Art fixes, Developer tools |

Free exploration goes last in Game (decided by the coordinator, 2026-11-10): the tab opens with focus on its
first row, so opening the options with Escape and confirming never turns it on by accident.

| What | Keyboard and mouse | Gamepad |
|---|---|---|
| Previous / next tab (they wrap around) | Q / E, or click on the tab or on the key next to it | L1 / R1 (LB / RB) |
| Moving through the rows | ↑ / ↓; the wheel scrolls the list if it does not fit | D-pad or stick |
| Changing the value | ← / → (numbers stop at 0 and at 10), or click on the row's ‹ › arrows | Left / right |
| Yes/No, or next value (wraps around) | Space, Enter or click on the row | South |
| Save, Return to title, Quit game | ↑ from the first row; ← / → between the three; Space or Enter; or click | ↑, left / right, South |
| Back | Ctrl, Backspace or right click (Esc closes the menu if it was opened with Esc) | East |

The actions `tab_previous` and `tab_next` (`defaultActionMap`: Q and L1, E and R1) reach the document as
`NavCommand::TabPrevious`/`TabNext` (`ui/navigation.hpp`); a page without tabs does not use them and the menu stays open.
With the menu open the field is stopped, so L1 does not open the map nor R1 make anyone walk. At the foot the keys of the
device in use are read (Q E Tab, ← → Change, Space Accept, Ctrl Back; on gamepad, its buttons).

The game's three actions go in a bar above the tabs (in the modern one, to the right of the title; in the
original, on the first line of the box), with their description in the box below. ↓ goes through the rows, then
"Reset to defaults" and "Back" at the foot, then "Save" and again the first row; ↑ from the first row reaches
"Save" and from there wraps around to "Back".

How each value looks: a yes/no is a pill ("Yes" lit, "No" off); a list goes between arrows
(‹ Keyboard/pad ›) and the two volumes are a bar from 0 to 10 with its number. Below the list, a box describes
the focused row in one or two lines (`pause_menu.help.<row>`, in es and en) with a tag: "As the original"
while the value is the original's and "Improvement" if not (the volumes and the options the original lacks are
always "Improvement"); display, sync, scale and language carry no tag.

"Reset to defaults", at the foot, returns the rows of the visible tab to their default value, after asking
("Reset the "Game" options?", with Reset and Cancel; cancel —Ctrl or Backspace— says no). It only sends the rows that have a
default value and do not already have it; display, skin, language and font do not change.

Both skins have the tabs: the original draws them with the folders of its player menu
(the same tab sprites), its blue arrows, its frames and its font; the modern one, with a tab bar underlined in amber,
pills, thin bars and the description box, and only animates color and opacity (0.12 s), with no movement. At
1280 × 720 all the rows of each tab fit without scrolling; if they did not fit (another scale, a mod's rows), the
list scrolls with a visible bar.

### From the title

"Options" in the title menu opens this same page, with the same tabs, rows, descriptions and keys, over
the title image: the menu sends `title_options`, `TitleScreen` hides its box and shows the pause menu with
`pause_menu.at_title` and `pause_menu.start_page` = `options` (`apps/game/src/title_options.hpp`). Without a game there is
nothing to save and no cover to return to, so the action bar only has "Quit game", which closes the
game at once ("Closes the game.", `pause_menu.help.quit_title`). Back (Ctrl, Backspace, right click, East or the
"Back" row) closes the page, sends `title_options_back` and the title returns to its menu with focus on "Options".

All the rows work on the title. Those that only a game in progress applies (the yes/no ones of `game_options.lua` that
`PlaySession` handles: free exploration, island map, long names, smooth motion, translated signs…) are
saved by the game in the user's settings and published again (`changeGameToggle`, `publishGameToggles`); the
next game starts with them. The others (display, scale, skin, language, font, volumes, graphics, minimap)
have the same handler as in game.

The display rows are saved in `settings.yaml` like the others (`DisplayChanger`,
`apps/game/src/display_settings.hpp`): "Display" (`fullscreen`), "Vertical sync" (`vsync`), "Interface
scale" (`ui_scale`, the factor; on reading it is clamped to the interface's range, 0.5 to 3) and the minimap's size from
the "Minimap" page (`minimap_size`: `small`, `medium` or `large`; another value counts as `medium`). The game reads them
before opening the window, so the first image already comes out with the saved mode, sync and scale;
`--fullscreen` and `--no-vsync` take precedence only for that game. Sync changes at once: the device rebuilds the
swapchain with the new presentation mode before the next image (`rhi::Device::setVsync`). An
automatic route or the control channel neither read nor write those settings. Tests: `display_settings_tests.cpp`.

The page fits in all windows: the design is 720dp high and at least 960dp wide, so a 4:3 window
leaves 960dp. The modern box measures 900dp; the original, 980dp with the tab keys outside, narrows to
880dp with the keys on its corners when the page is less than 1060dp wide (`@media` in
`skins/original/pause_menu/style.rcss`). `in_game_options_tests.cpp` checks it from 640 × 480 to 2560 × 1080 in both
skins and both languages.

| Change | Original | dwe | Test | Type |
|---|---|---|---|---|
| Options from the title | Does not exist: the title menu only has its four entries | "Options" opens the tabbed options page; "Quit game" in its bar | `in_game_options_tests.cpp` ("over the title…", "…fits the window…"); `title_options_tests.cpp`; `ui_tests.cpp` (title menu with keyboard and with mouse); routes `route-language.yaml` and `route-skins.yaml` | Improvement |

### Data

The engine's own options come from a list, `ui/common/game_options.lua` (`game_options`): the row's id,
the UI setting the game publishes and the row sends as a command, text, default value and, for those that are not
yes/no, their values (`values`) and the prefix of their texts (`texts`), or, for numeric ones, their range (`range`, the
integer shown as is). Each entry also gives its tab (`tab`: `game`, `graphics`, `sound` or
`interface`; without it, Game), the text of its description (`help`) and the value seen in the original (`original`;
without it, the option is always an improvement). `default` is also what "Reset to defaults" sends. The rows for
display, skin, scale, language and font are in `pause_menu/logic.lua` (`display_entries`), with the same fields.

The document has 16 rows (`row-1`…`row-16`) that the page distributes on showing each tab: it gives them the option's
id (`<id>`, `<id>-label`, `<id>-value`, `<id>-less`, `<id>-more`, `<id>-bar`) and hides the extra ones. A new
option only needs its entry in that list and for the game to handle its setting; no document needs a row of its
own. The routes and tests choose a tab with `show_tab(document, "<tab>")` and change an option by its id with
`adjust(document, "<id>", step)`, also without showing it. The autotests' `menu_select` goes through the tabs by itself
(down, up and `tab_next`; with the mouse, a click on the E key) until it finds the row.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Options by tabs | Does not exist: the original has no options | Tabs Game, Graphics, Sound and Interface; Q/E and L1/R1; ←/→ both ways; description with tag; "Reset to defaults" with a question | `in_game_options_tests.cpp` (tabs, values both ways, clicks, description, reset in both skins, everything fitting in es and en); `navigation_tests.cpp` ("Q and E, and the pad's shoulders…"); `glyph_art_tests.cpp` (modern arrows); route `route-options-tabs.yaml` in both skins | Improvement |

## Volume

An optional improvement (it changes no rule): the original **has no sound option**. Its title menu only
has "NEW GAME", "CONTINUE GAME", "DELETE GAME" and "BATTLE MODE" (`main_menu.c`: `TITLE_MENU_ITEMS`), and the master
volume is set once to the maximum on loading the sound (`sound.c`: `initializeMusic`, `SsSetMVol(127, 127)`); the game never calls
the sound library's stereo/mono functions. dwe adds two rows to the "Sound" tab of the in-game
options (`ui/common/game_options.lua`), "Music volume" and "Effects volume", from 0 (silence) to 10 (the original's level, the default).

| Where it is changed | Keyboard | Gamepad |
|---|---|---|
| In game (modern skin) | Tab, "Options", E to "Sound", rows "Music volume" / "Effects volume"; ←/→ lower or raise one step | North button, "Options", R1 to "Sound", the same rows; left/right |
| In game (original skin) | Tab, "Player", Tab, "Options", E to "Sound", the same rows | North button, "Player", north button, "Options", R1 to "Sound", the same rows |

← stops at 0 and → at 10 (the row's ‹ › arrows do the same with the mouse); confirm (Space, Enter or a click
on the row) raises one step and from 10 goes back to 0. The row sends the command `music_volume` / `effects_volume` (a number);
`VolumeChanger` (`volume_setting.hpp`) rounds it and limits it to 0–10, applies it at once to the `Music` and
`Effects` controls of `audio::Audio` (`setVolume`; the mixer does a short ramp, with no jumps), publishes the step and saves it in
the user's settings (`music_volume: 7`). At startup, `Game` applies the saved steps before anything sounds.
A step's gain is its fraction squared (`volumeGain`: 10 → 1, 5 → 0.25, about −12 dB; 1 → 0.01), so that
the steps sound evenly spread instead of crowding at the top. The `Master` control stays at 1.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Music volume | Does not exist: music always plays at maximum (`sound.c`: `initializeMusic`, `SsSetMVol(127, 127)`) | Row "Music volume", 0–10 (10 by default = the original); `Music` bus, which also carries the movies' sound (`MovieScreen`) | `volume_setting_tests.cpp` ("the options page's volume rows change the audio at once and save the steps", "the saved volumes reach the audio at start…"); `in_game_options_tests.cpp` ("the options page steps the music's and the effects' volumes from 0 to 10", original and modern skins); `user_settings_tests.cpp` | Improvement (option) |
| Effects volume | Does not exist: same as music | Row "Effects volume", 0–10; `Effects` bus: the game's effects and the menus' sounds | The same | Improvement (option) |
| Stereo/mono | No option: the game never changes the sound library's mode | Always stereo; no mono row is added | — | Gap (no clear value) |
| Reverb | The chip's, on at startup and forever (`sound.c`: `initializeMusic`, `STUDIO_B` at 0x7000) on tones with the reverb bit | Same by default ([audio, §7.4](../../architecture/audio.md#74-reverb)); row "Reverb" to turn it off: the command `reverb` (boolean) goes to `VolumeChanger`, which calls `Audio::setReverb` and saves it (`reverb: false`) | `volume_setting_tests.cpp` ("the reverb starts on, as the original, and its row turns it off and saves it"); `reverb_tests.cpp`; `user_settings_tests.cpp` | Same (option to remove it) |

## Shadows, light and vivid colors

Optional improvements that change no rule, no collision, no geometry and no random numbers; all three
on by default, like the other improvements that do not touch rules. They are changed in the pause menu's options
(rows "Shadows", "Enhanced lighting", "Vivid colors") and in the title's with the original skin, apply at once
and are saved in `settings.yaml` (`shadows`, `enhanced_lighting`, `vivid_colors`; `GraphicsChanger`,
`apps/game/src/graphics_options.hpp`). `--graphics shadows=off|round|sun,lighting=on|off,colors=on|off` (and those of
[sharpness](#hd-scenery): `scenery=`, `aa=`, `textures=`) changes them only for that run (captures and routes, which do not read the settings). `FieldScene` applies them in
`field_scene_graphics.cpp`; how they are drawn: [render.md](../../architecture/render.md#light-and-color-pass).

- Shadows: `round` is the original's round shadow under each character (player, partner and the map's NPCs;
  `drop_shadow.c: renderDropShadow`, which `model.c` calls for each character it draws), the size of its species and never
  on cells of value 0x80 or 0xFF (`getTileTrigger` gives -1). The objects on the ground already brought their own. `sun` (the
  default) adds a soft shadow cast by the sun: short at noon, long and turning at dawn and dusk,
  weaker with the dusk palette and none at night or on maps without a time (caves, houses). `off` draws
  none.
- Enhanced lighting: a tint according to the game clock (golden in the morning, neutral at noon, orange at dusk,
  blue at night) only on maps with a time, and each map's lights (`field/lights` of the game pack,
  `content/schemas/field_lights.schema.json`): lamps, fires and glass painted in the background that light what
  is near them. For now: twna01 (doors at night), leom01 (lamps, bottle and glass), tunn01 and frzl01 (the
  stove, flickering).
- Vivid colors: more saturation in dull colors than in vivid ones, a bit of contrast and a soft glow
  around the brightest. Only in the field scene: the HUD, the menus and the fades do not change.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Characters' round shadow | `renderDropShadow`: a square of 2 × the species' radius on the ground, SPOT256 (uv 0x40, 0x80, 63 × 63), subtract with color 0x30, behind everything (OT 0xFFD); not on cells with `getTileTrigger` -1 | Same, with the option on `round` or `sun`; drawn right after the background and before the models (`MapRenderer::groundDrawer`) | `scene_light_tests.cpp` ("the round shadow is the original's…"); `graphics_options_tests.cpp` | Ported |
| Sun shadow | Does not exist | A stretched patch from the feet in the direction opposite the sun, with the clock's time | `scene_light_tests.cpp` ("the sun's shadow turns through the day…", "the sun's shadow stretches from the feet…") | Improvement (option) |
| Light by time and map lights | The original only fades the palettes and its three model lights with the time (`map.c: updateTimeOfDay`) | A soft tint over the scene and point lights at background pixels, without touching the palette or the map's sampling | `scene_light_tests.cpp` (light table, tints, intensities); `graphics_options_tests.cpp` ("the game pack's light table…") | Improvement (option) |
| Vivid colors | Does not exist | Vibrance, soft contrast and glow on the field scene, never on the UI | `in_game_options_tests.cpp` ("…cycles the shadows and switches the light and the colours") | Improvement (option) |

## Weather

An optional improvement, on by default (it changes no rule; the original has no weather): the `weather` setting
(yes/no) is changed in the "Weather" row of the in-game options and from the title;
it applies at once and is saved in the user's settings. The zones, their weights and the maps without weather, the seed,
the drawing and the sound are in [packages/game/weather](../game/weather.md). `--weather <weather>` forces one for captures.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Field weather | Does not exist | Clear (sun by day), cloudy (gray veil and shadows of passing clouds), light and heavy rain (slanted drops and splashes, rain loop), light and heavy snow (drifting flakes, gusts of wind); per zone, every 3 game hours, with an 8 s fade; never in interiors, caves, city rooms or fixed-time maps; its own seed saved in the game | `weather_tests.cpp`, `weather_seed_tests.cpp`, `weather_particles_tests.cpp` | Improvement (option) |

## Seasons

An optional improvement, on by default (it changes no rule; the original has no seasons): the `seasons`
setting (yes/no) is changed in the "Seasons" row of the in-game options and in the title's,
right below "Weather"; it applies at once and is saved in the user's settings. Off, the field looks
and the weather comes out exactly as without seasons. The rules are in [packages/game/weather](../game/weather.md#seasons).
`--season <season>` forces one for captures (its look and its name; the weather follows the calendar).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Calendar season | Does not exist | Spring, summer, autumn and winter, 3 game days each from year 0, day 0 (a new partner sees all four); 8 s fade on changing | `weather_tests.cpp` | Improvement (option) |
| Scenery color | Does not exist | Only on maps with weather: the map's background and layers (never the characters); spring fresh greens, summer bright and warm, autumn orange and brown foliage, winter cold and pale; zones with their own weather (snow, desert, jungle) are only nudged a little | `weather_tests.cpp` | Improvement (option) |
| Petals and leaves | Does not exist | A few petals in spring and leaves falling in autumn, spinning and swaying; fewer with rain or snow, fainter at night | `weather_particles_tests.cpp` | Improvement (option) |
| Weather by season | Does not exist | More sun in summer, more rain in spring and autumn, snow in winter (part of the rain falls as snow where the zone allows it) | `weather_tests.cpp` | Improvement (option) |
| Season name | Does not exist | In the pause menu (modern skin), after the day; nothing with the option off; the original skin does not show it | — | Improvement (option) |

## Hidden content

An optional improvement, off by default: on starting a new game from the main menu (Play → slot → "Yes"),
the "Game rules" screen (`ui/new_game_rules`) lets you choose the rules before the
game starts. The cursor starts on "Start". "Every species as a partner: Yes/No" (on by
default, `new_game_rules.all`; [extra-evolutions.md](../game/extra-evolutions.md)) adds the `all_species` section;
"Hidden content: No/Yes" switches between the `original` and `oculto` rulesets (`species_k`, `species_m` and `species_o` also
by evolution: [evolution.md](../game/evolution.md#hidden-content)). The two together choose the ruleset:

| Every species | Hidden content | Mega step | Ruleset |
|---|---|---|---|
| No | No | — | `original` |
| No | Yes | No / Yes | `oculto` / `oculto_mega` |
| Yes | No | — | `todas` |
| Yes | Yes | No / Yes | `todas_oculto` / `todas_oculto_mega` |

The "Every species" row only appears if the packs bring the `todas` ruleset (`TitleScreen` publishes
`new_game_rules.all`), and cancel returns to the slot list. The "Ultimate to Mega step" row only appears with hidden content on and
when the packs bring the `oculto_mega` ruleset (`TitleScreen` publishes `new_game_rules.mega`).

It is not a user setting but a game one: `TitleScreen` stores the chosen id in `PlayStart::rulesetId`, the
session starts with it and the saved game's header points to it (`rulesetId`). On loading, `rulesetFor`
(`session_start.cpp`) reads that header before creating the session, so that the tables mounted once (the partner's
species, evolution tables, battle's `digimon`) come out already with the ruleset's patches; a game without the
field uses the command line's (`--ruleset`, `original` by default).

With the ruleset, the player page's evolution tree (`player_chart.cpp`: `publishRulesetChart`) reads the
paths from the ruleset's tables and adds the new cells (`player.chart.count`: 61 in the original, 65 with hidden
content and 66 with the Mega step; with every species, 110, 113 and 113, because `vand` and `muge` are already
put in by hidden content or the Mega step) and the Mega step's Mega list (`player.chart.mega`, ids separated by
commas; empty without it). A cell that does not fit in its box's grid (those of the third Ultimate column of
hidden content) goes to a following page of the tree ([canonical stages](#canonical-stages)).
A new form uses another species' icon with another palette until it has icons of its own.

**Game mode.** Before the rules, `TitleScreen` asks for the mode (`ui/new_game_mode`, `NewGameModes` in
`new_game_mode.hpp`) only if the packs bring more than one ([modes](../game/modes.md)); the game only brings
"Original story", so it does not appear. It publishes `new_game_mode.count` and `new_game_mode.<n>.id`, `name` and `description`
and receives `new_game_mode` with the id or `new_game_mode_back`. The chosen mode goes to `PlayStart::modeId` and to the session
(`SessionDesc::modeId`, `mode` section of the save). The rules appear afterward only for a mode that admits
`original` and `oculto`; another mode starts with its `ruleset`.

The test routes only see this screen if they ask for it (`new_game_rules: true`); the others start with the original's
rules, as before. Those that ask for it first turn off "Every species" (`menu_select: rules-all`).
`route-hidden-content.yaml` chooses hidden content and makes the partner evolve into
each new form; `route-hidden-content-off.yaml` leaves the original and checks that everything stays the same
([vertical slice](../../guides/vertical-slice.md)).

## Canonical stages

An optional improvement, off by default: the `canonical_stages` setting (row "Canonical stages" of the pause menu's
options, saved in the user's settings) arrives as a UI setting from
`PlaySession` (`CanonicalStagesSetting`). It only changes what the player page's tree shows; the rules
still count those species as level 5.

| Stage | Spanish | English |
|---|---|---|
| egg | Digihuevo | Digi-Egg |
| level 1 | Bebé I | Fresh |
| level 2 | Bebé II | In-Training |
| level 3 | Infantil | Rookie |
| level 4 | Maduro | Champion |
| level 5 | Perfecto | Ultimate |
| Mega | Supremo | Mega |
| Ultra | Súper Supremo | Ultra |

Egg and Ultra are defined (`pause_menu.chart_stage.*`) but have no species. On, a cell's detail
says its canonical stage ("Mega" for a Mega).

The red Mega box (`.stage-5`) appears when the tree has some Mega species: with the option on, those the
series calls Mega (`MEGA_ORDER` in `ui/pause_menu/logic.lua`: `species_v`, `species_bd`, and `species_o` and
`species_t` when the ruleset brings them), and with the Mega step, even if the option is off, the ruleset's Megas
(`player.chart.mega`). It goes on the same page, to the right of the Ultimate one and at its same height: the six boxes (Fresh,
In-Training, Rookie, Champion, Ultimate and Mega) in a 960dp panel. To make them fit inside the frame, with 30dp
of margin on each side like at the original's left, the Champion and Ultimate columns end up at 66dp instead of 72 and
the sides of those boxes at 21dp instead of 24; the Mega box measures 93dp with its column centered. To know which species
each cell is, `publishPlayerChart` publishes `player.chart.<n>.id`.

Each box has a grid and a page holds at most what fits in it:

| Box | Columns | Rows | Cells per page |
|---|---|---|---|
| Fresh | 1 | 4 (38 px apart) | 4 |
| In-Training | 1 | 4 (38 px apart) | 4 |
| Rookie | 1 | 9 | 9 |
| Champion | 4 | 8 | 32 |
| Ultimate | 2 | 8 | 16 |
| Mega | 1 | 8 | 8 |

Distribution rule (`chart_places`): a cell's box is that of its column in the original (the rightmost one
that starts at its x or before; the Megas, the Mega box). A cell in a free slot of its box's grid stays
in it on page 1, so the original's end up where the original puts them. The others (a Mega, a ruleset one
outside the grid or in an already occupied slot) take the free slots of their box in order, column by column from top
to bottom, first on page 1 and then on 2, 3...; the Megas in the order of `MEGA_ORDER` and then by cell. Thus,
with the option on `species_v` and `species_bd` leave their Ultimate slots and hidden content's `species_q`
takes one. With hidden content and everything off the Ultimate box (15 of the original and `species_m`) is full and
`species_o` and `species_q` go to page 2; with the Mega step and the option off `species_o` and `species_t` go to the
Mega box and `species_q` to page 2.

All pages have the same boxes in the same place; on the following ones there are only cells where there are species.
With more than one page, under the Fresh and In-Training boxes and in the same place on all of them, go the up arrow
(`#chart-less`, the battle sprites', `battle-up-arrow`) if there is a previous page, the page number
(`#chart-page`, "1/2") and the arrow flipped down (`#chart-more`, as in the saved game list) if there is a next
page. To change pages:

| How | To the next | To the previous |
|---|---|---|
| Cursor on the tree | Down (↓ arrow, gamepad down) on the last cell of a column | Up on the first cell of a column |
| No cursor (on the tab) | Down | Up |
| Mouse | Wheel down over the tree, or click on ▼ | Wheel up, or click on ▲ |

The cursor stays in its column: it goes to the top cell of that column when going down and the bottom one when going up, to the nearest
column with cells if on the other page it has none, and returns to the cell it left if it changes page again without
moving. The change is a 0.2 s slide (`#chart-pages` rises 564dp per page inside `#chart-clip`, which
crops to the frame's interior; the boxes of the page being left are `#chart-stage-other-<n>`): at 20 steps per
second like the original's boxes, or continuous with "Smooth motion". While it lasts no keys are accepted, and when it ends
the cells of the pages not in view are hidden (`.off-page`) so the pointer does not touch them. The cursor and
the detail work the same on all pages. The wheel reaches Lua with `event.wheel`
([editing menus](../../guides/edit-menus.md)). Without Mega species and with everything in its grid (the original's rules)
there is no Mega box, nor arrows, nor page 2, and the tree is the original's pixel for pixel.

Off, the tree is the original's and its names those of [decision 4 of the glossary](../../guides/spanish-glossary.md);
level 5 is called "Ultra" in Spanish in the tree too (before "Definitivo").

The options list no longer fits whole at 1280x720 in any skin: it scrolls (`#page-options`, `overflow-y: auto`),
navigation brings the focused row into view and the mouse wheel moves it. The text check does not count the
rows the list leaves outside its box (`text_fit.cpp`); they are checked on reaching them.

`route-canonical-stages.yaml` (en/es, original and modern skin) turns the option on with hidden content, captures the
tree of a page with the Mega box, opens `species_v`'s detail ("Mega") and `species_w`'s, turns it off and
captures the original tree with arrows, `species_w`'s detail, page 2 (Down from `species_s`, the last of its
column: the cursor falls on `species_o`, in the same column) and the return to page 1; `route-mega-step.yaml` captures
the tree with the Mega box and page 2 (`species_q`, which it evolves into first with `evolve` so its cell opens,
and its detail) with the option off, and with it on the four Megas on one page and `species_t`'s detail
([vertical slice](../../guides/vertical-slice.md)).

## Rare items

An optional improvement, off by default: the `rare_items` setting (row "Rare items", the last
of the Game tab of the pause menu's options and the title's, saved in the user's settings) arrives
from `PlaySession` (`RareItemsSetting`) at `FieldItemsAdapter::setRareItems`. On, and only in a game with
hidden content (rulesets `oculto` or `oculto_mega`, which bring the `rare_items` section), some random item
spots may leave a rare evolution item instead of their own
([evolution.md](../game/evolution.md#rare-evolution-items)). Off, or with the `original` ruleset, the field's
items are the original's. Its help (`pause_menu.help.rare_items`) warns that it needs hidden content.

## Quick battles

An optional improvement, off by default: the `battle_speed` setting (row "Quick battles" of
the Game tab of the pause menu's options and the title's, saved in the user's settings) has three
values: `normal` (the original), `key` (each battle starts at its speed and the `battle_speed` action, `R` or the
gamepad's right trigger, switches the double on and off) and `fast` (starts at double and the same key turns it off). It is carried by
`BattleSpeedChanger` (`battle_speed.hpp`) in the loop of `game.cpp`.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Battle speed | One logic tick per loop frame (`battle_main.c: BTL_battleTickFrame`); the game clock is stopped during battle | With double on, each clock tick runs two battle ticks (`ticksPerTick`), and what is drawn between ticks advances the same. Each tick is the original's: damage, AI, gains and game time do not change; only half the waiting. Sound and play time (`countPlayTime`) follow the real clock | "fast runs a fight at two ticks a frame, never the field, and the key switches it off" (`battle_speed_tests.cpp`) | Improvement (option) |
| Reaction moments | The player charges the finishing blow by pressing (`command_menu.c: BTL_tickFinisherChargeup`), revives the fallen partner before the count ends (`BTL_tickDeathCountdown`) and throws items with the bag open | While the finishing blow is being charged, the fallen partner's count runs, the bag is open or there is an item in hand or in the air, the battle goes at the original's speed (`BattleScreen::reacting`), so double never takes away time to react | "a fight waits at the original's speed while the player has to react" | Improvement (option) |
| Indicator | — | With double on, the battle document shows "x2" at the bottom right (`battle.fast`) | live | Improvement (option) |
| Field, menus and scenes | — | Only the battle (from its start to its final box, `BattleScreen::fighting`) goes at double; the field, menus, scenes and tournaments outside battle go at their speed | "fast runs a fight at two ticks a frame, never the field…" | Improvement (option) |

## Rivals' life

An optional improvement, off by default: the `enemy_hp_bars` setting (row "Enemy life bars" of
the Game tab of the pause menu's options and the title's, saved in the user's settings)
arrives from `PlaySession` (`EnemyHpBarsSetting`) at `BattleScreen::setEnemyHpBars`; the rule is
`screens::enemyHpBarShown` (`enemy_hp_bars.hpp`).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Rivals' bars in a wild battle | `battle_main.c: BTL_addEnemyHPBars` (only `BTL_initializeCombat` calls it) adds the object that draws, every frame, each rival's bar a while after taking damage (`BTL_renderEnemyHPBars`, 21 frames of `enemy_hp_bar_frames`) | Same without the option (`Battle::hpBarShown`) | "the original's bars: a wild enemy's a while after a hit, never a cup's" (`enemy_hp_bars_tests.cpp`) | Same |
| Bars in a cup battle | `std_main.c: STD_initializeCombat` does not add those bars: the cup only has its fixed panels for the two fighters | Without the option, no bar over the rival in the cup (before dwe drew it after each hit, as in a wild battle) | the same test | Same |
| With the option | — | A bar over each rival still standing (alive and not fallen) the whole battle, also in cups; with the original skin it is the original's bar with its line, with the modern one a compact bar with life in numbers (`#hp-bars.always`, setting `battle.hp_bars.always`) | "enemy life bars: every enemy still standing, cups too, and none once it falls" | Improvement (option) |

## Gym sessions

An optional improvement, 1 by default (the original's): the `gym_sessions` setting (row "Gym sessions" of the options,
from 1 to 5, in the Game tab of the pause menu's options and the title's, saved in the user's
settings) arrives at `GymScreen::setSessions`; the rule is `GymBatch` (`gym_batch.hpp`).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| One session per dialogue | The course machine asks (Normal / Roulette / Quit) and the script sets the course and the roulette (variables 254 and 246) and calls routine 7 (`script_interp.c`), which runs a single session; for another you have to talk again | Same with 1 (`GymScreen::startChosen`) | "at the original's one session a course never starts again" (`gym_batch_tests.cpp`) | Same |
| Several sessions in a row | — | With 2 to 5, when a session the story started ends, its stats box closes and the field is free (no script, text, menu, battle or partner scene: poop, toilet, eating, praising, sleeping, evolution, death), the same course starts again with the same roulette, until the number is completed (`GymScreen::continueBatch`). Each session is one of the original: same gains, same time, same tiredness and whatever the partner does afterward | "a batch runs its course again each time the field is idle…", "a batch waits while something holds the field" | Improvement (option) |
| Waiting for the partner | — | If between two sessions the partner shows a need the player can attend to (hunger, toilet, sleep, tiredness), the batch is not cut: it waits for the player to attend to it (feeding it, the poop or the toilet, sleeping, resting or an item) and goes on with the sessions that remain as soon as the need disappears and the field is free, as long as the player stays on the map and near the machine (`GymBatch::waitingForCare`). Tiredness counts as a need because it is what the training itself leaves: the batch waits for it to go down instead of training with it | "a hungry partner pauses a batch, which goes on once it is fed and the field is free", "a batch waiting for the partner stops on another map" | Improvement (option) |
| Cutting the batch | — | The batch ends if the player cancels a session before the prize (cancelling during the stats count does not cut it), if they change maps, if the partner is sulking, injured or sick (states that are not fixed next to the machine), or if the player walks away from the machine: the first frame after each session marks where they are and going more than 500 units (5 cells, `GymBatchLeash`) away from there cuts the batch; there is room to feed or clean up next to it, not to go to another part of the map | "a cancel, a sulking, injured or sick partner or a walk away stops a batch for good", "each session marks again where the player stands" | Improvement (option) |
| Console | — | `gym("<course>")` still runs a single session: only courses the story starts go in a batch | live | Improvement (option) |

## Battles without wait

An optional improvement, off by default: the `quick_battle_start` setting (row "Battles without wait"
in the Game tab of the pause menu's options and the title's, saved in the user's settings) arrives from
`PlaySession` (`QuickBattleStartSetting`) at `BattleScreen::setQuickStart`.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Wait before battle | `battle_setup.c: BTL_handleBattleIntro`: 20 frames of anger marks and then the banner, which lasts 200 frames or two more from when the tamer reaches their place, at 20 per second (measured, see [battle](../game/battle.md)): from 1.1 to 11 seconds with no way to give orders | Same without the option | — | Same |
| With the option | — | In a field battle (not in cups, which keep their opening) the opening is skipped on the same frame it would start (`BattleScreen::skipIntro`): the command strip opens, the tamer stays where they were in their battle stance with their saved path intact (a flee runs along it as always), the burst sounds and the battle starts. The battle itself is the original's | live | Improvement (option) |

## Difficulty

An optional improvement, `normal` by default (the original's): the `difficulty` setting (row "Difficulty",
values Normal and Easy, Game tab of the pause menu's options and the title's, saved in the user's
settings). `RuleOptions` (`rule_options.hpp`) changes values of the game's ruleset with the same changes as the tuning
panel (`RulesetStore::setValue`), so each system reads them like any rule; on going back to Normal each value goes back
to its own, and after reloading or changing ruleset the changes are made again (a value the panel changed in between
stays as the panel left it).

What Easy changes is given by the ruleset's `difficulty` section (`rules/original/difficulty.yaml`, schema
`ruleset_difficulty`): each level is a list of changes `{ section, path, value | percent }`, a new YAML value or a
percentage of the ruleset's value, rounded down. A mod adjusts difficulty with its own section. The
original ruleset:

| What | Normal (original) | Easy |
|---|---|---|
| Damage the partner deals (hits, finishers, blocks; also in cups) | 100 % (`battle.damage.partner_dealt_percent`) | 150 % |
| Damage the partner takes | 100 % (`battle.damage.partner_taken_percent`) | 50 % |
| Gains of each training session | 100 % (`training.gain_percent`) | 150 % |
| Time to feed it before the care mistake | 90 minutes (`care.hunger.window_minutes`) | 180 minutes |
| Part of the sickness probability that grows with the occasions | 5 (`care.sickness.tries_percent`) | 2 |

The two battle percentages and the training one are new ruleset values, at 100 in the original: the damage in
`attack_hits.cpp` (`damageOf`) and the gains in `strategies.cpp` are multiplied by them (rounded down, with
the usual damage limits). Tests: `rule_options_tests.cpp` (Easy's changes exist in the ruleset, are
applied, are kept after reloading and are undone).

## Skip evolutions

An optional improvement, off by default: the `skip_evolution` setting (row "Skip evolutions", Game tab, saved in the user's
settings). With it, confirming or cancelling during the evolution scene
([The evolution scene](#the-evolution-scene)) ends it at once (`EvolutionScene::skip`): the
field is restored and the new form is drawn. The result is the same: the evolution is already done in the partner's state before
the scene, which only shows it. Without the option the original's scene lasts its 0x162 frames and takes no buttons.

## Cancel closes choices

An optional improvement, off by default: the `choice_cancel_closes` setting (row "Cancel closes choices", Game tab,
saved in the user's settings) reaches
`game::story::Dialogue::setCancelClosesChoice`.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Cancelling a choice with an end marker | `script_interp.c: tickSelectionDialogue`: the cancel continues after the marker `0x12 0x00` that closes the choice | Same: the `else` arm of `story.set_selection` is the code after the marker | `lua_emitter_tests.cpp` | Same |
| Cancelling a choice without a marker | 23 of the 330 choices lack it: the cancel continues in the code that follows, the first option's (in the training manual's sale, cancelling buys) | Same without the option | "a selection without its end marker asks before its cancel arm, and reads back so" | Same |
| With the option | — | dwx writes at the start of the cancel arm of those choices `if story.cancel_closes_choice() then return ENDED end` (`CancelGuard`, see [scripts](../../formats/scripts.md)): with the option the script ends there and the choice just closes | the same test | Improvement (option) |

The option needs content extracted with `ExtractorVersion` 13 or later (modules `-- dwx lua 4`): the game
reads the player's disc again by itself when its content is of another version. Older content (`-- dwx lua 3`) still
loads and plays like the original, with the option having no effect.

## Quirks as options

Optional improvements, off by default (the original's quirks are kept): five rows of the Game tab,
saved in the user's settings, that turn off a quirk or a rule of the game's ruleset with `RuleOptions`,
like difficulty.

| Row | Setting | Value it turns off | Original |
|---|---|---|---|
| Fair sickness chance | `fair_sickness_chance` | `care.quirks.sickness_chance_wraps` | `tickSicknessMechanics` stores the probability in a `uint8_t`: a negative one wraps around and the roll always succeeds. With the option a negative one does not make it sick |
| Fair evolution average | `fair_evolution_average` | `evolution.quirks.selection_average_carries_over` | `getRookieEvolutionTarget` and `getChampionEvolutionTarget` only reset the sum and the stat count when a candidate becomes the best; otherwise they carry over into the next one's average. With the option each candidate is measured by its own |
| Fair move forgetting | `fair_move_forgetting` | `partner_moves.quirks.forget_mask_16_bits` | `unlearnMove` (`utils2.c`) clears the technique's bit with a 16-bit mask, so with it the technique 16 places further along in its 32-bit word goes away. With the option only the chosen one is forgotten |
| Card stock without repeats | `fair_card_stock` | `card_shop.quirks.stock_repeats` | `dailyPStatTrigger` compares the slot with itself, so a card can fill two slots on the same day. With the option the repeated one is rolled again |
| Choose the egg after losing all lives | `choose_egg_after_lives_lost` | `life.rebirth.random_egg_without_lives` | Without lives, section 0x49 of script 212 does not open the box and `DOOA_getSequenceState` rolls the egg (`rand() % 4`). With the option the box opens before the rebirth and the chosen egg counts; the rest of the section does not change |

Test: `rule_options_tests.cpp`.

## Talking from farther away

A tester could not talk to a character while standing next to it. Measured in the original (`tamer.c: checkMapInteraction`,
`map.c: entityCheckCollision`, `entityCheckEntityCollision`, `isRectInRect`, `entityIsInEntity`) and compared with
`game::field::FieldNpcs::touchFromPlayer`: dwe does the same, so it is not a bug.

| What | Original | dwe | Status |
|---|---|---|---|
| What is compared | The player's square (half side = its radius, the `radius` of its species in the original's table, 100) against the NPC's (its species' radius), centered on their positions; no angle cone: any side that touches counts, edges included | Same (`touchFromPlayer`, `npc.half`, `player.radius`) | Same |
| Reach when confirming | Only standing still (`animId == 0`) and with confirm just pressed: the square grows 50 toward the sides it faces (`rotation` in (0, 0x800) to −x, in (0x800, 0x1000) to +x, < 0x400 or > 0xC00 to −z, in (0x400, 0xC00) to +z; diagonally, on both axes) | Same (`talk_reach: 50` of `field/rules.yaml`, by the vector it faces) | Same |
| While walking | No reach: the square counts one step of its animation ahead | Same (`ahead` in `Field::touchNpcs`) | Same |
| Several NPCs | The first of the table that touches | Same | Same |
| Signs | The confirm cells (80–109) only under the player's position (`getTileTrigger`) | Same (`Field::checkConfirm`) | Same |
| NPC and cell at once | The NPC first: `callScriptSection` sets `IS_SCRIPT_PAUSED` to 0 and that frame's cell is lost, even if the NPC's section is empty | Same (`Field::touchNpcs` before `checkConfirm`) | Same |

So, from the side or with your back to an NPC, less than a step away but not touching it, the original does not talk either: the walking
collision stops the player up to one step before (running, 870 / 20 = 43.5 units).

An optional improvement, off by default: the `generous_talk` setting (row "Generous talk range"
Game tab, saved in the user's settings) reaches `game::field::Field::setGenerousTalk`. With it,
on confirming (standing or walking) the player's square grows 45 on all sides (`GenerousTalkAround`: more than the
43.5 step that the collision can leave, so next to an NPC you talk whichever way you face) and 100 toward where it faces
instead of 50 (`GenerousTalkAhead`, double the original); and if the player's cell is not a confirm one, it looks at the
cell 100 units ahead (`GenerousSignAhead`, one cell: the sign in front of them). Tests: "a confirm
talks to an NPC the player's square reaches, 50 further the way it faces" and "with the generous talk range a confirm
reads the sign's cell in front of the player" (`field_tests.cpp`).

The improvement only reaches what the original does not: first the NPC within the original's reach, then the confirm cell under
the player, then the one ahead and at the end the NPCs only the wide reach touches (`Field::touchNpcs`,
`Field::confirmCell`). A tester could not talk to the large NPC of the Ancient Glacier Region (koda02, radius 400 at
x 1439, z −2237: its square starts at x 1039) with the improvement on: what it says is in confirm cells 82
on its side (rows 73–77, columns 57–64; section 0x52) and its own talk section (7) is empty. Measured in the
original facing it: from x 850, z −2450 (850 + 100 + 50 = 1000, does not reach 1039) the dialogue (section 0x52) comes out;
from x 900 the confirmation goes to the NPC (section 7) and nothing comes out. Before, with the improvement, the enlarged square (850 + 245)
touched the NPC from almost all those cells and took them away. Test: "the generous talk range leaves a large NPC's
confirm cell to the player standing on it". The same case of an NPC next to confirm cells occurs at shop
counters and in other maps (67 NPCs in 45 maps have a confirm cell within the improvement's reach); in all, with
the improvement, the cell under the player wins as in the original.

## Online rooms

"Multiplayer" in the title menu opens the **Multiplayer** hub (`ui/online_hub`): Online versus, Co-op (Search rooms),
Profile and Back, over a card with the player's online name, icon and versus record. Co-op leads to **Search rooms**
(`ui/online_rooms`), and from there the **Room** lobby (`ui/online_room`). Every one is its own document, read only
through the UI API's `online` namespace ([UI API](../../modding/ui-api.md)), with a look of its own in each skin; the
co-op lobby that lived inside the main menu is now these screens.

- **Profile** (`ui/online_profile`): the online name (typed in place; Enter or the pad's confirm saves it, cancel
  erases a letter, and an empty field ends the edit), the icon from a fixed list (egg, device, crown, swords, tree,
  dumbbell; left and right pick it), the connection (Direct, or Relay only: confirm, left or right switch it) and the
  versus record (wins and losses). `OnlineProfile` (`online_profile.hpp`) keeps it in the game's settings
  (`settings.yaml`, `online_relay_only` for the connection), not in a save, and its name is the player's name in every
  room (12 letters at most; «Player» until one is typed). Relay only connects co-op and versus rooms through the
  signaling server's relay (TURN) alone, so the other player never learns this machine's address; a server with no
  relay makes such a room fail with «no relay».
- **Connecting and errors** (`ui/online_status`): while a room is being created or joined the screen says so with
  moving dots and offers Cancel; when it fails it shows why in words (the server cannot be reached, the room is full,
  the code is wrong...) with Retry (`online.retry` asks for the same room again) and Back.

- **Search rooms**: the public rooms (name, mode, players out of capacity, ping bars; a full room is shown but disabled),
  five rows with scroll arrows, a «Search room» filter, «Join by code» for private rooms and friends, «Create room»
  (asks Public, listed, or Private, by code only; Public first) and «Find match» (joins the first open room of a mode
  this game can play, or offers to create a public one). Prompts: Accept, Back, Refresh (`tab_next`), Find match
  (`tab_previous`). Text fields take the keyboard while edited; a pad cycles the code's letters with up and down.
- **Room**: the room's card (scene, name, code, host, mode, state, players, public or private), the mode list for the
  host (Campaign is co-op, Battle is versus; Training and Free say they are not available yet) or the chosen mode for a
  guest, the players (up to four) with the host's crown and the guests' ready marks, Start (host) or Ready (guest),
  the code chip on both, Leave room and a notice of what happens next. The host has no ready mark: it starts. Cancel
  goes to Leave room first, so a stray cancel does not drop the room.

`OnlineRooms` (`online_rooms.hpp`) publishes the `online.*` settings and takes the screens' steps. Two seams keep the
network side apart: `RoomDirectory` lists public rooms and `RoomBackend` hosts, joins and runs the room. The room
steps are the co-op lobby's own commands (`coop_host` with public or private, `coop_join` with a code, `coop_ready`,
`coop_start`), which `CoopRoomBackend` (`online_coop_room.hpp`) hands to `CoopScreen`; `online_leave` drops the link
(the lobby's `coop_close` keeps a connected peer for the save step). `RoomListing` carries the fields of the
directory's listing (room id, join code, name, message, host name, mode, players, capacity, created at, state waiting,
full or starting) plus the ping, which the directory does not send yet.

The room's phase is a `net::RoomSession` (`dwe/net/room_session.hpp`): Connecting, Open (the host has its code),
Connected (the other player arrived), Ready, Playing and Closed. It follows the link, and the lobby says the rest: the
guest's ready mark (or its taking it back) makes it Ready, the host's start and the guest's arrival in the host's world
make it Playing, leaving or a failure close it with the error. `CoopConnection` owns it and the Room screen shows its
phase as is. A public room's host tells the directory each step (`ready`, `playing`, `closed`, with the room's token),
so the list shows the room full and drops it once it plays; a notice that fails is logged and the entry expires on the
server. The host's name reaches the guest at the end of the co-op welcome, so the guest's Room screen and name tag show
it (an older host sends none: «Host»). `VersusFlow` keeps a room session too, from the code to the series' end.

With a signaling server configured, `SignalRoomDirectory` (`online_signal_directory.hpp`) first asks the server what it
can do (`/v1/info`; one without public rooms leaves the list unavailable), then lists the public rooms of this game's
compatibility key (`/v1/rooms?compat=`, from the co-op protocol's game version and content sum: `net::compatKey`). A
room created as Public is hosted with `public=1` and the host's name, icon, key and mode, so it shows in that list; a
Private one is reached by its code only. Rooms already playing are not listed, and a list refreshed too often says so
(`list_limit`). Without a server the list says so and points to the code. The server sends no ping, so a listed room
shows no ping bars. A co-op room holds the host and one guest, so its
rooms say capacity 2 and only Campaign is playable. With the developer tools on
(or `--online-sample`), the menu key switches to the sample source (`SampleDirectory`, `SampleRoomBackend`): seven
sample rooms of up to four players, a hosted room that two sample guests join and ready, marked «Sample data» on screen; it
never starts a game. Tests: `apps/game/tests/online_rooms_tests.cpp`, `apps/game/tests/online_profile_tests.cpp`,
`packages/engine/ui/tests/online_screens_tests.cpp`.

### In the co-op game

`CoopHud` (`coop_hud.hpp`) shows the `coop_hud` document, with its own look in every skin, while it has something to
show:

- **Name tag**: the other player's name floats over their head while the field runs (it hides with the HUD's panels in
  menus, talks and battles, and off the window). It is the other player's online name: the guest's from the `Hello` of
  the co-op link, the host's from its `Welcome`; without one, «Guest 1» on the host and «Host» on the guest.
  `FieldScene::coopPeerHeadInWindow` projects the guest's character (host) or the presented host (guest) with the
  map's camera.
- **«`<name>` disconnected.»**: when the guest's link closes during play, the host gets this notice for a few seconds
  and the tag goes; the guest's slot stays for a reconnect.
- **The host left**: when the host's link closes during play, the guest's game ends (it was the host's world), the
  main menu comes back and the notice «The host closed the room. You are back at the menu.» shows over it.

The game loop (`game.cpp`) hooks both where it drops the gameplay bridge (`coopLinkLoss`). Tests:
`apps/game/tests/coop_hud_tests.cpp`.

## Online versus

"Online versus" appears in the title menu (with the modern skin it is its own row;
with the original, "Battle Mode" asks whether to play it). The original's versus was two gamepads on one console; here it is
two machines: each player brings the partners they registered in their game's registration machine, and the battle
runs on both at once in lockstep ([versus](../game/versus.md), [net](../engine/net.md)). The title
demo does not change: it uses no network.

`VersusFlow` (`versus_flow.hpp`) is the flow without UI and `VersusScreen` puts it in the `ui/versus` document with real
WebRTC links (`net::RtcPeerLink`, with the signaling server from `versus/network.yaml` or from
`--versus-server wss://…`); the tests use two flows over `net::LoopbackNetwork`. None of this writes the game: the original
does not save anything from versus either.

To try it with a single machine, `--versus-loopback` (a development option, `VersusLoopback`) puts the other player in
the same process: the two flows talk through a `net::LoopbackNetwork` with its own clock, and the other player joins
the room that is created, brings the first game with enough partners, picks the first ones and gives no orders (its
fighters act on their own). Battles run in lockstep as online. Each time versus is opened there is a new other
player. That network's clock advances at most a quarter of a second per tick, so pausing the game or a slow load
does not cut the series for silence.

1. **Host a series**: the host chooses 1, 3 or 5 battles and the stage (the original's three coliseums). The screen says what is going on (creating the room, waiting for the
   other, opening the direct connection) and shows large the room's code, four characters, to give to the
   other player. No ports need opening: the server only introduces them and the game goes directly between the two.
2. **Join a series**: the guest types the code (letters and digits, case-insensitive); a code that
   cannot be one stays on the screen with a notice, and Back deletes the last letter.
3. Both choose the game they bring partners from (a game needs as many registered as the series has
   battles) and one partner per battle, without repeating.
4. Each battle is seen in the arena like the original: the entrance of the two fighters with their data and their music, the
   "BATTLE" banner, and the battle, in which both move, animate and throw their techniques with their effects, sparks
   and statuses, with the versus camera. On top, the original's versus HUD: per side, the HP and MP bars with their
   icon and their number, the letters of "FINISH!!" and the "PUSH" box, the name of the pointed-at command and the strip of icons of
   the commands and techniques; the clock with 1P and 2P in the middle. This side chooses with left and right and accepts; the
   finisher goes with its key (`extra`). When one falls, "1P WIN" or "2P WIN" in 3D and the winner's pose; when time
   runs out, TIME UP. Orders travel with 3 frames of delay; if the other lags, the battle waits and says so.
   Back twice abandons the series.
5. Between battles, the original's scoreboard: one row per battle and side with the fighter's face and a circle or a
   cross, the row of the battle that ends comes out, flashes and returns, and the wins and losses of each side; at the end, the
   winner's letters of the series above it.
6. If something fails, a clear message: no server configured, the server cannot be reached, no room with that code,
   the room is full, time ran out to connect, no direct route (with the suggestion to try from another
   network), the connection was cut, the host does not respond, the other left, another version, other content, a partner
   this content does not have, desynchronization or a damaged message.

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Number of battles | `vs_select.c: VS__tickSelectMode`: 1, 3 or 5 (`VS__BATTLE_COUNTS`), and 3 or 5 only if both registered at least that many | The host chooses from the ruleset's `versus.series`; since it does not yet know the guest's game, a game with fewer registered than battles cannot be chosen | `vs_select.c: a partner is picked once and a save needs one per battle` | Assumption (adapted to two machines) |
| One partner per battle, no repeats | `vs_select.c: VS__isAlreadySelected`, `VS__handleDigimonSelected` | `VersusFlow::pickPartner`; Back removes the last | the same test | Same |
| Choosing the arena | `vs_select.c: VS__tickSelectMap`: "STAGE SELECT", three coliseums (`VS_ARENA_MODELS`: T_TOGI, B_TOGI and E_TOGI), left and right wrap around | The host chooses it in their setup, a row with the arrows next to the value (`versus.arenas` of `battle.yaml`, the third arena in `tournament.yaml`); it travels in the room's `Setup` and both machines load the same one. Differences: it is a text row, without the screen of the three images; the first's name cannot be read in the original's capture and here it is "Coliseum" | `vs_select.c: the host's arena and each battle's entrance and score, on both machines` | Same (adapted to two machines) |
| Number of battles in the setup | — | "1 battle" / "N battles", with the arrows next to the number like the other selectors | — | Improvement (text) |
| The whole series | `vs_scene.c: VS_initializeVS` | `VersusFlow` over `session::VersusSeries` | `two machines play an online versus series from the menus to the result` | Same (the rules); own presentation |
| Abandoning and network failures | — (one console) | Messages by reason, timeouts of 10 s and 8 s, 30 s to open the route and rooms of 5 minutes | `a code that cannot be one stays on the entry...`, `a player who leaves a battle ends the series...` | Improvement (online) |
| Versus demo | `VS_playDemo` only exists in the Japanese trial version | The title demo stays as it was, without network | — | Does not apply |
| Arena loading | — (the original loads versus before its menu) | The versus screen opens without loading the arena (35 ms in Release; before, 1.7 s all at once). `VersusScreen` builds it while the players connect, one tick after the wait is seen (1.5 to 3 s in Release), and during the wait it loads the models of the other arenas, one per tick, so the battle does not wait for its own | — | Improvement (loading) |
| Battle in the arena | `vs_scene.c: VS_initializeVS`, `VS_loadVSAssets`, `VS_loadFighterEntities`; `vs_combat.c: VS__combatMain`. Measured in the original (state `vs_fight`): `VS_BATTLE_SETUP.stage` = 0, the first cup's arena; in 40 vblanks the two entities change animation (46 → 40 and 34 → 40) and place | `VersusArena` (`versus_arena.hpp`) draws the chosen arena in its own `FieldScene`, that of the story battles: the host's fighter as partner and the guest's as the stage fighter, with their clips, technique effects, statuses and sparks (`battle_scene_view.hpp`) and the versus camera (`game::tournament::MatchCamera`). Before the first frame it prepares each battle the same on both machines, with what only the content gives (the models' clips and the effect scripts' flights); the camera pulls on its own numbers and the attacker's sparks are not shared | `vs_scene.c: VS_initializeVS: both machines show and ready each battle alike` | Same |
| Entrance and banner | `vs_intro.c: VS__runIntro` (the cup's entrance: each fighter walks in with their data and their music), `vs_combat.c: VS__combatSetup` (the "BATTLE" banner without the anger marks, which bursts at 60 frames, sounds 0x16, 0x10 and 0x11, clip 0x21) | `VersusArena`: the cup's `MatchIntro` and `BattleIntro` with `skipAnger` and `burstAfter(60)`; while it is presented, the flow does not advance the lockstep (both machines wait the same) and accepting shortens the entrance | `vs_combat.c VS__combatSetup: a versus banner opens at once and bursts after 60`; the two-machine test | Same |
| Music | `vs_scene.c: VS_loadVSAssets`: each arena's bank (30, 32, 31) and its entrance, battle, victory and defeat tracks | The versus's `SessionMusic` with the tracks of `tournament.yaml` (`music_font` per arena) | — | Same |
| Battle HUD | `vs_hud.c: VS_renderFighterStatusBars`, `VS_renderHPBarDigits`, `VS_renderCommandMenu`, `VS_renderMoveName`; `vs_main.c: VS_COMMAND_NAMES`. Measured in the capture `versus2-original-fight.png` | `ui/versus` (`#hud`) with the story's sprites (frame and icons of BI_IN, "FINISH!!", "PUSH", BTLCOMND, the cup's clock) and the clock's 2P piece; the commands with the versus names ("Auto", "Defensive"). Differences: the rival's strip points at its current command (the other's cursor does not travel over the network); the letters' flash on filling is not there and "!!" appears at once; the local side's "PUSH" box carries the finisher key instead of the square button; in a wide window each side stays at its edge at the original's distance, as in the story | `vs_combat.c: VS__tickInput: left and right go round the commands past the finisher`; the two-machine test (icons, pointed-at, name) | Same; Improvement (widescreen) |
| Camera | `vs_combat.c: VS__tickBattle`: the cup's, which returns to the orbit as soon as nobody attacks or is sent flying | `MatchCamera` with `Kind::Versus` | `match_show_tests` | Same |
| End of each battle | `vs_combat.c: VS__deinitializeCombat`; `vs_hud.c: VS_addResultModelScene`, `VS_tickResultModelScene` (1P2PWIN.TMD; DRAW with both fallen), TIME UP, the winner's pose 0x2a | `VersusArena`: `VersusWinLetters` or the draw letters, TIME UP with the cup's box, the winner's pose 140 frames and its track; the techniques' effects go on until they end (`vs_combat.c: VS__deinitializeCombat` continues with `VS_tickFrame`) and the scoreboard removes them; it is seen between battles, when the result is already settled on both machines | `match_show_tests` | Same |
| Scoreboard between battles | `vs_scene.c: VS_playVersusIntroSequence`, `VS_tickVersusIntro`, `VS_renderVersusIntro` (TAISEN1, TAISEN2 and 16TAISEN; panels 0x5b7080 and 0x805b5b; the row comes out, flashes and returns; at the end the series winner's letters from frame 0x3c) | `VersusScoreShow` and `#score` of `ui/versus` with the sprites of STDDAT. Differences: the panels go at half opacity with the color doubled (over black they give the original's, and the 3D letters, which here end up under the UI, are seen through); "Win" and "Lose" and their numbers are text in the player's language (the original's text texture is not identified) | `vs_scene.c VS_tickVersusIntro: each row slides out, flashes, slides back and the next` | Same, with the differences noted |

With direct UDP it was tested live with two games on the same machine: joining, choosing, orders from both sides and one
leaving, which the other sees as "The other player left the series." With WebRTC the connection is tested with
two real `RtcPeerLink`s and an in-memory signaling server (`dwe_net_tests`); the live test of two
games waits for the server to be deployed.
