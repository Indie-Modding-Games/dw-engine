# Development assets

During development, the game and the tools can use **all** the local assets: the disc image, the folders that `dwx`
extracted and the decomp data. This is explicit and optional: it is never what a player receives. The product path
does not change: `dwx extract` places the `base` package on the user's machine and the game mounts it like any other
package ([asset pipeline](assets-pipeline.md)). No game data enters git through either path.

## How to enable it

With the `--dev-assets` option of `dwe-game` and `dwe-sim` (user decision of 2026-10-21): it exists in every build, it
is off by default and, while it is active, the game shows a notice on screen, above any other document
(`ui/dev_assets/document` of the `game` package, text key `dev.assets_active` in English and Spanish), and writes it
to the log. The roots are read from a `dev-assets.yaml` file:

```yaml
disc_image: /home/yo/juegos/mi-disco.cue
extracted_content:
  - ../content-f4
  - ../content-f3
decomp_root: /home/yo/code/dw_decomp
```

| Key | What it is | What is done with it today |
|---|---|---|
| `disc_image` | The disc image (`.cue`, `.bin` or `.iso`) | `dwx` extracts it to the development cache and that folder is mounted as the last of `extracted_content` (see "Reading the disc in the game") |
| `extracted_content` | Folders extracted with `dwx` (with packages such as `base/`), in order of preference | Their packages are mounted along with the normal ones |
| `decomp_root` | A copy of the decomp (with `include/dw` and `src`) | It is checked and logged; it is a behavior reference, nothing is ever copied |

All keys are optional, but the file has to name at least one root; an unknown key is an error. A relative path is
relative to the file's folder. If the file is missing, or a root does not exist or is not of its type, the game does
not start and the error says which root and why: with `--dev-assets` you never play halfway.

**Where the file is looked for** (`devAssetsConfigFolders`): first in the current folder (so each worktree has its
own) and then in the user's data folder, the one for `settings.yaml` and the saves. `dwe-sim` uses the same data
folder as the game (`content/user_data.hpp`), so both share the file and the cache. `--dev-assets` is only a switch:
it does not take the file's path. `dev-assets.yaml` is in `.gitignore`: it has the paths of one machine and is not
versioned.

## Precedence

The development assets **only fill in what is missing** (user decision of 2026-10-21). `GameContent::mount`
([vfs](vfs.md)) first gathers the normal packages (the content folder, its `packs/`, the team's if there is no `game`
package, and those of `--mods`) and then those from each extracted folder:

1. A normal package beats the development package with its **id** (the one in `pack.yaml`, not the folder name): with
   `--content` pointing to a folder with `base/`, a `base/` or a `base_viejo/` with id `base` from the development
   folders is left out. The development packages are chosen with the game's own alone, without the mods, and the game
   has to mount with them: a mod cannot change or break that base. A mod with the id of a chosen development package
   is left out (`skippedMods()`) because the id is already mounted, the same as, without development assets, a mod
   with the id of a game package, and the development package stays.
2. Among development folders, the first in the list beats the following ones. The folder that comes from `disc_image`
   goes last: those in `extracted_content`, which are named by hand, beat it by id.
3. The ones that come in go **below all the normal packages**, including `base` and those of `--mods`, whatever their
   layer and priority say (`PackLocation::fillsGaps`): they only contribute the IDs that no normal package has. Among
   themselves they stack by layer and priority. The log marks them "from the dev assets, under every other pack", and
   those left out are also listed, with the package that beats them.

This way a developer can play with `--content content --dev-assets` (the team's packages from the source tree and the
`base` from an extracted folder) without copying or packaging anything.

## Code

| Where | What |
|---|---|
| `packages/engine/assets/dev_asset_sources.hpp` | `DevAssetSources` (the roots by type) and `validateDevAssetSources` |
| `packages/engine/assets/dev_pack_selection.hpp` | `findDevPacks` (the packages in the extracted folders) and `selectDevPacks` (which ones come in, by id, and which are left out) |
| `packages/engine/assets/virtual_file_system.hpp` | `PackLocation::fillsGaps`: a package that stacks below all the others; `readPackManifest` |
| `packages/content/dev_assets.hpp` | The option, the file name, the notice key, `devAssetsConfigFolders`, `parseDevAssetsConfig`, `loadDevAssets` (finds, reads, checks and logs) and `openDevAssets` (all that plus the disc, for the game and `dwe-sim`) |
| `packages/content/dev_disc.hpp` | The disc image through `dwx`: where `dwx` is looked for (`dwxCandidates`, `findDwx`), `extractedDiscContent` (runs `dwx extract --cache`) and `withDiscContent` |
| `packages/content/user_data.hpp` | The organization and the application name of the user's data folder, shared by the game and `dwe-sim` |
| `apps/dwx/disc_cache.hpp` | The cache: `ExtractorVersion`, the key, the entry that is written separately and renamed, the old entries and `dwx extract --cache` ([module page](../modules/apps/dwx.md)) |
| `packages/engine/platform/process.hpp` | `runProcess` (runs a program without a shell and collects its output), `executableFolder` and `executableFileName` |
| `packages/content/game_content.hpp` | `GameContent::mount(..., devAssets)` mounts and labels the development packages |
| `apps/game` | `content_mount` (mounts with `openDevAssets`) and `dev_assets_banner` (the notice, and the `dev_assets_banner` setting that `tests/autotest/route-dev-assets.yaml` checks) |
| `tests/support` | The only owner of `DWE_DISC`, `DWE_CONTENT` and `DWE_DECOMP` in the tests, and the `TempFolder` temporary folder ([module page](../modules/tests/support.md)) |

The tests use the variables from `scripts/dev.sh`, not `--dev-assets`. In the container only the paths that `dev.sh`
mounts are visible (`DWE_DISC`, `DWE_CONTENT`, `DWE_DECOMP`), so a `dev-assets.yaml` with other paths fails inside it;
`scripts/dev.sh run` runs on the host and does not have that limit.

## What is in the decomp

A copy of the decomp (`~/code/dw_decomp`) has, for the `us` version:

| Path | What it is | Possible use |
|---|---|---|
| `include/dw/*.h` | Types, enums and opcode names of the original (`script.h`, `pstat.h`, `trigger.h`...) | Reference for naming and checking opcodes and state in the cards and the parity tests |
| `src/<overlay>/*.c` | The decompiled C | Behavior reference (`file.c: function` in a comment) |
| `text/<overlay>/*.yaml` | The texts that the decomp generates, per block and version | Compare the text that `dwx` extracts |
| `config/us/*.yaml`, `symbols_*.txt` | The split of the executable into segments and the symbols | Locate tables and functions when verifying an extractor |
| `disks/us/` | The disc files dumped by the user (outside the decomp's git) | Read a disc file without opening the image (tools only) |
| `build/`, `asm/` | Output of its build (outside its git) | None planned |

The engine only checks that the root has `include/dw` and `src`; it reads and copies nothing from it. The parity
tests that compare against the decomp find it with `test_support::userDecomp()` and know the paths they need.

## Reading the disc in the game

Rule 2 of [dependencies](dependency-rules.md) says that the runtime never depends on `formats`: only `dwx` knows how
to read the disc image. That is why, with `disc_image`, the game (and `dwe-sim`) ask `dwx`, in another process, for
the extracted content of the disc in a **development cache** and mount that folder as one more extracted folder (user
decision of 2026-10-21, option 1 of those below). Without `--dev-assets`, or without `disc_image`, nothing is run.

**How it works.** When starting with `--dev-assets`, `openDevAssets` reads the file and, if it names an image, runs

```sh
dwx extract --cache <datos del usuario>/dev-cache <imagen>
```

with the arguments as they are, without a shell (`platform::runProcess`). `dwx` computes the disc key,
`<sha1>-v<version>`: the SHA-1 of the file that holds the sectors (the `.bin` of a `.cue`) and `ExtractorVersion`,
the version of what `dwx extract` writes.

- If the `dev-cache/<key>/` entry exists, it uses it as is: this is the normal case from the second time on and it
  takes as long as the SHA-1 of the image (measured with a 380 MB image: about 12 s in the `ci` build, a debug build
  with ASan; the first extraction took about 5 minutes in that same build).
- If it does not exist, it extracts the whole disc (**the first time takes a few minutes and uses ≈1.5 GB** with the
  full game, in `~/.local/share/indie-modding-games/dwe/dev-cache/` on Linux —`$XDG_DATA_HOME` if it is set— and in
  `%APPDATA%\indie-modding-games\dwe\dev-cache\` on Windows; the log warns about it and `dwx` writes its progress to
  the error output) into a hidden temporary folder, `.<key>.partial-<random>/base/`, adds `dwx-cache.yaml` (the SHA-1
  and the version) and **renames** it to `dev-cache/<key>/`. An entry only appears through that rename, after
  flushing everything it has to disk (`fsync`), so an entry that exists is complete; if the extraction fails, the
  temporary folder is deleted. If two processes extract the same disc at the same time, the one that renames second
  throws away its copy and uses the first one's.
- An entry is a real folder with its `dwx-cache.yaml`. The cache never follows a link, neither to use an entry nor to
  delete it; whatever has an entry's name without being one (a link, a file, a folder without a record) is deleted
  (a link, without what it points to) and extracted again, with a warning.
- On its standard output `dwx` writes only the entry's folder, in UTF-8: that is the contract with the game.

What does not change: the cache only writes to the user's data folder, never to the image, to `~/code/assets` or to
existing content folders; its folder is mounted like those of `extracted_content` (after them), below all the normal
packages.

**When it is stale.** An entry is never patched: another disc or another `ExtractorVersion` gives another key and a
new entry. `ExtractorVersion` is bumped by hand every time what `dwx extract` writes for the same disc changes; the
fingerprint test (`apps/dwx/tests/disc_cache_tests.cpp`) extracts a synthetic disc that reaches several extractors and
fails, saying "bump ExtractorVersion and update the fingerprint", until it is bumped and the new version's row is
added to its fingerprint table, which only grows (versions go up and each has its own fingerprint). The test only
sees what that synthetic disc covers: a change in an extractor it does not touch (those that need the executable's
tables, for example) also requires bumping the version, and the [dwx page](../modules/apps/dwx.md) reminds you of
that.

**Cleanup.** After writing a new entry, `dwx` deletes the entries for the same disc from **earlier** versions that it
wrote itself (those with the `dwx extract` mark in `base/`). Those from later versions stay: they belong to another,
newer `dwx`, for example the one in another worktree. Temporary folders are not touched, because another extraction
may be writing them; those older than 24 hours show up in a warning so they can be deleted by hand when no extraction
is running. The whole cache can be deleted safely: it is extracted again.

**Where `dwx` is looked for.** Next to the executable that is running and, if it is not there, in its folder of the
build tree (`build/<preset>/apps/game/dwe-game` → `build/<preset>/apps/dwx/dwx`). If it is in neither place, the game
does not start and the error names both.

The options that were weighed:

1. **(Chosen) On-demand extraction to a development cache.** Rule 2 is kept and the product uses exactly the same code
   as development. Cost: the first time takes as long as a full extraction (minutes) and takes the same space as an
   extracted folder (≈1.5 GB).
2. **A development-only module that links `formats` in development builds.** The game reads the disc on the fly, with
   no prior extraction. It needs an exception ADR to rule 2, the development and product builds stop being the same
   (this contradicts "in every build") and the original format's limitations can slip into the runtime, which is
   exactly what the rule avoids.
3. **Only extracted folders and decomp in the game; the disc only through `dwx` by hand.** The simplest, but after a
   `dwx` change you have to extract again by hand.
