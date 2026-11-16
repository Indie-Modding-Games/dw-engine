# assets

The game's content by packs ([asset pipeline](../../architecture/assets-pipeline.md)). `AssetId` is the stable name of an asset (`map/file_city_01/background`) and `PackManifest` reads each pack's `pack.yaml`: id, version, layer, priority and dependencies. The optional `api` key says which version of the Lua API the pack's scripts ask for; `lua_api.hpp` has the version the engine provides (`SupportedLuaApi`, of which `script::LuaApiVersion` is an alias) and the rule that compares it (`checkApiCompatibility`, `checkPackApi`). It lives here, and not in `packages/engine/script`, because it is a data contract with `pack.yaml`: the game and `dwx validate` check it the same way without depending on Lua. The manifest format is also in [`content/schemas/pack.schema.json`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/pack.schema.json). `findPacks` finds the packs in a content folder (folders and `.pak` archives), `openPacks` opens the archives and `VirtualFileSystem` mounts them, stacks them by layer and priority and says which file wins for each ID. It reads files through `FileSystem`: `DiskFileSystem` for the disk, `PakFileSystem` for a [`.pak` archive](../../formats/pak.md) (which `writePak` writes) and `MemoryFileSystem` for the tests; each pack is mounted with its own (`PackLocation`). It only depends on `core`. `decodePng` decodes a PNG to 8-bit RGBA with lodepng, for the images the engine reads (those of the UI, the models' textures, the maps and the asset viewer of the debug overlay), and `inspectPng` reads only its header. `decodeIndexedPng` gives the indices of a palette PNG (one byte per pixel) and its palette, and `applyPalette` converts them to RGBA with any palette: this is how the map backgrounds and sprites are drawn with the time-of-day palette. `PngOnlyImages` (`png_only_images.hpp`) lists, by ID pattern, the images that the game reads from their PNG and not as a texture (the backgrounds and the texture pages of the maps, the two title images of the original look, the bitmap font atlas): `pngReadingOf` is the only rule, shared by `dwx cook` (which leaves them as PNG) and `checkContent` (which rejects, for them, any file that their reader does not read, with `openPngOnlyImage`); it also receives the image's `_meta`, so that an exception declared in the metadata can exempt an image that the pattern names. `loadMapImages` (`map_images.hpp`) reads the images of an extracted map: it checks that the background measures what its tiles say (128 pixels each) and that the variants match, uploads each texture entry to its VRAM rectangle like the original (the 8-bit ones and then the 4-bit ones, each with a `LoadImage`, so an entry covers whatever is underneath even if it is of another depth) and reads from there the 256 pages that the sprites look at; this is how entries of several pages load (tunn05 uploads three 8-bit pages in one), of half a page (gias02) or of few rows (gomi02: 256 × 32 4-bit texels at (704, 216)), and with `buildMapSpriteSheet` it resolves each sprite's page and palette from the VRAM layout that the data names (`MapVramLayout`); `mapSpriteOrderValue` gives its order value (`MapOrder`). They are properties of the extracted files, resolved here so that the render only draws quads ([ADR 0020](../../adr/0020-map-camera-and-depth-layers.md)). `decodeOpus` decodes an [Ogg Opus](../../formats/opus.md) from `dwx cook` to 16-bit PCM with libopus, with an Ogg page reader of its own. Version 2 `.pak` files are decompressed with zstd. `decodeKtx2` reads the cooked textures ([KTX2 with UASTC](../../formats/ktx2.md)) with libktx and transcodes them to BC7, or to 8-bit RGBA if the GPU does not sample BC7. `openAsset` opens the bytes of an asset with the engine's reader for its type (PNG, KTX2 transcoded to BC7, GLB with `parseModel` as the game loads it, glTF with cgltf, YAML, decoded Ogg Opus, and the headers of WAV, SF2, MIDI and Y4M); with the asset's ID, also the documents that the game reads with a strict reader (`map/<id>/data` with `parseMapDocument`, `data/maps` with `parseMapTable`, `data/items` with `parseItemTable`, `data/item_effects` with `parseItemEffectTable`, `data/shops` with `parseShopTable` and `data/new_game_stats` with
`parseNewGameStats`). `checkContent` does it with all the assets of a stack: it is the proof that the engine loads what is in a pack (`dwe-game --check-content <folder>`, `dwx validate`), also with packs in `.pak`. It opens several at once; an asset that fails is read and opened again alone before counting as a problem, and the one that then opens is returned separately (`dwx extract` warns about it with `dwx: warning:`). If `DiskFileSystem` cannot open a file, the error gives the system's reason (`lastOpenError`; on Windows it tells a file that another program has open from one without permission). `readModel` summarizes a glTF: external files, the parents of each skin's joints and the names of the animations. `parseModel` and `loadModel` read a binary glTF model (`.glb`, the ones `dwx` extracts) into `ModelData`: skinned vertices, materials, images, skin, nodes and clips with their `extras`, all validated; `loadCharacterModel` reads the `models/<name>.yaml` of the `game` pack that says which model and which clips a character uses ([models and animation in the engine](../../architecture/models.md)).

## Development assets

`DevAssetSources` (`dev_asset_sources.hpp`) describes the local assets that a developer uses with `--dev-assets`:
the disc image, the extracted folders and the decomp root, read-only. `validateDevAssetSources` checks
that each root exists and is of its type. `findDevPacks` and `selectDevPacks` (`dev_pack_selection.hpp`) choose, by the id of
their manifest, the packs from the extracted folders that get mounted: a normal pack beats the development one with its id
and an earlier folder beats a later one. The chosen ones fill gaps (`PackLocation::fillsGaps`): the VFS stacks them
below all the other packs ([development assets](../../architecture/development-assets.md)).

## Player images

The [custom images](../../guides/custom-images.md) arrive as a pack of the `user` layer
(`Layer::User`) that the launcher mounts. Their PNGs do not replace the asset, only **how it is drawn**: the size, the
rectangles and the positions remain those of the original image, and the new one only adds detail.

- `image_replacement.hpp`: `replacementScale(originalWidth, originalHeight, width, height)` gives how many times larger
  an image is (an integer from 1 to `MaxReplacementScale`, 8, the same on both axes) or nothing if it is not usable.
  `paletteImageId(image, palette)` gives the id of the version of an indexed image with a palette:
  `texture/etcdat/fi_info` with palette 12 is `texture/etcdat/fi_info_palette12` (`PaletteImageSuffix`).
- `VirtualFileSystem::read(source)` reads a specific source of an asset, whether or not it wins, and `packOf(source)` says which
  pack it is from: an image's loader keeps reading the game's even if a `user` pack has its own on top.
- `loadMapImages` (`map_images.hpp`) reads the game's backgrounds from below the `user` packs and leaves in
  `MapImages::replacement` (`MapBackgroundReplacement`: scale and one RGBA image per variant) the player's that
  fit. The indices and the palettes remain the game's, so the sprites, the cutouts and the draw
  order do not change; the alpha of each pixel is the class of the background texel underneath it. A variant without its own
  image uses the game's at the same scale. An image that does not fit (not a PNG, another proportion, a scale that is not
  an integer or different from that of the other variants) is discarded with a warning in the log and the game's is drawn.

The UI images go through `ui::loadDrawnImage` ([ui](ui.md)) with the same rules. The tests:
`tests/image_replacement_tests.cpp`.

## Maps

`MapDocument` (`<dwe/assets/map_document.hpp>`) is the `map/<id>/data` of a field map
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/map.schema.json), [format](../../formats/maps.md#in-the-engine)), with all the
fields typed and the numbers as the extractor wrote them: camera, lights, regions, tile grid, images,
sprites and instances, warps, NPCs and the 100 × 100 collision grid (`MapCollision`: free, blocked or trigger).
`parseMapDocument` reads the text and `loadMapDocument(vfs, id)` reads it from the VFS, applies the packs'
[patches](../../formats/maps.md#patches) to it (`<dwe/assets/map_patch.hpp>`: `patchMapDocument`,
`mapPatchSources`) and adds the id and its assets: the backgrounds (`map/<id>/background_<variant>`, in the order day, evening,
night, `palette3`...) and the textures (`texture8/<nnn>`, `texture4/<nnn>` and their `_meta`). `patches` says which packs
patched the map and `exitPatch(exit)`, which pack changed an exit.

`MapTable` (`<dwe/assets/map_table.hpp>`) is `data/maps` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/maps.schema.json)): per map,
its id, its index in the original (the one the warps use), the file, the flags (fixed time, NPCs), the doors, the bathroom and
the name that is seen when it loads (`displayName`, without the space padding). A map that a pack adds carries the
index the engine gives it (`originalIndex` false), so `findByOriginalIndex` only finds the original's.
`resolveWarpTargets` (`<dwe/assets/map_warps.hpp>`) decides where each exit of a map leads with the tolerant rule from
[maps](../../formats/maps.md#destination-of-an-exit) and returns its problems, such as the dead warp.

`DigimonTable` (`<dwe/assets/digimon_table.hpp>`) is `data/digimon`
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/digimon.schema.json)): per species, its id, name, model, level, specials, item
it drops and techniques. `RaiseTable` (`<dwe/assets/raise_table.hpp>`) is `data/raise`
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/raise.schema.json)): the care data of each partner (meal schedule, energy,
bathroom, favorite food, sleep pattern, training aptitude and weight). Both are read with a strict reader (an
extra or missing key is an error) and [`packages/game/partner`](../game/partner.md) reads them by the ruleset's
table role.

The conversions to [world space](../../architecture/world-space.md) are in the same header and only there:
collision cell ↔ position (`collisionCellAt`, `collisionCellCenter`), position and turn of a warp (`warpSpawn`) and
the original's turn ↔ yaw in radians (`yawFromOriginal`, `forwardOfYaw`).
`originalCellAt` gives the cell as the original counts it (getModelTile: truncates toward 0 and subtracts one on the
negative side), which at the exact edge of a cell differs from `collisionCellAt`: the point (600, 1700) is cell (56, 32)
for the original and (56, 33) by floor. A script's walk uses it to know whether it has arrived: with the floor rule,
the exit of `species_z` in tunn09 oscillated endlessly between (598, 1697) and (609, 1717). Before counting it rounds the position to the nearest
whole unit, as the original stores it: `species_w` in mgen98 slides along x from −400 to −399.99 and, truncated, that
position fell in the next column over and the walk to (−400, −500) never ended.

`ItemTable` (`item_table.hpp`) is `data/items`: per item, its id, its number in the original, the price, the merit
value, the sort category, where it is used and whether it can be dropped; the name and the description are UI texts and are not
read here. `ItemEffectTable` (`item_effect_table.hpp`) is `data/item_effects`: the function of each item
(`ItemHandler`), what it heals, what it evolves into and the values of its food case (`FoodValues`) or chip case
(`ChipValues`). `ShopTable` (`shop_table.hpp`) is `data/shops`: maps with a shop and shopkeepers. [`packages/game/inventory`](../game/inventory.md)
uses them. `NewGameStatsTable` (`new_game_stats.hpp`) is
`data/new_game_stats`: the stats with which the new-game script starts the partner
([scripts](../../formats/scripts.md#new-game-stats)); `loadNewGameStats` gives nothing if no
pack brings it. [`packages/session`](../apps/session.md) uses it.

## Content IDs and tables by entries

`ContentId` (`<dwe/assets/content_id.hpp>`) is the id of an entry in a content table (a Digimon, a map, an
item): the stable name of an original (`species_a`, `twna01`) or, for what a pack adds, `<pack id>:<name>`.
`assetPath` converts it to folders (`example_mod:meadow` → `example_mod/meadow`), so the asset IDs do not
change grammar. `composeTable` and `readComposedTable` (`<dwe/assets/table_composition.hpp>`) compose the file
of a table from all the packs that have it, from bottom to top: an entry with an id that already exists replaces it in
its place, a new one goes at the end with the next free `index` and a section that is not made of entries is replaced whole.
Each table says which sections are made of entries with its `…Composition` (`DigimonComposition`, `MapComposition`…), and its
reader reads it composed. `composedTableKinds` and `composedTablesOf` (`<dwe/assets/table_kinds.hpp>`) say which of the packs' tables
are composed and of what kind they are (the usual ones and those that the rulesets name by role), so that `checkContent` composes
the same ones as the game. `requireNewSpeciesContracts` (`digimon_table.hpp`) requires the model contract of each Digimon
that a pack adds. The rules are in [tables by entries](../../architecture/vfs.md#tables-by-entries).

## IDs in the session

At the edges everything is named by id: saved games, rulesets and data files, scripts and
hooks, text keys and logs. An entry's `index` is internal: that of an original is its number in the
game (given by the base pack) and that of an entry that a pack adds is set by the engine when composing (the next
free one), so it changes depending on the mounted packs. `ComposedTableText::engineNumbered` says which entries the
engine numbered and `ItemEntry::originalIndex` / `MoveEntry::originalIndex` keep it in each entry.

Inside the session the game keeps a *handle* (`ItemHandle`, `MoveHandle`; `<dwe/assets/table_handle.hpp>`): the place
of the entry in the composed table. They are distinct types for each table and only the table creates them
(`ItemTable::handleOf(id)`), never from a number; a handle never leaves the session. The composition is
immutable during the session: mounting other packs means starting a new session from the ids. Each composed table
carries its `CompositionGeneration` (its copies share the same one) and each handle its own; `at(handle)` with a handle from
another composition is a programming error (`DWE_ASSERT`), and `owns(handle)` asks it without failing.
`findByOriginalIndex` searches by the original's number and is only used by the migration of old saved games and the
checking of the rulesets' ranges.

`EntryClass` (`<dwe/assets/entry_class.hpp>`) is a class of entries that a ruleset names (the foods, the
finishers): ranges and thresholds of original numbers, which only cover original entries, plus a list of ids, through
which the mods' entries get in. `checkEntryClass` checks on loading that each number in a range is an original
entry and that each id exists ([ranges and ids](../../architecture/rulesets.md#ranges-and-ids)).

`YamlReader` (`<dwe/assets/yaml_reader.hpp>`) reads typed values from a YAML document and keeps the first error with
its line and column; the engine's document readers use it.

The tests in `map_content_tests.cpp` read all the maps of the extracted content if `DWE_CONTENT` points to it.

## Techniques and evolution

Alongside `DigimonTable` and `RaiseTable`, two more tables that `dwx` extracts from the user's disc to the `base` pack, with the
same strict reader (each row with exactly its keys, each id only once); their schemas are in
`content/schemas` and [`packages/game/evolution`](../game/evolution.md) and battle read them:

- `MoveTable` (`<dwe/assets/move_table.hpp>`, `data/moves`): each technique with its index, power, cost and other fields;
  `unknown_0d` is read as `flags` (bit 0: needs a target; bit 1: shares cooldown) and, if present,
  `learn_chances` (the probability of learning it in combat per specialty slot). A repeated index is also
  an error.
- `EvolutionTable` (`<dwe/assets/evolution_table.hpp>`, `data/evolution`): requirements, reference stat rows,
  evolution cards and, if present, hand-written evolution item destinations (`item_targets`, which `dwx` does not
  write: it extracts them into `evolves_to` of `data/item_effects`, and `item_targets` replaces them item by item).

Each one has `parse…Table(text)` and `load…Table(vfs, id)`. References to other rows (`TableReference`,
`<dwe/assets/table_reference.hpp>`) remain as text: an id, nothing, or a raw number that the extractor could not
resolve; whoever uses them resolves them.
