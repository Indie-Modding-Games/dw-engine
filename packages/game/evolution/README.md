# evolution

The partner's evolution: when it changes form, into which, and what it takes along to the new one.

- Folder: `packages/game/evolution`; CMake targets: `dwe::evolution` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/evolution](https://indie-modding-games.github.io/dw-engine/modules/game/evolution).

Build and test only this package:

```sh
scripts/dev.sh build --filter evolution
scripts/dev.sh test --filter evolution       # its tests (ctest label evolution)
scripts/dev.sh test --filter ...evolution    # and those of everything that depends on it
```
