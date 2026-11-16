# fishing

Fishing (`dwe::game::fishing`, `game` layer): the rod, the bobber, the line and the fish in a map's water. What the
original did is in the [fishing sheet](../../reference/fishing.md).

It draws nothing and does not know the bag or the partner: it depends on `core`, `assets` and `ruleset`. The numbers come from the
ruleset's `fishing` section (`rules/<id>/fishing.yaml`) and the `data/fishing` tables, which `dwx` extracts and the session
resolves into `FishingTables` and `FishingPlace` (items by their number, `ItemEntry::index`). The game does not pass tables to
`createGameSystems`, which reads `data/fishing` from the packs as it reads the cards; before, no map had water and the pause
menu never offered Fish (`route-stage7-fishing.yaml`).

## Usage

```cpp
auto rules = dwe::game::fishing::parseFishingRules(*ruleset.section("fishing"));
dwe::game::fishing::Fishing water{*rules, tables, place};
water.start(dwe::game::fishing::Rod::Old);
auto out = water.tick(input, view, random);   // one frame of the original (20 per second)
```

- **Frame**: `tick` moves the rod (`FISH_tickRod`) and then the water (`FISH_tickFishingWorld`). It is called only with
  no notice open; the notices come out as `Message` events. With the rod put away (without `start`) it only moves the
  water: the app calls it on every frame of the map, not just when fishing (see "The water without the rod").
- **Screen**: `FishingView` goes from the world to the background's screen (pixels from its optical center, and downward)
  and back; the fish zones and the water are on that screen, as in the original.
- **Input**: `FishingInput` brings the frame's buttons and where the rod tip, the hand and the bait are, which the
  player's model sets.
- **Output**: `FishingOutput` brings the events (sounds, player animations, notices, the bait menu, the bait
  eaten, the catch, what is hooked, the sea serpent on land, the end) and the player's turn in original units.
  The session applies to the bag and the partner what applies (`session::FishingAdapter::apply`).

## The water without the rod

In the original the water is one more field object from the moment the map loads (`checkFishingMap` → `FISH_init` →
`addObject(FISH_tickFishingWorld, FISH_renderFishingWorld)`), not part of the minigame: the fish and the serpent swim and are
always seen in the lake. The app does the same (`PlaySession::tickWater` → `FishingScreen::tickWater`), after the
field and the session on each tick, as `tickObjects` runs the objects in the order they were added (the player and the
partner before the water); this way the fish's rolls come after the field's on each frame.

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| The water advances 20 times per second from entering the map, with the rod put away, while walking and with the pause menu open | `FISH_init`, `FISH_tickFishingWorld`, `tickObjects` (`gameLoop`, `pauseFrame`) | `PlaySession::tickWater`, `FishingScreen::tickWater` | `fishing_water_tests`: rod away | Same. Measured in the lake (`fish_mayo06_rod_ready`, rod put away): 32 fish in the pool; its frame counter goes up 20 every 60 vblanks while walking and also with the menu open. Before, dwe only moved and drew the fish with the rod out |
| The fish, the serpent, the ripples and the marks are always drawn; the rod, the bobber, the line and the bait only when fishing | `FISH_renderFishingWorld` | `FishingScreen::showPrims` | capture `w20-fishC` | Same. Fish as dark blue blotches next to the shore in both |
| With fishing turned off on the map it does not move, roll or draw | `FISH_tickFishingWorld`, `FISH_renderFishingWorld` (`fishingDisabled`) | `Fishing::tick`, `showPrims` | switched off | Same |
| While the partner evolves (state 13) the water neither advances nor is seen | `FISH_tickFishingWorld`, `FISH_renderFishingWorld` (`PARTNER_STATE == 0xd`) | `PlaySession::tickWater` → `hideWater` | - | Same in the rule; not measured |
| In combat the water continues: it advances and is drawn as on the field, and after combat it continues the same (the map is not reloaded) | `BTL_battleTickFrame` (`tickObjects`, `renderObjects`), `startBattle` | `PlaySession::tickWater` | capture `w21-fishD` | Same in the rule. No battle in the game happens on a map with water (`mayo06` only has the friendly serpent, `mayo10` no NPC). Measured by forcing one (lake water set when the `species_bg` battle in `mayo02` starts): `FISH_tickFishingWorld` runs from `tickObjects` while the battle loads (`GAME_STATE` 3) and, as soon as the battle code (`BTL_REL`, at 0x80052EE0, 0x2349C bytes) overwrites the fishing code (`FISH_REL`, at 0x80070400), its call to `FISH_isTextBoxVisible` jumps into the middle of `BTL_initializeItemParticleVelocities` and the game hangs in the BIOS. dwe has no such limit and follows the code's rule. Before, the water stopped in combat |
| When the partner dies of old age on a map with water, the water disappears (fish, serpent, ripples) until a map is loaded again | `partnerTickDying` (`deinitializeFishing`) | `DeathScene::begin` → `FishingAdapter::leaveWater`, `FishingScreen::tickWater` | `fishing_adapter_tests`: natural death | Same. Measured in the lake (`fish_mayo06_rod_ready`, life 1 at 9:59): on the first frame of `partnerTickDying` `FISHING_DATA_PTR` becomes NULL and the serpent's stripes leave the lake; the death ends on another map (`room08`). Losing a life in combat or from sickness (`MURD_tick`) does not remove it. Before, dwe only turned it off (`setFishingDisabled`) and kept drawing it still |

## Camera, line and sea serpent

The fishing camera is the original's: `Fishing` keeps the view's center in background pixels
(`cameraCentre`) and the app centers the map camera there (`FieldScene::setCameraPixel`). The pure functions are in
`view.hpp`; the app draws the line and the serpent with `ScenePrim` (`FishingScreen::showPrims`).

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| When the rod comes out the view stays where the field left it; when it ends it recenters on the player | `FISH_enableCameraControl`, `FISH_applySavedCameraScroll`, `FISH_endFishing` | `Fishing::tick`, `endFishing` | fishing route | Same. Measured in the lake (background 1280 x 256): center (480, 8) in the original and in dwe |
| The view follows the bobber 16 pixels per frame with a 40 x 30 dead zone; the player, without a dead zone; the catch, 32 | `FISH_panCameraTo` and its call in each state of `FISH_tickRod` | `panCameraTo`, `Fishing::panTo` | `fishing_view_tests`: dead zone, radius | Same. Hanging bobber: the original raises the view from y 8 to 4, dwe to 6 (the model's hand is not the original's). In flight the view follows it frame after frame in both (original 480, 478, 473...; dwe 480, 476, 453... with another throw) |
| With the camera key (E; gamepad: North; the original's circle) the arrows move the view 5 pixels per frame | `FISH_panCameraWithDPad` | `panCameraWithDPad`, `Fishing::panWithDPad` | d-pad | Same. Measured: 45 pixels in 9 frames in the original; 50 in 10 in dwe; no return if the bobber stays in the dead zone |
| With a notice open the view goes to the bobber (or to the catch) 16 pixels per frame | `FISH_scrollViewToTarget`, `FISH_showTextBox` | `Fishing::scrollToNotice` | - | Same |
| The line goes from the rod tip to the bobber, clipped to the view's edge and in pieces of 64 pixels, in the color of the tension (white at rest) | `FISH_drawFishingLine`, `FISH_clipLineToRect`, `FISH_updateTension` | `fishingLine`, `clipLineToRect`, `tensionColor` | clip, pieces, colour | Same. The tip is the rod's (`FISH_updateRodPose`) |
| Fish appear in any zone of the background, not just in the view | `FISH_isScreenPointVisible`, `FISH_randomPointInSpot` | `CameraView::screenMin/screenMax` | fishing route | Same. Before, the view only covered what was visible and each zone outside it left "no water in sight" in the log; now none |
| The serpent: five pieces, each turned like the head one frame later than the previous, at 200, 175, 125 and 140 distance | `FISH_pushSeadramonSegment`, `FISH_drawSeadramon` | `pushSeaSerpentTurn`, `seaSerpentBody` | sea serpent | Same in shape. Assumption: its faces read an 8 x 8 piece of the texture page that is empty in the extracted image; white is subtracted, which gives the measured black silhouette. Each new serpent starts straight (in the original the history is global). The "thin black stripes" seen over the water at 8 and at 17 are this serpent seen from the shore: the original shows them the same (measured in `fish_mayo06_rod_ready` at 8: black stripes 1 to 2 pixels wide and about 70 long at 320 x 240; removing it, they disappear in both). Since dwe starts at 8:00, it showed up in all the tests |
| With the serpent on land fishing is turned off on the map and its scene starts | `tamerTickFishing`, `triggerSeadramonCutscene` | `SeaSerpentLanded` event (value: `sea_serpent.scene_section`, 0x4e4) → `StoryAdapter::request` | - | Same: section 0x4e4 of the header script jumps to 82 of script 7; the serpent comes out and "What the... oh no! It's the Lake Guardian!", as in the original |

## Marks, ripples, bait and notice

The bite marks and the ripples are two pools of 16 (`effects.hpp`: `MarkerPool`, `RipplePool`) that `Fishing`
advances in its water frame, before the fish; with a notice open they stay still. The app draws them with the
map's fishing image (`sprite_file` of `data/fishing`: `texture/etcdat/fishing2` in the lake) and its scale
(`FishingMapEntry::scale`), and the bait with its item's icon (`FishingScreen::showPrims`). Measured in the lake
(`fish_mayo06_waiting`, writing the original's pools).

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| When the bite roll lands on a fish a mark comes out: a heart if it can bite, a cross if not; it lives 10 and is seen 9 frames | `FISH_spawnMarker`, `FISH_tickMarker`, `FISH_tickMarkers`, `FISH_allocMarker` | `MarkerPool`, `Fishing::findFishToBite` | `fishing_effects_tests`: 9 frames, 16 at most | Same. Measured: life 10 at the roll, 9 at the end of that frame and 8 the next (the rod goes before the water in the same frame). 16 x 16 screen pixels, with its point at the bottom center, in front of everything |
| When a fish is hooked the marks are cleared | `FISH_clearMarkers` | `Fishing::hookFish` | 16 at most | Same |
| The ripples and splashes go through their table and are removed when finished; the scare to the fish lasts as long as its ripple | `FISH_tickRipple`, `FISH_tickRipples`, `FISH_spawnRipple` | `stepRipple`, `RipplePool` | each kind, scaring ripple | Same. The splash lasts 10 frames; without a free slot there is no ripple or scare |
| A ripple is a standing card on the water along x, additive, of the map's scale times its own | `FISH_drawRipple` | `addWaterSprites` | rectangles | Same. In the lake (scale 0x3800) a ring measures 3.5 units per texel: a few pixels, faint |
| The bait and the catch are seen with their item's icon, a screen-facing square 35 units on each side in the lake, only over the water | `FISH_showBaitSprite`, `FISH_drawBaitSprite` | `addBait` | fishing route | Same. Measured: the meat about 11 pixels near the bobber |
| The notice closes with Confirm after 10 frames open, or with Cancel at once, and sounds like confirm | `FISH_tickTextBox`, `FISH_closeTextBox` | `ui/fishing_notice/logic.lua` | `fishing_notice_tests` | Same. A Confirm before half a second does nothing and makes no sound |
| The bait menu lists one row per bag slot with food, in the bag's order and with that slot's quantity; two stacks of the same item are two rows | `FISH_openBaitMenu` | `session::baitRows`, `FishingScreen::openBaitMenu` | `fishing_adapter_tests` | Same. Measured: Sirloin 3, a non-food item, Meat 5, Sirloin 2 and Giant Meat 4 give Sirloin 3, Meat 5, Sirloin 2, Giant Meat 4. Before, dwe gave one row per item in the table's order |
| Each bait menu row puts the quantity after an x in two right-aligned slots: "x 3", "x12" | `FISH_renderBaitMenu` (`renderItemAmount(0, 2, ...)`) | `ui/fishing_bait/logic.lua`, `fishing.bait.entry` | `fishing_bait_tests` | Same. The original draws an 8-pixel x and the quantity with its digits ("Meat × 5" in `fish_mayo06_bait_menu`). In the original's font the code for × (0x817E, glyph 78) draws a triangle (and that of △, glyph 77, a cross), so with the original skin a triangle came out; both skins now use the x, which the font has |
| Choosing bait, canceling and the menu's sounds | `FISH_tickBaitMenu`, `FISH_closeBaitMenu`, `FISH_isBaitMenuVisible` | `ui/fishing_bait/logic.lua`, `Fishing::chooseBait` | - | Improvement: one column in pages of eight and the move, confirm and cancel sounds of the common interface (the original: two columns of nine rows and a single sound) |
| When the rod is put away or the map is left the bait menu and the notice close | `FISH_endFishing`, `FISH_closeBaitMenuIfOpen`, `FISH_shutdownTextBox`, `FISH_deinitialize` | `Fishing::endFishing`, `FishingScreen::enterMap` | - | Same |

## Rod, bobber and fish

The app draws the rod, the bobber and the fish like the original (`fishing_look.cpp`); the pure computations are in
`look.hpp` and the bobber's animation in `view.hpp`. Measured in the lake (`fish_mayo06_aiming`, `fish_mayo06_waiting`).

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| The rod is the model `ETCDAT/SAO.TMD` (4 pieces) hung from the player's bone 9; its joints bend toward the bobber according to the tension (128 at rest, from 0 to 512 with the tension); the tip is the start of the line | `FISH_updateRodPose`, `FISH_drawRodSegments` | `rodPose`, `rodBend`, `fishingRodPose` | rod: four segments | Same. Tip when aiming: (-90, 798, 124) from the player in the original, (-90 to -101, 795, 110 to 122) in dwe. The amazing rod copies rows 88 to 95 of its texture over 80 to 87 (`MoveImage`); dwe shifts the v coordinate 8 rows |
| On releasing the throw the rod takes the throwing pose before the bobber leaves the tip | `FISH_tickRod` (release) | `Fishing::release`, `throwFromTip_` | fishing route | Same with a one-frame delay (dwe applies the animation on the next frame). Tip on release: 36 behind and 814 up in the original, 96 behind and 797 up in dwe; before, 424 behind came out and the bobber fell on the shore |
| The bobber is a sprite from the fishing image, standing, at the map's scale (3.5 in the lake), one per rod (17 to 23 the old one, 24 to 30 the amazing one) | `FISH_bobberCommand` (command 1), `FISH_drawSprite3D` | `bobberSprite`, `spriteCorners` | each rod has its own sprites | Same: orange tip and dark body, about 3 x 3 pixels at 320 x 240 in both |
| The bobber hangs (1), sinks (1 to 5 and returns to 3), floats (3) and bobs (4, 3) once every 60 frames with a ripple; on a nibble it goes down (4, 5, 4) and on a bite it sinks (4 to 7 and disappears); on losing the bait it returns (7 to 3) | `FISH_bobberCommand` (commands 2, 7, 8, 9, 0xb, 0xc, 0xe) | `BobberAnimation`, `Fishing::tickBobberAnimation` | bobber: sequences | Same. While floating a point moves at random in x and z on half the frames |
| Each fish is a flat sprite over the water with its head toward where it swims: small (size < 100) with 4 frames every 4 and scale `fish_scale_small`; large with 8 every 3 and `fish_scale_large`; subtractive with shadow transparency × 30 / 128; the hooked fish that fights uses its own 6 frames | `FISH_drawFishSprite`, `FISH_drawFish`, `FISH_drawHookedFish` | `fishLook`, `hookedStruggles`, `addFishingFish` | fish: frames, scale, shade | Same. With the lake's grazing camera they look like thin dark blue stripes: in the original a size 206 fish next to the bobber takes up 37 x 3 pixels (prim read from the original); in dwe the small ones come out just as thin |
| Fishing is turned off while the partner evolves or dies and turned on again when the evolution ends, when the death light ends and when the rebirth ends | `setFishingDisabled`, `setFishingEnabled`; calls in `partnerTickEvolving`, `partnerTickDying`, `partnerTickDying2` | `FishingAdapter::setFishingDisabled/Enabled`, `EvolutionScene`, `DeathScene` | - | Same |
