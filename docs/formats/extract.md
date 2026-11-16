# `dwx extract`: the base package and the coverage report

`dwx extract` reads the user's disc and writes the **base package**: the assets in source format that the engine
mounts ([asset pipeline](../architecture/assets-pipeline.md)). Alongside them it leaves a coverage report with what
was extracted from each disc file and what is missing. The code is in `apps/dwx`; the readers, in `formats`.

## Usage

```sh
build/<preset>/apps/dwx/dwx extract [--out <folder>] [--force] [--progress] <image.cue|image.bin|image.iso>
build/<preset>/apps/dwx/dwx extract --cache <folder> <image>
```

- `--out`: where the package is written. By default `content/base`, which git ignores.
- The output folder has to be empty or have been written by `dwx extract` before (it carries the hidden file
  `.dwx-extract`). In that case it is emptied first, so that no files from a previous extraction remain.
- A folder with other files is rejected. With `--force` it is written over **without deleting anything**; if what was
  already there breaks the package, the final check says so.
- Nothing is written outside `--out`: output paths come only from already validated asset ids (lowercase, digits and
  `_`), never from names read from the disc.
- `--progress` prints the percentage and the step, for the launcher; `--cache` extracts into a per-disc cache and only
  prints the folder, for the development assets ([dwx page](../modules/apps/dwx.md#the-development-cache-dwx-extract---cache)).
- Exit codes: 0 if the package was written, the engine mounts it and opens all its assets (even if files
  remain pending), 2 if there was an error.

With the image outside the repository, from the container:

```sh
DWE_DISC="/path/to/disc.bin" scripts/dev.sh test -R dwx   # includes the test with the real disc
```

## What it does

1. It opens the image and the ISO9660 volume, and identifies the version by the executable's SHA-1 (like `--list`).
2. If it knows the version, it reads the **executable's tables** ([Tables](tables.md)) and their text ids
   (`makeTableIds`). All extractors receive them: Digimon and maps are named with those ids. Without tables
   (an unknown version) Digimon carry numbers and maps are left partial; a warning goes to the error output.
3. It computes the SHA-1 of each file in the volume and gives it to the first **extractor** in the registry that accepts it. Files
   are distributed among several threads (16 at most); the results are gathered in path order, so the
   output does not depend on the number of threads.
4. It prepares the output folder and writes the package: `pack.yaml`, the outputs, `dwx/coverage.yaml` and `.dwx-extract`.
5. It mounts the package with the engine's `VirtualFileSystem` (`packages/engine/assets`, on top of `DiskFileSystem`), checks that
   each output resolves to its file and opens each asset of the package with the engine's readers
   (`assets::checkContent`, the same as `dwe-game --check-content` and the last step of
   [`dwx validate`](../architecture/assets-pipeline.md#dwx-validate)). It is the proof that the engine loads what was extracted:
   if an asset does not open, it lists it and ends with an error. Schemas, references and contracts are not looked at
   here; that is what `dwx validate` is for.
6. It prints the summary: total, extracted, partial and pending files, bytes covered and number of outputs.
7. It runs the **bundled recipes** (`content/enhance/*.recipe.yaml`, built into `dwx` when it is compiled): today only
   `ui_original`, which enlarges ×4 with `index_scale` in `edge` mode the UI images drawn by the original look
   (icons, frames, clock, bars). The two title-screen images are not included: the look smooths them with the
   models' texture upscaler (`upscale: smooth` in `skin.yaml`; since version 14). It writes the `ui_original` package, of layer `remaster`, **next
   to** the base package (`--out content/base` gives `content/ui_original`) and prints where:
   `Upscaled pack: <folder> (5 images grown 4x, 0 not in the base pack)`. The images keep their asset id, so
   they cover those of the base, and their `_meta.yaml` gains `scale: 4`; the look measures its sprites in original pixels and
   sees them sharper, at the same size on screen. An image that the disc does not carry is skipped. It is a deterministic,
   rule-based pixel art upscale, with no models or network: it does not invent detail
   ([high definition](../architecture/high-definition.md#dwx-upscale-and-recipes)). To do it again by
   hand with another recipe there is `dwx upscale` ([dwx page](../modules/apps/dwx.md#dwx-upscale-and-the-bundled-recipes)).

   **For the player:** the game looks like the original by default (Decided by the user (2026-10-21)), and that
   look draws its icons with these images. Content extracted with an earlier version of `dwx` (without the
   `ui_original` package) is still valid, but the game starts with the modern look and says so in the log: you have to
   extract again to see the original. With the new content, the modern one remains under "Interface look"
   (main menu or pause menu options), and whoever had already chosen it keeps it.

The output is **deterministic**: two extractions of the same disc give the same bytes in all files, including
those of the `ui_original` package. There are no
dates or absolute paths in what is written.

## Extractors

An extractor is an entry in the registry (`apps/dwx/src/extractors.cpp`) with a name and two functions: `handles`,
which says whether it accepts a file by its path in the volume, and `extract`, which reads the file from the disc and returns its
outputs (asset id, extension and bytes), what it did not understand (`unknowns`) and notes for whoever reads the report
(`notes`). The first one that accepts the file wins, so specific ones go before patterns. It receives the
`Disc`: the source of sectors, the volume (`findFile` looks up another file by path), the version and the tables
(`tables()`, null if unknown). Since several files are extracted at once, an extractor keeps no state between
calls and only reads the disc through the `Disc` it receives.

| Extractor | Files | Output |
|---|---|---|
| `tim_pack` | The known fixed-stride packs: `CHDAT/ALLTIM.TIM` and `CARD/CARD.ALL` | A PNG and a YAML per entry |
| `stacked_tim` | The stacked TIMs: `ETCDAT/ETCTIM.BIN` and `STDDAT/STDTIM.BIN` | A PNG and a YAML per TIM |
| `tim_file` | Any other `.TIM`: one TIM, or several in a row | A PNG and a YAML per TIM |
| `executable` | The executable that `SYSTEM.CNF` boots (`SLUS_010.32` in the US) | The 15 documents of its tables, its own and the overlays' strings and the font |
| `boot_config` | `SYSTEM.CNF` | `disc/boot.yaml` |
| `map` | `MAP/MAP<n>/<name>.MAP` | The map's `data.yaml` and its 8- and 4-bit TIMs |
| `map_background` | `MAP/MAP<n>/<name>.TFS` | One background PNG per palette |
| `arena_collision` | `STDDAT/<name>.ATR` | The arena's collision grid |
| `digimon_model` | `CHDAT/MMD<n>/<name>.MMD` | A GLB with skeleton, animations and texture |
| `static_model` | Any `.TMD` | A GLB with one node per object, with a texture if it is known which |
| `sound_bank` | The seven `SOUND/VHB/*.VHB` | A SoundFont and a YAML per bank, a WAV per effect sample and a MIDI per music track |
| `event_script` | `SCN/MAPHEAD.SCN` and `SCN/DG.SCN` | A YAML listing per script, its Lua module (`story/<n>` and `story/<n>_lines`) and its texts; from all of them together, `data/new_game_stats`, `data/new_game_story` and `field/script_warps` ([Scripts](scripts.md)) |
| `overlay` | `<name>_REL.BIN` at the root | The documents of their game tables (FISH, STD, VS, TRN and BTL) |
| `padding` | `MMM.DAT` | Nothing: it checks that it is all zeros |
| `effect` | `ETCHI/EFEDAT.EFE` | Per technique: `effect.yaml`, `script.yaml`, the texture and, if it has a TMD, a GLB ([Effects](effects.md)) |
| `movie` | `MOVIE/*.STR` (a raw image is needed) | `video.y4m`, `audio.wav` and `movie.yaml` (see [Video](video.md)) |

The formats are in [TIM and packs](tim.md), [Executable tables](tables.md), [Maps](maps.md),
[Overlays](overlays.md), [Audio](audio.md), [Texts and font](texts.md), [Event scripts](scripts.md),
[Effects](effects.md) and [Video](video.md).

### The executable and `SYSTEM.CNF`

Only the data tables are taken from the executable: the code and the rest of the data are not assets. The file is left
`extracted` with notes that say so, the list of tables taken, the notes from `tablesToYaml` (texts with escaped
bytes, references outside their table) and the `MAP_ENTRIES` entries whose file is not on the disc (the
original never loads those maps). If the version is not known, the executable is left partial.

The same extractor writes the strings of the executable and the overlays that the version's manifest lists
(`text/en/<source>.yaml`) and the font (`font/main.png` and `font/main_meta.yaml`); the overlays are only read for that and
still have their own coverage. If the version has no manifest or located font, a note says so.

`SYSTEM.CNF` becomes a mapping of lowercase keys (`boot`, `tcb`, `event`, `stack`) with the values as text,
escaped like those of the tables (a backslash is written `\\`). A line without `=` is an `unknown`.

### Maps

Each `.MAP` is read with the `MAP_ENTRIES` entry that names it, because that entry says how many TIMs it has and whether it
carries NPCs; the path comes from the map id as in the original (`map.c: buildMapPath`). If two entries name the same
file, it is extracted once with the id of the first and the other is noted. A file without an entry is never loaded by the
game; it is extracted with its name as the id and the options given by the file itself (`inferMapReadOptions`, see
[Maps](maps.md)), with a note. If the file does not decide them, or its name is already the id of an entry, it is left partial.

- `map/<id>/data.yaml`: the `mapToYaml` document (camera, lights, tile grid, image sections, sprites,
  warps, NPCs and collision), with the map and Digimon ids from the tables.
- `map/<id>/texture8/<n>` and `texture4/<n>`: the TIMs of the 8- and 4-bit sections, numbered from 0 in each type,
  like any other image (PNG and `_meta.yaml`). The map's sprites are not separate images: they are rectangles of
  those textures and are in `data.yaml`.
- `map/<id>/background_<variant>.png` (from the `.TFS`): the background composed over the grid, one per palette, as an
  8-bit indexed PNG with its 256-color palette. The variants are `day`, `dusk`, `night` and then
  `palette3`, `palette4`...
- `.TFS` palettes after the third are written the same way. If no sprite of the map reads its VRAM row, a
  note says they are never seen; if any does read it, it is an `unknown`, because a TIM of the `.MAP` can put its CLUT on the
  same row.
- The following are `unknowns`: tiles that do not fall on the grid and bit `0x20` of the entry's flags.

### Arenas

The `.ATR` files in `STDDAT` are the 100 × 100 collision grid of a tournament arena, the same as that of a map
(`std_setup.c: STD_loadArenaAssets`, `vs_scene.c: VS_loadArenaAssets`). They are written with the same encoding as the
collision in `data.yaml`: `width`, `height`, `encoding`, `legend` and `rows`.

### Models

Each model is a `.glb` with everything inside (see [glTF](models.md#gltf)). The texture is not in the model file:
each primitive reads VRAM at the page, the `u v` and the palette it says. The extractor builds a **VRAM scene**
(`apps/dwx/src/model_textures.cpp`) with what the original has uploaded when it draws the model, each TIM in its
rectangle and later ones covering earlier ones. For each primitive it looks for the last image of its depth that
contains its texels and the last palette at its `CBA`, and writes each used image with each used palette as an
indexed PNG (color `0x0000` with alpha 0). Primitives that find no image or palette keep their `u v`
over their page, without an image, and go to `unknowns`.

- **Digimon** (`digimon_model`): the type comes from the file name and its folder, as in `model.c: loadMMD`
  (`PTR_DIGIMON_FILE_NAMES`, 30 types per `MMD<n>` folder). The skeleton is that of the type in `DIGIMON_SKELETONS`, the
  animations are all those of the MMD and the scene is only the type's entry in `CHDAT/ALLTIM.TIM`, which is read without
  reading the whole pack (9 sectors per type, `model.c: loadDigimonTexture`). An animation that cannot be played
  goes to `unknowns` and the rest of the model is written anyway.
- Next to each Digimon model goes its **contract**, `contract.yaml`: the model file, the Digimon whose skeleton it
  has, the parent of each bone (from `DIGIMON_SKELETONS`) and in `animations` the name (`anim_<nn>`) of each MMD animation
  that is in the model. Those that could not be played are not in the `.glb`, so they go separately, in
  `unconverted_animations` (only if there are any), as well as to `unknowns`: the contract still says the game has
  them without the extracted model breaking it. [`dwx validate`](../architecture/assets-pipeline.md#dwx-validate)
  checks that the model, the original or a mod's, meets `animations` and does not ask for those of
  `unconverted_animations`.
- **Other models** (`static_model`): one node per object, all at the root, without animations. The scene depends on the
  model:
  - Doors (`DOOR/DOOR<nn>.TMD`): the maps whose `DOOR_MAPDATA` entry includes the door
    (`map_object.c: loadDoors` loads them after the map's images).
  - Those in `ETCDAT`, `ETCHI`, `ETCNA` and `STDDAT` whose code is located: a table in
    `apps/dwx/src/model_extractors.cpp` says with which TIM, which maps or which script routine the original draws
    them, with the decomp function that does it. `ETCDAT/ETCTIM.BIN` is in VRAM from boot and
    `STDDAT/STDTIM.BIN` during tournaments.
  - Those that a script sets (`BOSS_EFE`, `T_YAMA`, `GAND`, `BIGBOX`, `SMABOX` and `ABOX`) are loaded by a
    `CALL_ROUTINE` routine (`script_interp.c: scriptInstruction64to7E`) that uploads no image, so they read those of the
    map. The extractor disassembles the disc's scripts (`apps/dwx/src/script_maps.cpp`) and looks for the maps where
    the routine is called: those that `SET_SCRIPT` loads with the script that calls it, or with one that jumps to it with
    `JUMP_TO_FILE` or `JUMP_TO_FILE_AND_LINK`. Each map is a scene and goes to `notes`.
  - Maps are opened by name, like the original (`file.c: lookupFileTable`), and uploaded as `overworld.c`
    does: 8-bit images with their CLUT unless it is at row 480 (the background's) and row `i` of the
    first 4-bit image's CLUT at `(16 · i, 486)`.
  - Scenes that have all the texels are grouped by the colors they give, as the PNG stores them (without the
    semi-transparency bit of opaque colors). The first group textures the model and each of the others gives a
    **variant**: the same model with that scene's texels, with the model's id and
    the slug of its first scene (`model/door/door16_yaka26`, `model/etcdat/sea_fishing2`). Each group's scenes
    go to `notes`. A scene that is missing texels goes to `unknowns`.
  - A map without images in its entry uploads none: there the model shows what the previous map left in VRAM.
    It is not a scene and goes to `notes`.
  - Without a scene, the model comes out without an image, with the `u v` over its page. It goes to `notes` if it is known why: no
    map has that door in its data (the original never draws it), the original never loads the file, or
    none of its maps upload images. It goes to `unknowns` if a scene could not be read or if the
    code that draws it has not been located.
  - `ETCDAT/DEFEFF.TMD`, `ETCNA/MEDAL.TMD` and `ETCHI/BOSS_EFE.TMD` change while they are drawn
    (`buff_model.c: morphBuffModel` alternates the palette, `map_object.c: activateMedalTexture` copies the medal,
    `eab.c: EAB_setModelColor` changes the primitives' color). The GLB has what is in the file and the change goes to
    `notes`.

### Padding

`MMM.DAT` (27.6 MB) only appears in the executable's file table; no code opens it. The `padding` extractor
checks that all its bytes are zero: if they are, it is left `extracted` with no outputs and a note; if not, it is left partial and
the `unknown` lists the ranges that are not zero.

### Overlays

Each `*_REL.BIN` is cut with its version's layout (see [Overlays](overlays.md)) and each part goes to a note with its
byte range: the embedded files of BTL, STD and VS (compared with the loose file: `same bytes as
ETCDAT/EX.TIM`; the asset comes from that file), the header, the code (not an asset), the data (the code's constants
named in the decomp's symbols), the saved memory (build leftovers; in MOV, the decoder's buffers) and the final word. SHOP's data, which the decomp does not build for the US, goes range by range with the
map of its layout (jump tables, texts, pointers and tables), and those that the executable also has are compared with
its copy. The game tables go to `data/fishing`, `data/stadium`, `data/versus`,
`data/training` and `data/battle`; the number of objects and TIMs of the arenas is read from the executable. TRN2 has the same tables as TRN:
they are compared and it is noted.
An overlay is left partial if it has no layout for the version, does not match its layout, has an embedded file that
is not equal to the loose one, has tables that cannot be read (or lacks the executable's tables) or has data that does not match
its map.

## The base package

```text
content/base/
├─ pack.yaml            base id, base layer, version 0.1.0 and the schema of each document type
├─ .dwx-extract         mark that dwx wrote it; hidden, the engine does not see it
├─ dwx/coverage.yaml    coverage report (asset dwx/coverage)
├─ data/<table>.yaml    executable tables: digimon, skeletons, moves, items, item_effects, evolution, raise,
│                       evolution_chart, evolution_chart_detail, cards, tournaments, medals, music, shops, maps;
│                       from the overlays: fishing, stadium, versus, training, battle; from the scripts:
│                       new_game_stats, new_game_story
├─ disc/boot.yaml       SYSTEM.CNF
├─ field/script_warps.yaml  the scripts' map changes
├─ script/maphead.yaml, script/dg/<n>.yaml  script listings
├─ story/<n>.lua + <n>_lines.yaml  the scripts as Lua modules
├─ text/en/<source>.yaml, font/main.png + main_meta.yaml
├─ digimon/<id>/texture.png + texture_meta.yaml, model.glb + contract.yaml
├─ card/<n>.png + <n>_meta.yaml
├─ map/<id>/data.yaml, background_<variant>.png, texture8/<n>.png, texture4/<n>.png (+ _meta.yaml)
├─ arena/<name>/collision.yaml
├─ model/<folders>/<name>.glb
├─ sound/<bank>/[<entry>/]bank.sf2, sample_<n>.wav, bank_meta.yaml
├─ music/<font>/font.sf2, track_<n>.mid, font_meta.yaml
├─ effect/<technique>/effect.yaml, script.yaml, texture.png + texture_meta.yaml, model.glb
├─ texture/<folders>/<name>.png + <name>_meta.yaml
└─ movie/<name>/video.y4m, audio.wav, movie.yaml
```

### Schemas

Each YAML document the extractor writes has its JSON Schema in `content/schemas/`, and the base's `pack.yaml`
says which one each follows ([`schemas` rules](../architecture/vfs.md#document-schemas)):

| Schema | Documents |
|---|---|
| `battle`, `cards`, `digimon`, `evolution`, `evolution_chart`, `evolution_chart_detail`, `fishing`, `item_effects`, `items`, `maps`, `medals`, `moves`, `music`, `new_game_stats`, `new_game_story`, `raise`, `shops`, `skeletons`, `stadium`, `tournaments`, `training`, `versus` | `data/<the same name>` |
| `boot` | `disc/boot` |
| `script_warps` | `field/script_warps` |
| `map` | `map/*/data` |
| `collision` | `arena/*/collision` |
| `image` | the images' `_meta`: `map/*/texture8/*_meta`, `map/*/texture4/*_meta`, `digimon/*/texture_meta`, `card/*_meta`, `effect/*/texture_meta`, `texture/**_meta` |
| `model_contract` | `digimon/*/contract` |
| `sound_bank` | `sound/*/bank_meta`, `sound/*/*/bank_meta`, `music/*/font_meta` |
| `script` | `script/maphead`, `script/*/*` |
| `story_lines` | `story/*_lines` |
| `text` | `text/*/*` |
| `font` | `font/main_meta` |
| `effect` | `effect/*/effect` |
| `effect_script` | `effect/*/script` |
| `movie` | `movie/*/movie` |
| `coverage` | `dwx/coverage` |

The rules come from `names::documentSchemas` (`apps/dwx/src/asset_names.cpp`). A new document type needs a
schema and a rule: without them, `dwx validate` fails in the disc test.

### Asset ids

All ids come from `apps/dwx/src/asset_names.cpp`, so that the scheme can be changed in a single place. Each file
meets the rules of `packages/engine/assets`: segments of `[a-z0-9_]` and a single dot in the name.

| Origin | Id | Example |
|---|---|---|
| A Digimon's asset (`names::digimonAsset`) | `digimon/<id>/<leaf>` | the texture from `ALLTIM.TIM`: `digimon/<id>/texture`; the model: `digimon/<id>/model` |
| Another model (`names::discModel`) | `model/<folders>/<name>` | `DOOR/DOOR05.TMD` → `model/door/door05` |
| Variant of another model | `model/<folders>/<name>_<scene>` | `DOOR/DOOR16.TMD` in `YAKA26` → `model/door/door16_yaka26` |
| `CARD.ALL` entry | `card/<n with 3 digits>` | `card/065` |
| Tables document | `data/<table>` | `data/moves` |
| `SYSTEM.CNF` | `disc/boot` | |
| Map (`names::mapAsset`, `names::mapImage`) | `map/<id>/<leaf>` | `map/<id>/background_day`, `map/<id>/texture8/000` |
| Arena collision | `arena/<name>/collision` | `STDDAT/B_TOGI.ATR` → `arena/b_togi/collision` |
| File with one TIM | `texture/<folders>/<name>` | `STDDAT/GRADE/AN.TIM` → `texture/stddat/grade/an` |
| File with several TIMs | `texture/<folders>/<name>/<n with 3 digits>` | the third of `STDDAT/T_TOGI.TIM` → `texture/stddat/t_togi/002` |
| Sound bank (`names::soundAsset`) | `sound/<bank>/<leaf>`, or `sound/<bank>/<entry with 3 digits>/<leaf>` in a packed file | `sound/ss/bank`, `sound/vball/003/sample_001` |
| Music font (`names::musicAsset`) | `music/<font with 3 digits>/<leaf>` | `music/001/font`, `music/001/track_00` |
| A technique's effect (`names::effectAsset`) | `effect/<technique id>/<leaf>`, or the slot with 3 digits without tables | `effect/<id>/script`, `effect/<id>/model` |
| Movie (`names::movieAsset`) | `movie/<name>/<leaf>` | `MOVIE/OP1.STR` → `movie/op1/video` |
| A script's listing (`names::scriptListing`) | `script/maphead`, `script/dg/<n with 3 digits>` | `script/dg/001` |
| Script as Lua (`names::storyModule`, `names::storyLines`) | `story/<n with 3 digits>`, `story/<n>_lines` | `story/001`, `story/001_lines` |
| An image's metadata | `<image id>_meta` | `card/065_meta` |
| Contract of a Digimon's model (`names::modelContract`) | `digimon/<id>/contract` | `digimon/species_a/contract` |

- A Digimon's id is that of `makeTableIds`, which comes from its model's file name
  (`PTR_DIGIMON_FILE_NAMES`, four lowercase letters). Without tables it is the type with 3 digits. All of a
  Digimon's assets (texture, model...) are named with `names::digimonAsset`, so they share a folder.
- A map's id is that of `makeTableIds`, which comes from the file name in `MAP_ENTRIES`; a repeated name carries
  `_<index>` after it.
- Cards still use numbers: the tables do not give them names. The number is their index in `data/cards.yaml`.
- Names from the disc are lowercased and any character that is not a letter, digit or `_` is changed to `_`.
- Two outputs with the same id, one with the manifest's id (`pack`) or one inside `dwx/` (the tool's folder)
  are a tool error.

### Images

- **PNG:** 8-bit indexed with palette 0 (`PLTE` and `tRNS`) if the TIM is 4- or 8-bit with a CLUT; RGBA if it is 16-
  or 24-bit. This way the indices and the palette are kept (see [TIM and packs](tim.md#png)). Map backgrounds are
  indexed with their whole palette; color `0x0000` is transparent, as in any TIM.
- **`<id>_meta.yaml`:** `source` (disc file), `entry` (number in a pack, in a list of several or among the
  TIMs of its type in a map), `offset`, `mode` (`indexed4`, `indexed8`, `direct16`, `direct24`), `width` and `height`
  in pixels, `image_rect` and `clut_rect` (where the original uploads it in VRAM, in 16-bit units), `png`
  (`indexed` or `rgba`), `png_palette` and `palettes`: each palette as a string of 4-digit hex colors separated
  by spaces, raw (STP bit included). The reader's warnings go in `warnings`. A TIM that cannot be
  converted to PNG gives neither the PNG nor the `_meta`, because the `_meta` describes a PNG: the reason goes to `unknowns`.
- **Digimon textures:** the PNG uses palette 0, but each model primitive chooses its own CLUT row; the
  model extractor has to use the YAML's palettes for each part.

## Coverage report

`dwx/coverage.yaml` has four parts:

- `disc`: volume, path and SHA-1 of the executable, and version (empty if unknown).
- `summary`: `files`, `extracted`, `partial`, `pending`, `bytes`, `bytes_covered` (those of extracted or
  partial files), `outputs` and `duplicate_outputs` (outputs that repeat an earlier one byte for byte).
- `files`: for each file in the volume, in path order, `path`, `size`, `sha1`, `status`, and if an extractor
  accepted it, `extractor`, `outputs` (paths inside the package), `unknowns` and `notes`.
- `duplicates`: groups of outputs with the same extension and the same bytes, in output order. All are written;
  the group says which are the same image stored in several disc files. Outputs written
  in parts (the movies' video, `OutputFile::stream`) are not compared.

The statuses:

| Status | Meaning |
|---|---|
| `extracted` | An extractor accepted it and understood it entirely; the `notes` do not change the status |
| `partial` | An extractor accepted it but left something in `unknowns`: entries that are not read, non-zero bytes that nobody explains, sizes that do not match, parts of unknown use, or the error if it failed |
| `pending` | No extractor accepts it yet |

## Status with the US disc

With the US disc (`SLUS_010.32`): 818 files, 818 extracted, none partial and none pending; 331.2 of 331.2 MB
(100%). There are 5475 outputs, of which 404 repeat another byte for byte, and two consecutive extractions give the same
bytes in all of them:

| Type | Outputs |
|---|---|
| Digimon textures (`digimon/`) | 180 PNG + 180 YAML |
| Cards (`card/`) | 66 PNG + 66 YAML |
| Other images (`texture/`) | 69 PNG + 69 YAML |
| Tables of the executable and the overlays (`data/`) and `SYSTEM.CNF` (`disc/`) | 15 + 5 + 1 YAML |
| Maps: `data.yaml` | 242 |
| Maps: backgrounds | 513 PNG |
| Maps: 8- and 4-bit TIMs | 413 + 167 PNG, with their YAMLs |
| Arena collision (`arena/`) | 2 YAML |
| Digimon models (`digimon/<id>/model`) | 178 GLB + 178 YAML contracts |
| Other models (`model/`) | 66 GLB + 4 variants |
| Sounds (`sound/`): 112 banks | 112 SF2 + 642 WAV + 112 YAML |
| Music (`music/`): 33 fonts | 33 SF2 + 74 MIDI + 33 YAML |
| Effects (`effect/`): 121 techniques | 363 YAML, 121 PNG, 89 GLB |
| Movies (`movie/`) | 4 Y4M + 4 WAV + 4 YAML |
| Event scripts (`script/`, `story/`) | 222 YAML; 222 Lua + 222 line YAMLs; 3 documents (`data/`, `field/`) |
| Texts (`text/`) and font (`font/`) | 219 YAML (7 from the executable and the overlays, 212 from the scripts); 1 PNG + 1 YAML |

- Effects: `ETCHI/EFEDAT.EFE` is left extracted; its 40 data ranges that nothing reaches (590 bytes in 33 slots)
  go as `unused` in each `script.yaml` and in the notes ([Effects](effects.md#unused-data)).
- Overlays: all 16 extracted. `SHOP_REL.BIN` is the `jp_rev1` build, byte for byte; its data goes range by range with the
  map in [Overlays](overlays.md#shops-data).
- `MAP/MAP16/MGEN17.MAP`, which no `MAP_ENTRIES` entry names, is extracted as `map/mgen17` with the options
  given by the file itself.
- `.TFS` palettes after the third (three maps) are never seen: no sprite reads their VRAM row.
- Three `MAP_ENTRIES` entries name files that are not on the disc; they go as notes of the executable. Entries 65 and
  232 have the same name in different folders: only that of 232 exists, which is extracted with the id of 232
  (`<name>_232`), the one the tables give.
- Models: all 248 GLBs pass `cgltf_validate` and take 112.8 MB; 235 have a texture (1219 images) and 13 do not. They carry
  4843 animations. All 244 files are extracted whole:
  - Variants: `ETCDAT/SEA.TMD` with `FISHING2.TIM` and doors 16-18 in `YAKA26`. `ETCDAT/SAO.TMD` looks the same
    with the two fishing TIMs, and `ETCNA/WARP.TMD` the same in `ICSA05`, `ICSA06` and `ICSA07`: the last two only
    change the semi-transparency bit.
  - The six that a script sets each come from a single map: `BOSS_EFE` from 225, `T_YAMA` from 28, `GAND` from
    146, `BIGBOX` and `SMABOX` from 151 and `ABOX` from 97.
  - Without a texture: ten doors that are not in any map's door data (01-04, 08, 19, 27-30), door 00, which
    is only in map 65, whose entry has no images (see above), `ETCNA/DOOR.TMD`, which the original never loads
    (it is the same as `DOOR00.TMD`), and `ETCDAT/FEEL_EF.TMD`, which has no textured primitives.
  - `CHDAT/MMD1/TANE.MMD` uses normals of other objects, like the console (see
    [Normals outside the object](models.md#normals-outside-the-object)).
- Pending: none.
- Duplicates: Digimon textures that share an image (48 groups), TIMs repeated between maps (62), the nine TIMs of
  `ETCDAT/ETCTIM.BIN`, which are also loose in `ETCDAT`, samples repeated between sound banks (43),
  tracks repeated between music fonts (6) and textures, models and a script that several techniques share (47).
- Audio: all seven `.VHB` are extracted whole. The 112 sound banks have 642 samples (19 with a loop, 13 beyond
  `vs` in `VBALL`, 16 that no tone plays) and 661 sound values; the 33 fonts, 205 samples and 74
  tracks (14 play once, 60 loop; 63 are in the shop's music list). 102 tones have a
  sloped sustain, approximated in the SF2. Entry 13 of `VLALL` is not used by any type.
- `STDDAT/T_TOGI.TIM` has four TIMs in a row: the original reads the arenas' textures with a fixed number of
  TIMs (`std_setup.c: STD_loadArenaTIMToVRAM`). That is why `tim_file` always reads a list.

## Time

In the `ci` build (debug with sanitizers), with 12 threads, the US disc extraction took 2 min 40 s (measured on
2026-10-19 with the host busy with builds of other branches: average load of 9 to 10 on 12 cores). About 13 s of that
is the final check, which opens the package's 4813 assets spread across threads. Under the same conditions,
without the movies, it took about 40 s. For that, `sha1.cpp` is compiled optimized in debug too, the debug builds
link the release lodepng (except on Windows) and the PNG encoder does not copy the pixels. The output is the same
byte for byte. Without the movies, the slowest part is the models: each door or map model reads and uploads to its scene the
maps where it appears, and each of the six models that a script sets disassembles all the scripts to look for their
maps.

The movies are most of the time: when extracting, each of the 13,089 frames only goes through the VLC stage
(`decodeBitstream`, to check its bitstream); the IDCT runs only once, when writing the Y4M, and 1.1 GB is written.
The movies' bitstreams and their WAVs (about 200 MB) stay in memory until the package is written.
`packages/formats/src/video/mdec.cpp` is compiled optimized in debug too, like `sha1.cpp`; without that it was almost 9 min.

With `dwx` built in release (`linux-clang-release`), the whole extraction takes about 15 s. Measurements from the close of
phase 1 (2026-10-20, host shared with other builds): 2 min 40 s to 3 min in `ci`, 15 s in release.
