# Reference sheet: event scripts

How the original game ran its event scripts (`MAPHEAD.SCN` and `DG.SCN`), according to the decomp: the execution
model, the state they read and write, where they enter from, what each of the 78 commands used by the American disc
does, how an `IF` is evaluated, the combat protocol and the engine routines (`call_routine`). It is the behavior
reference for the story port: `dwx` converts these scripts to readable Lua and the engine only runs Lua. The binary format and
the YAML listing are in [Event scripts](../formats/scripts.md); here only what each thing **does** is described.

Command names are those of the listing (`show_textbox`, `jump_to`...). "Variable" is a `pstat` (a byte of
`ScriptState.pstats`) and "trigger" a bit of `ScriptState.triggers`. Usage counts are from the American disc.

## What it does

Scripts move the story: dialogs, shop menus, combats with NPCs, scenes with camera and movement, map changes, rewards
and punishments. Each map has a script in `DG.SCN` and a section in `MAPHEAD.SCN` that loads it. A script is a list of
**sections**, entry points that the engine chooses: on entering the map, on talking to an NPC, on stepping on or
pressing a cell, when the partner dies... A section runs until its `end_section`; meanwhile, the rest of the game
freezes (if the section asks for it) and the player only responds to text boxes and menus.

## Rules

### Execution model

- **Only one script at a time** (verified, `script_interp.c: callScriptSection`). There is a single interpreter: one
  instruction pointer, one "pending" command and one call stack. `callScriptSection` does not check whether another is
  already running: it overwrites whichever there was. That is why each engine caller checks first that the interpreter
  is free; a request with another script running **is lost**, it is not queued (verified, `tamer.c:
  checkMapInteraction`, `partner_impl.c`, `tournament.c`, `item.c`). The only exception is the end of fishing, which
  calls without checking (verified, `fish.c: tamerTickFishing`).
- **Start:** `callScriptSection(script, section, isEvent)` only prepares: it loads the file, points to the section,
  sets the text-closing mode to 0, the dialog owner to the tamer, frees the 22 movement slots, saves variable 0 (the
  clock speed) to restore it at the end and sets the entity that was talked to as the default rival. It does **not**
  empty the call stack (verified). The first command runs on the next frame, except with `runMapHeadScript`, which
  does a first step on the spot (verified, `runMapHeadScript`).
- **One frame** (`tickScript`, verified): if there is no script, nothing. If there is, it advances the text boxes and
  the 22 movement slots, the map change if one is in progress, and checks the pending command. If it is still waiting,
  it executes nothing else. If not, it executes commands **one after another in the same frame** until one yields the
  frame.
- **Yielding the frame.** Each command ends in one of three ways (verified, `tickScript` and its
  `scriptInstruction*`): it continues with the next one in the same frame; it yields the frame (and, if it leaves a
  pending command, the script stays there until the wait ends); or it starts a map change. There is no coroutine: the
  whole state lives in globals.
  - **They block** (wait for something): `show_textbox` (until the page is turned), `set_selection` (until choosing
    or canceling), `wait_for_entity` (until the movements end), `delay` (N frames), `start_battle` (the whole combat;
    after winning, the camera's return) and the `call_routine` routines with a menu or scene (verified).
  - **They yield a single frame** without waiting for anything: `fadeout_hud`, `move_boulder`, routines 16, 19, 20, 21
    and 55, and opcodes `FC` and `FD` (verified).
  - **They do not yield:** all the others, including the movement and camera ones, which only order the movement
    (verified).
- **Freezing the game** does not happen at the start, but at the first command that calls `scriptPauseGame`: almost
  all the entity, camera, warp and damage ones, `set_textbox_size`, and `set_dialog_owner` when the owner changes
  (verified). The first time per script it freezes the tamer and the partner, releases the player's camera, sets
  variable 0 to 3 (clock stopped), stops game time and clears the text area. If the section is a talk with an NPC and
  trigger 44 is off, the NPC and the tamer turn to look at each other. Each call clears trigger 44 (verified,
  `scriptPauseGame`). A section that never freezes (a cell that only sets triggers) runs with the player moving.
- **End.** `end_section` and `end_script` do the same: they pop frames until the stack is empty (below) and, with the
  stack empty, the script ends (verified, `returnFromScriptFile`). On ending, the text boxes are closed, variable 0 goes
  back to its saved value and, if it froze, everything unfreezes: tamer and partner control, camera and clock
  (verified, `tickScript`). A box with a closing animation keeps the interpreter busy until it closes, so another
  script cannot start even if the player is already moving (verified, `closeAllTextboxes`).
- **An unknown opcode** (outside `10`–`3F`, `46`–`58`, `5A`–`5F`, `64`–`7E`, `FB`–`FF`) aborts the script without
  emptying the stack (verified). There is none reachable on the disc.

### The call stack

Eight frames, without overflow checking (a ninth writes outside; verified, `pushScriptStack`). Each frame stores a
pointer, a script, a type and an argument. The stack **is not emptied** between scripts: the frames left over are
popped at the next end (verified).

| Type | Who pushes it | When popped at an end |
|---|---|---|
| 1 | `jump_and_link`, `jump_to_file_and_link` (the return) | it is discarded and it keeps popping |
| 3 | `set_script` | it reads the map background (`readMapTFS`, `setupMap`) and runs section `0xFE` of the current map script; if it does not have it, it keeps popping |
| 4 | `warp_to`, `teleport`, `section_on_exit`, fleeing a combat | it yields a frame and runs its section of the current map script; with `0xFF`, the end of `MAPHEAD` (it ends) |

- `jump_return` pops the top frame **whatever its type** and returns to its pointer (verified). With the stack empty or
  on a type 3 or 4 frame it jumps to garbage (verified; on the disc it does not happen in the flow that is reached).
- That is why an `end_section` inside a `jump_and_link` subroutine ends the **whole** script, not the subroutine
  (verified). On the disc, 136 `jump_and_link` and only 14 `jump_return`: most subroutines end the script.
- The return frame stores the loaded map script, not the one that is running: a `jump_and_link` made from `MAPHEAD`
  code returns to the right pointer but with the map file as the base (verified; it does not occur on the disc).
- `jump_to_file` pushes nothing: it changes file and section (verified).

### Scripted movements

- 22 slots, which advance every frame inside `tickScript` and are emptied at the start of each script (verified,
  `tickScriptedMovements`). Slot 0 the tamer, 1 the partner, 2–9 the NPCs, 10 the camera, 11 the doors and 3D objects,
  12–21 the map objects (`move_object_to`).
- The move, turn, walk and camera commands **do not wait**: they write the slot and continue. A new command on a busy
  slot overwrites it, with no queue (verified). For waiting there is `wait_for_entity`.
- `wait_for_entity` waits according to its operand (verified, `scriptInstruction46to58`, `tickScript`):

  | Operand | Waits for |
  |---|---|
  | `0xFF` | all the slots free and the sync mark set |
  | `tamer`, `partner`, NPC id | that entity's slot; an NPC that is not there counts as `0xFF` |
  | `0xC8` | the camera (slot 10) |
  | `0xCA`–`0xD4` | slots 11–21 |
  | `0xC9` | the sync mark (set by the `call_digimon_routine` routines when they end) |
  | `0xD5` | clears the sync mark and waits for it to come back |
  | `0xD6` | an asynchronous load to end (routines 35, 39) |

### State

What `ScriptState` stores and what the engine reserves (verified, `script.h`, `readPStat`, `getTriggerOffset`).

- **Triggers:** 800 bits. The limits are not checked: a number ≥ 800 writes into the variables (verified). The ones the
  engine reads or writes:

  | Trigger | Meaning |
  |---|---|
  | 0 | the last `give_item` failed (set on failing, cleared on succeeding) |
  | 1 | combat without fleeing; nearby NPCs always join; after winning the camera is not waited for |
  | 2 | combat without a tamer |
  | 3 | shop mode: routine 8 opens recycling, 14 sells cards; routine 0 also uses it |
  | 4 | with 3 cleared, routine 14 buys single cards instead of a pack |
  | 5–13, 14 | `species_bn` exchanges (routine 2); 14 when all nine are there |
  | 15–36 | tournament cup won (15 + cup id) |
  | 37, 38, 39, 40 | registered in a tournament; pending notice before and after 20:00; level too high |
  | 41 | do not load the shop library on the next map load (set by the death section) |
  | 44 | do not turn the NPC and the tamer when freezing; each freeze clears it |
  | 45, 46 | fishing rods |
  | 47, 48 | icons of the player's sheet |
  | 49 | selections cannot be canceled (new game names) |
  | 50 | game finished once (jukebox, medals) |
  | 200 + Digimon | Digimon recruited in the city; counts for prosperity |
  | 350 | Toy Town; 148 and 341 are the fog |
  | 364–382 | medals |
  | 384 + item | item for sale in the shops |
  | 512 + Digimon | Digimon raised at some point |
  | 640 | energy threshold reached |

- **Variables:** 256 unsigned bytes. The engine reserves these:

  | Variable | Meaning |
  |---|---|
  | 0 | clock speed; 3 = stopped. It is forced to 3 on freezing and restored on ending; routine 25 changes the value that will be restored; on saving it is set to 0 |
  | 1 | prosperity, recomputed from triggers 200 + Digimon |
  | 2, 3, 4 | tournament: day (bit 7 = no notice), cup (`0xFF` none), type of the registered partner |
  | 5 | the species the partner was before becoming `species_p` |
  | 6 | map name on the next scripted warp: 0 according to the section type, 1 always, 2 never; it goes back to 0 |
  | 28–31 | day counters: +1 each day unless they are `0xFF` |
  | 121, 122 | curling: result and difficulty |
  | 200 | item stacks lost in the last defeat |
  | 243, 244 | high and low byte of the shop accumulator (routines 30, 31, 56); when forgetting techniques, the forgotten technique and the ones missing |
  | 245 | music mode: 0 day/night, 1 always day, 2–10 a fixed variant |
  | 246 | training parameter (assumption: its use is unconfirmed) |
  | 247, 248 | `species_ai` mood bubble and arguments of routine 21; `species_e` transport destination |
  | 249 | menu scratchpad: card drawn, jukebox track, menu type |
  | 250–253 | scripted combat (below) |
  | 254 | general argument of the routines: shop (the NPC that runs it), training type, chest, item to remove, name options, evolution species, new game egg choice (0 `species_a`, other `species_an`) |
  | 255 | result: of the combat (−1, 0, 1), of the tournament (rounds won), 0 on dying of sickness or old age |

- **Cards:** 66 4-bit counts; `give_card` does not go past 9 and `take_card` does not go below 0 (verified).
- **Money:** `add_money` does not go past 999,999; `reduce_money` does not go below 0 (verified).
- **Merits:** `set_merit` and `add_merit` do not go past 9999 (verified). `reduce_merit` writes `MERIT = -value` and
  stops it at 0: it leaves merits at 0 whatever the amount (`script_interp.c:1173`). No script on the disc uses it;
  the engine subtracts the amount and stops at 0, as its name says, for mod scripts.
- **Counters:** battles started and flees, +1 per `start_battle` and per flee, capped at 9999; the tournament ones are
  kept by routine 6 (verified).
- **Date:** year, day, hour and minute; a year is 30 days (verified).
- **What is saved in the game** (verified, `main_menu.c: writeSavegame`): only the first 0x17C bytes of
  `ScriptState`: the daily and recycling inventory, the cards, the 800 triggers and variables **0–34**. Variables
  35–255 and the stack **are not saved**: they are disposable. On loading, the saved map runs its `MAPHEAD` chain as
  on entering.

### Entry points

| When | Script and section | Section type |
|---|---|---|
| On entering a map (any map change, game load) | `MAPHEAD`, section = map id | event |
| New game | `MAPHEAD` `0xCC` (loads script 164 and map 204) | event |
| Talking to an NPC | map script, section = the NPC's script id | event |
| Cell 51–79 | map script, section = the cell's value, **on stepping on it** | cell |
| Cell 80–109 | same, **on pressing confirm** on it | cell |
| Cell 120 with the partner needing to go to the bathroom | `MAPHEAD` `0x4E2` | cell |
| Death or lost life | `MAPHEAD` `0x4DE` | cell |
| Using item 22 | `MAPHEAD` `0x4DD` | cell |
| Tournament notice / tournament lost | `MAPHEAD` `0x4D9` / `0x4DA` | event |
| End of `species_n` fishing | `MAPHEAD` `0x4E4` (jumps to script 7, section `0x52`) | cell |
| `species_e` transport | `MAPHEAD` `0x4E3`, changing the running script's pointer | — |

- **The map load chain** (verified, `map.c: changeMap`, `MAPHEAD.SCN`): `runMapHeadScript(map)` runs the map's section
  in `MAPHEAD`. That does `set_script` (script and map), which loads the map on the spot and pushes a type 3 frame;
  then it continues with its own (in the data: music mode, `set_bgm`, `if`). On ending, the type 3 frame prepares the
  background and runs section `0xFE` of the map script up to its first wait. Everything happens in the same call,
  under the fade to black; if `0xFE` waits (text, `delay`), it continues during the fade-in.
- **Walking warp** (cells 110–119): no script; a 20-frame fade, the load chain and fade-in (verified, `tamer.c:
  tamerTickChangeMap`).
- **Talking** (verified, `checkMapInteraction`): the tamer collides with an NPC and, either the NPC has `autotalk` 1,
  or confirm has just been pressed; and no other script is running. Also when an NPC with `autotalk` collides with
  the stationary tamer.
- **Cells** (verified): only with the tamer in its normal state and on the cell it is stepping on. The step-on ones
  **fire again every frame** while the tamer stays on top and no other script runs: the section must get out of the
  way (move the tamer, set a trigger) or it repeats.
- **Death** (`0x4DE`, verified): by defeat in combat (below), by sickness (it has had 12 or more of sickness; life −1
  and variable 255 = 0, `partner_impl.c`) or by old age (`tickDeathCondition`; variable 255 = 0). The section starts
  by clearing trigger 1 and setting 41.
- **Tournament** (verified, `updateTournamentRegistration`, every frame with the interpreter free): registered, with
  the partner of the registered type and from 12:00: if the day is not the registered one or it is past 23:01,
  `0x4DA` and the registration is deleted; if not, `0x4D9` once before 20:00 and once after.
- Sections `0x4D3`–`0x4D5`, `0x4DB`, `0x4DC`, `0x4E5` of `MAPHEAD` have no caller in the engine (assumption: they are
  texts that the routines read, or they are reached with `jump_to_file`).

### `IF`

A list of entries that are **all** evaluated, from left to right, without short-circuiting (verified,
`scriptIfInstruction`). The result starts at 0 (false).

- **Combination** (code byte): bit 7 → `result = result AND value`; otherwise, bit 6 → `OR`; otherwise,
  `result = value`. Bit 7 is looked at first (verified).
- **Test** (`code & 0x38`), verified in `scriptIfInstruction` and `script_value.c`:

  | Test | Operands | True if |
  |---|---|---|
  | `00` trigger | trigger | low bits 0: it is set; other value: it is cleared |
  | `08` variable | variable, value | `variable OP value`, OP = low bits |
  | `10` jump if true | target | jumps if the result is 1; does not combine |
  | `18` jump if false | target | jumps if the result is 0; does not combine |
  | `20`+0 stat | stat, OP, u16 value | `stat OP value` unsigned (16 bits); happiness (9) signed |
  | `20`+1 cards | card, OP, value | how many it has `OP value` |
  | `20`+2 technique | technique, negate | 0: the partner knows it; other: it does not know it |
  | `20`+3 condition | mask, negate | 0: it has some bit of the mask; other: none |
  | `20`+4 items | item, OP, value | how many it has (the first stack) `OP value` |
  | `20`+5 money | OP, s32 value | `money OP value`, unsigned |

- **Comparisons** (OP): 0 `==`, 1 `!=`, 2 `>=`, 3 `<=`, 4 `>`, 5 `<`. Always **game value OP script constant**
  (verified, `scriptCompareValue`). 6 and 7 give garbage.
- A jump in the middle of a list leaves the rest unevaluated; if it does not jump, the evaluation continues with the
  following entries. The list ends with a `19` byte; after that comes the next instruction.
- `28`, `30`, `38` and the extended 6 and 7 do not read operands or give a value: they leave the one from the previous
  step (garbage in the first). The disc does not use them (verified: the disassembler marks them `unknown` and there
  are 0).
- An `IF` without jump entries does nothing. The format allows several jump entries in the same list (several targets),
  and the control analysis treats them as a chain of branches; the disc has none: each `IF` ends in a single jump
  entry (verified, slow test).

### `SWITCH` and `SET_SELECTION`

- **`switch`**: jumps to the case the variable says; a value ≥ number of cases goes to the **last** one (verified,
  `scriptInstruction10to27`). With 0 cases it would read garbage (the disc does not do that). Only 6 uses on the disc.
- **`set_selection`**: shows the options in the dialog owner's box and waits (verified, `scriptShowSelection`,
  `tickSelectionDialogue`). Up and down wrap around; the cursor starts on the first one.
  - **Confirm** jumps to the option's target. The choice **is not stored** in any variable: only the jump records it
    (verified).
  - **Cancel** closes the page and continues **right after** the selection block (the `12 00` mark if there is one,
    which does nothing). With trigger 49 set it cannot be canceled (buzz). In the engine's menus, canceling chooses
    the last option (verified, `tickScriptDialogueBox`).
  - On the disc, after almost every selection there is a `jump_to` that takes the cancellation to one of the options.

### Text

- `set_dialog_owner` or `set_textbox_size` **create** the box; `show_textbox` writes a page and waits (verified).
- The next page is composed while the current one is shown: if the next command is another `show_textbox`, the
  non-final page is released as soon as it is seen and the script runs to the next `show_textbox`, which composes and
  waits. The box **stays open** after the last page until `fadeout_hud`, another owner or the end of the script
  (verified, `script_textbox.c: showTextbox`, `tickConfirmDialogue`).
- How the page is turned is said by `set_textbox_mode` (0 per section): 0 wait for confirm (releasing the button
  first), 1 as soon as it has been written, 2 when a frame count ends that is not rearmed per page. Another value
  leaves the box unclosed (lock) (verified).
- The first row is the speaker's name, in a color: the tamer, the partner (its name), the NPC's Digimon type, or a
  special name (`0xC8`–`0xCD`); `0xFF` without a name. After `set_textbox_size` there is no name (verified).
- The text control codes only insert names, numbers, colors and tabs; **none waits** (verified). References to
  variables (number, Digimon, technique, item, music, tournament) are resolved when composing the page.

### Combat (`start_battle`)

- **Rival** (verified, `scriptInstruction64to7E`, `btl.c: handleBattleStart`): if variable 250 is 1, predefined
  combat: variables 251–253 hold up to three enemy script ids (`0xFF` empty), which the interpreter rewrites in place
  with entity indexes; the other visible NPCs flee. If not, the rival is the NPC that was talked to when the section
  started, and up to two nearby NPCs can join (flee table by types, line of sight, items that change the roll; trigger
  1 forces them). The command's operand is read and ignored.
- For the music, it adds one battle started and runs **the whole combat inside the command**; 20 game minutes always
  pass (verified, `tamer.c: startBattle`).
- **Result** in variable 255, signed:
  - **−1, defeat:** happiness −30, discipline −20; 30% (rounded down) of the occupied stacks are lost, at random,
    going to recycling, and the count is left in variable 200; life −1 and, if it reaches 0, the remaining life to 0.
    The pointer jumps to `MAPHEAD 0x4DE` **without pushing**: the rest of the script is lost and its frames are
    popped when `0x4DE` ends (verified, `handleItemLoss`).
  - **0, flee:** happiness −10, discipline −6, tiredness +2; it adds a flee; scripted warp to the previous screen and
    exit, without a name, with an empty type 4 frame. The rest of the script does not run (verified).
  - **1, victory:** the map music returns, happiness +2. With trigger 1, it continues on the spot after the command;
    without it, the camera returns to the tamer and the command waits for it to arrive (verified).
- With the remaining life at 0, death by old age fires `0x4DE` afterward. Routine 47 forgets techniques (below); that
  `0x4DE` calls it only with 0 lives is an assumption.

### The commands

The 78 that the disc uses, by groups. "Waits" says whether it yields the frame; "freezes" whether it calls
`scriptPauseGame`. Everything verified in `script_interp.c` and the files cited, except what is marked.

#### Flow

| Command | Uses | What it does |
|---|---|---|
| `jump_to` | 3252 | jumps to a target in the same file |
| `if` | 2985 | see [`IF`](#if) |
| `end_section`, `end_script` | 2811, 222 | end: pops until the stack is empty (see [the stack](#the-call-stack)) |
| `set_selection` | 330 | waits; see [`SET_SELECTION`](#switch-and-set_selection) |
| `jump_and_link` | 136 | pushes the return (type 1) and jumps |
| `jump_to_file` | 135 | jumps to a section of another script, without pushing; if the section does not exist, it runs garbage |
| `jump_return` | 14 | returns to the top frame |
| `switch` | 6 | see [`SWITCH`](#switch-and-set_selection) |
| `set_script` | 255 | sets the current script and screen, for the music, loads the map on the spot and pushes a type 3 frame; it continues in the same frame |
| `section_on_exit` | 4 | pushes a type 4 frame with a section of the map script, which will run when the current one ends; it also restores variable 0 right away |
| `data` | 240 | table that the tournaments read; if it were executed it would do nothing and its bytes would run as commands (it does not happen: it always comes after an end or a jump) |

`jump_to_file_and_link` exists but the disc does not use it.

#### Text and dialog

| Command | Uses | What it does |
|---|---|---|
| `show_textbox` | 4327 | writes a page and **waits** (see [Text](#text)) |
| `set_dialog_owner` | 3607 | if the owner changes (or the box is closed): **freezes** (and the owner NPC) and creates box 0, which comes out of the owner's position or of the point from `set_textbox_origin`; same owner with the box open: nothing |
| `set_textbox_size` | 22 | **freezes** and creates a centered box of N columns and rows, without a name; confirm turns the page and closes after the last; cancel composes all the remaining pages and closes |
| `set_textbox_mode` | 33 | page-turning mode (0, 1, 2) and frames of mode 2 |
| `set_textbox_origin` | 19 | world point from which the next box with an owner will come out; used once |
| `fadeout_hud` | 560 | closes interface box N (0 = dialog) and **yields a frame**; it does not wait for the animation |

#### Triggers, variables and date

| Command | Uses | What it does |
|---|---|---|
| `set_trigger`, `unset_trigger` | 1146, 227 | sets or clears a bit, without limits |
| `set_pstat` | 1801 | writes the value |
| `add_to_pstat` | 272 | adds with a cap of 255 (without wrapping) |
| `copy_pstat` | 2 | copies one variable into another |
| `store_random` | 466 | a random number from 0 to `max`, both included |
| `store_digimon_type` | 39 | the partner's Digimon |
| `store_digimon_value` | 3 | the type byte (assumption: vaccine/data/virus) of the Digimon whose id is in a variable |
| `store_date` | 110 | year, day, hour and minute into four consecutive variables |
| `advance_to_date_at` | 17 | sets the clock to the date in four variables; if it is later, it first simulates the skipped hours on the partner (hunger, life, sickness...); if it is earlier, it only sets it; if the day changes, it runs the day change |
| `add_minutes_to_date_at` | 17 | adds minutes to the date in four variables (not to the clock); above 255 years it ends at an odd value (25 years, 14 days, 23:59) |

Not used by the disc: `reduce_pstat` (subtracts with floor 0), `store_map_id`, `compare_date` (compares a date from
variables with a literal one and leaves the result in a trigger), `set_merit`, `add_merit`, `reduce_merit` and `39`
(which **subtracts** minutes).

#### Items, money, techniques, cards and stats

| Command | Uses | What it does |
|---|---|---|
| `give_item` | 255 | gives items; sets trigger 0 if it fails (stack at 99, bag full) and clears it if not. See [inventory](inventory.md) |
| `remove_item` | 63 | removes from **each** stack of the item; 255 does nothing |
| `set_inventory_size` | 11 | sets the usable slots, without a cap or clearing the ones outside |
| `add_money`, `reduce_money` | 54, 57 | cap 999,999, floor 0 |
| `learn_move` | 118 | the partner learns the technique; two pairs of techniques are learned together |
| `give_card`, `take_card` | 67, 5 | +1 with cap 9, −1 with floor 0 |
| `set_stat`, `add_stat`, `reduce_stat` | 71, 292, 54 | see below |
| `remove_condition` | 8 | clears bits of the partner's condition (sleep, tiredness, hunger, bathroom, unhappiness, injury, sickness); it does not touch its timers |
| `deal_damage` | 32 | **freezes**; takes from the partner the percentage of its max HP that a variable says (the operand is the variable, not the number), without going below 1 HP; shows the number |

Stats of `set_stat`, `add_stat` and `reduce_stat` (and of the `IF` test), verified in
`script_value.c: getStatsPointer`:

| Id | Stat | Cap |
|---|---|---|
| 0–3 | offense, defense, speed, Brains | 999 |
| 4, 5 | max HP and MP | 9999 |
| 6, 7 | current HP and MP | the current max |
| 8, 9 | tiredness, happiness (signed) | 100 |
| 10–13 | discipline, energy, virus, remaining life | 999 |
| 14 | merits | 9999 |
| 15, 16 | battles started, flees | 9999 |
| 17–19 | tournaments won, tournament victories, tournaments lost | 999 |
| 20 | weight | 99 |
| 21 | tamer level | 10 |
| 22 | partner lives | reads outside the table (no reliable cap) |

- `set_stat` sets `min(value, cap)`; `add_stat` adds in signed 16 bits and applies the cap; neither has a floor.
  `reduce_stat` applies no cap and has floor 0 (−100 for happiness). After any of the three, energy is trimmed to the
  species' cap and hunger is removed if energy reaches the threshold (verified, `scriptUpdateEnergyBoundaries`).
  Changing max HP does not touch the current one.

#### Digimon and entities

The entities are `tamer`, `partner` or an NPC's script id; an NPC that is not there makes the command do nothing
(verified, `scriptIdToEntityId`), except where stated.

| Command | Uses | Waits | What it does |
|---|---|---|---|
| `load_digimon` | 502 | no | loads the model into the first of 8 slots; with all 8 full it does nothing |
| `set_digimon` | 756 | no, freezes | the NPC from slot 0–7 of the map appears if the Digimon matches the one in the map's table (if not, nothing), with its position, turn and animation, and sets its `autotalk` |
| `unload_entity` | 119 | no, freezes | removes the entity; for NPCs it also frees them. An NPC that is not there: undefined behavior |
| `unload_digimon` | 10 | no | unloads a model if it is among the loaded ones |
| `set_autotalk` | 8 | no | changes an NPC's `autotalk` |
| `call_digimon_routine` | 55 | no, freezes | puts the tamer or the partner into a scene (below); when it ends it sets the sync mark; it is waited for with `wait_for_entity 0xC9` |
| `play_animation` | 850 | no | starts an animation (scripted animation state); it does not wait for it to end |
| `entity_set_rotation` | 336 | no, freezes | turns the entity on the next frame (movement slot) |
| `entity_look_at_entity` | 607 | no, freezes | turns the entity toward another, at most 0x200 per frame |
| `entity_walk_to` | 547 | no, freezes | walks to a point with its walk animation until stepping on its cell; then goes back to animation 0 |
| `entity_walk_to_entity` | 38 | no, freezes | the same toward another entity; it also ends on colliding |
| `entity_walk_to_with_camera`, `entity_walk_to_entity_with_camera` | 57, 1 | no, freezes | the same, with the camera following the one walking |
| `entity_move_to` | 57 | no, freezes | slides to a point in N frames, without animation |
| `entity_move_to_axis`, `entity_move_to_axis_with_camera` | 15, 27 | no, freezes | slides one coordinate (X, height or Z) in N frames |
| `move_camera_to`, `move_camera_to_entity` | 62, 158 | no, freezes | takes the camera to a point or to an entity |
| `wait_for_entity` | 806 | **yes** | see [Movements](#scripted-movements) |
| `warp_to` | 723 | map change, freezes | see below |
| `teleport` | 1 | map change, freezes | like `warp_to` with screen and exit from two variables and without an arrival section |

- **`warp_to` (screen, exit, section)** pushes a type 4 frame with the section and starts the map change: it closes
  the boxes, restores variable 0, a 20-frame fade (the name according to variable 6), the new map's load chain (which
  **replaces** the script pointer) and fade-in. **The commands that follow the warp never run**. After the chain, the
  type 4 frame runs the arrival section of the new map's script (`0xFF`: none) and then the frames that were left are
  popped (verified, `scriptInstruction46to58`, `map.c: scriptTickChangeMap`).
- The slide commands use the speed operand as the **number of steps**: 0 divides by zero. And they store their progress
  in shared global variables: only one slide can run at a time (verified, `tamer.c: tickEntityMoveTo`).

`call_digimon_routine` (verified, `partner.c: callDigimonRoutine`, `tamer.c`): 0 the partner goes to the map's
bathroom; 1 curling; 2 undoes `species_p` (goes back to the species in variable 5); 3 death; 4 rebirth sequence (life
goes back to 360); 5 the opening; 6 the ending; 7 life lost to sickness; 8 `species_t` appears; 9 life lost in combat;
10 evolves into the Digimon in variable 254. Another id does nothing (and does not clear the sync mark).

#### Map, objects and effects

| Command | Uses | What it does |
|---|---|---|
| `set_obj_visibility` | 1669 | sets a map object's mark (`overworld.c: setMapObjectsFlag`); 1 hides it, as the layer drawing reads it (`MapObjectInstance::HiddenFlag`) |
| `move_object_to` | 293 | takes a 2D map object to a screen point in N steps (slots 12–21, several at once) |
| `rotate_3d_object` | 103 | animates a door, elevator or sliding door of the map according to its model (slot 11, one at a time) |
| `spawn_item` | 241 | leaves an item on the ground (11 slots; full: nothing) |
| `spawn_chest` | 71 | places a chest (8 slots) with its item; its trigger says whether it has already been taken (it comes out open and empty) and is set when taking it |
| `spawn_boulder`, `move_boulder`, `despawn_boulder` | 1, 13, 1 | the boulder of one specific map (fixed position); `move_boulder` changes its height and Z, makes it roll and **yields a frame** (scripts repeat it to animate it) |
| `set_rect_impassable` | 16 | marks a rectangle of cells as not walkable until the map reloads |
| `spawn_sprite_at_entity` | 18 | centers 2D map objects on a node of an entity's model |
| `call_routine` | 674 | see [Routines](#routines-call_routine) |
| `start_battle` | 499 | see [Combat](#combat-start_battle) |
| `delay` | 1817 | **waits** N frames (0: one) |

#### Sound

| Command | Uses | What it does |
|---|---|---|
| `play_sound` | 496 | plays a sound from a bank |
| `set_bgm` | 266 | sets a track (restarts it even if it is the same); variable 245 chooses the day or night variant or a fixed one; `0xFF` stops |

### Routines (`call_routine`)

A command with the routine number; the argument usually goes in variable 254. All from 0 to 56 have a case; 3, 4, 5
and 17 do nothing and those from 57 on do not either (verified, `scriptInstruction64to7E` and `tickScript`). The ones
that "wait" run a state machine of their own every frame until they end. The routines the story uses are in
`packages/session/src/story_routines/`; what is left of each is said by the [port coverage](port-coverage.md).

| Id | Uses | What it does | Waits | Decomp |
|---|---|---|---|---|
| 0 | 76 | choose an item that can be dropped; the chosen one in 254 (`0xFF` on cancel, and it can only be canceled with trigger 3) | yes | `script_common.c: openDiscardItem` |
| 1 | 2 | merit shop (cards for merits, items for merits) | yes | `shop/script_menu.c: openMeritShop` |
| 2 | 4 | the item swap (three offers, set in 249) | yes | `script_menu.c`, the swap shop |
| 6 | 3 | run the registered tournament (cup in 3; rounds won in 255) | yes | `tournament.c: scriptStartTournament`, `utils2.c: startTournament` |
| 7 | 32 | gym training (type in 254); needs 39 before | yes | `script_interp.c` + training overlays |
| 8 | 13 | item shop; with trigger 3, recycling | yes | `script_common.c: openShop`, `script_menu.c: openRecycleShop` |
| 9 | 2 | jukebox | yes | `script_menu.c: openJukebox` |
| 10 | 2 | `species_e` transport: destination in 247/248, charges and jumps to `MAPHEAD 0x4E3` | yes | `script_menu.c: tickBirdraTransport` |
| 11 | 2 | item keeper | yes | `script_menu.c: tickItemKeeper` |
| 12, 13 | 12, 69 | remove one / the whole stack of the item in 254 | no | `item.c: removeItem` |
| 14 | 6 | cards: sell (trigger 3), buy single ones (trigger 4) or open a pack | yes | `script_menu.c: openSellCardMenu`, `openBuyCardMenu`, `rollCardPack` |
| 15, 16 | 2, 2 | show / close the money box (16 yields a frame) | no | `script_common.c: createShopBitsBox` |
| 18 | 2 | tournament registration counter (cup, day, type; triggers 37–40) | yes | `dget.c: initTournamentSchedule` |
| 19, 20, 26, 27 | 10, 11, 3, 1 | cart and dirt pile (model, shrink) | 19, 20: one frame | `map_object.c` |
| 21 | 107 | restart the animation of 2D map objects (247, 248) | one frame | `overworld.c: resetMapObjectAnimation` |
| 22 | 5 | check and give tournament medals | if it gives one | `tournament.c: scriptCheckTournamentMedal` |
| 23, 24 | — | open / close the tray of the chest in 254 | yes, about 8 frames | `map_object.c` |
| 25 | 7 | the value variable 0 goes back to at the end = 254 | no | `script_interp.c` |
| 28 | 140 | `species_ai` mood bubble (247, 248) | no | `script_common.c: createMonochromonMoodBubble` |
| 29 | 1 | choose egg on rebirth | yes | `doo2.c: DOO2_openEggBox` |
| 30, 31, 56 | 2, 97, 4 | shop accumulator: to 0, add 243/244, store it in 243/244 | no | `script_interp.c` |
| 32 | 4 | introduction and names of the new game (egg choice in 254) | yes | `script_menu.c: newGameStateMachine` |
| 33 | 1 | force registration in the special cup | no | `script_interp.c` |
| 34, 37, 38 | 1, 1, 1 | effects of a map: `species_b` shake, remove fog (waits), `species_bo` | 37: yes | `map.c`, `overworld.c` |
| 35, 39 | 3, 16 | asynchronous load of the shop / training library (waited for with `wait_for_entity 0xD6`) | no | `script_common.c: loadShopLibrary`, `map.c: loadTrainingLibrary` |
| 36 | — | reload the map background | no | `map.c: readMapTFS` |
| 40, 51 | 1, 1 | final boss: preload and appearance effect | no | `eab.c`, `script_interp.c` |
| 41–44 | 1, 3, 1, 3 | Toy Town boxes | no | `map_object.c` |
| 45, 46 | 3, 2 | fade to white and from white in N frames (254) | no | `fade.c` |
| 47 | 2 | forget techniques on losing all the lives (one text per technique) | yes | `script_common.c: lostAllLives` |
| 48 | 2 | save machine | yes | `main_menu.c: openSaveMachine` |
| 49, 50 | 1, 2 | `species_ap` pedestal (appear; shake, waits) | 50: yes | `map_object.c` |
| 52, 53 | 1, 1 | preload and run the ending; then credits and title | 53: yes | `tamer.c: tamerTickEnding` |
| 54 | 1 | save the finished game | yes | `main_menu.c: gameClearSave` |
| 55 | 5 | a single animation loop for the entity in 254 | one frame | `overworld.c: setLoopCountToOne` |

## Data

- **`MAPHEAD.SCN`** (script 0, resident) and **`DG.SCN`** (221 scripts with code): see [the format](../formats/scripts.md).
  `dwx` writes a YAML listing per script; the YAML's format is a contract with the external map editor.
- **`MAPHEAD` sections** with text tables that the routines read (verified, `resolveMapHeadEntry`): `0x4C4`–`0x4CE`
  shopkeepers' phrases by Digimon type, `0x4D3` card packs, `0x4D4` merits, `0x4D5` item keeper, `0x4D6` shops,
  `0x4D8` miscellaneous texts (tournaments, forgotten techniques), `0x4DB` medals.
- **`DATA` blocks**: tournament tables (rivals per cup, triggers that enable them) inside the arenas' sections; they
  are read by `dget.c` and `utils2.c`, never by the interpreter.

## Shape of the control flow

The converted Lua has no jumps (user's decision, 2026-10-21): `formats/script` builds the control graph of
each routine and structures it into `if`/`else`, loops with `break`, choices and calls to shared tails
(`control_flow.hpp`, `structuring.hpp`). What cannot be nested (a loop with two entries, an exit from two loops at
once, too many copies) becomes a state machine: a loop that dispatches according to a state variable. Counts on the
American disc (slow test `the user's disc, extracted: every event script routine structures and runs like its graph`):

<!-- The figures are given by the slow test; they are updated when the analysis changes. -->
- **222 scripts** (`MAPHEAD` and 221 from `DG.SCN`), **1553 sections**, 7 `jump_and_link` subroutines and 353 shared
  tails: code that several routines jump to (a common ending, a loop that several sections enter), which is converted
  once and called as the last command.
- **1550 of 1553 sections (99.8%) end up nested without a state machine.** The 3 that do not: 2 with a two-entry loop
  (long dialog menus that come back in the middle of a conversation) and 1 paged menu that jumps from each page to any
  of the previous ones (exits from several loops at once). All convert the same: the state machine is the last resort
  and works for any graph, so 100% of the sections convert without jumps.
- 3569 `if`, 104 loops, 127 `break`, 372 choices (`SWITCH`/`SET_SELECTION`) and 1247 calls to tails. Maximum nesting 9
  counting each `elseif` at the level of its first `if`.
- **Copies**: 48 routines carry some copied piece (short tails, of fewer than 3 commands, that are repeated instead of
  called). In total 31174 commands are emitted for 28282 distinct ones (1.10 times).
- **No `IF` on the disc has more than one jump**; 0 `jump_to_file_and_link`, 135 `jump_to_file`, 255 `set_script`; no
  reference to a section that does not exist; 122 sections share code with another.
- `warp_to` and `teleport` end the flow like `end_section` (nothing that follows them runs): the dead code after them
  no longer counts, and a two-entry loop that was only one because of that code becomes nested.
- Each routine runs the same commands as its graph for several sequences of condition results (the test's parity
  check).

## Oddities

- **`reduce_merit` sets merits to 0** whatever the value (it sets them to −value and then clips to 0) (verified). The
  disc does not use it; the engine fixes it (subtracts and stops at 0).
- **Command `39` subtracts minutes** although it seems the twin of `add_minutes_to_date_at` (verified).
- **`add_stat` adds with a 16-bit sign**: a value ≥ 0x8000 subtracts. `set_stat` and `add_stat` have no floor
  (verified).
- **`give_item` adds in one byte**: 98 + 200 wraps around before capping at 99 (verified).
- **Step-on cells repeat** every frame while the tamer stays on top (verified).
- **Script requests with another running are lost** (verified).
- **The stack is not emptied between scripts** and does not check overflow; an `end_section` inside a subroutine ends
  the whole script; `jump_return` with the stack empty jumps to garbage (verified).
- **After `warp_to`, `teleport` or a flee nothing else runs** of the script (verified).
- **`entity_move_to_with_camera` does not move to its point**: it stores the type of "move toward entity with camera"
  and follows an old target; `entity_move_to_entity` and its camera variant check arrival against (0, 0) (verified).
  The disc uses none of the three broken ones.
- **Only one slide at a time** (`entity_move_to*`): they share global variables (verified).
- **`wait_for_entity` with an NPC that is not there waits for everything** (verified).
- **`set_textbox_mode` with a mode that is not 0, 1 or 2** leaves the box unclosed (verified).
- **Routine 7 with type 5 in one of the new gyms** prepares nothing and waits forever; **routine 22** gives at most one
  medal per call (verified). **Winning the special cup** sets trigger 37, which is the "registered" one (verified).
- **Triggers ≥ 800** write into the variables (verified); the disc does not use them.
- **An `IF` with a test without a handler** leaves the previous result; the disc does not use them (verified).

## Where it is in the decomp

- `include/dw/script.h`: opcodes, operands, `ScriptState`, movement types.
- `include/dw/trigger.h`, `include/dw/pstat.h`: names of triggers and variables.
- `src/main/script_interp.c`: `callScriptSection`, `runMapHeadScript`, `tickScript`, `scriptInstruction10to27`,
  `28to3F`, `46to58`, `5Ato5F`, `64to7E`, `FBtoFF`, `scriptIfInstruction`, `scriptPauseGame`, `returnFromScriptFile`,
  `pushScriptStack`, `tickScriptedMovement`, `getTriggerOffset`.
- `src/main/script_textbox.c`: `showTextbox`, `scriptShowSelection`, `tickSelectionDialogue`, `tickScriptDialogueBox`,
  `tickConfirmDialogue`, `tickTextboxHandling`.
- `src/main/script_value.c`: `IF` comparisons, `getStatsPointer`, `scriptIdToEntityId`.
- `src/main/script_common.c`: `resolveMapHeadEntry`, `handleItemLoss`, `lostAllLives`, `setDialogueOwner`, shops.
- `src/shop/script_menu.c`: shops, merits, jukebox, item keeper, `species_e`, `newGameStateMachine`.
- `src/main/tamer.c`: `checkMapInteraction` (talking and cells), `startBattle`, movements.
- `src/main/map.c`: `changeMap`, `scriptTickChangeMap`; `src/main/main.c`: new game and load.
- `src/main/partner_impl.c`: death by sickness and by old age; `src/main/btl.c`: `handleBattleStart`.
- `src/main/tournament.c`, `src/dget/`, `utils2.c`: tournaments.
