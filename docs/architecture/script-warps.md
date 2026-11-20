# Script warps

Status: **done and in `main`.** Talking, entering a map and the transport are run by the
[story interpreter](story-interpreter.md); when the story carries the map's script, `field/script_warps` is a map
graph (free exploration, click routes, reach tests), and it only changes map by itself without the story.

The original makes many map changes from event scripts (`warp_to`, `teleport`), not from the map's warp
table. `dwx` extracts all of them, with what is needed to reach each one, to `field/script_warps` in the
base pack, and the field takes them. Everything comes from **a single representation** of the scripts (`formats/script`), the
same one that the Lua conversion and the story interpreter use.

## Behavior of the original (reference, without copying code)

| Fact | Locator |
|---|---|
| `warp_to` (0x4B): screen, exit and arrival section (`0xFF`: none); nothing that follows runs | `script_interp.c: scriptInstruction46to58`, `map.c: scriptTickChangeMap` |
| `teleport` (0x58): screen and exit from two variables (247 and 248 on the disc); no arrival section | `script_interp.c: scriptInstruction46to58` |
| Entering a map runs the `MAPHEAD` section with the map's number, which loads its script with `set_script` (on the disc, always with the map equal to the section); then it runs section `0xFE` of the script | `map.c: changeMap`, `script_interp.c: runMapHeadScript` |
| Tiles 51–79: the section with the same number when stepped on; 80–109: when confirm is pressed; 110–119: the table; talking to an NPC runs the section with its script id | `tamer.c: checkMapInteraction` |
| A new game runs section 0xCC of `MAPHEAD` (`twna01`) with all triggers off | `main.c: newGameScene`, `script_interp.c: initializeScripts` |
| `IF`: entries that accumulate from left to right (replace, `and`, `or`) and a jump if the result is true or false | `script_interp.c: scriptIfInstruction` |
| A section is looked up by its number in the script's table and the first entry wins: another with the same number never runs (scripts 4 and 52 on the disc repeat some) | `script_interp.c: getScriptSection` |
| `give_item` leaves its result in trigger 0; `compare_date`, in its trigger | `script_interp.c: scriptInstruction28to3F`, `script_value.c: scriptCompareDate` |
| `call_routine` and `call_digimon_routine` run game code that can set or clear triggers (the discard-items menu sets 3, the shops, trades, the tournament, evolution and death set others) | `script_interp.c: scriptInstruction64to7E, tickScript`, `partner.c: callDigimonRoutine`, `script_common.c: openDiscardItem` |
| Other map changes from code: death (to 0xCD or 0xDA) and the `MAPHEAD` sections that do not load a script (transport, items) | `map.c: changeToDeathMap`, `dooa.c`, `murd.c` (`setDeathMap`) |

## The representation (formats/script)

1. **`ScriptListing`** with named operands and conditions. Two constructors, `readScriptListing` (YAML) and
   `listingOf` (disassembly), which a disc test compares on all scripts.
2. **`FlowRole::MapChange`**: `warp_to` and `teleport` end the flow, like `end_section`.
3. **`walkSection`** (`guarded_walk.hpp`) walks the structured tree of a section (`structureScript`) and gives
   each command that a filter asks for with its path: the decisions (`BranchTerm` of an `IF`, with its tests and the
   result they must give; `ChoiceTerm` of a `SWITCH` or of a `SET_SELECTION`, with `selection` if the player
   chooses it), the triggers it sets and those it knows on arrival. It follows calls, tails and `jump_to_file`. An `IF`
   whose tests are on known triggers takes its branch without leaving a decision; branches that join again with
   the same state are merged. A loop or a state machine leaves the path `unstructured`.
4. **Walk limits**, so that it ends on all the scripts on the disc (`WalkOptions`):
   - `maximumPaths` (`DefaultMaximumPaths`, 64): if a statement leaves more paths, they continue as one,
     `unstructured`, with the decisions and effects that all of them start with and the triggers that all of them know
     the same (what a path knows is a map from trigger to set, cleared or **unknown**; a trigger on which
     they do not agree becomes unknown even if the others consider it off). It is safe: every command that a
     path reached remains reachable (on an unknown trigger both branches are walked), a decision that all of them
     took is kept, and a map change after the merge is never certain (`unstructured`), so the field
     does not take it in vanilla. A synthetic test with 128 paths checks it against the full enumeration.
   - `maximumRoutineWalks` (`DefaultMaximumRoutineWalks`, 16): a routine that the walk has already followed that many
     times is **summarized**. Each path that arrives goes through the routine's walk from what that path knows, which
     is done once for all those that know the same, and it is placed after their decisions and effects. It is what following
     it whole would give, because the walk of a routine only depends on the path through what it knows; only the
     merges of `maximumPaths` can change, which depend on how many paths pass together, and which routines are being
     walked, because the recursion check cuts the call to one of them. That is why each summary is stored
     with the routines it tried to enter (also those of the summaries it used) and which of them were being
     walked, and it is only used again where the same ones among them are being walked: that way the cuts are the
     same. A routine that calls itself, as a loop does, is cut the same at each use and its summary works.
     Without the summaries, chains of routines that are called from several branches are walked once per path:
     script 165 does not finish in minutes. Without storing the summaries with cuts the same happens: an earlier version did not
     store them and the extraction did not finish. A synthetic test with a chain of routines that call themselves checks
     it by the number of summary walks.
   - **Recursion cut**: the call to a routine that the walk is already walking is not followed. The path that
     makes it is not lost: it continues after the call with a **`RecursionTerm`** term (the routine, with its script and
     its offset) that says that what that call decides is not known, and with what it knew about the triggers
     forgotten (the routine could have changed them). The path's effects (`sets_flags`) do not carry what that
     call sets or clears. It is never taken as false or discarded: a map change after a
     routine that calls itself (even if it is not its last command) comes out with that term. Synthetic tests: an `IF`
     whose branch calls the routine again leaves the term (not a single branch), and a map change after a
     self-call that is not the last command is found.
   - **Game code that writes triggers** (`ForgetTerm`, **additive CONTRACT CHANGE**): after a
     command that runs game code that can set or clear triggers, the path continues with a
     **`ForgetTerm`** term (the command, with its script and its offset, and the triggers it can write) and without knowing those
     triggers: a later test of one of them walks both branches. `give_item` forgets 0;
     `compare_date`, its trigger; `call_routine` and `call_digimon_routine`, whatever the `routineWrites()` table
     (`routine_writes.hpp`) says, read from the game code with a locator per entry. A routine
     that is not in the table writes none, and leaves no term: that is how what writes is told apart from what does not. The
     frame triggers (37–40 and 44) are never known and do not count. Without this the walk took as decided
     the test of 3 that follows the discard-items menu (script 206) and lost two map changes.
     - With listed triggers: `call_routine` 0x00, 0x01, 0x08 and 0x0E (item and card menus) the 3;
       0x12 those of the tournament (37–40); 0x16 the medals (0x16C–0x17E, `script_common.c: unlockMedal`); 0x20 the 49
       (`shop/script_menu.c: newGameStateMachine`); 0x21 the 37, the 38 and the 39.
     - **All of them** (the code does not bound what it writes), with their reason: `call_routine` 0x02 (the trade sets
       the trigger of the item under the cursor, `setMojyaItemTradedTrigger`), 0x06 (the tournament: trigger 15 plus the
       cup from a variable, and its fights reach death, evolution and other scripts), 0x30 and 0x36 (they can
       load a game, `main_menu.c: loadSavegame`); `call_digimon_routine` 2 (evolution) and 4 (rebirth), which
       return the partner to its whole state machine, and 3, 7 and 9 (death), which reach the change to the death map and
       other scripts. There are 9.
     - Over-approximations (they do not write triggers, but the walk goes on after them as if they returned):
       `call_routine` 0x0A, the transport (`shop/script_menu.c: tickBirdraTransport`): choosing a destination is a
       hand-off to section 0x4E3 of MAPHEAD, and the script that called does not continue; canceling returns to the script
       (`script_menu.c:1004-1006`), and that path is real. The destinations come from a table in the executable, not from the
       MAPHEAD walk (see [The transport](#the-transport)). And 0x35 ends the script
       (`script_interp.c: scriptInstruction64to7E`). What the walk finds after them in the calling
       script runs in the original only if the transport was canceled.
     - How it was read: what each routine runs at the start and while the script waits for it, with everything it calls or leaves
       as a callback, and the partner and tamer states it moves to; the search stops at the states that
       return control to the game (tamer 0, 1 and 6; partner 1, 11 and 12). A test checks that the table only
       has routines that exist, with no repeats.
   - `WalkMonitor` (optional): counts merges, summarized calls, summary walks and cut recursions
     (`recursionCuts`), and stops the walk at a deadline (for the tests that measure it).
   - `GuardedInstruction::state` lists only the triggers that the path knows.
   - **Proof on the disc** (optional: `DWE_WALK_PROOF`, `packages/formats/tests/script_walk_proof_disc_tests.cpp`):
     script by script, it checks that every `warp_to` and `teleport` comes out in the result or is not reached by the flow of
     any section (except `StaticUnreachable`), performs the merge check and compares the walk with
     summaries against the full one, with 10 minutes per script. If the full one does not finish, the script is proven with the
     **fallback**: the walks that summarize after 256 and after 1024 walks must give the same as the one at 16,
     conditions included; the output says "proven by fallback: N (ids)". `DWE_WALK_PROOF` accepts ranges
     (`0-59,60-119`) to run it in pieces, each in its own container with its time limit.
     `StaticUnreachable` are only the 6 map changes of script 124 (offsets 0x5C0, 0x5D0 and 0x5D4 under the `IF` at
     0x59C; 0x620, 0x630 and 0x634 under the one at 0x5FC): they are on the false side of a test of trigger 221, which the
     script sets at 0x56C on every path; the proof checks it with the walk: every path to the map changes on the true
     side of those tests (0x5E4, 0x5F4, 0x5F8, 0x644, 0x654 and 0x658) knows 221 as set (the walk does not pass
     an `IF` through the filter, because it walks it as a statement, so what runs afterwards is looked at). Those of script 206 (0x836 and 0x1396) are not an exception: with `ForgetTerm` they come out. In the same source, a disc test checks that the
     walk that the extraction does finishes on each script before `ExtractionBudget` (2 minutes, in the build with
     sanitizers), and that no map change exceeds `MaximumGuardTerms` (32) terms; on the disc the maximum is
     16 (script 174). The optional proof also walks with a large `maximumPaths` (`LargeMaximumPaths`, 4096)
     the scripts that have merges and checks that this walk finishes without merging, that the merge does not join different
     map changes and that it does not invent conditions; a script that cannot be compared goes in `MergeExceptions` with
     its reason (none). Those that still merge with 4096 paths are a precision note, not a failure: they go in
     `MergeStillMerges` with what was measured, they are still compared (same map changes, no invented condition) and
     the proof fails if one on the list no longer merges. There are 9 (2026-10-22): 165 and 211 (150 merges, 130 with 4096;
     proven by fallback), and 168 and 214 (69 and 11), 176 (4 and 2) and 199–202 (90 and 16), the four groups proven
     by the full walk. None of the map changes of these scripts in `field/script_warps` is
     `unstructured`, so none ends up after a merge (measured on 2026-10-22; it is not a CHECK).
5. **`ScriptListing::runnableSections`** gives the sections that the game can run (the first entry of each
   number); `walkSection` takes the first and the extraction and the proof only walk those. A synthetic test with two
   entries for section 51 checks it.
6. **`findMapChanges`** filters the `MapChange` entries; **`findScriptWarps`** walks each section of `MAPHEAD`, looks at which
   script it loads and with which decisions, and gathers the map changes of each section of that script, with what
   triggers them (`warpViaOf`). **`findNewGameTriggers`** walks section 0xCC and the `0xFE` of the script it loads, with
   everything off, and gives the triggers set when control is taken.

## Format: `field/script_warps` (dwx output, base pack)

Schema `content/schemas/script_warps.schema.json`. One record per path to a map change:

```yaml
warps:
- map: room08          # the map (the MAPHEAD section that loads the script)
  via: step            # step (51–79) | confirm (80–109) | enter (its MAPHEAD section) | talk (the section of an
                       # NPC on the map) | other (another section)
  section: 51
  to: {map: twna01, exit: 2}   # or {screen: N, exit: N} if no map has that number
  teleport: {pstat: 247}       # instead of `to`: the variable with the screen; no static destination
  arrival_section: 5           # only if it is not 0xFF
  script_guard: {...}          # what MAPHEAD requires to load the script; without it, nothing
  guard:                       # what the section requires; without it, nothing
    unstructured: true         # only if the path goes through a loop or a state machine
    terms:
    - if: [{test: trigger, trigger: 203, clear: true}]   # the IF's entries, as in the listing
      holds: true
    - switch: 1                # or selection: 0 (chosen by the player)
  transport: {entry: 0, cost: 1000, hand_off: 1251}   # only in a choice of the transport menu
    - recursion: {script: 63, offset: 288}   # a call that the walk cut (RecursionTerm); offset optional
    - forget: {script: 206, offset: 1694, triggers: [3]}   # game code that can write those triggers
                                           # (ForgetTerm); without triggers: any
  sets_flags: [{trigger: 83, set: true}]   # set/unset_trigger of the path, in order; without what a cut
                                           # recursion (`recursion`) sets, which is not walked
  source: {script: 178, section: 51, offset: 296}
```

`data/new_game_story` (schema `new_game_story`): `map`, `script`, `triggers_set` and `agreed: false` if the paths of
the new game do not end with the same triggers (then `triggers_set` are those they share), and
`triggers_unknown`: those that the game code may have set before the new-game section and the walk
does not model, which the field takes neither as set nor as off. They are the raised-partner ones of the two partners the
game can start with (0x203 and 0x211: `main.c: main` raises one according to pstat 0xFE with
`script_common.c: setDigimonRaised`, which sets 0x200 plus its id). On the American disc: `twna01`, script 164, none
set; unknown are 0x203 and 0x211.

`opening` (optional): the map change that the new game makes before the player moves. The new game
enters the first map through exit 9 (`map.c: initializeMap` sets `CURRENT_EXIT`; `NewGameExit`), and step-on tiles
run their section on every tick that the player is on top (`tamer.c: checkMapInteraction`), so if the arrival
lands on one it runs it right away. `dwx` reads the tile under that arrival in the `.MAP` of the first map (like
`map_collision.c: getTileTrigger`; `formats::map::collisionUnderSpawn`) and walks its section with the new-game
state (`findNewGameOpening`); if it makes a single certain map change, it stores it: `exit`, `section`, `to` (map or
screen and exit) and `sets`, the triggers it leaves set. It is only certain if all the paths of the new game
end with the same triggers (`agreed`), if it gives the same with each combination of the unknowns (on or
off; at most `MaximumOpeningUnknowns`, 8) and if the path does not turn off any of those that the new game sets, which
the field would leave set; otherwise there is no `opening` and `dwx` explains it in a note. On the American disc the arrival lands on
tile 54 of `twna01`: with 55 off it jumps to the code of 66 (the "To Opening" entry of a debug menu,
which the live path skips) and, with 75 and 54 off, it sets 75, 54 and 55 and goes to `topn01` exit 9, the opening.
From there, in vanilla `twna01` is not stepped on again with those triggers off: 54 is the door of `room08` and
66 does nothing.

```yaml
opening: {exit: 9, section: 54, to: {map: topn01, exit: 9}, sets: [54, 55, 75]}
```

On the American disc there are 1870 records (version 5 of the extractor): 1602 on step, 168 on confirm, 100 on talking
to an NPC and none from other sections; 36 are choices of the transport menu; 201 without a condition; none to a screen with no map; 37 with a `forget` term and none with `recursion`. Compared with the
extraction without `ForgetTerm` or `runnableSections`: 14 more from script 206 (offsets 0x836 and 0x1396, in `frzl15` and
`room20`) and 2 fewer, those of the repeated section 81 of script 52 (`yaka11b`), which never runs. 26 sections of
`MAPHEAD` do not load a script (new game, death, transport...) and are left out; the transport one (0x4E3) only
shows a text window and continues at 0x4E5, whose `teleport` leads to the map of pstat 247, which the transport routine
sets from the executable's table. Version 4 had 1834: the 36 transport choices are new (12
on confirm, 24 on talk) and the 76 of "NPC or other sections" are now `talk`. 3 maps cannot be read, and their
NPC sections are not told apart.

## From the Lua

`dwx extract` takes `field/script_warps` from the listings (`script/...`), but a mod changes the story in
Lua (`story/<n>.lua`): moving a `story.warp_to` there did not change the document that the field reads. `dwx script-warps` extracts it
again from the packs' Lua ([dwx](../modules/apps/dwx.md#dwx-script-warps)):

```sh
dwx script-warps <content folder> [--with <folder>]... [--rules <id>] [-o <file>] [--spawns <file>]
```

- **How.** `formats::script::readLuaModule` (`lua_reader.hpp`) reads each module back into the listing and the
  structure that the walk reads (`guarded_walk.hpp`), and `findScriptWarps` and `scriptWarpsToYaml` do the same as
  in the extraction. It is not a separate scan of calls: the conditions, the `sets_flags`, the choices, the
  calls to another script (`jump_to_file`) and the transport menu come out of the same walk, so the result cannot
  differ from that of the extraction.
- **The Lua it reads.** What the emitter writes (`lua_emitter.hpp`) and the same written by hand: each
  `story.<name>(...)` is the command of that name; an `if` is an `IF` with its condition read from left to right
  (`and`, `or`, `not` and the reads that the emitter writes; a chain like `(a or b) and c`, not
  `(a and b) or (c and d)`, which the original's `IF` cannot express); `while true do` a loop; the choices
  (`set_selection`), the variable `switch` statements and the state machines as the emitter writes them;
  `if routines.<f>() == ENDED then return ENDED end` a subroutine; `return routines.<f>()` a jump to shared
  code; and `return`, `return ENDED` and `return story.<name>(...)` the endings. Anything else (a `for`, a function of its
  own, a local variable that is not that of a choice or a `switch`...) is an error with its line:
  the walk cannot follow code it does not know, and guessing would give map changes that do not exist.
- **Strict.** If a module is not read, it writes nothing and names each module with its line: leaving it out would remove its
  map changes without warning.
- **Offsets.** With the line map (`story/<n>_lines`) that corresponds to the Lua, each command recovers its offset and the
  code that the emitter copied in several places becomes a single command again. Without it, or if the Lua no longer matches it
  (a line that runs a command without an offset, an offset on a line that runs none, an `at_<hex>` function that
  does not start at its offset, two lines with one offset and different commands), the commands are numbered in the order of the
  module, those numbers mean nothing outside, and the records of that script come out **without `offset`** (in `source`,
  `recursion` and `forget`; the schema already had it optional and the field only uses it in the log). A note says so.
- **What else is needed.** The NPC sections come from the map documents (`npcs[].script_id`), the map ids
  from `data/maps` and the transport destinations from `rules/<id>/bird_transport` (`destinations`; `--rules`, by
  default `original`). That ruleset is in the team's pack, so with the player's content `--with content/packs/game`
  is needed; without the document, the transport is left out and a note says so.
- **Check and write.** Without `-o` or `--spawns` it compares with the packs' `field/script_warps` and
  `field/npc_spawns` (the latter only when the packs have it) and says the first line that differs (code 1); with `-o`
  it writes `field/script_warps` and with `--spawns` the NPCs the map heads place, `field/npc_spawns`, the same
  document `dwx extract` writes (for example to the mod's `field/script_warps.yaml` and `field/npc_spawns.yaml`, which
  win over the base pack's; the folder is made when missing). `data/new_game_story` is not extracted again.
- **Story patches.** Before the walk it applies the packs' `story/patches/*.lua`
  ([story](../modding/story.md)) like the game (`session::StoryLuaHost`): from the bottom of the stack up and by id
  within a pack, and the topmost one wins each section. `formats::script::readLuaPatch` reads each patch and
  `composeLuaPatches` joins it to the module: each patched section becomes a function `routines.patch_<section>` of the
  module with the patch's body, and `original()` a call to the routine of the original section
  (`story.ENDED`, `story.PLAYER` and `story.PARTNER` are read like the module's locals). The patch's private routines
  (`local routines = {}` and its `function routines.<name>()`) enter as `routines.patch<n>_<name>`
  (`n`, the patch's position in the stack from 1; with one more `_` if the module already uses that name) and their calls are
  renamed; a `routines.<name>` that the patch does not define is an error with its line. A script that is only given by
  patches is read the same, without a module. The reader only follows `original()` as a subroutine (`return original()` or
  `if original() == ENDED then return ENDED end`); a top-level `local Name <const> = 5` is a named number, which the
  patch's code gets as the number. Any other use (`local r = original()`, `original` in a condition, a top-level local
  that is neither a `story` name nor a named number, a function of the patch's own) is an **error with the patch's
  file and its line**
  (`my_mod:story/patches/x.lua:4: ...`) and nothing is written: leaving the patch out would give the map changes of the
  section that the game no longer runs. The patch's lines have no offset, so the records of a patched module
  come out without `offset` and a note names the patches. Without patches, the result does not change.

With the American disc's base pack and the team's pack, the Lua gives `field/script_warps` **byte for byte**: the
1870 records of the 222 modules (`MAPHEAD` and the 221 scripts). Without the line maps the same comes out without the
`offset` lines. A test with `DWE_CONTENT` (`apps/dwx/tests/script_warps_command_tests.cpp`) checks it; the others
use synthetic Lua (`packages/formats/tests/lua_reader_tests.cpp`: round trip with and without a line map, an old line
map, hand-written modules, the story patches and the errors).

## In the engine (part a)

- `packages/game/field` loads `field/script_warps` (`loadScriptWarps`; empty if there is none) and the new-game state
  (`assets::loadNewGameStory`); `apps/game` passes it a trigger oracle (`TriggerState`) with that state.
- **The opening in a seeded start.** With `story_scenes.new_game.chain: original` the field starts at exit 9 and the
  story plays the opening. With `seeded`, the field starts where `field/warps.yaml` says (`twna01` exit 1) and
  `newGameTriggers(story, map, exit)` also takes as set the `sets` of `opening` when the field starts somewhere other
  than its arrival (the first map at the exit of `opening`), which is the state that the opening leaves. Without that,
  crossing 54 or 66 of `twna01` led to `topn01`, a state that the original never reaches.
- The autotest walks (`walk_to`) and the click to move avoid the same tiles: those that change map
  now (`Field::warpsOn`: table exits with a destination and step-on tiles whose script warp is taken), except
  the destination one.
- On entering a tile 51–79, or on pressing confirm on one of 80–109, the field looks at that map's warps, via and
  section, in order, and takes **the first one that holds with certainty** (`evaluate` gives `Truth::True`): every decision is
  an `IF` of triggers that the oracle knows, the path is not `unstructured` and the warp has a destination map. Those
  that cannot be decided (other tests, choices by the player or by a variable, unstructured paths) are not
  taken and are logged once per map and tile (`ScriptWarpUndecided`).
- **The same conditions for the head's NPCs**: `dwx extract` also writes `field/npc_spawns`, each NPC a map's MAPHEAD
  section spawns (`set_digimon`) with the guard of its path, in the form above, simplified (a test the path already
  made is not repeated, a path that contradicts itself is dropped, two paths that differ in one decision become one). Those guards also carry `pstat` tests
  (a variable byte against a value); `GuardFacts::variables` reads them, and without that reader they stay unknown, so
  the warps above decide exactly as before. Co-op uses them (see [multiplayer](multiplayer.md)). `dwx script-warps
  --spawns` derives the same document from the Lua, so a mod's map head reaches it ([from the Lua](#from-the-lua)).
- **Policy for the unknown** (`UnknownPolicy`, `script_warps.hpp`): decides what the field does with the two things
  that the extraction says it does not know, and only with them. `decide(warp, oracle, policy)` gives the truth and what the
  policy let through.
  - A `recursion` term (the recursion that the walk cut).
  - A test of a trigger that the path's game code may have written (`forget`).
  - An `IF` whose tests are **all** of triggers and one is not known by the oracle: in vanilla, one that is not
    among those that the paths of the new game share (`agreed: false`; `newGameTriggers` gives it as unknown,
    and as off only if the paths agree).

  With **`Allow`** the term holds: the `IF` passes whatever result it asks for (both polarities), and the
  warp is taken and logged **once per warp** (`ScriptWarpAllowed`: map, tile, origin and what was let through:
  the cut site, the trigger, or the trigger and the site of the code that could have written it). With **`Deny`** the term is unknown and the warp is not taken.
  `VanillaUnknownPolicy` is `Allow`: the original does not lose an exit over what the extraction cannot know.
  `Deny` exists only for tests and strict mode; **it is not a user option**. Everything else unknown (a
  test that is not on triggers, even if it goes with an unknown one; a choice; an `unstructured` path; a
  warp without a destination) is still not taken and logged, with any policy. A term that fails decides the warp
  even if there are other unknowns.
  - **After a recursion** the path knows no triggers (the routine could have changed any): every later
    trigger test is unknown, even if the new-game state knows it, and with `Allow` it passes in both
    polarities. A recursion in `script_guard` (the `MAPHEAD` path, which runs earlier) does the same with the
    `guard` tests.
  - **After game code** (`forget`, `GuardForget`) the path does not know the triggers it lists (all of them if it
    lists none): its later tests are unknown and with `Allow` they pass, with the site in the log; those
    of other triggers are still decided by the state. It also carries over from `script_guard` to `guard`.
  - **Which one on the tile** (`warpOnCell`): with `Allow`, two paths that split on an unknown trigger
    can both hold; the original runs the section and only takes one. If exactly one holds without the
    policy (it needs nothing unknown), the field takes that one; if not, the first in the file's order, and if they go to
    different places the tile is **ambiguous** and is logged once per map and tile (`ScriptWarpAmbiguous`). The
    reach test uses the same function, so it does not count more edges than the field takes, and it fixes how many
    tiles and sections are ambiguous with the new-game triggers, listed by map, tile and destinations.
- A `teleport` has no static destination: it is in the record (without `to`), the field never takes it and the map
  graph (reach test) gives it no edge. There are none in the disc's map scripts.
- The door of `room08` comes from the extraction (trigger 203 off → `twna01` exit 2).
- **Reach** (disc test and figures in the [field sheet](../reference/field.md#map-reach)): in
  vanilla, with the new-game triggers and those of the opening that is skipped, 5 maps, all with a way back; with any warp, 236 and 233 with
  a way back, with 8 documented exceptions. The test lists by name (map, tile and origin), with their number, the warps
  with a recursion term, those that in vanilla are taken through the policy (`Allow`) and those reachable in vanilla
  that are not taken because they are `unstructured`, and those that in vanilla are taken only because the game code could have
  written a trigger that they test (with its site). On the American disc (2026-10-22) they are 0, 0 and 0: no
  vanilla warp depends on the unknown; the test fixes them and fails if they change.

**CONTRACT CHANGE (modders):** `field/warps.yaml` only carries `start`; `scripted_warps` leaves the file and the
`field_warps` schema, and a pack that brings it no longer validates. A mod that changes script warps replaces the whole
`field/script_warps`. `dw1fe` neither reads nor writes either of the two: no impact; the scripts' YAML does not
change ([log](external-tools.md#contract-change-log)).

**Additive CONTRACT CHANGE:** a `guard` and `script_guard` term can be `forget` (`script`, `offset`
optional, `triggers` optional). Whoever reads `field/script_warps` and does not know it must treat it as unknown, the same
as `recursion`.

## Part (b)

What it adds on top of part (a):

- **Conditions on items, money and partner** (question 6). The entries of an IF that look at how many there are of an
  item, money, a stat or the partner's conditions are read with their operands and decided with
  `GuardFacts`, which the field asks live (the session provides the readers: triggers, bag, money, stats,
  conditions). They compare like the original: one byte for the item count, money as an unsigned 32 bits,
  stat 9 signed and the others unsigned 16 bits, a condition by its mask and negation. A reader that
  is missing leaves the test unknown, and the vanilla policy only lets the unknown through on trigger
  tests. Chance, choices and conversations are still not decided (closed and logged).
- **`sets_flags` and the game's `story` section** (question 8). When a warp is taken, the field writes its
  `sets_flags` through its trigger writer and decides the map's tiles again (`refreshScriptWarps`), so
  only the field decides which warps are active. `packages/game/story` stores 800 triggers and 256 variables; the
  game writes them in a new, additive section, `story` version 1, with a migration test: a game without it
  loads the story as a new game leaves it and logs it.
- **The partner trigger.** The original sets, before the new-game section, 0x200 plus the species of the
  first partner (0x203 or 0x211, `main.c`). `dwx` does not change: `data/new_game_story` still gives them as
  unknown (`triggers_unknown`), and it is the engine that sets the one of the partner the game starts with. The
  story is the sole owner of the raised triggers 0x200–0x23E: evolution writes them on evolving and on
  rebirth and reads them from there, so a warp condition sees which forms the partner has reached. The 5-map
  pin does not change.
- **Talking to an NPC** (`via: talk`). `dwx` reads the NPCs of each map, and a map change from a section that an
  NPC on the map runs is `talk` instead of `other`. The field does not take them (the story runs the NPC's section), but they enter
  the map graph.

### The transport

The `call_routine` 0x0A call (scripts 167 and 209) opens the transport menu. Its destinations are a table in the
executable (`formats/exe`, `transportTargets`: 6 entries of 8 bytes, map, exit, trigger and price; only the
American layout). Choosing a destination is a hand-off: the routine sets the map and the exit in pstat 247 and 248,
subtracts the price and continues at section 0x4E3 of MAPHEAD, which shows a text window and moves on to 0x4E5, whose
`teleport` changes map. Canceling returns to the script (`script_menu.c:1004-1006`).

`dwx` writes each entry as a map change at the call site, with the table's destination and as
condition the entry's trigger, money at least its price and the menu choice, plus the record
`transport: {entry, cost, hand_off}` (hand_off 0x4E3). Neither the field nor free exploration takes these warps or
subtracts the price (decided by the coordinator, 2026-10-22): the vanilla flow (the menu, the price subtraction of
case 5 and the hand-off to 0x4E3) is run by the story's bird transport (`session::BirdTransport`, section
`bird_transport` of the ruleset). The `selection` of the choice is the index of the entry in the table, not the menu
row, which only lists the entries whose trigger is set.

### Free exploration

Setting `free_exploration` (in the pause menu) and `--explore`, off by default. Vanilla never shows it.
With it, a script warp on a tile leads to any of the places that the warps of that
tile lead to, whatever their condition (`explorationWarps`: the first warp of each different map and exit, only to
maps that the pack has):

- a single place: on a confirm tile it is taken, and if it is not the vanilla one the door it opens is logged;
  on a step-on tile it is taken only if it is the vanilla one; if the story has it closed, the selector
  shows it and staying is valid. A step-on tile can block the way: the blizzard strips of the snowy
  maps (script 90 section 0x33 and script 203) lead to the house only with the sick partner, and taken at
  every crossing they left the player trapped between the house and the strip. If the player stays on a step-on
  tile, that tile does not ask again until the map changes (afterwards it goes as in vanilla);
- several: the field stops and the `destination_choice` selector shows them 8 at a time, open on the page of the
  story's one and with focus on "Stay here" (decided by the coordinator, 2026-11-10): confirming without looking never
  changes map, and down leads to the first place on the page; it is chosen with keyboard, mouse or gamepad, in en/es;
  canceling (Ctrl, Backspace, right button, the gamepad's East) and Escape (or F10, the gamepad's Back) is staying;
- the chosen warp's `sets_flags` are also applied (decided by the coordinator, 2026-10-22); if the story's
  place is chosen, the warp that the original would take is taken, with its `sets_flags`;
- the taken warp carries its `arrival_section` (`MapChange::arrivalSection`): on entering, the story runs that section
  of the new map's script after its header, like the original's `warp_to` (`StoryRunner::arriveWith`; a
  map change by the running script itself keeps its own). Without it, choosing the house from a blizzard
  strip left the partner sick and trigger 133 unset, and each strip led to the house again; with
  it the house scene runs (script 132 section 0x33): it removes the sickness, sets 133 and returns to `frzl07`
  exit 2. The same holds for a map whose tiles the story does not run.

**With free exploration on, the order of the story's events may not be the vanilla one**: maps are
reached before the story opens them and the triggers of the warps taken along the way are set, so
a conversation or an event can be seen earlier, later or never compared with the original.

On the American disc there are tiles with 1, 2, 3, 6, 12, 13 and 24 places (those of 24 are the city phases).
The free exploration graph, made with `explorationWarps`, reaches the same maps as the open graph of
the reach tests. Routes: `route-free-exploration` (keyboard), `-mouse` and `-gamepad`.
