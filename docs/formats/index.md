# Formats

To identify which file to edit, start with [Game extensions and files](extensions.md): models, maps, UI, audio and cooked content.

Reference for the original's formats as `packages/formats/` reads them. Each page describes what is **implemented**:
the structure, the checks the reader makes and where the original reads it in the decomp (`dw_decomp`). The overall
plan is in [Original formats and `dwx`](../architecture/formats.md).

No page contains game data: only structures, file names and references to the decomp
([legal policy](../legal/assets.md)).

| Page | What it covers | Code |
|---|---|---|
| [Disc](disc.md) | BIN/CUE or ISO image, sectors, ISO9660, executable and file table, `dwx extract --list` | `formats/disc`, `formats/exe` |
| [TIM and packs](tim.md) | TIM, stacked TIMs, fixed-stride packs and the extractor's PNG | `formats/image` |
| [Models and animations](models.md) | TMD, MMD, skeletons, animation simulation and glTF writing | `formats/model` |
| [Tables](tables.md) | Executable data tables per version, overlay addresses, text escaping, YAML with text ids and schemas | `formats/exe`, `content/schemas` |
| [Overlays](overlays.md) | `*_REL.BIN`: header, parts, embedded files and the fishing, tournament, versus, training and battle tables | `formats/exe/overlay*`, `content/schemas` |
| [Maps](maps.md) | `MAP_ENTRIES` and map tables, `.MAP`, `.TFS`, background composition by palette, map YAML and collision | `formats/map`, `content/schemas/map.schema.json` |
| [Effects](effects.md) | `ETCHI/EFEDAT.EFE`: slots per technique, header, scripts and their 97 subopcodes, data area, VRAM textures and models | `formats/effect` |
| [Audio](audio.md) | VHB, VAB, ADPCM, SEQ, packed banks and writing to SF2, WAV and MIDI | `formats/audio` |
| [Video](video.md) | STR (`StHEADER` and XA), MDEC v2/v3 bitstream, MDEC IDCT, XA ADPCM, `MOV_MOVIES`, Y4M + WAV output | `formats/video`, `content/schemas/movie.schema.json` |
| [`dwx extract`](extract.md) | Base package, asset ids, extractors and coverage report | `apps/dwx` |
| [Texts and font](texts.md) | Character set, byte strings and script text to UTF-8 with tags, font to PNG atlas and string manifest | `formats/text` |
| [Event scripts](scripts.md) | `MAPHEAD.SCN`, `DG.SCN`, header, disassembler, sweep, YAML listing and Lua modules | `formats/script` |
| [Evolution tree](evolution-tree.md) | Overview positions and icons and detail geometry (`dwx ui-chart`, `dwx ui-chart-detail`) | `formats/exe`, `content/schemas/evolution_chart*.schema.json` |

The engine's own formats, the ones `dwx` writes for the cooked state:

| Page | What it covers | Code |
|---|---|---|
| [`.pak` file](pak.md) | A package in a single file: header, table of contents, SHA-1 per entry, how it is mounted | `packages/engine/assets` |
| [KTX2 textures](ktx2.md) | The UASTC textures with zstd that `dwx cook` writes, which PNGs are converted, the mipmaps and transcoding on load | `apps/dwx`, `packages/engine/assets` |
| [Ogg Opus sound](opus.md) | The Ogg Opus that `dwx cook` writes from each WAV, its loop comments and what the reader checks | `apps/dwx`, `packages/engine/assets` |
