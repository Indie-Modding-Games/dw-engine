# weather

The field's weather: what the weather is on each map and when it changes.

- Folder: `packages/game/weather`; CMake targets: `dwe::weather` (`game` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/weather](https://indie-modding-games.github.io/dw-engine/modules/game/weather).

Build and test only this package:

```sh
scripts/dev.sh build --filter weather
scripts/dev.sh test --filter weather       # its tests (ctest label weather)
scripts/dev.sh test --filter ...weather    # and those of everything that depends on it
```
