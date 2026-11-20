# dwx

The asset tool: reads the user's disc image, extracts the `base` pack and checks it, cooks and packs packs and
validates them ([asset pipeline](../../architecture/assets-pipeline.md)). It is the only program that reads disc
images (rule 2 of [dependencies](../../architecture/dependency-rules.md)). The code is a module (`dwe::dwx`) so the
tests can link it; `src/main.cpp` is the command line (`dwx` without arguments explains it).

## The game's first launch (`dwx identify`, `dwx extract --progress`)

The game's first-launch screen runs `dwx identify <image>`, which
writes `supported <version>` (exits with 0), `other-version <version>` for a known version from another region whose
tables are not localized (3; today `eu`, `jp` and `jp_rev1`, which are recognized by the executable's fingerprint) or
`unknown -` (1), and 2 if the image cannot be read. Then it runs `dwx extract --progress`, which in addition to the
report writes to the error output a `progress <percentage> <step>` line for each change (`read`,
`extract` from 2 to 80 by files read, `write`, `check`, `upscale`, `done`). `tests/identify_tests.cpp` tests both.

## Cooking and packing with progress (`dwx cook --progress`, `dwx pack --progress`)

With `--progress` at the end, `dwx cook` and `dwx pack` write to the error output a
`progress <percentage> <step>` line (`cook` or `pack`) each time the percentage of files cooked or copied, or written
to the `.pak`, changes. What the cache already has does not count, so a second `dwx cook` with no changes writes none.
The packaging scripts use them for the team packs (`scripts/package/cook_team_packs.sh`). They are tested by
`tests/cook_tests.cpp` and the progress test in `packages/engine/assets/tests/pak_tests.cpp`.

## Cooking to a `.pak` in one go (`dwx cook-pak`)

`dwx cook-pak <pack folder> -o <file.pak> [--progress]` is what the launcher and the first-launch screen use to leave
the `base` pack in a single `base.pak` ([ADR 0017](../../adr/0017-cooking-and-pak-files.md)):

1. It cooks the pack into `<file.pak>.cook`, next to the archive (`dwx cook`, with its cache).
2. It packs it into `<file.pak>.partial` (`dwx pack`).
3. It checks the archive: each entry is read again and matches its SHA-1, the manifest is the pack's, it has the
   same asset ids as the source folder and the engine opens each asset by mounting it alone, as `dwx extract` checks
   the extraction (`assets::checkContent`).
4. Only then does it give it its name (replacing any existing one) and delete the cooked folder.

It never touches the source folder: deleting it is up to the caller, after `dwx cook-pak` finishes successfully. If
it fails, the `.partial` is deleted, the previous `.pak` stays and so does the cooked folder, for the next attempt.
Since that folder is a pack with the same id, the archive has to go outside the folders the game mounts
while it is cooking (the launcher uses `<data>/content.partial` or `<data>/content.convert`).

With `--progress` it writes `progress <percentage> <step>` to the error output, with the percentage of the whole job:
`cook` up to 80, `pack` up to 95, `check` and `done` at 100. They are tested by `tests/cook_pak_tests.cpp`, including
a from-scratch install with a synthetic disc that ends with only `base.pak` and that the game mounts.

## The development cache (`dwx extract --cache`)

With `--dev-assets` and `disc_image`, the game and `dwe-sim` run `dwx extract --cache <folder> <image>` and mount the
folder that `dwx` writes to its standard output, the only thing it writes there
([development assets](../../architecture/development-assets.md#reading-the-disc-in-the-game)). `disc_cache.hpp`
has the key (`<sha1 of the disc>-v<ExtractorVersion>`), the entry that is written in a temporary folder and
renamed into place, the deletion of entries from earlier versions of the same disc and the warning about
temporary folders older than 24 hours.

An entry is a real folder with its `dwx-cache.yaml`: the cache never follows a link (neither to use an entry nor
to delete it), and anything that has the name of an entry without being one (a link, a file, a folder without its record)
is deleted and extracted again. Before the rename, `dwx` flushes to disk (`fsync`) each file and folder of the
entry, and then the cache folder, so that a power cut does not leave a half-written entry with its
final name. On Windows a folder cannot be flushed, so there it relies on NTFS.

### When to bump `ExtractorVersion`

`ExtractorVersion` (`packages/engine/assets/include/dwe/assets/extractor_version.hpp`, shared with the game) is the
version of what `dwx extract` writes; the game reads the player's disc again when its installed content
is of another version. **Bump it in the same commit
that changes the output for the same disc**: a new or changed extractor, another name or format of a file, another
`pack.yaml` or coverage report. Otherwise, other people's caches keep serving the old output without warning.

The test "the extractor's output for the synthetic disc matches the fingerprint of its version"
(`tests/disc_cache_tests.cpp`) watches over it in part: it extracts a synthetic disc that reaches several extractors (boot,
TIM images, arena collision, scripts, effects, models) and compares the fingerprint of what it writes. When it fails with
"bump ExtractorVersion and update the fingerprint":

1. bump `ExtractorVersion`;
2. add to the end of the `Fingerprints` table a row with the new version and the new fingerprint (the one in the
   test message). The table only grows: a row is never edited, and another test checks that the versions go up and that each
   one has its own fingerprint, so a version never names two different outputs.

The synthetic disc does not have the tables of a known executable, so the extractors that need them (maps,
Digimon models, executable texts...) do not change the fingerprint: if you change one of them, bump the version anyway
and add the row with the previous fingerprint and `.unseenBySyntheticDisc = true` (version 5, the conversation and
transport warps, is like that). The table test accepts that repeated fingerprint only in a marked row.

## `dwx upscale` and the bundled recipes

```sh
dwx upscale <recipe.recipe.yaml> <pack folder | content folder> -o <folder>
```

Enlarges indexed images from the packs with the built-in `index_scale` step and writes them, with their `_meta.yaml` and
`scale: k`, to a `remaster` layer pack with the recipe's id. The images keep their asset id, so the
pack covers the base without touching anything else. It reads a small recipe format
([high definition](../../architecture/high-definition.md#dwx-upscale-and-recipes)): `recipe: 1`, `id`,
`asset_type` (`ui` or `icon`), `select.include` with asset ids, `scale` and a single `builtin: index_scale` step with
`params.mode` (`edge` or `nearest`). No providers, models or network: the same bytes on any machine.

- It reads the packs in the given folder, not counting the output one, so repeating it always reads the base and not its own
  output.
- It writes to `<output>.partial` and renames it. It only replaces a folder that is already a `remaster` pack with the id
  of the recipe; any other is rejected without touching it.
- If there is no image to enlarge it writes nothing.

**The bundled recipes.** `content/enhance/ui_original.recipe.yaml` goes inside `dwx` when it is built
(`src/bundled_recipes.hpp.in`) and `dwx extract` runs it after finishing the base pack, also for the development
cache. It writes `ui_original` next to the base pack and skips the images that the disc does not bring. Changing the
recipe changes the output of `dwx extract`: bump `ExtractorVersion` in the same commit (the synthetic disc brings one
image of the recipe, an invented `ETCDAT/SYSTEM_W.TIM`, so the fingerprint sees it). Version 2 is the first one that
writes `ui_original`.

### Future work

Each launch computes the SHA-1 of the whole image, also when the entry already exists (about 12 s for 380 MB in the
`ci` build, a debug build with ASan). If it ever becomes a nuisance, a quick path would be to remember the size and the
modification date of the image next to the SHA-1 and only compute it again when they change.

## `dwx export-images`

```sh
dwx export-images <content folder> [--with <packs folder>]... [--language <code>] -o <folder>
```

Writes the images that can be changed with the [custom images](../../guides/custom-images.md),
with the names the game expects. The launcher uses it to prepare its Images section.

- It mounts the packs of the content and of each `--with` (for example the team ones, for the translated names).
- `map/<id>/background_<variant>.png`: the PNG bytes of the base, as they are.
- `texture/.../<image>_palette<N>.png`: each image in `texture/` in RGBA, once per palette of its `_meta`; without
  palettes, only `_palette0`. The `texture8/` and `texture4/` pages are not written.
- `maps.tsv`: `<id>\t<name>` per map, with `map_names.NNNN` from `text/<code>/main` (default `en`) or the name
  from the table if there is no translation.
- It writes to `<output>.partial` and renames it. It only replaces an empty folder or an earlier export (one that
  has `maps.tsv`).

The tests: `tests/export_images_tests.cpp`, with synthetic content.

## `dwx script-warps`

```sh
dwx script-warps <content folder> [--with <packs folder>]... [--rules <id>] [-o <file>] [--spawns <file>]
```

Extracts `field/script_warps` again from the story Lua of the packs (`story/<n>.lua` and its line map, with
the `story/patches/*.lua` patches applied as in the game), so
that the map changes that a mod moves in the Lua reach the field. Details in
[script warps](../../architecture/script-warps.md#from-the-lua).

- It mounts the packs of the content and of each `--with`. The transport destinations come from
  `rules/<id>/bird_transport` (default `original`, from the team pack: `--with content/packs/game`).
- Without `-o` or `--spawns` it compares with the packs' `field/script_warps` and, when they have it,
  `field/npc_spawns`: code 0 if they match, 1 if not (it says the first differing line). With `-o` it writes
  `field/script_warps` there, and with `--spawns` the NPCs the map heads place (`field/npc_spawns`, which co-op reads
  to show them at once), making the folder when missing.
- It fails (code 2, without writing) if a module cannot be read, with the module and the line, or if a patch uses `original` in a
  way the walk does not follow, with the patch file and its line. The notes say which modules do not use
  their line map (their records come out without `offset`), which maps are not read and whether the transport ruleset is missing.

The tests: `tests/script_warps_command_tests.cpp`, with synthetic content (a mod's patch of a map head included), and
one with `DWE_CONTENT` that checks that the base pack's two documents come out byte for byte and that the game pack's
patches are followed.

## `dwx evolution-guide`

```sh
dwx evolution-guide <content folder> <new file.md> [--language <en|es>]
```

Writes in Markdown the evolution requirements of each species, from the evolution and species tables of the
content extracted from the player's disc, for the guide [how evolutions work](../../guides/evolutions.md#per-species-table-on-your-machine).
Those tables are not published ([legal policy](../../legal/assets.md)): the file is for the player only.

- One section per level (Fresh to Champion; Ultimates do not evolve) and one table per species, with one row per
  destination on its card and the four groups of the score: care mistakes ("0-N" at most with the 0x10 mark,
  "N+" otherwise), weight (± 5), stats (HP and MP multiplied by 10, because the table stores them in tenths; in a
  Rookie destination, the favorite stats) and extra. A destination without mistakes or weight comes out as "Special evolution"; that of a Fresh one,
  "Always", as in the game panel.
- It reads the tables as the content composes them, without a ruleset: they are the requirements of the original.
- `--language` picks the text of the guide: `en` (the default) or `es`, with the game's own words for each. Another
  code is an error.
- It does not replace a file that already exists.

The tests: `tests/evolution_guide_tests.cpp`, with a synthetic table.
