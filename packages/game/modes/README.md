# modes

The game modes: each mode's `modes/<id>/mode.yaml` manifest and the catalog of those the packages bring.

- Folder: `packages/game/modes`; CMake targets: `dwe::modes` (`game` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/modes](https://indie-modding-games.github.io/dw-engine/modules/game/modes).

Build and test only this package:

```sh
scripts/dev.sh build --filter modes
scripts/dev.sh test --filter modes       # its tests (ctest label modes)
scripts/dev.sh test --filter ...modes    # and those of everything that depends on it
```
