# Evolution tree metadata

`data/evolution_chart.yaml` keeps the positions and icon references of the Chart. The extractor gets them from the local copy of the executable; the engine does not distribute the original table or images. The schema is `content/schemas/evolution_chart.schema.json`.

The document contains `image` (the identifier of the local texture) and `entries`. Each entry identifies a `digimon` and its `index`, the `x/y` position on the native canvas, the icon origin `u/v` and `palette`. Icons are 16×16 pixels; their second frame is 16 pixels to the right. Species names use the same identifiers as the rest of the tables.

The normal disc extraction includes this document when its location is known. To update only this metadata, without regenerating all the assets:

```sh
dwx ui-chart /path/to/original-executable /new/path/evolution_chart.yaml
```

The command recognizes the version by SHA-1, checks the bounds of the references and refuses to overwrite an existing file. Only the US table has been located; other versions are rejected without guessing addresses. The output file belongs to the user's private data, outside the engine repository.

The 61 entries declared by the symbol and its size are read, without extending the read to the extra entry that the original code tries to walk. The table gives positions and icons; the "raised" state comes from the game's history. The Chart tab of the player sheet uses it (`apps/game/src/player_chart.cpp`), and a ruleset can add cells to it (`species_extra`).

## Detail geometry

`data/evolution_chart_detail` is a separate, optional document, extracted from the recognized executable. It does not change the `data/evolution_chart` contract. It can be obtained without regenerating the content:

```sh
dwx ui-chart-detail /path/to/original-executable /new/path/evolution_chart_detail.yaml
```

`lines` contains 20 records of eight coordinates: first the four X values and then the four Y values of each line. Consecutive groups correspond to even origins (4), odd origins (5), even destinations (6) and odd destinations (5). `sprites` keeps those same groups, with two coordinates per record. Their positions are relative to the center of the 320×240 canvas. `colors` contains 12 RGB colors, in pairs of main stroke and border for the destinations. `insets` contains six windows `[x, y, width, height]` with the origin at the corner of the canvas.

The reader checks the bounds of the executable and of the coordinates. Only the US location is confirmed. The Chart detail screen combines it with the routes from `data/evolution`, the species raised in the session and the localization. There are no original tables embedded in the sources and no images in this document.

The engine validates the exact dimensions of the arrays, the color ranges and the bounds of each inset before publishing the geometry. The origin/destination relations are obtained from `data/evolution` by stable identifier, preserving the gaps of each route. The interface distinguishes whether there is geometry and whether there are routes (`player.chart.detail.layout_ready` and `player.chart.detail.paths_ready`); without them the detail does not open.

To check the reading without modifying the pack or the saves, the demo accepts `--ui-demo-chart <view.yaml> --ui-demo-chart-detail <detail.yaml>`. The five detail icon palettes match those of the overview in the checked US version, even though their tables are at different addresses. This match must be verified again before enabling other versions.

The demo also supports `--ui-demo-chart-all-raised` to review icons and layouts of even, odd and empty routes. This option affects only the isolated scenario; it does not modify the player's history or saves.
