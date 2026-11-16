# fishing

Fishing: the rod, the float, the line and the fish in a map's water.

- Folder: `packages/game/fishing`; CMake targets: `dwe::fishing` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/fishing](https://indie-modding-games.github.io/dw-engine/modules/game/fishing).

Build and test only this package:

```sh
scripts/dev.sh build --filter fishing
scripts/dev.sh test --filter fishing       # its tests (ctest label fishing)
scripts/dev.sh test --filter ...fishing    # and those of everything that depends on it
```
