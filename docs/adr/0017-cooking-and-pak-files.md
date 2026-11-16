# 0017. Cooking to KTX2 UASTC and Opus, and our own `.pak` archives with zstd

- **Status:** Accepted
- **Date:** 2026-10-19

## Context

The [asset pipeline](../architecture/assets-pipeline.md) ([ADR 0005](0005-asset-pipeline.md)) has a third state, the
**cooked** one: what the game loads fast, and a distributable pack in a single file. Until now only `dwx extract`
existed, which writes the base in source format (PNG, binary glTF, YAML, WAV, MIDI, SF2).

A first version with no new dependencies cooked PNGs to uncompressed RGBA8 KTX2, with a writer of our own, and
stored the `.pak` uncompressed. It worked, but the cooked pack was much bigger than the extraction (1.6 GB versus
about 350 MB) and did not save GPU memory. On 2026-10-19 the user decided the final formats and the dependencies
needed for them.

## Decision

- **`dwx cook <pack> -o <folder>`** passes each file through the **cooker for its extension**, in a registry like the
  one for extractors. What has no cooker is copied as is. The cookers:
  - **PNG → KTX2 with Basis Universal UASTC and zstd supercompression** ([KTX2](../formats/ktx2.md)), with the full
    mipmap chain, in sRGB for color and linear for data. UASTC level 2, no RDO, one thread per texture, zstd level 18
    and fixed metadata, written with libktx. The engine **transcodes it to BC7 when loading**, or to RGBA8 if the GPU
    does not sample BC7 (`assets::decodeKtx2`, `rhi::Device::supportsBc7`). The metadata decides, not the name: the
    color space comes from the `color_space` key of the sidecar YAML (`<id>_meta.yaml`; sRGB if it does not have it),
    and the PNGs whose palette is chosen at play time are copied as is (below).
  - **GLB → GLB**: the header is checked and it is copied.
  - **WAV → Ogg Opus** at 48 kHz with libopusenc, with fixed settings (64 kbps per channel, VBR, complexity 10, fixed
    serial number) and the WAV's loop and note in comments ([Opus](../formats/opus.md)). The engine decodes it with
    libopus (`assets::decodeOpus`) and an Ogg page reader of our own. SF2 and MIDI are copied as is, with the SF2's
    samples untouched.
  - **Y4M → AV1 in IVF** with libaom (added later, #105): each movie becomes `video.ivf` and its WAV becomes Opus like
    the other sounds; `movie.yaml` is copied ([video](../formats/video.md), measurements in
    [movies](../architecture/movies.md#cooked-format-and-later-work)). The movies, three quarters of the extracted pack (below), end up at
    about 27 MB.
- **Images that the game reads from their PNG, as is.** Those that an engine reader decodes itself, and not as a
  texture, are copied as PNG: the list `assets::PngOnlyImages` says so, by asset type and not by the sidecar YAML
  (`packages/engine/assets/include/dwe/assets/png_only_images.hpp`). Today they are the map backgrounds
  (`map/<id>/background_*`) and the texture pages (`map/<id>/texture8/NNN`, `texture4/NNN`), from which
  `loadMapImages` reads the indexes and the palette (the time of day chooses the background's palette, and sprites
  with `clut` −1 are drawn with it), and the bitmap font atlas (`font/<name>`), from which `ui::BitmapFont` reads the
  exact alpha of each pixel. `dwx validate` and `dwe-game --check-content` reject any other file for those IDs, so a
  cooked pack that the game cannot read fails before playing.
- **Palettes chosen at play time, as is.** An indexed PNG is also copied as PNG, with its indexes and its palette, if
  its sidecar YAML shows that the game draws the same pixels with more than one CLUT row:
  - it lists **more than one palette** (`palettes`): the Digimon textures, whose primitives each choose their row;
    those of the effects; the multi-palette TIMs; or
  - it is a **map TIM with a CLUT** (`source` ends in `.MAP`): each map sprite chooses its row (`clut` in
    `data.yaml`); −1 is row 480, the time-of-day one, so the same TIM looks different with the day, dusk or night
    palette.

  Everything else, including single-palette indexed PNGs (cards, loose textures), is converted to UASTC.
  **Correction (2026-10-21):** the first version of this rule also converted the map backgrounds and the font atlas,
  but `loadMapImages` and `ui::BitmapFont` read them from their PNG, so a cooked pack did not load any map; since then
  the rule starts from the reader of each asset type. "Decided by the manager, 2026-10-21": it is kept that
  single-palette indexed PNGs whose reader reads colors (cards, loose textures) are cooked to UASTC; an image stays
  PNG exactly when its reader reads the indexes or the exact pixels (`PngOnlyImages`), and all the others can be KTX2.
  **This rule is the integration owner's interpretation**, not a user decision: the user decided that cooked textures
  are UASTC, and which image is a texture and which one holds a palette chosen at play time was decided by the owner
  from the metadata. The user can review it; if the engine ends up drawing the palettes in another way (for example,
  composing each variant when cooking), the rule changes and not the format.
- **Content cache:** the output folder keeps in `.dwx-cook` the SHA-1 of the source, the name, the version and the
  options of the cooker and the SHA-1 of each file's output. What did not change is not cooked again, and two assets
  with the same bytes are cooked once. The result is the same with or without the cache. The texture cooker moves to
  version 3 with UASTC.
- **`dwx pack <pack> -o <file.pak>`** writes a `.pak` archive of our own, versioned and documented
  ([format](../formats/pak.md)): header, data aligned to 16 bytes, and at the end the table of contents sorted by asset
  id (offset, sizes, SHA-1 and compression), the names and the `pack.yaml`. Version 2 compresses each file with
  **zstd** (level 9 and fixed settings) and stores it as is if that way it takes up no less; the SHA-1 is of the
  uncompressed file. The reader also accepts version 1, which is version 2 with nothing compressed. It is
  deterministic: the same files give the same bytes.
- **The engine mounts `.pak`:** `PakFileSystem` is one more `FileSystem`, and each pack is mounted with its own
  (`PackLocation`), so a layer can be a folder or a `.pak`.
- **The team's packs travel as `.pak`:** the packaging scripts (`scripts/package/cook_team_packs.sh`) cook and pack
  each folder in `packs/` with the package's own `dwx` (`game.pak`, `es-translation.pak`) and delete the folder. A
  package build (`DWE_PACKAGE`) only mounts the team's packs as `.pak` and warns if it finds a folder; the first-run
  screen reads its font and its texts from the `.pak` (`content::readTeamPackFile`). In development the
  `content/packs` folders are still mounted. `dwx cook` and `dwx pack` accept `--progress` so that whoever runs them
  can show progress.
- **The player's `base` pack is installed as `base.pak`** (added on 2026-11-09, #268). `dwx cook-pak <folder>
  -o <file.pak> --progress` cooks, packs and checks in one go: it cooks next to the file (`<file.pak>.cook`), writes
  `<file.pak>.partial`, rereads every entry, checks that it has the folder's ids and that the engine opens each asset
  mounted on its own (`assets::checkContent`, like `dwx extract`), and only then gives it its name
  ([dwx](../modules/apps/dwx.md#cooking-to-a-pak-in-one-go-dwx-cook-pak)). The launcher and the first-run screen
  (`content::DiscInstall`) run it after `dwx extract` in `content.partial`, delete the `base/` folder there and move
  `base.pak` to `content/`; the loose `base/` of a previous installation is removed afterwards, in one go. A
  from-scratch installation ends up with `base.pak`, `ui_original/` and `install.yaml`. Installations that already
  existed are converted by the launcher when it opens, without rereading the disc: it cooks in `content.convert/`,
  moves the file to `content/` and only then removes the loose folder, so an interruption always leaves a whole base
  pack ([launcher](../modules/apps/launcher.md#installs-with-the-loose-base-pack)). `ui_original` (what the
  `dwx extract` recipe grows) stays in a folder: it is small and every extraction rewrites it.
  - What used to read loose files from `content/base` now reads either form: the launcher background (a map
    background, which stays PNG inside the `.pak`, read with a minimal reader of the format in Rust) and
    `dwx export-images`, which mounts the packs with the virtual file system and decodes to PNG the images that
    cooking turned into KTX2 (the single-palette ones, which come out in color as `_palette0`).
  - `--content <folder>` accepts `base.pak` and, for developers, the `base/` folder from `dwx extract`; if both are
    there, the folder wins (`assets::findPacks`). The game without the launcher does not convert a loose
    installation: it keeps playing it that way.
  - `ExtractorVersion` does not change: the extraction is the same and a loose installation plays the same, so there
    is no need to reread the disc; only the shape of the pack changes.
- **Dependencies** (pinned by the vcpkg baseline): `ktx` 4.4.2 (KTX-Software, Apache-2.0, with Basis Universal inside)
  to write and read KTX2; `zstd` 1.5.7; `opus` 1.6.1 and `libopusenc` 0.3, which ships its own Ogg packer (neither
  `libogg` nor `opusfile` is needed). The `ktx` port exports only `KTX::ktx` (not the read-only variant `ktx_read`),
  so the engine links the whole library.

## Determinism

Cooking and packing give the same bytes with the same inputs, within this scope:

- the versions pinned by the vcpkg baseline (libktx with its Basis Universal, zstd, libopus and libopusenc);
- the same CPU path: Basis Universal, and libopus if built with runtime detection, choose SIMD code at startup (SSE
  4.1, AVX...) if the CPU has it, and the paths do not promise the same bytes; another architecture does not either;
- the same compiler and the same standard library for the mipmaps (`pow`).

Outside that scope the output may change, but it is still valid and the engine reads it the same. The `dwx cook`
cache stores each output by the SHA-1 of the source and the name, the version and the options of the cooker, so
bumping a dependency requires bumping the cooker's version so as not to mix old and new outputs. The tests check that
cooking the same texture twice, also in several threads at once, gives the same bytes.

## Measurements

With the extraction of the US version (4813 assets, 1530 PNGs, the 4 movies included), `dwx` from 2026-10-19 built
in release (`linux-clang-release`). Each size, with the `movie/` folder and without it (in MB of 10⁶ bytes):

| | Extracted | Cooked |
|---|---|---|
| Base pack folder, without `movie/` | 351.7 MB | 420.9 MB (the uncompressed RGBA8 cooking took 1617 MB) |
| Base pack folder, with `movie/` | 1523.5 MB | 1592.7 MB |
| `.pak` (version 2, zstd), without `movie/` | 240.9 MB | 313.3 MB |
| `.pak` (version 2, zstd), with `movie/` | 611.1 MB | 683.5 MB |
| The 645 PNGs that become KTX2 UASTC | 167.5 MB | 246.7 MB (as uncompressed RGBA8 KTX2, 1190.7 MB) |
| — of them, the 513 map backgrounds | 166.1 MB | 244.1 MB |
| The 642 WAVs → Opus | 17.1 MB | 7.0 MB |

- **Movies:** `movie/` takes up 1171.8 MB (4 Y4M, 4 WAV and 4 YAML), three quarters of the pack. These measurements
  are from when it was copied as is (in the `.pak`, zstd left it at about 370 MB); cooked to AV1 and Opus it takes up
  27.4 MB ([movies](../architecture/movies.md#cooked-format-and-later-work)).
- **Team packs** (2026-11-07, debug `dwx`): `game` goes from 3.5 MB in a folder to 0.9 MB in `game.pak` (cooking
  0.66 s, packing 0.41 s; only the two PNG flags become KTX2) and `es-translation` from 1.6 MB to 0.16 MB (0.13 s and
  0.15 s). `dwe-game --check-content` with the base, `ui_original` and the two packs opens 6037 assets without
  failures in 11.7 to 13.2 s with the `.pak` files and in 13.5 to 14.4 s with the folders: the cost is set by the
  base, and reading the team packs from a `.pak` is not noticeable on the first load.
- **What passes as is:** 885 indexed PNGs stay as PNG (14.2 MB): 580 map TIMs, 179 Digimon textures, 93 effect
  textures and 33 loose multi-palette textures. 645 are converted: the 513 map backgrounds, 66 cards, 36 loose
  textures, 28 effect ones, one Digimon one and the font. `dwx cook` counts 611 PNG copies because files repeated
  byte for byte are counted separately (403 shared assets in total).
- **Size:** UASTC stores 8 bits per texel plus a third for the mipmaps, and zstd lowers it only a little; an indexed
  PNG of a prerendered background stores 8 bits per texel and deflate compresses it better. That is why the
  converted textures take up 1.47 times their PNG on disk (the backgrounds, 1.47; the rest, 1.86), although 4.8 times
  less than in RGBA8 KTX2. On the GPU, BC7 takes up a quarter of RGBA8, and the indexed PNG cannot be uploaded without
  decoding.
- **Times** in release, with 12 threads shared with other builds: the extraction, 15.1 s; `dwx cook` of the whole
  pack without cache, 285 s (before the movies, 222 s; with the debug build with sanitizers, `ci`, 1337 s and the
  same bytes in all files); `dwx pack`, 20.7 s the base and 20.2 s the cooked one, reread included (2.7 s and 3.3 s
  without `movie/`).
- **Check:** `dwx validate` finds no problem in the cooked folder (5.8 s) or in its `.pak` (10.1 s): 4813 assets,
  2078 documents, 9271 references and 178 contracts, and every asset is opened with the engine's readers, including
  KTX2 transcoded to BC7 and Opus decoded. `dwe-game --check-content` (`ci` build) opens all 4813 in the folder (27 s)
  and in the `.pak` (36 s) without failures, and `dwe-game --content` mounts both without warnings.

## Consequences

- Textures take up 4.8 times less on disk than uncompressed KTX2 but 1.47 times more than the indexed PNGs they come
  from, and on the GPU a quarter of RGBA8. UASTC is almost lossless, and transcoding to BC7 on load is fast. If disk
  size ever matters, UASTC has an RDO mode that prepares the blocks so that zstd compresses them better at the cost of
  some quality; this decision leaves it off.
- Cooking is much slower than before (285 s in release for the whole pack, movies included), almost all of it from
  encoding UASTC. The cache means it is paid only once per texture.
- Palettes chosen at play time stay as PNG: the engine will have to draw them with their palette (a lookup shader or
  composed variants) when model and map rendering arrives.
- Opus is lossy: a short loop of an instrument may reveal the jump at the loop point. The lossless samples stay in
  each bank's SF2. A few very-low-frequency WAVs come out somewhat larger in Opus.
- The `ktx` port builds Basis Universal with `-msse4.1` on x86-64, so `dwx` and the engine need a CPU with SSE 4.1
  (all x86-64 since 2008).
- Every read of a `.pak` checks the entry's SHA-1: it detects damaged archives in exchange for a bit of CPU. When
  decompressing, memory grows with what the frame actually produces, not with the size the table says, so a forged
  archive does not make it reserve 2 GiB.
- Bumping a cooker's version invalidates its cache; it must be done every time its output changes.
- Installing the disc takes longer: on top of the extraction (under a minute) comes `dwx cook-pak` of the whole base
  (about 4 minutes: 239 s with 12 threads on 2026-11-09, more on a more modest computer), once per disc read. In
  return, `content/` goes from 5473 loose files (1.5 GB) to a single `base.pak` of about 280 MB, which the game opens
  whole (`dwe-game --check-content`: no resource fails), and textures take up a quarter on the GPU. While cooking,
  there must be room for the cooked folder and the archive at the same time (the launcher asks for 1.5 GB free to
  convert an installation).

## Alternatives rejected

- **Uncompressed RGBA8 KTX2 with a writer of our own** (the first version): no dependencies, but the cooked pack went
  from 344 MB to 1.6 GB and did not save GPU memory.
- **Basis Universal ETC1S:** smaller on disk, but with much more loss, which would show on textures with few colors
  and hard edges. UASTC transcodes to BC7 almost losslessly.
- **BC7 directly in the KTX2:** no transcoding on load, but no path for GPUs without BC7 and bigger on disk than UASTC
  with zstd.
- **The `basisu` port instead of `ktx`:** it ships the same codec, but leaves the KTX2 container to us on both sides.
- **`lz4` in the `.pak`:** it decompresses even faster, but compresses less; zstd decompresses fast enough for
  loading packs.
- **zip:** reading and writing it without a library is almost as long as our own format, it does not guarantee data
  alignment and its index is not sorted by id.
- **tar:** it has no index; you have to walk all of it to find a file.
- **DDS or KTX 1:** KTX2 is the standard container for Vulkan and the one that supports Basis supercompression.
- **Treating `.pak` files as folders inside `DiskFileSystem`:** it would hide where each file comes from; one file
  system per pack is clearer and changes nothing of what already mounted folders.
