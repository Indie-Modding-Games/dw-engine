# Overlays

The 16 `*_REL.BIN` files at the disc root are code and data that the executable loads whole at a fixed address
when a scene needs them (`utils2.c: loadDynamicLibrary`, which does not look at the file). How they are laid out
inside comes from the decomp's build (`config/<ver>/<overlay>.yaml` and `overlay.ld`); `formats/exe/overlay.hpp`
stores it per version, without data. The load addresses are in [Tables](tables.md).

## Structure

| Part | What it is |
|---|---|
| Embedded files | BTL, STD and VS only: `ETCDAT/EX.TIM`, `ETCDAT/EX.TMD`, `ETCDAT/FEEL_EF.TMD`, `ETCHI/MAHI.TMD` and `ETCDAT/DEFEFF.TMD`, one after another with no padding in `[0, 0x4188)` |
| Header (0x40 bytes) | `"MWo1"`, an id byte, the address of the header itself (load + offset), the code size, the data size and a NUL-terminated ASCII name; the rest are zeros |
| Code | From the end of the header to the first `.rodata`/`.data` |
| Data | The code's constants and tables |
| Saved memory | The `*_bss_us` sources: memory the overlay only uses at run time, saved with what it held when the file was generated |
| Final word | A zero (`LONG(0)` in `overlay.ld`, or inside the last array of the saved memory) |

`readOverlayHeader` checks the signature, the zeros and the name's NUL. `readOverlayParts` cuts the file using the
header and the version, and notes what does not fit: an address that is not load + offset, sizes that do not end 4
bytes before the end, a saved memory outside the data or a final word that is not zero. If the layout brings a map
of the data (SHOP only), it also checks that its ranges cover the data from end to end and that each one holds what it
should: jump tables point into the code, pointers into the data (or are zero) and texts end in zero.

- The header id matches the decomp's `Overlay` enum up to SHOP; from DGET on it is one less (DGET has the same one as
  SHOP).
- The header name does not always have the same capitalization as the file (`DoO2_REL.BIN`, `DoOA_REL.BIN`,
  `EaB_REL.BIN`).
- Neither DGET nor SHOP has saved memory.
- The `SHOP_REL.BIN` on the US disc is the decomp's `jp_rev1` build, byte for byte (the SHA-1 of
  `config/jp_rev1/shop.yaml`). The US executable links its own copy of that code (`shop/script_menu` in
  `config/us/main.yaml`) and never jumps to the overlay, although `script_common.c: checkShopMap` and the script op
  `0x23` (`script_interp.c`) still load the file. Its symbols are in `config/jp_rev1/symbols_shop.txt`.
- The saved memory of TRN and TRN2 is all zeros; that of the others, leftovers of the build's memory. MOV's is the
  video decoder's buffers (`mov_bss_us.c`).

## SHOP's data

The decomp does not build SHOP for the US, so its layout carries a map of the data (`OverlayDataRange`), made by hand
from the overlay's code: each `lui`/`addiu` or `lui`/`lw` pair that points to the data and each `jr` through a
table. It covers `[0x7860, 0x7E0C)` entirely; the names are those of `symbols_shop.txt`.

Jump tables, `[0x7860, 0x79F8)`: each one is that of a `switch`, with the index bounded by a `sltiu` with its number
of cases, and all its entries fall inside the function.

| Offset | Function | Cases |
|---|---|---|
| `0x7860` | `openRecycleShop` | 10 |
| `0x7888` | `rollCardPack` | 5 |
| `0x789C` | `openBuyCardMenu` | 5 |
| `0x78B0` | `openSellCardMenu` | 5 |
| `0x78C4` | `openMeritShop` | 14 |
| `0x78FC` | `tickItemKeeper` | 7 |
| `0x7918` | `openJukebox` | 5 |
| `0x792C` | `tickBirdraTransport` | 7 |
| `0x7948` | `openMojyamonShop` | 7 |
| `0x7964` | `newGameStateMachine` | 32 |
| `0x79E4` | `tickNamingBox` | 5 |

Data, `[0x79F8, 0x7E0C)`:

| Range | What it is | Read by | Copy in the executable |
|---|---|---|---|
| `[0x79F8, 0x7A78)` | Jukebox titles: font and track, one byte each (`MAIN_D_801303B8`) | `jukeboxFillTitleList`, `tickJukeboxMenu` | `0x801303B8`, identical |
| `[0x7A78, 0x7A84)` | The 12 items the swap asks for (`MOJYAMON_ITEMS_GIVE`) | `mojyaTradeFillItemList` | `0x80130438`, identical |
| `[0x7A84, 0x7A90)` | The 12 it gives in exchange (`MOJYAMON_ITEMS_GET`) | `mojyaTradeFillItemList` | `0x80130444`, identical |
| `[0x7A90, 0x7AD4)` | The four texts of the name box (`STR_YOUR_NAME` to `STR_BACK_OK`) | `updateNamingPreview`, `fillNamingMenuStrings` | no |
| `[0x7AD4, 0x7DB0)` | Character rows of the name box (11 bytes and a zero each), in six groups, each group followed by its table of 9 row pointers (`CHAR_PAGE3_LEFT` to `CHAR_PAGE1_RIGHT`) | via `NAMING_CHAR_PAGES` | the rows yes, from `0x80130450`; one byte of a row differs |
| `[0x7DB0, 0x7DC8)` | The six row tables (`NAMING_CHAR_PAGES`) | `tickNamingBox`, `fillNamingMenuStrings` | no |
| `[0x7DC8, 0x7DEC)` | 18 `s16`: positions of the box's controls (`NAMING_CTRL_BOXES`) | `renderNamingBox`, `renderSelectionBox` | no |
| `[0x7DEC, 0x7E00)` | 10 `u16`: where the cursor jumps when it leaves each row (`NAMING_ROLLOVER_CHARS`) | `namingSelectionLeft`, `namingSelectionRight` | no |
| `[0x7E00, 0x7E0C)` | 6 `u16`: the same on the controls (`NAMING_ROLLOVER_CTRL`) | `namingSelectionLeft`, `namingSelectionRight` | no |

None of this is an asset: the US executable does not run this code and has its own copies of the game tables. The
jukebox one already comes out of the executable (`music`); the swap ones do not (see below).

## Tables

Four overlays have game tables; `overlay_tables.hpp` reads them with `MemoryImage::fromOverlay` and
`overlay_yaml.hpp` writes them as YAML, with the ids from the executable's tables for items, maps, techniques and
Digimon. Each document has its schema in `content/schemas`.

| Overlay | Document | Tables |
|---|---|---|
| FISH | `fishing` | Probability of each bait, bite probability by depth and fish type, fish types, the two shore curves of the water test and the two fishing maps (zones of each fish, times and route of the sea serpent, sprite) |
| STD | `stadium` | Base stats of the rivals per tournament, TIM of each tournament's grade, win probability by levels, cameras, arenas and the tournament bracket (`bracket`: rows, jump, paths of each slot, sprite palette and shot of each species) |
| VS | `versus` | Cameras and arenas |
| TRN | `training` | Hit animation and frame per partner, techniques that training teaches and their weights, slot game strips, assists and training points per map |

- Variables of 8 bytes or less from those sources go to the executable's `.sdata`, not to the overlay. That is why the
  number of objects and TIMs of each arena is read from the executable (`STD_ARENA_COUNTS`, `VS_ARENA_MODEL_COUNTS`,
  `VS_ARENA_TIM_COUNTS`).
- An arena without files (the three pointers null) is not written; one with some and not others is an error.
- `FishingMapData` stores two function pointers: the water test is written as the index of the curve it follows
  (`FISH_isFishableWater1` interpolates by x, `FISH_isFishableWater2` by y); the other is a code stub. The number of
  zones per map (6) is fixed by the code.
- The bite table is 200 bytes: 33 rows of 6 types starting at depth `0x26`; the 2 leftover bytes are stored in
  `unused`.
- Training points end at map `-1` and those of each map at stat `-1`.
- TRN2 is another build of training: its tables are those of TRN, without the techniques. The extractor checks this
  and notes it; if they differ, it is left partial.

These are not converted, because they are code data and not game data: jump tables, message texts, interface sprites
and UVs, battle text positions, effect and camera constants, the Digimon names of the tournament introduction
(`STD_INTRO_DIGIMON_NAMES`) and the bracket's routes (`STD_BRACKET_PATHS`). Nor are those in the executable's `.sdata`
that are not game tables, nor `REINCARNATE_BABY_TYPE` (DOOA).

## Extractor

`dwx` (the `overlay` extractor) accounts for each part in the report's notes, with its byte range:

- Embedded files: they are compared with the loose file; if they are equal, a note (`same bytes as ETCDAT/EX.TIM`)
  and the asset comes from that file. If they differ or the loose one is missing, it is an `unknown`.
- Header, code (not an asset), data, saved memory and final word: one note each.
- SHOP: one more note with what the executable does with the file, and one for each range of its data map. Ranges
  with a copy in the executable are compared with it and the note says whether they are equal or how many bytes differ.
- The tables of FISH, STD, VS, TRN and BTL: `data/fishing`, `data/stadium`, `data/versus`, `data/training` and
  `data/battle` (the drawing of each species' Flat shot).
- The following are `unknowns`: what `readOverlayParts` notes (also the data map ranges that do not fit), an overlay
  without a layout for the version, a range with a copy that could not be compared (no executable) and the tables
  that could not be read.

## What is not known

- The swap tables (`MOJYAMON_ITEMS_GIVE` and `MOJYAMON_ITEMS_GET`) are not converted to YAML from the executable,
  which is where the US reads them.
- The `unkN` fields of the fishing tables, which are written as `unknown_*`.
