# partner

Partner care: hunger, sleep, toilet, sickness and injuries, happiness and discipline, and its life.

- Folder: `packages/game/partner`; CMake targets: `dwe::partner` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/partner](https://indie-modding-games.github.io/dw-engine/modules/game/partner).

Build and test only this package:

```sh
scripts/dev.sh build --filter partner
scripts/dev.sh test --filter partner       # its tests (ctest label partner)
scripts/dev.sh test --filter ...partner    # and those of everything that depends on it
```
