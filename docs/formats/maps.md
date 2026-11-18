# Maps

How `formats/map` reads the field maps: the `.MAP` (camera, lights, images, sprites, entities and collision), the
`.TFS` (the prerendered background) and the executable tables that describe them. The field names are those of the
decomp; a field the decomp does not name is called `unknownXX`, with XX its offset in the record.

## Executable tables

They are read with `formats/exe` (see [Tables](tables.md)) and go into the `maps` document:

| Table | Entries (US) | Content |
|---|---|---|
| `MAP_ENTRIES` | 255 × 16 | `MapEntry`: file name, number of 8- and 4-bit TIMs, flags, doors, toilet and loading name |
| `MAP_NAME_PTR` | 70 `char*` | names shown when a map loads |
| `DOOR_MAPDATA` | 24 | `DoorStruct`: six doors with a model (`DOOR\DOORnn.TMD`, −1 = none), position and turn |
| `TOILET_DATA` | 10 | the toilet's rectangle (`posX1`, `posY1`, `posX2`, `posY2`) |
| `MAP_LIGHT_UPDATE_DATA` | 1 | map objects that are only seen by day or by night, and their trigger |

Bits of `MapEntry.flags` (`map.c: getMapSoundId`, `overworld.c: loadMapDigimon`, `map.c: updateTimeOfDay`):

| Bits | Meaning |
|---|---|
| `0x1F` | map sound |
| `0x20` | no known code reads it |
| `0x40` | fixed time: the background always uses the first palette and there is no fade when the time changes (`map.c: renderMap`) |
| `0x80` | the entity section has NPC records |

Entries with an empty name have no file. The file path is `\MAP\MAP<id / 15 + 1>\<name>.MAP` (or
`.TFS`) (`map.c: buildMapPath`, `mapFilePath`). Two US entries share a file name; in the YAML the second
carries the suffix `_<index>` in its id.

## `.MAP`

It starts with a table of `u32` offsets, one per section, in this order (`map.c: loadMap`):

1. configuration;
2. the 8-bit TIMs (`num8bppImages`);
3. the 4-bit TIMs (`num4bppImages`);
4. the sprites, only if the map has any image;
5. the entities;
6. the collision.

The number of sections depends on the `MAP_ENTRIES` entry, so `readMapFile` receives `MapReadOptions` with the
two image counts and the NPC bit. Each section goes from its offset to the next (the last, to the end of the file).
The reader checks that the table fits, that the offsets grow and that none goes outside the file.

A `.MAP` that no entry names says part of that itself (`readMapFileShape`): the first offset is the size of the
table, and with it the number of sections; each image section starts with a TIM whose mode says whether it is 8- or 4-
bit. `inferMapReadOptions` gets the options from there, but only if the entity section ends right after the
warps and the number of NPCs: some maps without the NPC bit keep bytes after them that the game does not read, so a
longer section cannot be decided. In the US, the number of images always matches the entry; the split by
mode matches in all maps but one, which the game loads with a 4-bit TIM through the 8-bit loader.

### Configuration

All `s32` (`map.c: loadMapSetup`):

- the camera's viewpoint and reference point (`GsRVIEW2`; the original doubles each value when applying it);
- three lights (`GsF_LIGHT`: direction and color);
- the ambient, which the original reads and does not use (it always sets `0x800`);
- the projection distance (`GsSetProjection`);
- two regions, which are compared with `RAISE_DATA[partner].favoredRegion`;
- width and height of the background in tiles and the tile grid, row by row, with −1 in empty cells.

The original reserves 35 tiles; a larger grid is an error.

### Images

The TIMs are standard TIMs and are left as a byte range (`MapImageSection`: bits per pixel, offset and size) for the
image reader. The 8-bit ones are loaded with `overworld.c: loadMapImage1` and the 4-bit ones with `loadMapImage2`.

`loadMap` uploads the 8-bit ones first and then the 4-bit ones, in order, each with a single `LoadImage` to its VRAM
rectangle, with no further check: a TIM can occupy several pages (tunn05), half a page (gias02, 128 × 256 8-bit
texels) or a few rows (gomi02 and betl04, 256 × 32 4-bit texels at (704, 216)), and a later one covers what it
overlaps of an earlier one even if it has another depth (in gomi02 the 4-bit one falls inside the 8-bit page at
x 640). In the US, 20 of the 241 maps have one like that. The engine does the same (`loadMapImages`): it uploads the entries to
a map VRAM and reads the pages from it.

### Sprites

An `s16` with the number of sprites and then the sprites, 9 `s16` each in this order: `texX`, `texY`, `width`, `height`,
`someX`, `someY`, `someZ`, `clut`, `transparency` (`overworld.c: loadMapObjects`, `buildMapOverlayPrim`). `clut` −1 is
the map's 8-bit palette, 0–15 a 4-bit CLUT and 16 or more an 8-bit row (`clut + 468`).

Then, an `s16` with the number of instances (188 at most) and the instances (`LocalMapObjectInstance`): 8 sprites
per frame (−1 ends the list, −2 is an empty frame), 8 durations, position `x`, `y` in background pixels and
`flag` (1 = hidden, 0x80 = in screen coordinates).

The original stores the sprites in `LOCAL_MAP_OBJECTS[180]` without checking the number. One US map has more than 180 and
writes past the array, so the reader puts no limit on sprites.

### Entities

- **Warps** (`tamer.c: MapWarps`): six lists of 10 `s16`: `spawnX`, `spawnY`, `spawnZ`, `rotation` (4096 = one
  turn), `targetMap` and `targetExit`. Exit `i` says where the player appears when entering through it and where it leads.
  Unused exits have meaningless values, including `targetMap` outside `MAP_ENTRIES`.
- **Digimon count** (`s16`), stored as is even if the map has no NPCs.
- **NPCs** (`overworld.c: loadMapDigimon`), only if the entry has bit `0x80`; 8 at most. Each record is
  `s16`: 34 fixed (type, follow mode, position, turn, reach, `unknown12`, script, HP and MP, stats, brains,
  `bits`, load mode, `unknown2A`, 4 techniques and their priorities, flee point and number of waypoints), 8
  AI sections and 3 per waypoint (8 at most). `unknown12` and `unknown2A` are `NPCEntity.unk2` and `unk1`.

### Collision

100 × 100 bytes, row by row, one cell per 100 world units (`map_collision.c`). 0 is free, bit `0x80`
is blocked and the remaining values are triggers. The section has to be exactly 10,000 bytes long.

The tournament arenas keep the same grid in `STDDAT\*.ATR`, 10,000 bytes with no header that the original reads as is
into `MAP_COLLISION_DATA` (`std_setup.c: STD_loadArenaAssets`, `vs_scene.c: VS_loadArenaAssets`).
`collisionToYaml` writes both the same way.

## `.TFS`

The prerendered background (`map.c: readMapTFS`, `setupMap`, `updateTimeOfDay`):

1. a `u32` that the original skips; in the US files it is `height << 16 | width` of the background in pixels;
2. a `u32` with the number of palettes (31 at most);
3. the palettes, of 256 `u16` colors in the console's 15-bit format;
4. the tiles, one per non-empty grid cell and in its order: a `u32` that the original skips (in the US,
   `row * 128 << 16 | column * 64`, the position in VRAM) and 128 × 128 8-bit indices.

The number of tiles comes from the file size, which has to be a whole number of tiles.

Palette 0 is day, 1 is dusk and 2 is night; fixed-time maps use only 0
(`map.c: updateTimeOfDay`). The original uploads palette `i` to VRAM row `481 + i` (`map.c: setupMap`) and copies to
row 480 the one that applies. Palettes after the third would only be seen through a map sprite whose `clut`
points to their row (`clut + 468`); in the 3 US maps that have them no sprite does, so they are never seen.
`setupMap` also stores a pointer to each palette in `MAP_CLUTS`, which has 3 entries: the extra ones overwrite the next
variable.

`composeBackgrounds` places the tiles on the `.MAP`'s grid and gives an `IndexedImage` (`width * 128` by `height * 128`
indices and its palette) for each palette; empty cells are left with index 0, like the black the original clears them
with. It fails if the grid has more cells than the file has tiles. `backgroundVariantName` gives the name of each
variant: `day`, `dusk`, `night` and then `palette3`, `palette4`...

## YAML

`mapToYaml` writes one document per map with the schema `content/schemas/map.schema.json`:

| Key | Content |
|---|---|
| `camera` | `viewpoint`, `reference` and `projection` |
| `lights`, `ambient` | direction and color of the three lights; the ambient |
| `regions` | `a` and `b` |
| `tiles` | `width`, `height` and `rows`: one string per row with the numbers separated by spaces |
| `images` | bits per pixel, offset and size of each TIM |
| `objects` | `null` if there is no section; otherwise, `sprites` and `instances` (the frames, up to the −1) |
| `warps` | the 10 exits with `spawn`, `rotation`, `target_map`, `target_map_id` and `target_exit` |
| `digimon_count`, `npcs` | stored count and NPC records, with `type_id` and `digimon` |
| `collision` | `width`, `height`, `encoding`, `legend` and `rows` |

Numbers go as they are in the file. When a number points to another table its text id is also written
(`target_map_id`, `digimon`), or `null` if it falls outside the table: this way a meaningless value from the original is not lost.

Collision is written as 100 strings of 100 characters with a legend: `.` free, `#` blocked and a letter or digit
for each trigger value, in value order (`A`–`Z`, `a`–`z`, `0`–`9`). If a map had more than 62 trigger
values, `encoding` changes from `legend` to `hex` and each cell is two hexadecimal digits.

## In the engine

The engine does not read the `.MAP` or the `.TFS`: it reads what `dwx` leaves through the VFS. `assets::MapDocument` is `map/<id>/data`
and `assets::MapTable` is `data/maps` (see the [`assets` page](../modules/engine/assets.md#maps)). They keep the
numbers as they are; conversions to [world space](../architecture/world-space.md) are done in
`map_document.hpp` and nowhere else:

| Datum | Original | World |
|---|---|---|
| Position (`spawn`, NPC, doors) | `(x, y, z)`, Y down | `(x, -y, -z)` (`toWorldPosition`) |
| Collision cell | column `x / 100 + 50`, row `50 - z / 100` (`map_collision.c: getTileTrigger`, `getModelTile`) | column `floor(x / 100) + 50`, row `floor(z / 100) + 50` (`collisionCellAt`) |
| Turn about Y | 4096 per turn; 0 faces −Z, 1024 −X, 2048 +Z | `yaw = -turn * 2π / 4096`; a character with yaw 0 faces +Z and `forwardOfYaw` gives `(sin yaw, 0, cos yaw)` |

The cell: the original divides truncating and then corrects negatives, so on the exact lines `x = -100 k` and
`z = 100 k` (in its axes) it takes the neighboring cell. The engine uses `floor` in all cases; the difference is a point
with no thickness.

The direction of the turn comes from `tamer.c: tamerTickWalkingState`: up on the pad gives `getMapRotation() + 0x800`, and
`getMapRotation` is `_atan` of the vector from the reference to the camera's viewpoint; on a map with the camera
looking at +Z (in the original), up gives 2048 and the player moves away from the camera. The warps confirm it: in `twna01`,
exit 1 (`rotation` 3072) appears next to its warp cells, on the west side, and with 3072 it faces east, into the
map.

Collision: 0 is free, any value with bit `0x80` blocks (`checkMapCollisionX/Z`) and the rest are
triggers (`tamer.c: checkMapInteraction`): 51–79 call a script when stepped on, 80–109 when pressing confirm,
110–119 are the warps (110 + i takes exit i of this map to `target_map` / `target_exit`) and 120 calls a
script if the partner has bit 8 of `condition` (probably, that it needs the toilet).
Those bands are the original's; the engine reads them from `field/triggers` of the `game` package, so a package can
move them, and the field, the map debug layer and the map editor use those of the stack.
In the US only 0, 51–86, 110–116, 120 and 128 appear.

### Destination of an exit

An exit names its destination twice: `target_map`, the original's raw index, and `target_map_id`, the id from
`data/maps`. `assets::resolveWarpTargets` (`map_warps.hpp`) decides where it leads when entering the map:

1. A `target_map_id` that names a map in the composed table rules.
2. Otherwise, a `target_map` of 0 or more leads to the **original's** map with that index; never to one a package adds,
   whose index is set by the engine and depends on the mounted packages.
3. Otherwise, the exit leads nowhere and the field ignores it.

The base package is the engine's own package with the `base` layer: its origin and its layer decide this, like the
other rights of the base ([tables by entries](../architecture/vfs.md#tables-by-entries)). A mod that declares
`layer: base` counts as another package.

| Case | Base package | Other package |
|---|---|---|
| Both and they match | valid (what `dwx` writes) | valid |
| Both and they name different maps | error; the warp is disabled | error; the warp is disabled |
| An id of a map and a `target_map` outside the table | error; the warp is disabled | valid: the id rules |
| Only `target_map_id` (`target_map: -1`) | error: the base writes both | valid; this is how a map in a package is reached |
| Only `target_map` of a map (`target_map_id` `null` or absent) | error | valid |
| `target_map_id` that is not any map and a `target_map` of the original | error; the warp is disabled | leads to the original's map with that index, but `dwx validate` gives the id as an error and the game warns in the log |
| `target_map_id` that is not any map and no index that is | error | error |
| No id and a `target_map` outside the table, without its trigger in the collision | no destination, as in the original | unused exit |
| No id and a `target_map` outside the table, with its trigger | no destination, as in the original | **dead warp**: error |
| `target_map: -1` without an id, without its trigger (110 + i) in the collision | unused exit | unused exit |
| `target_map: -1` without an id, with its trigger in the collision | unused exit | **dead warp**: error |

A `target_map` outside another package's table includes the number the engine gave to a map in a package: that
number depends on the mounted packages and a package never writes it.

The dead warp is what a map editor that only knows the original's indices leaves when saving an exit to a map in
a package: it writes `-1` (or the number the engine gave the map) and loses the id. `dwx validate` gives it as an error with the
map, the exit and its line, and warns that the editor may have lost `target_map_id`. In the game, each exit with a
problem is disabled with a warning in the log that names it, except one with an unknown id next to an original index, which
still leads to that map; it never crashes. Two destinations that do not match disable the warp in any
package, and `dwx validate` gives it as an error.

### Maps a package adds

A package can bring maps that the original does not have. Its id is `<package>:<name>` and the game needs:

- **Data:** `map/<package>/<name>/data.yaml`, with the same schema as the others.
- **Entry:** a line in the package's `data/maps.yaml` with the id and **without** `index`: the engine numbers the maps of
  packages after those of the original, and that number depends on the mounted packages. `loading_name` is the
  index of the zone name.
- **Background:** `background_<variant>.png` (indexed PNG, whole 128 px tiles, up to 35) in its folder, or in the
  folder of the map named by the optional `assets_from` key in its data. `assets_from: twna01`, for example, takes
  the backgrounds and textures from `map/twna01/` without copying them; `dwx` never writes it.
- **Name:** the text `map_names.<name>` of `text/<language>/<package>.yaml`; without it, the game shows the key.

Exits that lead to such a map use only `target_map_id` with `target_map: -1`
([Destination of an exit](#destination-of-an-exit)). The story lock never closes a package's map or the
exits toward it. `dwx validate` (and `dwe-game --check-content`) gives as an error data without an entry in
`data/maps` ("the game never enters"), an entry without data in any package and a map without a background. The map editor
writes all of this at once ([New map](../modules/apps/mapper.md#new-map)); the step by step is in
[Adding a new map](../modding/new-maps.md).

## Patches

A package does not have to copy the whole `map/<id>/data` to change an exit or move an NPC: it can bring
`map/<id>/patch.yaml` (`map/<package>/<name>/patch.yaml` for another package's map) with only what changes
([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/map_patch.schema.json),
`packages/engine/assets/include/dwe/assets/map_patch.hpp`):

```yaml
camera: {projection: 1900}       # the camera fields that change
lights:
  - {index: 1, color: {r: 200, g: 180, b: 160}}
warps:                           # by exit number (its `exit`)
  - exit: 2
    target_map: -1
    target_map_id: example_mod:meadow
    target_exit: 0
npcs:                            # by position in the list, from 0
  - index: 0
    position: {x: 1200, y: 0, z: -800}
    waypoints: []
objects:
  sprites:   [{index: 0, clut: 3}]
  instances: [{index: 2, flag: 1}]
collision:
  cells:                         # rectangles of cells with a raw value
    - {column: 40, row: 50, width: 3, height: 2, value: 128}
    - {column: 41, row: 51, value: 0}
```

- **Each change names an entry** that the data already has (an exit by its `exit`; a light, an NPC, a sprite or an
  instance by its position) and gives only the fields it replaces, with the same keys as the data. Each field is
  replaced **whole**: a vector, the `waypoints` list or the `frames` of an instance. Entries are not added or removed:
  an exit, an NPC or an index that the data does not have is an error. That is why a mod that adds or removes NPCs brings the
  whole map `data` (the [mapper](../modules/apps/mapper.md#edit) saves it that way on its own); the new NPC appears
  when a story section makes it appear with `story.spawn_npc`.
- **Collision** is given whole in `collision.grid` (with the shape of the data's `collision`) or by rectangles in
  `collision.cells` (column and row of the first cell, `width` and `height` 1 by default, and the raw value: 0 free,
  bit `0x80` blocked, another a trigger). Rectangles are applied after the grid, in order. A patch's grid
  overrides the rectangles of the patches below it.
- **The order is the stack's.** The patches of the package whose `data` wins and of all those above it are applied,
  from bottom to top; if a package brings `data` and `patch`, its data goes first. A patch from a package that is below the
  winning `data` is left out: it was written for other data, whose positions may not be the same
  (`mapPatchSources`). A patch without its map's `data` in any package is a `dwx validate` problem.
- **Values are checked when applied**: after each patch the document is read again with the data's strict reader,
  so a value that is not valid is an error that names the patch file and its line. In the game, a map with a
  patch that cannot be applied does not load, just like broken `data`.
- **A changed exit belongs to the package that patches it** for the purposes of [its destination](#destination-of-an-exit): a mod that
  patches an exit of a base map can make it lead to its map by id alone (`target_map: -1`), and `dwx validate` gives
  its problems at the patch's line. A patch from an engine package of the base layer follows the base rule.

All those that read maps see it, because all go through `loadMapDocument`: the game field and the map
preview (`render::loadMapSceneData`), the route and simulation tests and `dwx validate`, which checks each patch against its schema (those of all packages, not only the
top one), applies it as the game does and looks for dead warps in the already patched map. The map editor
([mapper](../modules/apps/mapper.md), in Rust) applies patches with the same rules (`packages/rust/dwe-map`,
with tests that repeat the engine's) and says in its panel which packages patched the map. In the running game,
`reload_map` of the [control channel](../guides/remote-control.md#live-map-editing) reads the map again with
its patches, and the `field: entered ...` log line names the packages that patched it. The schema rule
`map_patch: [map/*/patch, map/*/*/patch]` is declared by the `game` package; a mod that is validated without it declares it in its
`pack.yaml`, as it does with `map: [map/*/*/data]` for its maps.

**Doors and toilet.** They are not in the map's `data` but in `data/maps` ([tables by
entries](../architecture/vfs.md#tables-by-entries)), which is composed entry by entry only in `maps`: a map's entry
says which set of doors (`doors`) and which toilet (`toilet`) it uses, by its index in the `doors` and
`toilets` sections, and those two sections are replaced whole by those of the highest package that brings them. So, a mod
that wants a map to use another set of doors or toilet that already exists brings only that map's entry with the index
changed; one that wants to move a door or a point of the toilet, or add a new set (at the end, with the next
`index`), brings the whole `doors` or `toilets` section, copied from the base and edited. Two mods that bring the same section
are not added together: the higher one wins. The mapper saves the doors and toilet that are edited this way: the whole section and, if the
set is shared by other maps, a new one with the map's entry changed.

## How it is drawn

What the original does to draw a field map, which is the specification of the engine's map rendering
([ADR 0020](../adr/0020-map-camera-and-depth-layers.md), [`packages/engine/render`](../modules/engine/render.md)).
`W` and `H` are the width and height of the background in 128-pixel tiles.

### Camera

- A map's camera **does not move**. `map.c: loadMapSetup` fills `GS_VIEWPOINT` with the file's viewpoint and reference
  point **multiplied by 2** (`<< 1`), and calls `GsSetProjection` with the projection distance
  as is. `rz` and `super` are 0 and the near plane is 100 (`main.c: view_init`). The game loop applies
  the same view again every frame; only versus mode moves the camera.
- The view from `GsSetRefView2` looks from `vp` to `vr` with no twist about its axis: it is a *look-at* with the original's Y
  (down) as the screen's "down". In the engine's world it is the usual *look-at* with +Y up.
- The screen is 320 × 240 non-interlaced (`GsInitGraph(320, 240, ...)`), with square pixels for the calculation:
  the same `H` applies on both axes. The GTE projects with offset 0 (`sx = H · Xv / Zv`, `sy = H · Yv / Zv`)
  and the screen center is set by the GPU's drawing offset (`GsSetOrign`).

### Background and scrolling

- The `.TFS` tiles are drawn as 128 × 128 `POLY_FT4` at the last position of the ordering table (`0xFFF`):
  behind everything (`map.c: renderMap`, `setPosDataMapTile`). An empty cell (−1) comes out black.
- `map.c: setupMap`, `initializeDrawingOffsets`, `tickCameraFollowPlayer` and `updateDrawingOffsets` move at once
  the visible corner of the background (`cameraX`, `cameraY`) and the drawing offset, and always keep
  `cameraX + DRAW_X = W · 64` and `cameraY + DRAW_Y = H · 64`. From there comes the exact relation between the world and the background:

  ```
  background pixel = (sx + W · 64, sy + H · 64)
  ```

  That is: the reference point `vr` falls at the center of the background, and the whole background is the image of **a single
  fixed camera** with its optical center at the center of the background. The screen is a 320 × 240 window over that image;
  moving it shifts the background, the sprites and the models equally (for the models it is a shift of the optical center,
  not a camera turn).
- The window follows the player with no dead zone or smoothing:
  `cameraX = clamp(W · 64 + sx(player) − 160, 0, W · 128 − 320)`, and the same in Y with 120 and 240. It only moves
  while the player walks with the D-pad; scripts move it with `moveCameraByDiff` and `createCameraMovement`.
  The return to center after an edge (`updateDrawingOffsets`) only looks at the pressed D-pad direction, so at
  an edge the window can end up somewhat off-center; the engine does not copy this.
  The original applies the offset one frame late, without background and models getting out of alignment.

### Depth layers

- The map's sprites (`LOCAL_MAP_OBJECTS`) are cutouts of the background that are drawn again **in front of** the
  models that pass behind. `someX`, `someY` and `someZ` are a world point (in the file's units, not
  doubled) that only serves for their depth.
- `overworld.c: calcMapObjectOrder` computes once, when the map loads, the position of each instance in the ordering
  table: it projects the point of the first valid sprite of its frames and takes `orderValue = SZ >> 4`, between `0x23` and
  `0xFF6` (`0x1000` or more gives `0xFF6`); if `someY` is 10,000 it gives `0xFFE` (just in front of the background), and instances with
  bit `0x80` give `0x1E`.
- Models are sorted per polygon (`GsSortObject4J` with offset 2 and `ZSF4 = 0x100`): their position is
  `average SZ >> 4`. A larger index is drawn earlier. A sprite covers a polygon when
  `average SZ >> 4 > orderValue`: the sprite is equivalent to a plane at distance `orderValue · 16` from the camera.
- `overworld.c: renderMapOverlays` draws each visible instance (`flag` other than 1) at `inst.x − cameraX`,
  `inst.y − cameraY`, with the sprite's size, and advances its animation once per game frame (the field runs at
  20 frames per second: `VSync(3)` in `main.c: gameLoop`). A −2 frame draws nothing but counts its time.
  Hidden instances also advance. Bit `0x80` (snow, in a few maps) draws in screen coordinates with random
  positions.
- Texture (`overworld.c: buildMapOverlayPrim`): the page is `384 + (texX / 256) · 128` in VRAM for 8 bits and
  `384 + (texX / 256) · 64` for 4 bits, with `u = texX % 256`, `v = texY`. The palette:
  - `clut` −1: row 480, that is, the background palette that applies for the time of day;
  - `clut` 0–15: row `clut` of the CLUT of the **first** 4-bit TIM, which `loadMapImage2` uploads by rows to
    `(row · 16, 486)`; the CLUTs of the other 4-bit TIMs are not uploaded;
  - `clut` 16 or more: VRAM row `clut + 468`. There is the CLUT of an 8-bit TIM that goes to that row
    (`loadMapImage1` uploads all but those of row 480, which the `.TFS` rules) or, if not, the `.TFS` palette `clut − 13`
    (row `481 + i`).
- `transparency` 4 is opaque; 0–3 are the console's semi-transparent modes (half, add, subtract, add a
  quarter), which only affect colors with the STP bit (`renderMapOverlays` marks the primitive semi-transparent
  when it is not 4). Color `0x0000` is never drawn. In the US `.TFS` files, 71 of 242 have colors with STP (in
  `twna01`, 506 of the 768 of its three palettes; in `room08`, none). The usual trick is black `0x8000` (black
  with STP) around the cutout in add mode: adding black changes nothing, so the cutout has no border.
- The engine stores STP in alpha (`colorFromOriginal`: 128 with STP, 255 without it, 0 for `0x0000`) and draws a
  semi-transparent sprite in two passes, its texels without STP opaque and those that have it with their blend. The `.TFS`
  palettes arrive as PNG, which does not store STP; the only color that can be recovered is opaque black, which can only
  be `0x8000` (`paletteFromPng`). With that, sprites with `clut` −1 come out exact in 97 of the 100 US maps that
  have them semi-transparent; in `fact11a`, `gias06b` and `miha01` the palettes also have other colors with
  STP that today are drawn opaque, until `dwx` stores the `.TFS` palettes with their STP bit.

### Time of day

`map.c: updateTimeOfDay`: from 6:00 to 15:59 palette 0 (day), from 16:00 to 19:59 palette 1 (dusk) and the rest 2
(night); at dusk the lights lose 30% of green and blue and at night half of everything. Fixed-time maps
(`0x40`) always use 0 and do not touch the lights. The palette change starts right at 6:00, 16:00 and 20:00
(`map.c: renderMap`, except on fixed-time maps) and is a 25-frame fade
(`map.c: tickDaytimeTransition`) in steps: each color advances 1/6, 1/5, 1/4, 1/3 and 1/2 of the way from the starting palette,
five frames each, and at the end it jumps to the new one; the lights change all at once at the end. The fade
pauses while the game is not in the field (`GAME_STATE` other than 0). The clock and its pace are in the
[field sheet](../reference/field.md#time-of-day). `light_updates` in `data/maps` says which
instances are seen only by day or only by night (`utils2.c: updateMapLightState`).

## Check with the disc

With `DWE_EXE_SAMPLE` and `DWE_MAP_DIR` (the US disc's `MAP` folder), `packages/formats/tests/real_data_tests.cpp` reads all the
entries with a file, composes their backgrounds and checks the YAML against the schema. In the US: 244 entries have a file
name, 3 of them are not on the disc and the other 241 are read without errors (216 with NPCs); all collisions fit
in the legend, with at most 26 distinct values. In 24 maps the options taken from the file read the same as
those of their entry; 1 map without the NPC bit has bytes after the warps. 512 backgrounds come out: 108 maps with 1 palette, 130 with 3, 1 with 4 and 2
with 5. No entry uses bit `0x20`.

## What is not known

- Bit `0x20` of the flags.
- One US map with a single palette does not have the fixed-time bit.
- `unknown12`, `unknown2A`, `followMode` and `bits` of the NPCs.
