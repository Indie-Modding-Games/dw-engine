# story

The story (`dwe::game::story`, `game` layer): its state and the runtime that runs the sections of the event
scripts with the original's semantics. The design is in the [story interpreter](../../architecture/story-interpreter.md)
and the original's behavior in the [scripts sheet](../../reference/scripts.md).

It depends on `core` and `assets`. It does not know Lua or any other `packages/game/*`: what it needs from the world it asks for through ports that
`packages/session` implements, and the tests, with doubles.

## State

`StoryState` holds the triggers and the variables of the event scripts,
as the original stores them (`script.h: ScriptState`). It is used by the interpreter (`StoryRunner`, through `StoryStatePort`) and
by the field's script warps, which read the triggers and apply their `sets_flags`
([script warps](../../architecture/script-warps.md)).

- `StoryState`: the original's 800 triggers (0–799) and 256 variables (`BaseTriggerCount`,
  `BaseVariableCount`) plus what the ruleset adds (`StoryCapacity`: `capacity` of the `story` section, numbered
  after them; [capacity](../../modding/story.md#capacity)). The variables are integers: the range of each one is
  set by the rules (`StoryRules::variableMax` for the original's, `extraVariableMax` for the extra ones). A number
  outside the capacity is an error and changes nothing; `setCapacity` changes the capacity keeping what fits.
- `toYaml`/`fromYaml`: the `story` section of the saved game, version `StoryStateVersion` (2). `triggers` are
  the bit bytes in hexadecimal digits (byte k carries triggers 8k to 8k + 7; number n, in bit n % 8,
  like the original; 100 bytes of the original and then the extra ones), `variables`, the first 35 (0–34, the ones that
  `main_menu.c: writeSavegame` saves; the original's other ones only live while the game runs) and `extra_variables`,
  all the extra ones. `fromYaml` reads within the ruleset's capacity and discards, with a warning, what does not fit.
  `storyStateFromVersion1` is the migration from version 1 (exactly 100 bytes and no extra variables).

- `ScreenHistory`: the current screen and the previous one with their exits (`map.c: changeMap`, `PREVIOUS_SCREEN`,
  `PREVIOUS_EXIT`), where a flee goes back to (`script_interp.c`: `START_BATTLE`). `arrive` follows each map change
  that the field plays (`StoryFieldLink::followScreen`): an exit on foot saves its warp number (trigger − 110,
  `checkMapInteraction`), a script change the exit it was entered through (`map.c: scriptTickChangeMap`).
  The saved game carries it in the `screens` section (`main_menu.c: writeSavegame` saves all four); a game
  without it, or a start on a map, begins the story on the map where it appears.

- `StoryNames` (`story_names.hpp`): the labels of `story/names` from the `game` pack for triggers and variables,
  only for tools (mapper, remote control, logs); `loadStoryNames` without the file gives none. The schema is
  `story_names` ([story API](../../modding/story.md#names-of-triggers-and-variables)).

It does not know the other systems: the session (`packages/session`, `StoryAdapter`) creates the new game's state, registers it
in the saved game and gives it to the field.

## Runtime

- **`StoryRunner`**: the only interpreter. It runs one script at a time (a request while another is running is dropped and
  noted in the debug log), gives one frame of the original every `ticksPerFrame` engine ticks (3 at 60 Hz,
  from the ruleset) and resumes the section in progress once per frame unless it is waiting. A section starts on the
  frame after the request (`StartTiming::NextFrame`) or right away (`StartTiming::Now`).
  `arriveWith(section)` ends like a `warp_to` the map change that the field took on its own (free
  exploration, `field/script_warps`): the next `enterMap` runs the map's header and then that arrival section
  of the map's script (`script_interp.c: scriptInstruction46to58`, `map.c: scriptTickChangeMap`). If there is already a
  map change from the script itself in progress, its own stays.
- **`SectionHost`**: how the runner starts, resumes and closes a section without knowing about Lua. The session implements it
  with the script VM (one coroutine per section). A section yields with a wait (`FrameWait`: n frames, at
  least one, and the value it receives on returning; `FileJump`: jump to another script) or ends (`SectionEnded`,
  `SectionReturned`, `SectionFailed`).
- **Jumps to another script** (`jump_to_file`): the destination section runs on the same frame. The original only moves
  the script pointer: outside any subroutine (`linkDepth` 0) the section that jumps ends and the destination takes its
  place, so two scripts can jump to each other endlessly, as in the original. Inside `linkDepth` `jump_and_link` calls,
  their return frames are still on the stack: the section that jumped waits under the destination; if the latter
  ends the script, both are closed, and if it returns with nothing to return to (a `jump_return`), the one that jumped receives
  `nil` and its subroutine returns to whoever called it. The host counts `linkDepth`, the calls from which it jumps.
- **`StoryOrders`**: the orders that only change the state: `set_trigger`, `unset_trigger`, `set_pstat`,
  `add_to_pstat`, `reduce_pstat`, `copy_pstat`, `store_random` (`(limit × r) >> 15`, like the original) and `delay` (at
  least one frame). The operands are plain integers: the original's byte or 16-bit widths were its
  encoding, not game rules.
- **`StoryRules`**: the rules of the state that the player does see, from the ruleset. A variable's value goes from 0 to
  `variableMax` (255 in the original): `set_pstat`, `add_to_pstat`, `reduce_pstat` and `store_random` stay in that
  range, like the original's cap of 255 and floor of 0.
- **`ScriptMotion`** (scripted movements, `script_interp.c: tickScriptedMovement`): each character has a slot;
  a new order on the same character takes its slot and the previous one stops counting. **Assumption** (read from the
  decomp; the measurement in the original is queued): when a turn or a walk ends, the character goes back to its
  rest animation (animation 0 of the player, the companion or the NPC) and stays still. In the original it is the animation
  that carries it, so a walk whose slot an `entity_look_at_entity` removed goes on until the turn ends and
  then stops. Before, the engine kept the walk's speed and the player went on running to the wall (trop03,
  `species_ae`: from (-708, -2323) to (-4647, 3941) in the original's axes). Remaining difference: during the turn the engine
  keeps the walk's direction instead of bending it with the turn (a few frames).
- **A walk toward a character looks at the destination and collides by its front edges** (`tamer.c: tickEntityWalkTo`,
  `map.c: entityCheckCollision`, `entityCheckEntityCollision`, `entityMoveForward`): each frame the one walking looks
  at the destination (`entityLookAtLocation`), and the walk ends on stepping on the destination cell or on colliding with a character
  (the 0 to 9). The test advances the walker by its clip's momentum, which `startAnimation` leaves at zero, so the
  first frame tests no step; the player in the field tests its whole square and the others only the edges that
  are in front (the axes with a step other than 0, with edges that touch counting). The first step of the walk clip
  already goes toward the destination. Same. Measured in the original (gias04, script 70, section 0x33, the ambush): the companion
  approaches the player and stops at (−454, 3314) with the player at (−697, 3149); after animation 22 during the 20-frame
  `delay`, the player leaves west at 50 per frame and the companion takes its first step, of 30, toward the bird
  ((−474, 3291)) and arrives at (−805, 2963). Before, the engine tested a circle against the player and the companion on all
  frames, the first included, and the clip's first step went where the character was looking before (50 toward the
  companion in the player): the companion collided at once and stayed still. Now it stops at (−460, 3315), takes its
  first step to (−480, 3292), the player to (−747, 3151), and it arrives at (−796, 2972). `script_motion_tests`: "a walk to a
  character takes its first frame whatever stands ahead", "a walk faces its point as it starts, before the clip's
  first step"; `field_tests`: "a scripted walk bumps on its clip's step, into what lies ahead or, the player, anyone",
  "a step under one unit moves no edge into the collision test". Remaining: the engine slides the walk at the clip's
  ground speed (45 per frame in the companion) instead of by the momentum of each frame (~30 measured).
- **A walk to a point on the corner of its cell does not go around endlessly** (`tamer.c: tickEntityWalkTo`,
  `getModelTile`): the walk ends on stepping on the point's cell, and a point with round coordinates is on the corner
  of its cell. Whoever arrives from a side that is not the cell's crosses the corner without stepping on it, turns toward the
  point and crosses it again: with exact steps, forever. In the original the steps are in whole units (the
  step of the turned and displaced clip), which move a little off the line on each pass until one falls in the
  cell. mgen13 through the mansion (script 63, section 0x3c): after the fixed battle the boss goes back to (−100, 800) from
  where the battle ended and `wait_for_entity(6)` never finished, so trigger 202 never came. Now, if the
  step of this frame passes the point without ending in its cell, the walker stays on the point (which is always
  in its cell) and the walk ends. Assumption: the original ends a few passes later and a little farther. The
  other walks end as before, on the first step inside the cell. `script_motion_tests`: "a walk at a point on
  its cell's corner from outside the cell stops on the point", "a walk still ends as it steps into its point's
  cell, short of the point"; `route-stage9-back-mansion.yaml`.
- **The step of a slide is one for all** (`tamer.c: tickEntityMoveTo`, `tickEntityMoveToAxis`, with
  `MOVE_TO_DELTA_X`/`MOVE_TO_DELTA_Z` and `PREVIOUS_CAMERA_POS_INITIALIZED`): the first frame of an `entity_move_to`
  or `entity_move_to_axis` takes its step (the distance in whole units divided by the frames, with integer
  division) and saves it; the step is only released when a slide arrives. A new order while another runs
  goes on with the previous one's step and arrives as soon as it passes it. Same. Measured in the original (mgen06, the boss of
  script 194, `w21_mgen06_devimon_leaves`): the first slide takes (−107, −12) from (−2405, 298); the next
  order, given before it arrives, goes on with that step and arrives at ~29 frames; the one after, already released,
  takes (46, 4) from (−5487, −47) and arrives at ~21. Before, the engine took a new step per order and, since the boss
  moves away with its root, it never arrived: it was cut off 40 frames later (already removed). `script_motion_tests`:
  "a move to a point ordered while another runs goes on with that one's step", "a move to a point ordered after one
  ended takes a new step", "a move to a point closer than its count ends at once where it stands". Remaining: in the
  original the walk with camera (`entity_walk_to` with camera) also uses that mark; the engine does not share it.
- **A slide ends where the step that passes the point leaves it, not on the point** (`tamer.c: tickEntityMoveTo`,
  `tickEntityMoveToAxis`; `anim.c: applyRootMomentum`): each frame the step moves the position and also the
  animation's own position (`setEntityPosition`); the axis that passes the point is set to the point, but the
  next animation tick copies its own position again (`anim->locX >> 15`) and leaves it where the
  step took it. An axis that has already passed keeps taking steps until both pass, and the slide ends with the character up
  to one step beyond. Same. Measured in the original (2026-11-08, `twnb03` at night, the shopkeeper of the market box,
  entity 7, script 163 on entering: `entity_move_to(7, -1232, 2903, 10)` from (225, −143)): step (−145, 304), ends
  at (−1370, 0, 3201) at the eleventh step, with the animation's position equal (−1370 and 3201 in `anim.locX`/`locZ`
  shifted 15 bits). Before, the engine put it on the point, (−1232, 2903), closer to the camera and half sunk
  in its box; now (−1370, 3201). `entity_move_to_axis` does the same on its axis (Assumption: same code, not
  measured separately). `script_motion_tests`: "a move to a point ends where the step that takes it past the point leaves
  it", "a move to an axis position ends where the step that takes it past leaves it".
- **Each section starts with the boxes in confirm mode** (`script_interp.c: callScriptSection`, `TEXTBOX_CLOSE_MODE`
  0, and its `initializeTextbox`, which sets the timed mode's frames to 0): the mode that the previous section left (automatic
  or timed) does not carry over to the next. Before, the engine carried it over: a section that ended in automatic mode
  (`textbox_mode(1, 0)`) made the first page of the next one close by itself. Same
  (`dialogue_mode_tests.cpp`). The confirm latch when opening a box (`setFreshDialogue`,
  `isXPressedAfterDialogue`) is `Box::freshGuard`. Same.
- **The subroutine stack** (`popScriptStack`, `returnFromTop`): same, without the original's cap of 8 frames, which was
  from its memory and not from the game (Improvement with no effect on the disc's scripts).
- **Ports** (`story_ports.hpp`): `StoryStatePort` (triggers and variables; it says how many it has: 800 and 256 are the
  original's base set and a mod can extend it) and `StoryRandomPort` (the session's generator, from 0 to 0x7FFF).

The other pieces of the runtime, one per header: `dialogue.hpp` (the text boxes, their pages and choices),
`frame_systems.hpp` (what runs on the story's frame besides the sections), `story_task.hpp` (the
waits that another system ends), `story_battle.hpp` (`start_battle` and its result), `story_routines.hpp` and
`partner_routines.hpp` (`call_routine` and `call_partner_routine`, which each system registers), `npc_orders.hpp` (putting and
removing the map's NPCs), `new_day.hpp` (what a new day does to the variables), `prosperity.hpp` (the
city's prosperity) and `medals.hpp` (the medals earned by walking through the field).

## The pointer in dialogues

A PC improvement (the original has no pointer) that changes no rule, so it has no option: in a text box
choice, `DialogueKeys::clicked` picks that option even if the cursor is on another (with the confirm sound),
and `DialogueKeys::pointed` moves the cursor to the option under the pointer when it moves, without sound:
it is the hover highlight. An index outside the options does nothing, and the keys and the pad go on as
always. The caller gives the rows: in the game, `optionPointer` (`apps/game/src/story_text_rows.hpp`) looks at the
`row-N` boxes of the `ui/story_textbox` document. Outside a choice, a click anywhere turns the page, like
confirming; in a choice, a click outside the options does nothing (Enter or the pad pick the one under the cursor). Tests:
`packages/game/story/tests/dialogue_pointer_tests.cpp`.

## Engine guards

These are not rules of the original but nets against runaway scripts, far above what the disc asks for:

- **Call depth in a jump:** 256 (`StoryRunner::MaxLinkDepth`), adding up the sections that wait under
  a jump. The original's stack has 8 frames (`pushScriptStack`), but it is a limit of its memory, not of the game.
- **Resumes per frame:** 1024 (`StoryRunner::MaxResumesPerFrame`). The original has no limit; this only
  stops a section that never waits from hanging the game.
- **A battle that cannot start:** `start_battle` (`tamer.c: startBattle`) always starts in the original; when the
  port cannot build the battle (a content error), `BattleOutcome::NotStarted` logs the error and the story takes it as
  a flight (outcome variable at 0, the flight counter untouched), never as a win, so the story does not advance.

Passing a guard ends the script and notes it in the error log with the script and the section.

## Errors

A trigger or a variable that the state does not have, passing a guard, a jump to a section that does not exist and a
`jump_return` with nothing to return to are errors: the order fails (the section sees it as a Lua error) or the
runner ends the script and notes it in the error log. It is the list of the design's Q11; nothing else is an error.

Talking to a character whose section is not in the map's script is not an error: in the original
`getScriptSection` gives a null pointer, the interpreter reads the bytes at address 0 and the script ends at once
(measured in the original with character 0 of room08 pointed at section 0x60: nothing happens). That is what happens to the
card shopkeeper of gias06b, character 5 of script 126, which only has sections 0x51 and 0x52 of the counter.
The same goes for a map cell (`tamer.c: checkMapInteraction`, `SectionSource::Cell`): in topn01 outside the
opening the cell of section 0x42 has no section in script 193 and the runner ends it without noting an error
(before, it left an `[error]` line per second when standing on it). The cell asks for its section every frame: after the first
time the runner remembers it is missing and ends it with no more lines until the next `set_script`; for a cell with a
section only the first request is noted while the player stays on it. The other sources without a section are still
a content error.

## Tests

The module's tests use a `SectionHost` with fixed steps and fake ports, without Lua. `story_names_tests.cpp` reads the labels,
also those of the `game` pack. The per-section patches (`story/patches/*.lua`) are loaded by `StoryLuaHost` in
`packages/session` and tested by `packages/session/tests/story_patch_tests.cpp`
([moddability](../../architecture/story-interpreter.md#moddability-how-a-mod-changes-a-section)). On the extracted disc,
`packages/session/tests/story_lua_disc_tests.cpp` ("the training loop of script 49 runs as tail jumps") runs several
laps of section `0x52` of script 49, which jumps to `0x33` of scripts 199–202 and goes back endlessly in the
original: it is the proof that a `jump_to_file` stacks nothing.
