# Executable tables

How `formats/exe` reads the data tables of the main executable and how it writes them as YAML. The structures and
field names are those of the decomp (`include/dw/*.h`); a field the decomp does not name is called `unknownXX`, with XX
its offset inside the record.

## Addresses

`MemoryImage` presents an executable or an overlay as the console's memory sees it, to read at the addresses
the decomp gives:

- **Main executable:** the PS-X EXE header (`readPsxExeHeader`) gives the address and size of the text, which
  starts at byte 0x800 of the file. In the US the text is loaded at `0x80090800`, so
  `offset = address − 0x80090000`.
- **Overlays** (`*_REL.BIN`): they are loaded whole at a fixed address (`utils2.c: loadDynamicLibrary`, which does not
  look at their header), which is passed to `MemoryImage::fromOverlay`; their structure is in [Overlays](overlays.md).
  The US ones come from the first `vram` of `config/us/<overlay>.yaml` (and from `shop_START` in `utils2.c` for SHOP):

  | Overlays | Load address |
  |---|---|
  | BTL, STD, VS | `0x80052AE0` |
  | KAR | `0x80053800` |
  | EVL, ENDI, EAB | `0x80060000` |
  | FISH, DOO2 | `0x80070000` |
  | MURD | `0x8007C000` |
  | DOOA | `0x80080000` |
  | DGET, SHOP | `0x80080800` |
  | TRN, TRN2 | `0x80088800` |
  | MOV | `0x80010000` |

Every read checks that the bytes fall inside the image; a table or pointer outside it gives an error that names the
table and the index.

## Versions

`ExecutableLayout` stores, per version, the address and number of entries of each table (`executableLayouts`,
`findExecutableLayout`). Only the US is filled in, from `config/us/symbols.txt` and the array declarations in
`src/main`. EU, JP and JP rev. 1 exist with their tables unlocated (address 0): reading them gives an error until
someone fills them in from `config/<version>/symbols.txt`. The only known structural difference is that the EU does
not have the `char name[20]` name at the start of `DigimonPara` or `Item` (`inlineNames`).

## Tables

| Table | Entries (US) | Record | Index |
|---|---|---|---|
| `DIGIMON_DATA` | 180 × 0x34 | `DigimonPara` (`params.h`) | Digimon type |
| `PTR_DIGIMON_FILE_NAMES` | 180 `char*` | 4-letter MMD name | type |
| `DIGIMON_SKELETONS` | 180 pointers | `SkeletonBone {s8 objIndex, s8 parentIndex}` × `boneCount` | type |
| `MOVE_DATA` | 122 × 16 | `Move` (`move.h`) | technique |
| `MOVE_NAMES` | 122 `char*` | | technique |
| `MOVE_LEARN_CHANCES` | 58 × 3 | `u8` per specialty slot of the learner (`battle_ui.c`) | learnable technique |
| `ITEM_PARA` | 128 × 32 | `Item` (`item.h`) | item |
| `ITEM_DESCS` | 128 `char*` | | item |
| `ITEM_CLUT_DATA` | 128 `u8` | icon palette in `ETCDAT/ITEM.TIM`: CLUT row 0x1E8 + the value (`utils2.c: setItemTexture`) | item |
| `ITEM_FUNCTIONS` | 128 pointers | function that applies the item, or null (`item.c`) | item |
| `HEALING_VALUES` | 4 `s16` | HP or MP it heals (`handleHPHealingItem`, `handleMPHealingItem`) | item, or item − 4 for MP |
| `EVOLUTION_ITEM_TARGET` | 44 `u8` | target type (`handleEvoItems`) | item − 0x47 |
| `EVOLUTION_REQS` | 63 × 28 | `EvoRequirements` (`evl.h`) | target type |
| `EVOLUTION_STATS_GAINS` | 66 × 14 | `EvoStatsGains` | target type |
| `EVOLUTION_PATHS` | 62 × 11 | `EvolutionPath {s8 from[5], s8 to[6]}` | type − 1 (`evolution.c`, `game_menu.c`) |
| `RAISE_DATA` | 66 × 28 | `RaiseData` (`partner.h`) | type |
| `CARD_DATA` | 66 × 4 | `CardData` (`script.h`) | card |
| `CARD_PRICES` | 5 `s32` | | |
| `TOURNAMENT_NAMES`, `TOURNAMENT_GRADES` | 23 `char*` | | tournament |
| `TOURNAMENT_SCHEDULE` | 30 × 6 `u8` | tournaments of the day; `0xFF` ends (`dget.c`) | day % 30 |
| `TOURNAMENT_MEDAL_IDS` | 6 `u16` | | |
| `MEDAL_NAMES` | 15 `char*` | | medal |
| `MEDAL_DESCRIPTIONS` | 45 `char*` | three lines per medal | |
| `BGM_TRACK_NAMES` | 63 `char*` | music menu | |
| `ENEMY_MUSIC_MAPPING` | 48 `u8` | | |
| `SHOP_MAPS` | 8 `s16` | maps that load the shop (`checkShopMap`) | |
| `SHOPKEEP_DIGIMON_TYPES` | 11 `u8` | `0xFF` ends | |
| `MAP_ENTRIES`, `MAP_NAME_PTR`, `DOOR_MAPDATA`, `TOILET_DATA`, `MAP_LIGHT_UPDATE_DATA` | | see [Maps](maps.md) | |
| `DIGIMON_VLALL_SOUND_ID`, `DIGIMON_VBALL_SOUND_ID`, `MAP_SOUND_PARA`, `MAIN_D_801303B8` | | see [Audio](audio.md) | |

Notes:

- The shop has no stock table: item `i` is sold while trigger `0x180 + i` is active
  (`script_common.c: shopFillBuyItemList`).
- The participants of each tournament are not in the executable: they come from the map script, section 10
  (`tournament.c: getCupDataJumpTable`).
- `DIGIMON_SKELETONS` has null pointers for types without bones (`boneCount` 0); they are read as empty skeletons.
  A null pointer with bones is an error.
- In the US, the last pointer of `MOVE_NAMES` is null although `MOVE_DATA` has that technique. A null pointer in a
  text table is read as empty text.
- `RaiseData` has a padding byte at `0x0B` that is not stored.
- The game tables of the overlays (fishing, tournament, versus, training and battle) are in [Overlays](overlays.md).

## Item effects

`readItemEffects` (`formats/exe/item_effects.hpp`) gathers what the executable says about each item. `ITEM_FUNCTIONS`
gives each one's function, which is recognized by its address (`ItemCodeLayout::handlers`); `HEALING_VALUES` and
`EVOLUTION_ITEM_TARGET` give the numbers for healing and evolution. The values of food and chips are not in data:
each item is a case of a `switch` in `handleFood` or `handleChips` that only puts constants into local variables and
jumps to the common end.

`evaluateConstantSwitch` (`formats/exe/constant_switch.hpp`) reads them without copying code: it executes the function
from its entry with the item number in `a0` up to the address of the common end (`join`) and returns the values of the
variables the version gives (`ValueLocation`: a register or a stack slot). It only understands the instructions of
such a `switch` (additions, comparisons, loads and stores, jumps with their *delay slot* and jump tables); a
call, an instruction it does not know, a jump on an unknown value or a variable with no value stops the read and the
case is noted instead of guessed. In the US, all 35 food cases and 14 of the 16 chips are read: the Auto Pilot (it
calls the script) and the Portable Potty (it calls `handlePortaPotty`) are left without values and their logic is
given by the ruleset. `packages/formats/tests/item_effects_disc_tests.cpp` checks it against the disc.

## Texts

Texts are stored as they are in the executable: ASCII in the US and Shift-JIS (cp932) in the JP. The names in
`DigimonPara` and `Item` are `char[20]` inside the record; the others are tables of `char*`. A text ends at the first
NUL, which must arrive within 256 bytes at most.

When writing YAML, `escapeGameText` leaves printable ASCII as it is, writes the backslash as `\\` and any other byte
as `\xNN`. This way nothing is lost even if the text is not ASCII, and it can be decoded later. Each escaped text is
noted in the conversion notes.

## YAML

`tablesToYaml` converts the tables into thirteen documents (fifteen if the version has the two tables of the
[evolution tree](evolution-tree.md) located, today only the US), each with its schema in
`content/schemas/<name>.schema.json` (JSON Schema 2020-12):

| Document | Content |
|---|---|
| `digimon` | `DIGIMON_DATA` with the model's file name |
| `skeletons` | the bones of each type |
| `moves` | `MOVE_DATA` with its name, and `learn_chances` (`MOVE_LEARN_CHANCES`) on the techniques that can be learned |
| `items` | `ITEM_PARA` with its description and `icon_palette` (`ITEM_CLUT_DATA`): the palette of its icon in `texture/etcdat/item` |
| `item_effects` | what each item does: its function, what it heals, what it evolves into and the values of its food or chip case |
| `evolution` | requirements, gains and the evolution tree |
| `raise` | `RAISE_DATA` |
| `evolution_chart`, `evolution_chart_detail` | positions and icons of the tree and geometry of its detail ([Evolution tree](evolution-tree.md)) |
| `cards` | prices and `CARD_DATA` |
| `tournaments` | names, grades, schedule and medal ids |
| `medals` | names and the three lines of each description |
| `music` | music menu names and `ENEMY_MUSIC_MAPPING` |
| `shops` | maps with a shop and shopkeepers |
| `maps` | `MAP_ENTRIES`, doors, toilets, map names and `MAP_LIGHT_UPDATE_DATA` |

Rules:

- Keys are in `snake_case`; texts go in double quotes and numbers without quotes.
- What other tables reference has a **text id**: Digimon, by the name of their model
  (`PTR_DIGIMON_FILE_NAMES`, which does not repeat); techniques, items, tournaments, medals and music, by their name;
  maps, by their file name. The id is the name in lowercase with any other character converted to `_`
  (`makeIds`). If it ends up empty it is `<type>_<index>`, and if it repeats `_<index>` is appended. The original index
  is always stored in `index`.
- A reference is written as the id of what it references, `null` if it is the "none" value (`-1` or `0xFF`) or the
  number as is if it falls outside its table; the latter is noted.
- Lists with meaningful positions are written in full, with `null` in the gaps (the 16 techniques of
  `DigimonPara`, `from` and `to` of the tree, the 6 doors of `DOOR_MAPDATA`). Those that end in `0xFF` are cut there.
- `doors` and `toilet` of each map go from 1-based index (0 = none) to 0-based index or `null`.
- The same input always gives the same text.

The tests check each document against its schema with a minimal validator (types, allowed and required keys,
lists, `enum`, `const` and local `$ref`) written in the tests themselves, without new dependencies.

## Check with the disc

`packages/formats/tests/real_data_tests.cpp` runs only if `DWE_EXE_SAMPLE` points to the US executable (and `DWE_MAP_DIR` to the
`MAP` folder for the maps). With the US executable extracted from the disc: all tables are read, the fifteen documents
meet their schema and no text has escaped bytes. One out-of-table reference remains: a `-2` in
`EVOLUTION_PATHS`, which is neither a type nor the `-1` of "none".

## What is not known

- The meaning of `Move.unknown0D`–`unknown0F`, of `Item.unknown1E` and of `RaiseData.unknown0E`.
- The `-2` of `EVOLUTION_PATHS` (a special gap in the evolution chart?).
