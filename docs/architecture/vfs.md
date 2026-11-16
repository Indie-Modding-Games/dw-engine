# Packages and virtual file system

How the engine finds each asset among the mounted packages. This is the "content layers" part of the
[asset pipeline](assets-pipeline.md), and the code is in `packages/engine/assets`.

## A package

A package is a folder with a `pack.yaml` at the root and the files of its assets underneath:

```text
hd_backgrounds/
├─ pack.yaml
└─ map/
   └─ file_city_01/
      └─ background.ktx2      → map/file_city_01/background
```

### The manifest

```yaml
id: hd_backgrounds
version: 1.2.0
layer: remaster
priority: 0
name: HD backgrounds
description: Scaled map backgrounds.
dependencies:
  - id: base
    min_version: 1.0.0
```

| Field | Required | What it is |
|---|---|---|
| `id` | Yes | 1 to 64 characters of `a-z`, `0-9` and `_`. There cannot be two mounted packages with the same one |
| `version` | Yes | Semantic version `MAJOR.MINOR.PATCH`, without suffixes (`-beta`, `+build`) |
| `layer` | Yes | `base`, `remaster`, `mods` or `user`. A mod (from a mods folder or from `--mods`) can only declare `mods` or `user`; otherwise it is left out |
| `priority` | No (0) | An integer. It orders the packages of the same layer: the highest wins |
| `name`, `description` | No | Text for people: one text for every language, or a mapping of language codes (`en`, `es`, `pt_br`) to texts that has `en`, which a missing language falls back to |
| `dependencies` | No | A list of `id` and `min_version` |
| `schemas` | No | Which schema each YAML document in the package follows (see below) |
| `api` | In `mods` and `user` | The Lua API version that its scripts ask for: `1.x` (any 1.y) or `1.4` (1.4 or later) ([script](../modules/engine/script.md)). Mandatory in the `mods` and `user` layers: a mod with Lua files without it is left out, and a data-only one is mounted with a warning (those written before it was required); the engine's own packages may omit it |

An unknown key is an error, so that a typo does not go unnoticed. The format is also available as a JSON Schema in
[`content/schemas/pack.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/pack.schema.json),
for editors and tools.

### Document schemas

`schemas` says which JSON Schema from `content/schemas/` each YAML document follows. Each key is the name of a schema
(`map` is `content/schemas/map.schema.json`) and its value is the list of **ID patterns** of the documents that
follow it. In a pattern, `*` is any text within a segment and `**` is any text even if it crosses `/`:

```yaml
schemas:
  digimon: [data/digimon]
  map: [map/*/data]
  image: ["map/*/texture*/*_meta", "texture/**_meta"]
```

The rules of all mounted packages apply to all: a mod that overrides `data/digimon` does not need to repeat them, and
its document is checked with the same schema as the base's. The schema is declared in the manifest and not guessed
from the path, so that a new document type never goes unchecked: [`dwx validate`](assets-pipeline.md#dwx-validate)
gives an error if a document fits no rule or fits rules of two different schemas. `dwx extract` writes into the base's
`pack.yaml` a rule for each document type it generates (`names::documentSchemas` in `apps/dwx/src/asset_names.cpp`).

## Asset IDs

- They are segments of `a-z`, `0-9` and `_` separated by `/`, at most 200 characters: `map/file_city_01/background`.
- No empty segments, no capitals and no dots, so an ID never leaves the package's folder nor depends on whether the
  disk distinguishes case.
- **A file's ID is its path inside the package without the extension.** `map/file_city_01/background.png` is
  `map/file_city_01/background`. The file name carries exactly one dot and a lowercase extension (`model.lod1.gltf` is
  not valid).
- Two files of the same package with the same ID (`model.gltf` and `model.glb`) are an error.
- `pack.yaml` and the files and folders that start with a dot (`.git`, `.gitkeep`) are ignored. Any other file that
  does not give a valid ID is an error.
- On disk, symbolic links are an error: a package cannot read outside its folder.

## The stack

When mounting a set of packages, the engine stacks them from bottom to top:

0. The packages that **fill gaps** (`PackLocation::fillsGaps`, those of the development assets) go below all the
   others; among them the following rules apply.
1. By **layer**: base (1), remaster (2), mods (3), user (4).
2. Within a layer, by **priority**, from lowest to highest.
3. If they tie on layer and priority, each package goes on top of the packages in that tie that it depends on, and if
   not, by **id** in alphabetical order.

The order in which the folders are passed does not matter: the same packages always give the same stack.

**Overriding and adding:** if several packages have the same ID, the one higher in the stack wins, even if the
extensions are different (a remaster's `background.ktx2` replaces the base's `background.png`). An ID that only one
package has is simply added.

**Map patches:** `map/<id>/patch` is the exception to "the top one wins". A map's `map/<id>/data` follows the rule
(one whole file wins), but over it the patches are applied, from the bottom up, of the package with that `data` and of
all those above it (`sources(id)` gives each one in stack order); those below are left out. Each patch changes
individual fields of an exit, an NPC, an object, a light or the camera, and the collision, whole or by rectangles,
without copying the map ([patches](../formats/maps.md#patches)).

## Tables by entries

Content tables are not replaced whole: they are **composed entry by entry**, so that a mod adds a Digimon or a map
without copying the table (`readComposedTable`,
`packages/engine/assets/include/dwe/assets/table_composition.hpp`). There are six table kinds: Digimon, raise, maps,
items, techniques and evolution (`composedTableKinds`, `packages/engine/assets/include/dwe/assets/table_kinds.hpp`).
For each kind, its usual table (`data/digimon`, `data/raise`, `data/maps`, `data/items`, `data/moves` and
`data/evolution`) is composed, along with any other that a ruleset names with the kind's role in its `tables`
(`raise: data/hard_raise`). The game and `dwx validate` compose the same ones (`composedTablesOf`). The rest of the
assets follow the stack rule: the highest one wins.

### Content IDs

Each entry is named by its content id (`ContentId`, `packages/engine/assets/include/dwe/assets/content_id.hpp`):

- **Originals:** their usual stable name, without a namespace (`species_a`, `twna01`). It is the one that
  `dwx extract` already writes and the tools read, so it does not change.
- **What a package adds:** `<package id>:<name>`, like `example_mod:meadow`. Each part is `[a-z0-9_]`; the name is at
  most 64 characters.
- **Its assets** go in one folder per part: the map `example_mod:meadow` is in `map/example_mod/meadow/` and the model
  of the Digimon `example_mod:pebblemon` in `digimon/example_mod/pebblemon/model`. This way the
  [asset IDs](#asset-ids) are still `[a-z0-9_/]`.
- **The number (`index`) is internal:** the original's, which its data uses (the warps, the saves), or the one that
  the engine assigns to a new entry. A package refers to entries by their id.

### How they are composed

The table's files are read from the bottom of the stack up. Each table says which sections are **lists of entries**
and which key names each entry (`id`, or `digimon` in `data/raise`):

1. An entry with an id that already exists **replaces** that entry in place and keeps its `index`. If it gives a
   different `index`, it is an error, and outside the base it cannot give an `index` to an entry that another package
   added either: that number is set by the engine and depends on the mounted mods. "The base" means the engine's
   packages (`PackOrigin::Engine`) in the base layer: a package that only declares `layer: base` does not have its
   rights.
2. A new entry is **added at the end**, in stack order and, within a package, in its file's order, with the next free
   `index` (the largest plus one). Only the base gives its entries' `index`, and two base entries cannot have the same
   one; outside the base it is left out.
3. Outside the base layer, a new entry is called `<package id>:<name>`: a name without a namespace or with another
   package's is an error.
4. The sections that go by another table's id (the rows of `data/raise` and those of `data/evolution`, by Digimon or
   item) accept rows of any id and **do not number** the new ones: the `index` of a base row is the place of the entry
   it belongs to in the original, it is kept when a package replaces it and a new row has none.
5. A list of entries can only be given as a list: `maps: null` or `maps: 1` is an error, not a way to empty it.
6. Any other section (the doors and restrooms of `data/maps`, for example) is **replaced whole** by that of the
   highest package that has it, like the sections of a ruleset with `extends`. A new section is added.
7. Composition is always done, even if only one package has the table: this way its ids are checked and its entries
   numbered. If nothing changes, the file is read as is, so that the readers' errors give their lines.

The result is the same YAML that the game would read from a single file, so each table's strict readers do not change.
**A package that brings the whole table** (a copy of the base's, edited) gives the same table as before: each entry
replaces its own with the same `index`. A mod **cannot remove** entries: an entry that is missing from its file is
still there.

### A new Digimon brings its contract

A Digimon that a package adds (an id with a namespace) has to bring its model's contract,
`digimon/<package>/<name>/contract.yaml`
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/model_contract.schema.json)): it
is what its model is checked against, because there is no original to compare it with. Without it, the game does not
load the table (`loadDigimonTable`) and `dwx validate` gives a problem (`requireNewSpeciesContracts`).

### Saves, rulesets and the `index`

A new entry's `index` depends on which mods are mounted and in what order, so nothing that leaves the session uses it:
the saves (the partner's species, the map, the learned techniques, the raised species and, since version 2 of its
section, the bag's items), the rulesets, the scripts, the text keys and the logs name each entry by its id. Inside the
session, the engine uses composition handles ([IDs in the session](../modules/engine/assets.md#ids-in-the-session)). A
ruleset's number ranges only cover the original's entries ([ranges and ids](rulesets.md#ranges-and-ids)), and a warp
reaches a package's map by its id ([an exit's destination](../formats/maps.md#destination-of-an-exit)).

Removing a mod does not leave the game unloadable: a stack in the bag whose item is no longer in any table is
dropped, with a warning in the log that names its id and the amount ([inventory](../modules/game/inventory.md)).

### Names of what is added

What is seen of a new entry comes from the package's text table, `text/<language>/<package id>.yaml`, with the key
`<block>.<name>`: `digimon_names.pebblemon`, `map_names.meadow` (`addedEntryTextKey`,
`packages/engine/localization/include/dwe/localization/catalog.hpp`). The originals keep their numbered keys.

A full example is in [`tests/mods/example_mod`](../modules/tests/mods.md).

## Dependencies

Each dependency has to:

- be mounted;
- have at least version `min_version`;
- be in the same layer or a lower one (the base cannot need a mod);
- not form a cycle.

Dependencies do not change the layer or the priority: if a mod depends on another with higher priority, the other one
stays on top.

## What can be asked

| Function | What it returns |
|---|---|
| `resolve(id)` | The winning package and the path of its file, or nothing |
| `read(id)` | The bytes of the winning file |
| `ids()` | All the IDs, sorted |
| `sources(id)` | All the packages that have that ID, from bottom to top |
| `overriddenIds()` | The IDs that more than one package has, to debug what overrides what |

A package can also be a [`.pak` archive](../formats/pak.md), which is opened with `PakFileSystem`. Each package is
mounted with its own file system (`PackLocation`: the file system and the package's root in it), so a stack can mix
folders and archives: `mount(fileSystem, folders)` is the case of all of them in the same one. `listPackFiles` lists
a package's files with their ID and checks the same rules as mounting.

To know what to mount, `findPacks(folder)` returns the folder itself if it has `pack.yaml` (and nothing else). If
not, it returns each immediate subfolder that has it and each immediate `.pak` file, sorted by path. This way you can
mount `content/base`, all of `content/` or a `content/` that mixes folders and archives. The precedence rules:

- A folder beats the `.pak` with its name: with `base/` and `base.pak`, `base/` is mounted, and `base.pak` shows up in
  `hidden`. The folder is what gets edited and the archive is built from it (`dwx pack`), so the folder is the most
  recent version.
- Only the file's name counts, not the `id` of its `pack.yaml`: two packages with different names and the same `id`
  are both returned and mounting fails with "mounted twice", which says both paths.
- A `.pak` inside a subfolder does not count (the same as a `pack.yaml` two levels down), and a `.pak` inside a
  folder that is already a package is an asset of that package.
- `findPacks` does not decide the stack's order: the layer and priority of each manifest decide it when mounting.

`openPacks(disk, found)` opens each archive with `PakFileSystem` and returns the `PackLocation`s for `mount` together
with the open archives, which must live as long as the virtual file system. The debug overlay's asset viewer uses it
that way and says in its status which archives were left hidden.

## Development assets

With `--dev-assets` (development only, never by default) `GameContent` also mounts the packages from the extracted
folders that the development configuration file names, only to fill in what is missing. A normal package beats the
development package with its id, and an earlier folder beats a later one; those left out are logged. Those that come
in fill gaps (`PackLocation::fillsGaps`): they go below all the normal packages, whatever their layer and priority
say, and the log marks them. A disc image in that file arrives as one more extracted folder, the last one: `dwx`
extracts it to a development cache. Details in [development assets](development-assets.md).

## Errors

Mounting fails, with a message that says the package or the file, if:

- `pack.yaml` is missing or has an error (with line and column);
- there are two packages with the same `id`;
- a dependency is missing, its version is lower than `min_version`, it is in a higher layer or there is a cycle;
- a file does not give a valid ID, two files of a package give the same ID or there is a symbolic link.

`read` fails if no package has the ID or if the file cannot be read.

## YAML

The manifests and the rest of the source format are read with `core::parseYaml`
([ADR 0016](../adr/0016-yaml-with-libyaml.md)). Only one document per file is accepted, without anchors, aliases or
tags, with scalar keys and without repeats. There is a limit of 16 MiB and 64 levels of nesting.
