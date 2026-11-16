# curling

The stone game on the ice rink.

- Folder: `packages/game/curling`; CMake targets: `dwe::curling` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/curling](https://indie-modding-games.github.io/dw-engine/modules/game/curling).

Build and test only this package:

```sh
scripts/dev.sh build --filter curling
scripts/dev.sh test --filter curling       # its tests (ctest label curling)
scripts/dev.sh test --filter ...curling    # and those of everything that depends on it
```
