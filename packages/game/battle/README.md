# battle

The rules of combat as a deterministic simulation with no screen.

- Folder: `packages/game/battle`; CMake targets: `dwe::battle` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/battle](https://indie-modding-games.github.io/dw-engine/modules/game/battle).

Build and test only this package:

```sh
scripts/dev.sh build --filter battle
scripts/dev.sh test --filter battle       # its tests (ctest label battle)
scripts/dev.sh test --filter ...battle    # and those of everything that depends on it
```
