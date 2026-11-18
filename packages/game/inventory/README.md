# inventory

The inventory: the player's bag, their money, item use and trading.

- Folder: `packages/game/inventory`; CMake targets: `dwe::inventory` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/inventory](https://indie-modding-games.github.io/dw-engine/modules/game/inventory).

Build and test only this package:

```sh
scripts/dev.sh build --filter inventory
scripts/dev.sh test --filter inventory       # its tests (ctest label inventory)
scripts/dev.sh test --filter ...inventory    # and those of everything that depends on it
```
