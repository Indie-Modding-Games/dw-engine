# screens

The view models of the game's screens: what combat, the field HUD, the bag and the menus show.

- Folder: `packages/screens`; CMake targets: `dwe::screens` (`apps` layer).
- Depends on: `assets`, `battle`, `core`, `inventory`, `localization`, `partner`, `ruleset`, `session`, `ui`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/apps/screens](https://indie-modding-games.github.io/dw-engine/modules/apps/screens).

Build and test only this package:

```sh
scripts/dev.sh build --filter screens
scripts/dev.sh test --filter screens       # its tests (ctest label screens)
scripts/dev.sh test --filter ...screens    # and those of everything that depends on it
```
