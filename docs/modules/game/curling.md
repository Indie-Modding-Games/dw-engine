# curling

The stone game of the ice rink (`dwe::game::curling`, `game` layer), a kind of curling: the
player and the rival each throw five stones toward the target, between obstacles, and whoever scores more points wins. It
draws nothing: it depends on `core`, `assets` and `ruleset`. The numbers come from the ruleset's `curling` section
(`rules/<id>/curling.yaml`): the types and weights of the stones, the friction, the force, the rings and their points.

## Usage

```cpp
auto rules = dwe::game::curling::parseCurlingRules(*ruleset.section("curling"));
dwe::game::curling::Match match{*rules, dwe::game::curling::Opponent::Penguinmon, random};
auto events = match.tick(buttons, random);   // one frame of the original (20 per second)
if (match.finished()) { /* match.result(): 2, 1 or 0 */ }
```

- **Start**: the script opens it with `call_partner_routine(1)` when stepping on the rink's starting square (frzl17,
  section 0x33) after talking to the rival (variable 113 to 1, variable 122 with the rival: 0 the first, 1 the
  second). The rival takes the player to frzl17 through exit 1, which leaves them on top of that square: the match
  starts on arrival, without moving. `session::CurlingAdapter` creates the match and `apps/game/src/curling_screen.cpp` plays it in place of the
  field.
- **Controls** (keyboard first): Up/Down (W/S or arrows) choose stone and option, Left/Right (A/D or
  arrows) turn the throw (Up/Down scroll the view while aiming), Enter or Space confirm (stone, direction and force when the indicator is where you want it).
  On a gamepad, the D-pad and the confirm button.
- **End**: the result goes to variable 121 (`KAR_finishMatch`): 2 if the player wins with 10 points or more, 1 if
  they win, 0 if they lose or tie. The script does the rest: with 2 it gives a medal (only once) and, if the player
  wins, the first rival joins the city (trigger 257).

## Parity with the original

Status: **Same** (does the same as the decomp), **Assumption** (the decomp is not clear; the most
likely was chosen and it is marked in the code), **Gap** (missing).

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| The match starts on arriving at the rink | `script 131` section 0x33, `checkMapInteraction`, `map_collision.c: getTileTrigger` | `Field::checkTriggers` reads the original's cell (`originalCellAt`) | `field_tests`: "a step cell is the one the original reads ..."; route `stage5-curling` | Same. Measured in the original (the MCP's `curling_start` state): on entering through exit 1 the question appears without moving. Before, the player stayed on the rink with nothing happening until they left the cell and came back |
| Stones of each team (type by roll on the table, weight by type) and 0 to 2 obstacles | `kar.c: KAR_setupMatch` | `Match::setupMatch` | setupMatch draws | Same (same order of rolls, `rand() % n`) |
| Sliding: advances speed/weight, keeps 98%, stops below 2; spiked stones dig in when they stop | `KAR_tickStones` | `tickStones` | tickStones | Same |
| A stopped stone near the throwers leaves the game | `KAR_checkStonesStopped` | `checkStonesStopped` | stopped stone goes out | Same |
| Side walls, diagonals, back and front | `KAR_bounceOffWall`, `KAR_getWallZone`, `KAR_findWallContact`, `KAR_placeAtContact` | `physics.cpp` | wall bounce | Same |
| Collisions in 11 substeps (with the retry of the last one), against a stuck, stopped or moving stone | `KAR_updateCollisions`, `KAR_resolveStoneCollision`, `KAR_collide*`, `KAR_computeImpactShare`, `KAR_computeSeparation` | `physics.cpp` | collision | Same. `ratan2` (`core::original::ratan2`) takes the table's step like the original (the smaller side over the larger in 1024ths, rounded down); the table is computed and matches the disc's in 1017 of 1025 steps (the other 8, within one unit). Measured: with a rounded `atan2` one unit different came out for 13.8% of the angles; now for 0.8% |
| Rings and points (+2 center, +1 ring, −2 and +2 on the side targets), recounted after each throw | `KAR_classifyStoneRings`, `KAR_tickScoreTally` | `classifyStoneRings`, `tickScoreTally` | rings and tally | Same |
| Match: place, play/rules question, "Ready", choose, aim, force, throw, tally, turn of whoever has thrown least | `KAR_tickMatchState`, `KAR_tickYesNoPrompt`, `KAR_beginAiming`, `KAR_select*Stone`, `KAR_beginThrow` | `Match::tickMatchState` | whole match | Same |
| Rival: direct shot if there is a clear path, otherwise bank shot, stone in a ring or at random; force inverse to the friction minus 600 or 500 | `KAR_chooseOpponentShot`, `KAR_findClearShotAngle`, `KAR_aimBankShot`, `KAR_aimAtStoneInRing`, `KAR_aimAtRandomStone`, `KAR_computeThrowPower` | `match.cpp` | whole match | Same |
| Result 2/1/0 in variable 121 | `KAR_finishMatch` | `Match::result`, `CurlingAdapter::finish` | finish result | Same |
| Drawn stones: the `KARRING.TMD` object per team and type (the spiked ones plain in the hand), on the rink with their spin, in the thrower's hand (node 9 of the player, node 4 of the second rival, 0x78 down) and on the racks at the sides of the screen; the racks fill one by one every ten frames after starting, and the rival's row waits for their helper to come out | `KAR_start`, `KAR_renderScene`, `KAR_setupMatch`, `KAR_beginAiming` | `stoneModelObject`, `Match::stoneDrawn`, `CurlingScreen::showStones`, `CurlingScreen::placeHands` | stoneModelObject ...; stoneDrawn ... | Same in which stone and when. Measured in the original (`curling_start` state): the rack columns at x≈52 and x≈268 of 320. Assumption: the racks are drawn in the original with their own view on top of everything; here they go on the ray of that screen point, halfway between the camera and the ground, with the size of that view and facing the camera as they face that view, tilted 0x96 (in 16:9 they end up where the original puts them in 4:3, closer to the center) |
| Target rings that light up: those a moving stone steps on (each stone lights the first free one that contains it) and, in the tally, the counted stone's new ring while it blinks | `KAR_updateRingMarkers` | `Match::updateRingMarkers`, `CurlingScreen::showRings`; the map objects of each ring in `rules/original/curling.yaml` | updateRingMarkers (two) | Same |
| HUD: portraits and scores, start sign, force indicator and hand over the chosen stone | `KAR_tick` (draw order), `KAR_renderNamePlates`, `KAR_renderScores`, `KAR_renderReadyPrompt`, `KAR_renderPowerMeter`, `KAR_renderStoneCursor`, `KAR_renderSprite` (`KAR_D_8005B478` to `KAR_D_8005B56C`) | `Match::hud` (`scoreGlyphs`, `powerChevrons`), `CurlingScreen::showHud`, document `curling_hud` (`structure.rcss` with each position, `logic.lua`) | hud: the scores' glyphs, the hand, the power chevrons and the start sign; route `stage5-curling` | Same. The original's sprites, in both skins: the first 4-bit image of the rink (VRAM (640, 0), `map/frzl17/texture4/000`) with the palette of each (CLUT x divided by 16), placed from the screen center in original pixels. Player portrait at (-140, -95) over its blue box at (-130, -70); the rival's at (100, -95) over the red box at (80, -70) (the second rival with their own portrait); two 16 × 16 digits at y −62, the ones at x −105 and 104 and the tens 16 to the left (a minus in the tens below zero); the "GAME START" sign at (−52, 0) blinks on frames 8 to 15, 24 to 31 and 40 to 47 of state 3; the indicator (frame at ±50, −50, blue for the player's and red for the rival's) with one chevron per 250 of force, the tenth in another color, in states 8 and 9 of both throwers; the hand at (−120 or 100, row × 30 − 45) in state 5. The draw order is the original's (each sprite goes to the same slot of the order table, the last one added is drawn underneath). Measured in the original (`curling_start` states and the throw): portraits, boxes and digits at those x; with force 1500, six chevrons inside the frame. The y values of the MCP capture come out stretched (it crops the border and scales to 240 lines), so the decomp's are taken. Before, they were texts in a panel ("You 0 - 0 Rival", "Ready!", the instructions of each phase and a bar), which no longer appear |
| Play / rules question | `KAR_tickYesNoPrompt` (`MAIN_D_80134A10`: −130, 42, 262 × 59; selection bar of 94 × 13) | `Match::promptOption`, document `curling_hud` (`#question` in the `#box` box) | route `stage5-curling` | Same: the story's dialog box in its place (the same box as the help pages) with "Start Game" and "Explain Game" and the original's bar over the chosen one; in the modern skin, the same box with its selection square |
| Rules help pages: the rival's name and three lines per page, from the first or second rival | `KAR_tickHintBox`, `KAR_drawHintPage*` | `CurlingScreen::showHud` (texts `kar.*_dialogue`) | - | Assumption: the original's text in the story's dialog box, in its place (the original's is in the same one); the four lines appear at once (the original writes one per frame); the lines name the gamepad buttons, like the original |
| When aiming, Up/Down scroll the view 10 pixels per frame and Left/Right 8 when the turn is already fast (10 frames held) or help is open, without leaving the background | `KAR_handleAimScroll`, `moveCameraByOffset` | `Match::handleAimScroll`, `CurlingScreen::scroll` | handleAimScroll | Same |
| While the thrown stone slides, the camera follows it: each frame it moves by what the stone advanced (the state runs before the stones, so it is one frame behind), without the background's limit | `KAR_tickMatchState` (states 9, 0xA and 0xB, `KAR_D_800638F4`), `map.c: moveCameraByDiff` | `Match::tickMatchState` (`Follow` event), `CurlingScreen::follow` | the camera follows the thrown stone ...; live in frzl17 with keyboard: the stone stays on screen up to the target | Same (#181). Before, the camera stayed on the throwers, the stone left the screen and you could not see where it stopped. Measured in the original (the MCP's `curling_throw` state): the camera follows the stone until it stops |
| Aim arrow: 15 points at 200 ahead of the stone in the thrower's direction, bounced off the walls (x = ±0x2EE), with a gray wave that goes down the line, a missing point that advances every 3 frames, and the tip turned like the line; only when aiming (states 6 and 7) | `KAR_renderAimArrow`, `KAR_rotatePoint` | `Match::renderAimArrow`, `Match::aimArrow`, `CurlingScreen::showAimArrow`, `FieldScene::setPointQuads` | aimArrow ... | Same. The image is the original's: the rink's first 4-bit image with its ninth palette (VRAM (640, 0), CLUT (0x80, 0x1E6)) |
| Sounds: the rub of the thrown stone plays from release until the rings are read; collisions, walls, stuck stone, rings, victory and defeat from the minigame's bank; cursor and page from the menu's bank | `KAR_tickMatchState` (states 9 and 0xC: `playSound2(8, 0)`, `stopSoundMask`), `KAR_tickStones`, `KAR_resolveStoneCollision`, `KAR_tickScoreTally` | events `Sound`, `RollingSound`, `SoundStop`; `CurlingScreen::follow` | the rolling sound starts at the release ... | Same. Before, the rub kept sounding until it ended on its own |
| Animations and orientation of the throwers and the partner: still at the start; the one throwing faces forward when aiming and turns opposite to the aim; crouch, push and throw; at the end the three face the camera in their pose; the stone thrown by the first rival slides turned as they released it | `KAR_tickMatchState`, `KAR_beginAiming`, `KAR_turnAimLeft`, `KAR_turnAimRight`, `KAR_beginThrow`, `KAR_finishMatch` (`startAnimation`, `rotation.vy`) | `Match::facing`, `Animation` events, `CurlingScreen::animate`, `CurlingScreen::faceCharacters` | facing ...; finishMatch ... | Same. The clips stay in the hands of the routine's script like the story's: the player and the partner until the script releases them, the rival until it returns to 0 |

## Tests

`dwe_curling_tests`: the ruleset section, the setup rolls, sliding and stopping, the stone that
goes out, the stuck stone, the bounce, a collision, the rings and the tally, the result and a whole match with a
fixed seed against both rivals, with and without the rules.
