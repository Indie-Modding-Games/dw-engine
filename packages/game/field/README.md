# field

The field logic: the player moves around a map, collides, changes screens and talks to NPCs.

- Folder: `packages/game/field`; CMake targets: `dwe::field` (`game` layer).
- Depends on: `assets`, `core`, `scene`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/field](https://indie-modding-games.github.io/dw-engine/modules/game/field).

Build and test only this package:

```sh
scripts/dev.sh build --filter field
scripts/dev.sh test --filter field       # its tests (ctest label field)
scripts/dev.sh test --filter ...field    # and those of everything that depends on it
```
