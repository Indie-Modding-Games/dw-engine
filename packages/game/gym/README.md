# gym

The town gym: the choreography of each training course and what it gives.

- Folder: `packages/game/gym`; CMake targets: `dwe::gym` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/gym](https://indie-modding-games.github.io/dw-engine/modules/game/gym).

Build and test only this package:

```sh
scripts/dev.sh build --filter gym
scripts/dev.sh test --filter gym       # its tests (ctest label gym)
scripts/dev.sh test --filter ...gym    # and those of everything that depends on it
```
