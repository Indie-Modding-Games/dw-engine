# Event scripts

How `formats/script` reads the event scripts (`SCN/MAPHEAD.SCN` and `SCN/DG.SCN`) and how `dwx extract` writes them.
The disassembler follows the opcode set of `include/dw/script.h` and the operand reads of
`script_interp.c: tickScript` and its `scriptInstruction*`. What the interpreter would not execute (an opcode with no case,
an `IF` test with no handler, operands that go out of bounds) is an `unknown`, never a guess.

## Files

- **`MAPHEAD.SCN`** is script 0: `initializeScripts` reads it whole and leaves it resident.
- **`DG.SCN`** stores the rest. `initializeScripts` reads its first 0x2000 bytes as a table of `u32` offsets by script
  id, and `getScript(id)` reads `[table[id], table[id + 1])`. `readScriptTable` walks the table up to the entry
  that equals the file size and fails if an offset goes backward or out of bounds. The table bytes after that entry are
  leftovers that nobody reads (the 0x2000-byte read also overwrites the start of the first scripts).
- Each script takes whole sectors: after its code comes **sector padding**, leftovers that often look like code and
  that nobody reads. The real end is given by the sweep (below), never by the size of the gap. `dwx` counts it in a note.
- Script 0 of `DG.SCN` is a byte-for-byte copy of `MAPHEAD.SCN` (plus padding) that is never loaded: `getScript(0)`
  returns the resident one. `dwx` checks this and extracts it only from `MAPHEAD.SCN`; if it were not a copy, both
  would be `story/000`, so the extraction of `DG.SCN` stops with a message instead of choosing one.
  Only the bytes of `MAPHEAD.SCN` are compared with the start of the slot: the rest is sector padding and is not looked at.

## Header

```
+0  u16 codeStart                     = 2 + 4 * sections + 2: the offset of the first instruction
+2  {u16 id, u16 offset}[sections]   offsets from the start of the script
    u16 0xFFFF
```

`readScriptHeader` checks that `codeStart` is the end of the list and that each section starts inside the code.
A section is an entry point that the engine chooses (`getScriptSection`, `callScriptSection`): in `MAPHEAD`, the map
id (and some special sections that the engine's code calls); in the others, `0xFE` when entering the map, the script
id of an NPC when talking to it and the tile triggers.

## Instructions

An opcode byte and its operands, in little-endian. 16- and 32-bit operands are read aligned, so the
encodings carry padding bytes that the interpreter skips; one-byte instructions (`FC`–`FF`) are followed
by a `00`. Every instruction starts at an even offset, except the `DATA` blocks (below). Jumps are absolute `u16`
offsets from the start of the script.

Operand types (`ArgumentKind`):

| Type | Bytes | What it is |
|---|---|---|
| `Pad` | 1 | padding; if it is not 0 it is stored in `pads` |
| `U8`, `U16`, `S16`, `S32` | 1, 2, 2, 4 | numbers; `S32` is read in two halves (`pollNextInt`) |
| `Pstat` | 1 | index of a script variable (`ScriptState.pstats`) |
| `Trigger` | 2 | number of a trigger bit (`ScriptState.triggers`) |
| `Entity` | 1 | script id of an NPC; `0xFD` is the tamer and `0xFC` the partner (`scriptIdToEntityId`) |
| `Label` | 2 | target of a jump |

The opcode names are the decomp's `SCRIPT_OP_*` in lowercase (`show_textbox`, `jump_to`...); those without a name
there are `unused_XX`. Groups (`tickScript` dispatches `10`–`3F`, `46`–`58`, `5A`–`5F`, `64`–`7E` and `FB`–`FF`):

| Opcodes | What they do |
|---|---|
| `10`–`1A` | flow: selection, jumps (`13` with return, `16` without it, `14`/`17` to another script), `15` return, `18` `SWITCH`, `19` `IF`, `1A` text |
| `1B`–`27` | dialogue, triggers, variables, date, text box size |
| `28`–`3F` | items, money, techniques, cards, merits, stats, date |
| `46`–`58` | Digimon and entities: load, move, turn, camera, animations, warps |
| `5A`–`5F` | sound and music |
| `64`–`7E` | engine routines, combat, waits, damage, `6B` `DATA`, map objects and sprites |
| `FB` | `SET_SCRIPT`: loads another script and map and pushes a type 3 frame |
| `FC`, `FD` | no case in `tickScript`: they wait one frame |
| `FE`, `FF` | `END_SECTION` and `END_SCRIPT`: both call `returnFromScriptFile` and unwind the whole stack |

The variable-length ones:

- **`IF` (`19`)**: after `19 00`, entries `code, padding, data` that are evaluated from left to right, up to an
  entry whose code is exactly `19` (`script_interp.c: scriptIfInstruction`, `script_value.c`). Bit 7 of the code:
  `result = result AND c`; otherwise, bit 6: `OR`; otherwise `result = c`. `code & 0x38` chooses the test:
  `00` trigger (low bits 0 = set, other = cleared), `08` variable against value (low bits = comparison),
  `10` / `18` jump if true / false, `20` extended by the low bits: stat, card, technique, condition,
  number of items and money. The comparisons are `eq`, `ne`, `ge`, `le`, `gt`, `lt` (0–5); 6 and 7 have no case.
  `28`, `30`, `38` and the extended 6 and 7 have no handler: they are `unknown`.
- **`SWITCH` (`18`)**: variable, `u16` number of cases and a `u16` target per case.
- **`SET_SELECTION` (`10`)**: `u8` number of options and a target per option, the word `1A 00` (which
  `scriptShowSelection` counts as one more entry) and the question text. If `12 00` follows, it is the mark that
  skips the cancellation (`tickScriptDialogueBox`) and is part of the instruction.
- **`SHOW_TEXTBOX` (`1A`)**: padding and text (see [Texts](texts.md)).
- **`DATA` (`6B`)**: padding and bytes up to a `0xFF`. It is never executed: tournaments read it through jump tables
  (`tournament.c: getCupDataJumpTableEntry`, `dget.c: isTournamentEnabled`, `tournamentCheckEligible`), which stop at
  the first byte `≥ 0xFE`; a list that starts with `0xFE` stands for all triggers (`FE FF`). Since they are read
  byte by byte, the blocks follow one another unaligned.

## Sweep

`disassembleScript` decodes linearly from `codeStart`:

1. It decodes the instruction; if it cannot, it notes an `unknown` and stops.
2. After an instruction that ends at an odd offset, a `00` is padding. Anything else that is not a `DATA` is a
   desynchronization: `unknown` and stop.
3. It stops at the first `END_SCRIPT` that lies beyond every section and every target seen. The arena scripts
   keep `DATA` blocks after their first `END_SCRIPT`, which their jump tables reach; the sweep continues
   through them.

Then it checks that each section and each target fall on the start of an instruction (if not, `unknown`) and counts the
instructions that no flow reaches from a section. Those stay in the listing: most are a
jump after an `END_SECTION` that the compiler left, and `MAPHEAD`'s jump tables would look dead.

On the US disc all code bytes decode, with 0 `unknown`, and the text of each instruction goes back to its
same bytes.

## Listing

`dwx extract` writes one YAML per script: `script/maphead.yaml` and `script/dg/<id>.yaml` (`script/dg/007.yaml`).

```yaml
code_start: 0x000c
code_end: 0x0020
sections:
- id: 0x00fe
  at: l_000c
- id: 0x0005
  at: l_001e
unreachable_instructions: 0
instructions:
- at: 0x000c
  label: l_000c
  op: if
  conditions:
  - test: pstat
    compare: ge
    pstat: 5
    value: 2
  - test: goto_if_false
    target: l_001e
- at: 0x0018
  op: entity_set_rotation
  entity: partner
  rotation: -200
- at: 0x001c
  op: end_section
- at: 0x001e
  label: l_001e
  op: end_script
```

- `at` is the offset; `label` (`l_<offset>`) only if something jumps there; then `op` and the operands by name.
- Targets are label names. Entities `0xFD` and `0xFC` come out as `tamer` and `partner`.
- `conditions` (the `IF` entries, with `combine: and|or`, `clear: true` for a cleared trigger and `code` only if
  the byte is not the one the fields would give), `cases` (`SWITCH`), `options` (`SET_SELECTION`), `text`,
  `cancel_marker: true` (the `12 00` mark), `data` (the `DATA` bytes in hex) and `pads` (padding that is not 0).
- `unknowns` only if there are any.

The stable id of a script text is the script and the offset of its instruction (`dg/007@0x0040`); the keys of the
story texts use it (`story_<n>.text.<offset>`, see [Lua](#lua)). The listing is not Lua: `lua_emitter.hpp` translates
it.

## Control flow

The step to Lua starts from the listing, not from the bytes, and cannot emit jumps: the converted Lua is
nested code that can be read and modified. `formats/script` prepares it in three steps:

1. **`ScriptListing`** is the representation of scripts that everything else uses: sections, offsets, the role of
   each command (jump, call, return, `IF`, `SWITCH`, `SET_SELECTION`, map change, end), their targets, their
   named operands and conditions and the text of `show_textbox` and `set_selection` (as the listing writes it;
   the options of a selection, one per line). It comes from the YAML (`readScriptListing`, a mod's) or from the
   disassembly (`listingOf`, `dwx`'s) with the same command table; a disc test checks that they give the same
   in all scripts, going through the YAML text. Only the `DATA` bytes and the padding stay in the
   YAML. `warp_to` and `teleport` end the flow (`FlowRole::MapChange`): nothing that follows runs. The YAML format
   does not change.
2. **`buildScriptFlow`** splits the code into basic blocks. An `IF` is a chain of branches, one for each
   `goto_if_*` entry, in its order. A `SWITCH` chooses among its cases; a `SET_SELECTION`, among its options and, on
   cancel, continues after the block. Routines are the sections and the targets of `jump_and_link`.
3. **`structureScript`** turns each routine into statements: `if`/`else` whose branches join at the
   post-dominator, natural loops with `break` to their exit, choices and tail calls to another routine. Code
   that several routines jump to, or several paths of one, becomes a **shared tail** (one more routine) if it has at
   least 3 commands; if it is shorter it is copied. What cannot be nested (a loop with two entries, an exit from two
   loops at once, too many copies) becomes a **state machine**: a loop that dispatches each block according to
   a variable. This way every routine is converted, without jumps.

On top of the tree, **`walkSection`** (`guarded_walk.hpp`) walks a section and gives each requested command along with
what is needed to reach it: the decisions of its `IF`, `SWITCH` and `SET_SELECTION`, the triggers it
sets on the way and those it knows on arrival. An `IF` whose tests are on triggers that the path knows takes its
branch without leaving a condition. A call to a routine that is already being walked is not followed: it leaves a
`RecursionTerm` term (the called routine) that says what it decides is not known, and the path continues afterward with what it
knew forgotten. A command that runs game code that can write triggers (`give_item`, `compare_date` and
the routines of `call_routine` and `call_digimon_routine` that do so, according to `routine_writes.hpp`) leaves a
`ForgetTerm` term and the path continues without knowing those triggers. They are used by the [script warps](../architecture/script-warps.md) (`script_warps.hpp`) and the map changes of
a section (`map_changes.hpp`).

Lua has no `continue`: a branch that returns to the loop head takes the rest of the body into the other branch. The
figures over the disc are in the [scripts sheet](../reference/scripts.md#shape-of-the-control-flow).

## Lua

`lua_emitter.hpp` (`formats/script`) writes each structured script as a Lua module, and `dwx extract` leaves it next to the
listing (which does not change: it is still the format that `dw1fe` edits):

| File | What it is |
|---|---|
| `story/<n>.lua` | The module: returns `{ script = <n>, sections = { [0x0051] = <function>, ... } }` |
| `story/<n>_lines.yaml` | For each line that runs a command, its offset in the listing ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/story_lines.schema.json)) |
| `text/en/story_<n>.yaml` | The texts of `show_textbox` and `set_selection`, with the key `story_<n>.text.<offset>` |

`<n>` is the script id with three digits, the same number the maps' NPCs use (`script_id`): `000` is
`MAPHEAD.SCN`, which the game loads as script 0, and `001`–`221` are those of `DG.SCN` (`script/maphead.yaml` and
`script/dg/<n>.yaml` in the listing). The names of the functions and keys are in
[the story API](../modding/story.md).

The shape of the module:

- Each routine (section, subroutine, shared tail) is a function of the `routines` table, named by the offset of
  its entry (`routines.at_04c0`); those that start at the same command are one.
- If the section table repeats an id, `sections` carries only the first, which is the one `getScriptSection` finds;
  the others are left as a comment. On the disc this happens in some scripts (4 and 52, for example).
- Each command is a call `story.<name>(operands)` with the listing's operands in their order, without padding or
  labels, and a comment with their names if there are two or more. `<name>` is the command's, except for those the API
  renames ([table](../modding/story.md#names)). An entity is `PLAYER`, `PARTNER` or the NPC id.
- A routine returns `ENDED` if the script ended (`end_section` and `end_script` end it from any
  depth) and nothing on a `jump_return`. A `jump_and_link` is `if routines.at_X() == ENDED then return ENDED end`;
  a jump to shared code, the tail call `return routines.at_X()`; `jump_to_file`, `warp_to` and `teleport`,
  `return story.<name>(...)`.
- An `IF` is the fold of its entries from left to right with explicit parentheses
  (`(story.trigger(3) and story.var(5) >= 2) or story.money() < 4294967295`). Each constant goes in the domain in which
  the original compares: unsigned 32-bit, except happiness (stat 9), which is signed 16-bit. An entry without `and` or
  `or` replaces the previous result (`script_interp.c: scriptIfInstruction`, `result = cond`): the listing of
  `story/128` at 0x0094 says `trigger 8`, `trigger 9`, `and trigger 10` and the original only looks at 9 and 10, just like the
  Lua. The `if` then carries a comment with what was discarded
  (`-- the original drops story.trigger(8): a test with no and/or replaces the result`). It happens in `story/128` (0x0094,
  0x0100, 0x0276, 0x05a4) and `story/129` (0x0086, 0x0216); they look like typos in the original (they wanted all three
  triggers) and are kept as in it.
- A `SWITCH` is an `if`/`elseif` chain over `story.var(n)` in which an extra value goes to the last case; a
  `SET_SELECTION`, `local choice <const> = story.set_selection(key, n)` with the cancellation (`choice == n`) in the
  last `else`. If the choice does not have its end mark (`cancel_marker` in the listing; 23 of the game's 330), the
  original's cancel continues into the code that follows, that of the first option, and that `else` starts with
  `if story.cancel_closes_choice() then return ENDED end` (`CancelGuard`): the "Cancel closes choices" option
  ([game](../modules/apps/game.md#cancel-closes-choices)) ends the script there, and without it everything goes on as in the
  original. The Lua reader removes that line and leaves the choice without a mark; a choice without it is read with a mark (modules from
  before version 4 play the same). Loops are `while true do` with `break`; a state machine, a `while true do` that dispatches
  over a variable `stateN`.
- There are never jumps. The output is the same byte for byte for the same listing, whether it comes from the YAML or from the disassembly.

`dwx convert-script <listing> -o <folder>` converts a loose listing (the YAML a mod edits) with the same
code, and `dwx validate` compiles all the packages' Lua. The tests run each section in Lua, with a fake `story`
API, against a reference interpreter of the listing, and compare the calls and how it ends
(`packages/formats/tests/lua_emitter_tests.cpp`, and over the disc `lua_emitter_disc_tests.cpp`). The same tests check
that no generated name has a forbidden word (`ForbiddenNameWords`).

The test with random listings has two sizes. The per-commit one (`FastParity`: 120 listings, 4 worlds, about
5 s) cuts loops earlier: one that calls less than once every ~25 steps of the listing is cut before 200 calls,
and two cut executions are only compared on the calls that both made, so it does not see a difference
after the shorter cut. The full one (`FullParity`: 300 listings, 8 worlds, limits with which every loop with
calls reaches 200 and cut executions must match call by call) runs with `DWE_FULL_PARITY=1` and
takes minutes; the parity test over the disc always uses those limits.

## New game stats

No C code sets the stats the partner starts with: script 193 of `DG.SCN` sets them after the
introduction. It stores the partner's type in a variable (`store_digimon_type`), asks with an `if` whether it is a specific type
and, in each branch, a series of `set_stat` sets the eight values (attack, defense, speed, brains, max HP and
MP, current HP and MP); the first branch ends with a `jump_to` that skips the other. `findNewGameStats`
(`formats/script/new_game_stats.hpp`) finds that block in the listing and `dwx` writes it to `data/new_game_stats`
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/new_game_stats.schema.json)): `script`, `at` (the offset of the `if`), `partners` (the
Digimon it asks about, by its id, and its stats) and `others` (those of any other partner). If the script does not have the
block, it is noted and the document is not written. The session reads it when starting a game (`packages/session`).

## Coverage

A file is left `extracted` if all its scripts decode to the end of the code without `unknown`; sector
padding, table leftovers and the copy of script 0 are notes. Otherwise it is left `partial` with each `unknown` preceded
by its script.
