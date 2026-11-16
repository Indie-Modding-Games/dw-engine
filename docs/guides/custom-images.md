# Custom images

The player can export the images the game read from their own disc, improve them and load them in the launcher. The
game draws them in the same place and at the same size on screen, with more detail. The images always come from the
player's disc: neither the launcher nor the repository carries any.

Today you can change:

- **The map backgrounds:** `map/<map>/background_<variant>.png`, with the variants `day`, `dusk`, `night` and, in a
  few maps, `paletteN`.
- **The title screen, the menus, the icons and the digits of the date:** each image in `texture/`, once per palette:
  `texture/.../<image>_palette<N>.png`. The game draws many of those images with several color palettes and each file
  is the image with one of them. For example, `texture/etcna/title2_palette0.png` is the title screen and
  `texture/etcdat/fi_info_palette12.png` has the field menu's icons and the digits of the year and the day.

## In the launcher: the Images section

The **Images** section of the sidebar prepares the images the first time (a few seconds, with `dwx export-images`)
and offers:

- **Export originals:** copies all the images into a new folder, `dwe-images`, inside the one you choose, with
  `README.txt` (what each folder is and how to improve them, in the launcher's language) and `maps.tsv` (the name of each map).
- **Load folder / Load .zip** (or drop them onto the section): it looks for the files with the export's names and,
  before touching anything, shows a summary: how many images can be used, how many are the same as the originals
  (they are skipped), which names do not belong to any image and which have a size that does not work. **Use N
  images** copies only the good ones. It does not matter if the export's folder is inside another one: the name
  counts from the first `map/` or `texture/` folder, ignoring case.
- **The list:** the maps grouped by the letters of their id (`mayo`, `twna`...), with the name that most of them carry
  in the launcher's language, and then the title, menu and icon images by folder. Each row shows the original and
  yours, if there is one, with its scale. The search box filters by name or id.
- **Assign / Change / Reset** on each image: pick a PNG for that image (with the same checks as the bulk load) or go
  back to the original.
- **Use my images in the game** turns them on and off without deleting them; **Remove all** deletes them.

The game has no zones the launcher can read, so the map groups are the letters of the id; each group is named like
most of its maps.

## Which image works

An image works if:

- it is a PNG (of any type: indexed, RGB or RGBA);
- it has the same aspect ratio as the original and is 1, 2, 3... up to 8 times larger, the same number of times in
  width and in height (from 768x640 to 1536x1280 yes; to 1600x1280 no);
- in a map, all the variants you change have the same scale;
- it is not identical to the original.

If an image does not work when playing (for example, it was changed by hand in the folder), the game draws the
original and notes it in its log.

## How the game uses it

Everything goes into a single pack in the `user` layer at `<data>/custom-images/pack` (`pack.yaml` with
`id: custom_images`). While it is on, the launcher starts the game with `--mods <data>/custom-images/pack`. The
exported originals live in `<data>/custom-images/originals` and are prepared again when the game or the disc read
changes.

- **Map backgrounds** (`packages/engine/assets`, `map_images.cpp`): the game always reads the map's data (indices and
  palettes) from the highest game pack that is not in the `user` layer, so a player's image never breaks a map. The
  player's image is drawn through the high-resolution background path (`MapRenderer`, the same one the HD scenery
  improvement uses) with its scale: each original pixel corresponds to a square of k×k pixels of the image. Which
  parts cover the characters still comes from the original. A variant with no image of its own uses the enlarged
  original. The maps' texture pages (`texture8/`, `texture4/`) cannot be changed yet.
- **Title, menus and icons** (`packages/engine/ui`, `image_reference.cpp`): when the interface asks for
  `texture/... ?palette=N`, it looks for `<image>_palette<N>` (without a palette, 0). It tells the interface the size
  of the original, so the sprite sheets, the `<img rect>` rectangles and the decorators stay in pixels of the
  original, and the texture keeps its whole resolution.

## For developers

```sh
dwx export-images <content> [--with <packs folder>]... [--language es] -o <folder>
```

It writes the export the launcher reads (without `README.txt`, which the launcher adds): the base's images at their
size and `maps.tsv` (`<id>\t<name>`). It only replaces an empty folder or a previous export. It reads the base pack as
the install left it, folder or `base.pak`: in `base.pak` the single-palette images are cooked to KTX2, and the export
decodes them to color PNG (the palette is already applied, like `_palette0`).

To test in the game without a launcher: create a folder with the `pack.yaml` from
`apps/launcher/src-tauri/src/custom_images/pack.yaml` and the images with the names above, and start the game with
`--mods <folder>`. The log says `map <id>: drawing the player's background images, 2x, ...`.

The tests: `packages/engine/assets/tests/image_replacement_tests.cpp` (scale, failures and return to the original),
`packages/engine/render/tests/scenery_upscale_tests.cpp` (where each pixel lands),
`packages/engine/ui/tests/image_replacement_tests.cpp` (palettes and size), `apps/dwx/tests/export_images_tests.cpp`
and, in the launcher, `custom_images/tests.rs` (names, checks, bulk and per-image load, export) and `launch.rs`
(`--mods`).
