# mapper

The `dwe` map editor (`dwe-mapper`): it opens the content folder as the game does and draws each map's collisions,
exits, characters, doors and toilet over its background. It edits the collision (brush, rectangle, fill, eyedropper),
the exits (destination, arrival, cells) and the NPCs (position, facing, fields, route), with undo and redo, and saves
what changed as `map/<id>/patch.yaml` in a mod package (or the whole map). It reads the maps with the packages' patches
and the trigger bands of `field/triggers` just as the game does, and connects to a running `dwe-game`: it follows its
map, shows where its characters are, sends it the unsaved change as a preview, takes the player to a point or an exit,
pauses it, reloads its map after saving and reads and writes the story state. It is a desktop app in Rust (egui),
apart from CMake.

- Folder: `apps/mapper` (the program, `crates/mapper-core` and `crates/mapper-live`); it uses the crates
  `packages/rust/dwe-pack` and `packages/rust/dwe-map`, which are in the same workspace (the `Cargo.toml` here).
- Depends on: no CMake package.
- Documentation: [modules/apps/mapper](https://indie-modding-games.github.io/dw-engine/modules/apps/mapper).

Build, test and open it (in the launcher's image, `scripts/mapper.sh`):

```sh
scripts/dev.sh test --filter mapper
scripts/mapper.sh run --content <content folder>
scripts/mapper.sh run --content <content folder> --mods <carpeta de mods> --map <id>
```

For modders: [Editing a map with the mapper](https://indie-modding-games.github.io/dw-engine/modding/maps).
