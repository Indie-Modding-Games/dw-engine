# Battle effects

How `formats/effect` reads `ETCHI/EFEDAT.EFE`, the file of the techniques' effects, and what `dwx extract` gets out of
it. The field names are those of the decomp (`include/dw/battle_effect.h`, `src/btl/battle_effect.c`); functions are
cited as `file: function`.

## The file

It is 122 slots of 0x5000 bytes, one per technique: slot `n` is the effect of technique `n` and its effect id is
`0x100 + n`. The original loads a slot with a `CdRead` of 0x5000 bytes at offset `n × 0x5000`
(`BTL_loadEFESlot`) and prepares it in `BTL_setupLoadedEFEFile`.

- Slot 121 is all padding (`0xCD`): no technique uses it.
- Slot 110 stores the id `0x158`, that of slot 88, although its content is different. `BTL_setupLoadedEFEFile`
  compares the stored id with the requested one and, if they do not match, marks the slot as failed (`isLoaded = -3`)
  and does not use it: that technique comes out with no effect. `dwx` extracts it as is and says so in the report and
  in its `effect.yaml` (`loads: false`).

`efeSlotCount` requires a whole number of slots; `efeSlot` gives the bytes of one, and `isEmptyEfeSlot` says whether
it is all padding.

## Header

`EfeFileHeader`, 0x34 bytes, all `s32`. Offsets count from the end of the header (the base, which the
decomp calls `EFE_SCRIPT_HEAD`):

| Offset | Field | What it is |
|---|---|---|
| 0x00 | `startScript` | script the effect starts with (`BTL_startEFE`) |
| 0x04 | `initScript` | script that runs while the slot loads, four instructions per frame |
| 0x08 | `uvAnims` | room of `EfeUvAnim`, 0x20 bytes per TMD object |
| 0x0C | `uvAnimsEnd` | |
| 0x10 | `subEffects` | room of `EfeSubEffect`, 0x1C bytes each |
| 0x14 | `tmdStart` | the TMD (empty if the slot has no model) |
| 0x18 | `tmdEnd` | |
| 0x1C | `timStart` | the TIM |
| 0x20 | `timEnd` | |
| 0x24 | `dataEnd` | end of what the header addresses |
| 0x28 | `spawnCount` | `unk28` in the decomp: no code reads it; it is the number of `spawn` instructions in the slot |
| 0x2C | `numSubEffects` | records in the sub-effect room |
| 0x30 | `effectId` | `0x100 +` slot |

`spawnCount` is named that because in the 121 used slots it matches the number of `spawn`s (and of instance
tables); the original does not use it.

`readEfeHeader` checks that the parts go in this order and without gaps, which is how the original tool leaves them:

```
0 ≤ startScript ≤ initScript ≤ uvAnims ≤ uvAnimsEnd = subEffects ≤ tmdStart ≤ tmdEnd = timStart ≤ timEnd = dataEnd
```

as well as that `dataEnd` fits in the slot, that the UV room is a whole number of records and that the sub-effect one
is `0x1C × numSubEffects` long.

## Parts of a slot

| From | To | Content |
|---|---|---|
| 0 | `startScript` | scripts' data area: variables, tables, lists, instances |
| `startScript` | `uvAnims` | the two scripts, one after the other |
| `uvAnims` | `tmdStart` | work rooms: UV animations and sub-effects |
| `tmdStart` | `timStart` | TMD, with offsets relative to its object table |
| `timStart` | `dataEnd` | 4-bit TIM |
| `dataEnd` | end of slot | padding |

- **Work rooms.** What the file stores there is not data: the loader clears `EfeSubEffect.inst` and
  `EfeUvAnim.unk0` and `numKeyframes`, and the subopcodes write the remaining fields before reading them. Many rooms
  carry old bytes from the tool.
- **Padding.** After `dataEnd` everything is `0xCD` except in slots 110 and 112, which have leftovers: blocks
  equal to bytes of other slots (55, 78, 89). Nothing addresses them; the report notes them.
- **TMD.** `efeModelGaps` checks that the header, the object table, the primitive packets, the vertices and
  the normals cover the whole TMD.

## Textures in VRAM

The original ignores the positions the TIM carries. It can have 16 effects loaded at once, each in its VRAM cell
(`BTL_setupLoadedEFEFile`):

- pixels: page `0x19 + cell / 8`, at `x = (page % 16) × 64`, `y = (page / 16) × 256 + (cell % 8) × 32`;
- CLUT: `x = 128 + (cell / 4) × 16`, `y = 488 + (cell % 4) × 6`.

When loading the TMD it rewrites it for its cell with `graphics.c: updateTMDTextureData`: in each object, up to the
first untextured primitive, it changes the texture page to the cell's, adds the cell's offset within its page to the
UVs and moves the CBA from the CLUT of cell 0 (`0x7A08`) to that of the cell. `efeVramCell` gives the
cell and `placeEfeModel` does the same rewrite. The TIMs on the disc already carry the position of cell 0, so the
models are textured as cell 0 sees them.

## Scripts

An instruction starts with a `u16`: the opcode in the low byte and a variant in the high one. The interpreter
(`BTL_runEFEScript`) dispatches through the table of 18 opcodes; each handler advances a fixed number of bytes, so the
two scripts are decoded straight through, without following jumps (`decodeEfeScripts`). A script can end with a
`0xCDCD` after the last `return` (73 cases in the US): it is padding and is noted.

A variable is a pair `(s16 offset, u16 flag)`: with flag 0 it counts from the data of the instance that is running,
with another value (`0xDAAD` in practice) from the base. The width goes in a nibble of the variant: 4 is `s32`, 2 is
`s16` and anything else `s8`. There is a 32-bit register and a data stack.

| Opcode | Name | Bytes | Operands | Handler |
|---|---|---|---|---|
| 0x00 | `load_immediate` | 6 | `s16` value to the register | `BTL_loadEFEImmediate` |
| 0x01 | `load_variable` | 6 | variable to the register | `BTL_loadEFEVariable` |
| 0x02 | `load_random` | 2 | random number to the register | `BTL_loadEFERandomValue` |
| 0x03 | `load_indexed` | 10 | table (from the base), index: `table[index]` to the register | `BTL_loadEFEIndexedVariable` |
| 0x04 | `apply_operator` | 6 | variable `op=` register | `BTL_applyEFEVariableOperator` |
| 0x05, 0x06, 0x08 | `stop`, `stop_6`, `stop_8` | 2 | stops the script | `BTL_stopEFEScript` |
| 0x07 | `branch_if` | 10 | variable, target: jumps if `variable cmp register` | `BTL_branchEFEOnComparison` |
| 0x09 | `jump` | 6 | target | `BTL_jumpEFEScript` |
| 0x0A | `push_immediate` | 6 | `s16` value to the stack | `BTL_pushEFEImmediate` |
| 0x0B | `push_variable` | 6 | variable to the stack | `BTL_pushEFEVariable` |
| 0x0C | `push_address` | 6 | address of a variable to the stack | `BTL_pushEFEVariableAddress` |
| 0x0D | `call` | 6 | target | `BTL_callEFESubroutine` |
| 0x0E | `sub` | 2 | subopcode in the variant | `BTL_dispatchEFESubOpcode` |
| 0x0F | `return` | 2 | | `BTL_returnFromEFESubroutine` |
| 0x10 | `pop_to_variable` | 6 | pops from the stack into an `s32` variable | `BTL_popEFEValueToVariable` |
| 0x11 | `spawn` | 12 | how many, instance table, stride | `BTL_spawnEFESubEffect` |

Details of each one:

- **Immediates** (0x00, 0x0A): the handler reads a single `s16`. The tool wrote a 32-bit literal, whose high half
  (the second `u16`) is almost always the sign extension; when it is not, the YAML shows it as `value_high`.
- **`apply_operator`**: the low nibble chooses the row of `BTL_EFE_VARIABLE_OPERATORS` (rows 0, 1 and 3 are `s8`, 2 is
  `s16`, 4 is `s32`) and the high one the operation: `set`, `add`, `subtract`, `multiply`, `divide`, `modulo`,
  `shift_left`, `shift_right`.
- **`branch_if`**: the high nibble chooses the comparison from `BTL_EFE_COMPARISONS`: `equal`, `not_equal`, `less`,
  `less_or_equal`, `greater`, `greater_or_equal`. The fifth `u16` is a flag that nobody reads.
- **`load_indexed`**: the low nibble is the width of the elements and the high one that of the index; the table's flag
  is not read. The table's length is not stored anywhere.
- **`push_address`**: the handler does not read the variant; the tool left there the width of what it points to (4, 2
  or 1) or padding. The YAML shows it as `declared_width`.
- **`call`** pushes target, context and return onto a separate call stack; the data stack is kept, so the caller
  passes arguments through it.
- **`spawn`** looks for a free instance in the table (`count` entries of `stride` bytes, each an `EfeInstance`
  followed by the sub-effect's variables) and continues in its context; if none is free it does a `return`. The
  variant numbers the slot's `spawn`s from 0 (the tool left it that way; it is not read). The fourth and sixth
  `u16` are not read.
- The `u16`s the handler does not read are checked against their usual value (`efeIgnoredOperands`); if one is
  different, the YAML shows it.

`decodeEfeInstruction` rejects an opcode from 0x12 up, an instruction that does not fit in its script, an operator
row or comparison outside its table and a subopcode from 0x61 up.

### Entry points

- `startScript` is executed by `BTL_startEFE`; `initScript`, by the load.
- Subopcode 2 (`set_sub_effect_scripts`) sets in the context the per-frame script (`inst`, which `BTL_tickEFEEngine`
  runs) and the drawing one (`someInst`, `BTL_renderEFEEngine`). Both arrive on the stack as `push_address` from the
  base.

## Subopcodes

All 97 are in `packages/formats/src/effect/efe_subops.cpp`, each with its handler and its arguments in the order in
which they are pushed. The names come from what the handler does with each argument. None has variable arity and none
leaves anything on the stack. Subopcode 4 pushes seven values and calls 24, and 6 pushes −1 and calls 91; their
arguments are the net ones. `EFERotateVector` (7) and `EFECreateFlash` (18) are in `src/main/efe.c`.

An argument that is an address says how much the handler reads:

| Type | What is at the address |
|---|---|
| `int32` | an `s32` that the handler reads or writes |
| `vector`, `color` | three `s32` (`EfeVector`, `EfeColor`) |
| `bone_offsets` | 10-byte `EfeTechBoneOffset` (Digimon type, bone, offset) up to a type < 0 |
| `parallax_sprites` | 10-byte entries up to one with size < 0 |
| `uv_keyframes` | `s32` n and n `EfeUvKeyframe` (`s16` frames, `s8` du, dv) |
| `parent_field` | as many bytes as the `size` argument says (12, 8, 4, 2 or 1) |
| `int32_array` | as many `s32` as the count argument says |
| `clut_colors` | as many colors `{u16 r, g, b, stp}` as the count says |
| `clut_row` | 16 colors from the index given by another argument |
| `tile_map` | 11 × 9 tile ids |
| `tile_offsets` | one `s16` per id up to the largest in the tile map |
| `ribbon_points` | 10 points of four `s16` |
| `quad_corner` | with transparency < 0, an 8-byte point; otherwise, the argument is a number |
| `code` | a script |

Two arguments remain with no meaning, named `unknown0`:

- subopcode 1 (`init_uv_anim`): goes to `EfeUvAnim.unk0`, which only `BTL_unloadEFESlot` reads to clear it;
- subopcode 85 (`render_ribbon`): goes to `EFE_RIBBON_SCRATCH->width`, which nothing reads.

Those the handler pops from the stack and does not use are called `unused` (subopcodes 11, 36, 67 and 89).

## Data area analysis

The data area has no index; only the scripts say what each byte is. `analyzeEfeScripts`:

1. Gathers the targets of jumps, calls and code arguments as labels; a target that falls between instructions is a
   problem.
2. Walks the instructions with a symbolic stack (empty at each label and after a jump, call, `return` or `stop`) and
   joins each subopcode argument and each `pop_to_variable` with the instruction that pushed it. A value that comes
   from the caller is left unjoined. It repeats until no new labels appear.
3. Types the data: base variables that the instructions read or write, `load_indexed` tables (up to the next covered
   byte), `spawn` instance tables and what each address argument points to according to its type. An address pushed
   for something else uses its `declared_width`. If the argument that gives the size or index of a read is not a
   pushed number, the read has no known end; with one exception: an index that the same block leaves as
   `load_immediate n` and `apply_operator variable modulo` takes the values 0 to n − 1 (the dividend on the disc is the
   instance's frame counter), so the CLUT row covers the n it can start at.
4. Fills in the gaps: if they are all `0xCD` they are padding; if not, they are unused data (`unused`), unless a read
   with no known end starts before them, which could reach them: then they count as unknowns.

### Unused data

The interpreter only forms addresses in the data area in three ways, and the analysis follows all of them:

- an instruction operand added to the base (`EFE_SCRIPT_HEAD + ip[n]` in the `battle_effect.c` handlers), or to the
  instance that is running, for variables with flag 0;
- `load_indexed` (`BTL_loadEFEIndexedVariable`), which adds index × width to the operand;
- a pointer pushed by `push_address` (`BTL_pushEFEVariableAddress`) and from which a subopcode reads what its
  argument's type says.

The C code does not read the area in any other way: `EFE_DATA_ITERATOR->data` is only used to set `EFE_SCRIPT_HEAD`.
Scripts also do not store pointers to do arithmetic with them: the two `pop_to_variable` in the US pop values passed
by the caller. Instance variables have positive offsets and no instance table comes before a gap.
A `load_indexed` table extends up to the next covered byte, so a gap never follows it: to reach it the index would
have to skip over data the script uses as something else. In tables with a bounded index it is clear that the tool
measured them exactly: slot 77 reads the 6-`s16` table at `0x12c` with the instance's counter, and its frame script
(`l_1938`) frees it as soon as it goes past 4. A gap before a table would require a negative index, and indices are
counters.

With the US disc, 40 unused ranges remain, 590 bytes in 33 slots. They are written whole in `script.yaml` and the
report notes them slot by slot. They are of these kinds:

| What is there | Slots |
|---|---|
| Vectors or colors of three `s32` that no `push_address` points to, next to others that are used | 10, 26, 29, 32, 34, 56, 77, 81, 86, 91 |
| The block of six `s32` `76, 69, 3, 13, 6, 0`, the same in all | 1, 33 and 61 at the start; 77 at `0x198` |
| Six `s16` in a ramp (130 to 592, or 130 to 905) at the start | 112 to 120 |
| Other `s16` tables that no `load_indexed` reads | 3, 29, 54, 76, 104 |
| One more keyframe than the count says: `BTL_tickEFEUVAnimation` goes back to the first on reaching `numKeyframes` | 34 (`0x40`, `0x54`) |
| Bone lists nobody pushes (one entry and its end, or just the end) | 62, 98 |
| Color 16 after a window of 15: `BTL_loadClutColors` copies `count` colors and the scripts always push 15 | 31 |
| What follows slot 14's CLUT row: the last color of the double palette and 28 more bytes | 14 |
| Two or four zero bytes after a variable or a vector | 86, 94, 102, 109 |

Slot 14's CLUT row is the only one that is read: `queue_clut_load` (`BTL_addClutLoadPrim`) copies 32 bytes
from the colors plus two times the index, and the index is the instance's counter modulo 16. The palette is there
twice in a row so that the window rotates, and the row covers 62 bytes of a gap that was 92 long.

## What `dwx` writes

For each used slot, in `effect/<technique>/` (the technique id from `data/moves`, or the slot with 3 digits without
tables):

| File | Content |
|---|---|
| `effect.yaml` | slot, technique, header, number of UV animations, whether the original loads it (`loads`) and the files in the folder that it wrote: `texture` (`texture.png`) and `model` (`model.glb`, only if it has a TMD) |
| `script.yaml` | `start_script` and `init_script` instruction by instruction, `padding` and `data` |
| `texture.png` + `texture_meta.yaml` | the TIM, as in [TIM and packs](tim.md) |
| `model.glb` | the TMD rewritten for cell 0 and textured with the slot's TIM in that cell |

In `script.yaml` each instruction carries `at` (offset from the base), `op` and its operands already decoded; targets
are labels (`start`, `init`, `l_<offset>`) and each `sub` lists its arguments by name with what was
pushed: a number, a variable (`base+0x0040`, `instance+0x0008`), an address (`&base+0x0040`) or a label. An
unjoined argument is `null`. The data area goes element by element with its type and values; a value that is
padding (a variable with no initial value) is `null`. Unused data goes as `kind: unused` with its bytes and the
reason (`reason`); a CLUT row with a computed index carries `row_starts` and all the colors it can read.

At game time it is read by `assets::readEffectScript` (`packages/engine/assets/include/dwe/assets/effect_script.hpp`), without
going through `formats`: together with `effect.yaml` it gives an `EffectScript` with the technique (`move`), the number of sub-effects,
the instructions of the two scripts and the image of the data area, from the base to the start script, with the values
of variables, tables, instances and numeric arguments; what only drawing reads (bones, sprites, UVs, CLUTs, tile
maps, ribbons) is left as padding. For each `sub` it stores the name and how many values it pops from the stack. It
rejects an unknown `op`, operator or comparison and data outside the area.

An invented example:

```yaml
start_script:
- label: "start"
  at: "0x0010"
  op: push_immediate
  value: 0
- at: "0x0016"
  op: push_address
  address: "base+0x0000"
  declared_width: 4
- at: "0x001c"
  op: sub
  sub: draw_tmd
  args:
    object_index: 0
    scale: "&base+0x0000"
data:
- at: "0x0000"
  kind: vector
  size: 12
  values:
  - 4096
  - 4096
  - 4096
- at: "0x000c"
  kind: fill
  size: 4
```

## Status with the US disc

- 121 used slots with 52530 instructions; 89 have a TMD. This gives 121 `effect.yaml`, 121 `script.yaml`, 121 PNGs with
  their YAML and 89 GLBs (all 89 pass `cgltf_validate` and have a texture). 29 PNGs and 17 GLBs repeat others: several
  techniques share a texture and model.
- All instructions decode, all arguments join with what pushes them and all pointers to code fall on
  instructions.
- The file is left **extracted**: every byte of the data area is typed, is padding or is an unused datum of those in
  [Unused data](#unused-data) (40 ranges, 590 bytes in 33 slots), which the report notes.

It is checked with `DWE_DISC` in `apps/dwx/tests/effect_real_disc_tests.cpp`.
