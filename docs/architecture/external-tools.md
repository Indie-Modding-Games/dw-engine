# Compatibility with the team's tools

Some of the organization's tools read what `dwx extract` writes and write packs that the engine loads. What those tools
use is a **contract**: if the engine changes it without notice, the tools break. This page says what is part of the
contract, who depends on each part and how it is changed.

## Tools

| Tool | Repository | What it reads from the engine | What it writes |
|---|---|---|---|
| Digimon Animation Studio (Blender add-on) | [3D-Moddeler-Base](https://github.com/Indie-Modding-Games/3D-Moddeler-Base) | `digimon/<slug>/model.glb` from the base pack | A pack in the `mods` layer with `digimon/<slug>/model.glb`, with the new animations as `anim_NN` |
| DW1 Field Editor (`dw1fe`) | [Digimon-World-Map-Editor](https://github.com/Indie-Modding-Games/Digimon-World-Map-Editor) | `map/<id>/data.yaml`, the backgrounds and the textures `map/<id>/texture8/NNN.png` and `texture4/NNN.png` from the base pack (`engine-check` uses them to compare itself); `data/maps.yaml`, `data/digimon.yaml`, `data/skeletons.yaml` and `data/items.yaml`; the door models `model/door/doorNN.glb`; `digimon/<slug>/texture.png` and `texture_meta.yaml`; the scripts `script/dg/NNN.yaml` and `script/maphead.yaml`, which its Script panel edits as text; `texture/etcdat/item.png` and its `_meta.yaml` | A pack in the `mods` layer with `map/<id>/data.yaml`, `map/<id>/background_*.png` and the edited scripts `script/dg/NNN.yaml` and `script/maphead.yaml`, all in the engine's YAML |

Both tools are verified against the output of `dwx extract`:

- The Studio checks that its 4843 animations match the engine's.
- The editor checks that its 241 `data.yaml` files are identical, byte for byte, to those in the base pack.

A pack that a tool produces is checked together with the base pack, which is what it depends on:
`dwx validate --with <content>/base <pack>`. The `--with` option also mounts the packs in that folder to resolve
dependencies, and the pack is checked as a mod, the way the game loads it. Besides the schemas, references and contracts, `dwx validate` loads each asset
with the same readers the game uses, so a pack that passes validation is a pack the game accepts:

- The `api` of each `pack.yaml`, with the game's manifest reader (`MAJOR.x` or `MAJOR.MINOR`; otherwise the pack is
  not mounted) and with the same compatibility check the game does (`assets::checkPackApi`): a different major
  version or a minor version newer than the engine's is a problem in the `pack.yaml`.
- Each `.glb` model with `parseModel` (`packages/engine/assets/src/model_data.cpp`): nodes, a single skin, joints and
  the weights of each vertex (which add up to 1 and name joints of the skin), embedded PNG images and the `extras` of
  each clip (`readClipExtras`: `ticksPerSecond`, `tickCount`, `loops`, `rootMotionNode`...).
- Each `map/<id>/data.yaml` with `parseMapDocument` (`packages/engine/assets/src/map_document.cpp`).
- `data/maps.yaml` with `parseMapTable` (`packages/engine/assets/src/map_table.cpp`).
- `data/digimon.yaml`, `data/raise.yaml`, `data/moves.yaml` and `data/evolution.yaml` with their readers
  (`parseDigimonTable`, `parseRaiseTable`, `parseMoveTable` and `parseEvolutionTable`, in `packages/engine/assets/src/`).
- The data tables are read the way the game reads them: composed entry by entry with those of the packs below
  ([entry-based tables](vfs.md#tables-by-entries)), so a pack that only brings its new rows is validated
  together with the whole table.
- `data/items.yaml`, `data/item_effects.yaml`, `data/shops.yaml` and `data/new_game_stats.yaml` with their readers
  (`parseItemTable`, `parseItemEffectTable`, `parseShopTable` and `parseNewGameStats`, in `packages/engine/assets/src/`).
- The texts, `localization/<language>/*.yaml` and `text/<language>/*.yaml`, with `parseTextTable`
  (`packages/engine/assets/src/text_table.cpp`).

Both YAML readers are strict: an extra or missing key makes the document fail even if it meets its schema. Each
failure comes out as a problem with the file, the asset ID and the reason
(`<file>: the engine cannot load <id>: <reason>`), and `dwx validate` ends with an error.

## What is part of the contract

| Part | Source of truth | Who depends on it | Detail |
|---|---|---|---|
| `pack.yaml` manifest | `content/schemas/pack.schema.json`, [VFS](vfs.md) | Both | `id`, `version`, `layer: mods` and `dependencies` with `base` and `min_version`. Optional: `api` (`1.x` or `1.3`), the Lua API version that the pack's scripts require |
| Asset ID rules | [VFS](vfs.md#asset-ids) | Both | The ID is the path without the extension; a file that does not give a valid ID makes the pack fail |
| Layers and priority | [VFS](vfs.md) | Both | A `mods` pack wins over `base` by ID |
| Content IDs and entry-based tables | [VFS](vfs.md#tables-by-entries), `packages/engine/assets/include/dwe/assets/content_id.hpp` | Both | The originals are named as before (`agum`, `twna01`); what a pack adds is named `<pack id>:<name>` and its assets go in `<type>/<pack id>/<name>/`. A table in a pack replaces, by `id`, the entries of those below and adds the new ones; a whole table (an edited copy of the base one) gives the same table as before |
| Digimon IDs | `apps/dwx/src/asset_names.cpp` (`digimonSlug`) | Studio | `SPCA` → `digimon/spca/...` |
| Map IDs | `apps/dwx/src/asset_names.cpp`, from `MAP_ENTRIES` | Editor | The map folder is the first `MAP_ENTRIES` record that names the file |
| `.glb` model | `packages/formats/src/model/gltf.cpp` (`writeModelGlb`), [models](../formats/models.md) | Studio | `bone_NN` nodes, a rigid skin, axes `(x, -y, -z)`, scale 4096 = 1 |
| Animations | `packages/formats/src/model/animation.cpp`, [models](../formats/models.md#animations) | Studio | `anim_NN` with NN the id, one frame per tick at 20 per second, root motion on bone 0 and `extras` (`id`, `frameCount`, `hasScale`, `ticksPerSecond`, `tickCount`, `endsInLoop`, `rootMotionNode`, `loops`, `textureMoves`, `sounds`). The engine reads them in `packages/engine/assets/src/model_data.cpp` (`parseModel`, which calls `readClipExtras`) |
| Model contract | `content/schemas/model_contract.schema.json`, `digimon/<slug>/contract.yaml` | Studio | What `dwx validate` requires of a model that replaces the original |
| Map table `data/maps.yaml` | `content/schemas/maps.schema.json`, `packages/engine/assets/src/map_table.cpp` | Editor | The editor only reads it, and only uses `maps[].index` and `maps[].id`; it does not write it. The engine's reader is strict about keys: an extra or missing key makes the table fail |
| Tables `data/digimon.yaml`, `data/skeletons.yaml` and `data/items.yaml` | `content/schemas/digimon.schema.json`, `skeletons.schema.json`, `items.schema.json` | Editor | The editor reads them |
| Map `data.yaml` | `content/schemas/map.schema.json`, `packages/formats/src/map/map_yaml.cpp` (`mapToYaml`) | Editor | Fields, and also **the exact text**: the editor writes with the same rules as `emitYaml` (`packages/engine/core/src/yaml.cpp`) |
| Map backgrounds | [maps](../formats/maps.md) | Editor | `background_day.png`, `background_dusk.png`, `background_night.png` or `background_paletteN.png` |
| Map textures | `content/schemas/image.schema.json`, [extract](../formats/extract.md) | Editor | `map/<id>/texture8/NNN.png` and `map/<id>/texture4/NNN.png`, each with its `NNN_meta.yaml` (`mode`, `image_rect`, `clut_rect`, `palettes`, `offset`) |
| Door models | `packages/formats/src/model/gltf.cpp` (`materialExtras`), [extract](../formats/extract.md) | Editor | `model/door/doorNN.glb`; the `extras` of each material say where its texels go: `image`, `clut` and `depth` |
| Digimon texture | `content/schemas/image.schema.json` | Studio and editor | `digimon/<slug>/texture.png` and `digimon/<slug>/texture_meta.yaml` |
| Event scripts | `content/schemas/script.schema.json`, `packages/formats/src/script/script_yaml.cpp` (`scriptToYaml`), [scripts](../formats/scripts.md) | Editor | `script/dg/NNN.yaml` and `script/maphead.yaml`; the operation names, the offsets, the labels, `code_end` with its padding and the count of unreachable instructions. The editor converts each script to text and back, and checks that the same YAML comes out |
| Item textures | [TIM](../formats/tim.md) | Editor | `texture/etcdat/item.png` and `texture/etcdat/item_meta.yaml` |

## How the contract is changed

1. **Adding is free:** a new optional field, a new file or a new ID breaks no one, as long as a pack written earlier
   remains valid.
2. **Changing or removing is a contract change.** For example:
   - renaming a field, a file or an ID;
   - making a field required;
   - changing the axes, the units or the ticks;
   - changing how `emitYaml` formats the text, which breaks the editor's byte-for-byte comparison.

   A contract change:
   - goes in its own commit, with the explanation in the message;
   - is noted in the log below, with the date and what each tool has to change;
   - is announced to the tools' owners before it is published.
3. **If possible, the old form is accepted for a while.** For example, a reader that understands both the old and the
   new field.
4. **The engine's tests protect the contract.** The output of `dwx extract` is deterministic and covered by tests.
   A test that changes because the contract changes is the sign that step 2 must be followed.

## Contract change log

| Date | Change | What the tools have to do |
|---|---|---|
| 2026-10-19 | Additive: a content folder also mounts the `.pak` files it has directly inside (`findPacks`), and if there is a folder and a `.pak` with the same name (`base/` and `base.pak`) the folder wins and the archive is left out. A pack in a folder is mounted as before | Nothing. To test a packaged mod, leave its `.pak` next to `base/` |
| 2026-10-20 | Additive: the table lists what the map editor already reads and writes (`data/maps.yaml` and the other tables, map textures, doors, Digimon texture, scripts in the engine's YAML), the `extras.rootMotionNode` of the animations and `dwx validate --with`. The contract does not change | Nothing |
| 2026-10-20 | Stricter validation: `dwx validate` loads the `.glb` files, the `map/<id>/data.yaml` files and `data/maps.yaml` with the game's readers, so it rejects what the game would reject. The format does not change | Nothing, if the game already loaded the pack. A pack that now fails validation was not loaded by the game either |
| 2026-10-21 | Additive: new documents `data/item_effects` and `data/new_game_stats`, the optional field `moves.learn_chances` and the optional `evolution.item_targets` of the ruleset, written by hand. Both tools are compatible with phase 3 and the contract does not change | Nothing |
| 2026-10-21 | Stricter validation: `dwx validate` also reads `data/items`, `data/item_effects`, `data/shops`, `data/new_game_stats` and the texts (`localization` and `text`) with the game's strict readers. The format does not change | Nothing, if the game already loaded the pack. A pack that now fails validation was not loaded by the game either |
| 2026-10-21 | Review of 3D-Moddeler-Base at `8acd5ca` (`.dasanim` exporter and importer, glTF viewer, `.opencode/skill/dw_engine.md`, `anim_format.md`) and `d4e349f` (only `.gitignore`); Digimon-World-Map-Editor stays at `f067e23`. What the Studio writes into a pack (`pack.yaml`, `digimon/<slug>/model.glb`, `anim_NN`, 20 ticks per second, root motion on bone 0, `extras`) does not change. The `.dasanim` is an intermediate Studio format and by default it is not written inside a pack. If someone copies one into a pack, the engine does not ignore it: a name with capitals or spaces does not give a valid ID and the pack does not mount, and if the name is valid `dwx validate` rejects it because the engine has no reader for `.dasanim`. Compatible | Nothing. Do not leave `.dasanim` files inside a pack |
| 2026-10-21 | Additive: round-trip tests of the contract with the map editor (see [below](#round-trip-tests)). The format does not change | Nothing |
| 2026-10-21 | Additive: [entry-based tables](vfs.md#tables-by-entries). The `data/digimon`, `raise`, `maps`, `items`, `moves` and `evolution` tables in packs are composed by `id` instead of being replaced whole; `index` is optional (the engine numbers new entries); content IDs accept the `<pack>:<name>` namespace, with their assets in `map/<pack>/<name>/` and `digimon/<pack>/<name>/`; text keys accept `<block>.<name>`. The top-level sections of `data/maps` and `data/evolution` are optional. A whole table gives the same as before | Nothing. To add a map or a Digimon without copying the table, write only its rows with an id `<pack>:<name>` |
| 2026-10-21 | Stricter validation: `dwx validate` also reads `data/digimon`, `data/raise`, `data/moves` and `data/evolution` with the game's readers, composed with those of the packs below. The format does not change | Nothing, if the game already loaded the pack |
| 2026-10-21 | Stricter validation: a Digimon that a pack adds needs its `digimon/<pack>/<name>/contract.yaml`; two base entries with the same `index` and an entry list set to `null` are errors; the tables that a ruleset names by role are composed and read like the usual ones. `dwx validate` understands `patternProperties` in schemas. The rows of `data/raise` and `data/evolution` may have no `index` | Nothing: the tools' packs only replace originals |
| 2026-10-21 | Additive: optional key `api` in `pack.yaml` (`MAJOR.x`, like `1.x`, or `MAJOR.MINOR`, like `1.3`) with the Lua API version that the pack's scripts require. The game leaves out, with the reason, a mod that requires another major version or a newer minor one, and mounts the rest; `dwx validate` flags it as a problem. A `pack.yaml` without `api` is read as before | Nothing. A pack with Lua scripts can declare `api: "1.x"` |
| 2026-10-21 | Stricter: a mod (a pack from a mods folder or from `--mods`) can only declare `layer: mods` or `user`; one that declares `base` or `remaster` is left out with the reason. What a pack can do like the engine (its interface Lua, ids without a namespace and the `index` values of tables) depends on where it was found, not on its layer. `dwx validate` checks packs as mods by default: it flags that layer as a problem, and also a new id without the pack's namespace. `--engine` checks them as packs of the game itself | A tool pack that is installed as a mod declares `layer: mods` and names its new entries `<pack>:<name>`. It is validated as always, with `dwx validate --with <content>/base <pack>`, which now checks it as a mod |
| 2026-10-21 | **CONTRACT CHANGE** (map editor). The destination of an exit follows a tolerant rule ([maps](../formats/maps.md#destination-of-an-exit)): a `target_map_id` that names a known map takes precedence; otherwise, a `target_map` of 0 or more leads to the original's map with that index. In the engine's base pack (by its origin and its layer, not by the layer it declares) both are required and match (what `dwx` already writes). In another pack the id is enough, `target_map_id` may be missing, and two that do not match are an error that disables the warp; an unknown id next to an original `target_map` leads to that map, but `dwx validate` reports the id as an error. An exit with no id, with `target_map: -1` or another number that is not an original map, and with its trigger on the collision is a dead warp: `dwx validate` gives an error with the map and the exit, and the game disables it with a warning. `data/maps` is also composed correctly when a pack keeps a whole copy with the base entries and their indices | `dw1fe` has to read `target_map_id` from `map/<id>/data.yaml` and write it back as is, also when `target_map` is -1 or missing, and resolve ids against the maps of the `mods` pack as well as those of the base. Without that it cannot create or re-save warps **to** maps in a pack. When `dw1fe` changes `target_map`, it must also change `target_map_id` or set it to `null`. For a warp to a map in the pack itself it writes `target_map: -1` and the id, never the number the engine gave the map. Editing base maps, and maps in a pack whose warps only go to the base, keeps working. A new `data/maps` entry cannot carry `index` (the engine sets it): when saving the whole table, `dw1fe` must not write the index of the pack's maps |
| 2026-10-21 | **CONTRACT CHANGE** (modders). `scripted_warps` is removed from `field/warps.yaml` (and from its `field_warps` schema): the file only carries `start`, and a pack that brings `scripted_warps` no longer validates. The map changes of the event scripts are extracted by `dwx` to `field/script_warps` in the base pack, with their conditions ([script warps](script-warps.md)); a mod that wants to change them replaces the whole `field/script_warps` (schema `script_warps`). Additive: `data/new_game_story` (schema `new_game_story`), the triggers set when a new game starts | `dw1fe` and the Blender add-on neither read nor write any of these files: nothing to do. The scripts' YAML (`script/dg/NNN.yaml`, `script/maphead.yaml`) does not change. A mod with `scripted_warps` has to remove it and, if it changed a script warp, bring its `field/script_warps` |
| 2026-10-22 | Additive: a `guard` or `script_guard` term in `field/script_warps` can be `forget` (`script`, `offset` and `triggers` optional): a command that runs game code that can write those triggers, or any code without `triggers` ([script warps](script-warps.md)). Whoever reads the file and does not know the term treats it as unknown | Nothing: `dw1fe` and the Blender add-on do not read `field/script_warps`. A mod that replaces it whole can keep going without `forget` |
| 2026-10-22 | **CONTRACT CHANGE** (modders), additive. `data/new_game_story` (schema `new_game_story`) gains the optional block `opening` (`exit`, `section`, `to` with `map` or `screen` and `exit`, `sets`): the map change that the new game makes on arrival, which `dwx` extracts ([script warps](script-warps.md)). A start in `field/warps.yaml` that is not that arrival takes its `sets` as already set. A document without `opening` is still valid | `dw1fe` and the Blender add-on neither read nor write `data/new_game_story`: nothing to do. A mod that brings its own `data/new_game_story` can add `opening`; without it, the start sets nothing more |
| 2026-10-22 | **CONTRACT CHANGE** (modders), additive. `field/script_warps` (schema `script_warps`): `via` can be `talk` (the map change of a section that a NPC on the map runs; previously `other`), and a record can carry `transport` (`entry`, `cost`, `hand_off`), a choice from the transport menu with the destination from the executable's table ([script warps](script-warps.md#the-transport)). Whoever reads the file and does not know `talk` or `transport` ignores them (a `talk` can be read as `other`). The saved game gains the additive section `story` (version 1: story triggers and variables); a game without it loads. `dwx` goes up to version 5 of the extractor | Nothing: `script_warps`, `scripted_warps` and `new_game_story` were searched for in the code of Digimon-World-Map-Editor (`813739e`) and of 3D-Moddeler-Base (`d76a424`) and they neither read nor write them, nor read saved games. A mod that replaces the whole `field/script_warps` can keep going without `talk` or `transport` |

**Plain texts (outside the contract).** `text/<language>/` are flat files `key: "text"`
([localization](localization.md)). It is not part of the contract, because no tool reads them.

## Round-trip tests

The map editor does not convert anything. It reads the engine's YAML as a tree, changes a value and writes it with the
same rules as `emitYaml`. The tests in `apps/dwx/tests/editor_*.cpp` repeat that with each type of file that the editor
writes and check two things: that opening and saving without changes gives the same bytes, and that an edit changes
only its value and the engine reads it.

| File | Synthetic (every commit) | User's base pack (`DWE_CONTENT`, slow tests) |
|---|---|---|
| `map/<id>/data.yaml` | A map written with `mapToYaml`: same bytes; a spawn point is moved and `parseMapDocument` reads it; if the change is undone, the original bytes come back | The 242 maps of the base pack, with the same edit in each one |
| `data/maps.yaml` | Same bytes; the `toilet` of the first map is removed and `parseMapTable` reads it; `parseMapTable` rejects an extra key (`weather`) or a missing one (`doors`) and names it in the error | The real table, with the same cases |
| `script/dg/NNN.yaml` and `script/maphead.yaml` | A script written with `scriptToYaml`: same bytes; a text with quotes and line breaks is edited; the labels are consistent (`at` in order, each `label` is `l_` and its offset, and the labels are exactly the targets of the jumps and sections); a jump to a label that does not exist is detected | The 222 listed, with a text edited in the 212 that have any |
| `mods` pack | `pack.yaml` (layer `mods`, dependency on `base` with `min_version`), an edited map with its backgrounds, a whole `data/maps.yaml` with one change, two edited scripts and a Studio `model.glb` with an extra `anim_01`: `dwx validate --with` gives no problems and the VFS resolves those IDs from the mod. A model that lacks an animation from its contract does not pass | A pack made in a temporary folder from the user's base pack: the map, the map table and a script, edited; a background and the model, copied unchanged. It passes `dwx validate --with` and wins over the base. Nothing is saved |
| `emitYaml` format | A test fixes the exact text of each type of value in these documents: hex, negatives, `null`, booleans, quoted texts with escapes and Unicode, empty lists and maps, nesting and unbroken lines | — |

If a change breaks the test that fixes the format, it is a contract change (step 2 above). Updating the test is not
enough.

An unquoted value that YAML has to quote (`a: b`) comes out in single quotes. When read back, the tree keeps it as
quoted, and the next write uses double quotes. So that document changes the first time it is opened and saved, and
after that it stays stable. The engine's writers quote all texts, so no extracted document has such a value. The test
fixes it so the editor knows.

The engine has no YAML-to-script reader, that is, there is no assembler. The editor does its own conversion of the
listing to text and back, and it is compared with the engine's YAML. The engine's tests check the properties of the
listing that this conversion relies on. The conversion of scripts to Lua already exists (`dwx convert-script` turns an edited listing into the Lua module that the
game loads), but the round trip from a listing to bytes is still pending: `formats::script::assembleScript`, which the
design owner's decision (2026-10-21) placed in that package with a byte-for-byte test of every script on disk, does
not exist yet.

The quoting behavior described above is left as it is (decision of 2026-10-21); the test fixes it.

## What is not yet tested

- Repeating it with a real pack generated by each tool: a `dw1fe` pack with edited maps, table and scripts, and one from
  the Studio with new animations. The round-trip tests prove our side of the contract, but not the tools' code.
- The round trip of an edited script all the way to its bytes, with `assembleScript` (does not exist yet).
