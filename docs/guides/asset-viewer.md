# Asset viewer

The debug overlay has a viewer that shows what is in the packs without writing code: which IDs exist, which pack
provides each one, what it overrides and, if it is a PNG or KTX2 texture, how it looks. The code is in
`packages/engine/debug/src/asset_viewer.cpp`.

## Opening it

1. Create the base pack from your disc: `dwx extract <image>` writes `content/base/` (see
   [formats and `dwx`](../architecture/formats.md)). Any folder with your own packs also works.
2. Start the game. By default the viewer mounts `content/base` relative to the folder you launch the game from;
   for another folder:

   ```bash
   scripts/dev.sh run --content content          # all the packs in content/
   scripts/dev.sh run --content ~/my-packs/hd # a single pack
   ```

3. Open the overlay (`º`, `` ` `` or `F1`) and check **asset viewer** in the **Frame** window. The **Assets** window
   appears.

The folder can be a pack (it has `pack.yaml` at its root) or a folder with packs: subfolders with `pack.yaml` and
`.pak` files (the ones `dwx pack` writes), mixed. If a folder and a `.pak` have the same name (`base/` and
`base.pak`), the folder is mounted and the window says the file was left out. It is mounted with the virtual file
system, with its layers, priorities and dependencies ([packs](../architecture/vfs.md), which explains these rules).
At startup, the log says what was mounted: a line
`asset viewer: mounted '<folder>': <n> assets; packs ...` with each pack, its version and its layer.

In the window itself you can type another folder and press **Mount** (or Enter) to mount it without restarting; this
is also useful to reload after extracting again.

## What it shows

- At the top, the folder, the number of assets and the pack stack from bottom to top.
- On the left, the IDs grouped by their first segment (`map`, `ui`...), with a text filter. IDs with no folder go in
  the `(top level)` group.
- On the right, the chosen ID, the pack and the file that provide it and, if other packs lower in the stack have it,
  which ones it overrides.
- If the winning file is a PNG or a cooked KTX2, the texture with its size in pixels and in bytes, how the file
  stores it and a zoom from 1x to 16x with *nearest* filtering, to see each pixel as it is. A KTX2 is transcoded to
  BC7 if the GPU samples it, and otherwise to RGBA8 (`assets::decodeKtx2`, see [KTX2](../formats/ktx2.md)). A broken
  file shows the error in red.

If the folder does not exist or has no packs, the window says so and explains how to create the base pack. A pack with
errors (a badly written `pack.yaml`, a missing dependency) appears with the virtual file system's message.

## Limits

- **Only PNG and KTX2 textures.** Models (`.glb`) and maps need the 3D render path, which this viewer does not use:
  it lists them and says which pack provides them, without a preview. To see them there are `--model-viewer` and
  `--map <id>` ([vertical slice](vertical-slice.md#playing)).
- The engine decodes PNGs with its own code (`assets::decodePng`), not with `packages/formats/`, which only `dwx`
  uses ([dependency rules](../architecture/dependency-rules.md)). It does not accept images more than 16384 pixels on
  a side.
- The viewer reads, decodes and uploads the texture when you select it, in the same frame. With textures the size of
  the original's you do not notice it.
- It is a development tool: it is not part of the game's UI.
