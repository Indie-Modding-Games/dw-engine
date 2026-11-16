# tournament

The arena tournaments: each cup's rivals, the bracket and the prizes.

- Folder: `packages/game/tournament`; CMake targets: `dwe::tournament` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/tournament](https://indie-modding-games.github.io/dw-engine/modules/game/tournament).

Build and test only this package:

```sh
scripts/dev.sh build --filter tournament
scripts/dev.sh test --filter tournament       # its tests (ctest label tournament)
scripts/dev.sh test --filter ...tournament    # and those of everything that depends on it
```
