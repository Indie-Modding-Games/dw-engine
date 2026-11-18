# save

The saved games: the versioned YAML file, each system's sections and their migrations.

- Folder: `packages/game/save`; CMake targets: `dwe::save` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/save](https://indie-modding-games.github.io/dw-engine/modules/game/save).

Build and test only this package:

```sh
scripts/dev.sh build --filter save
scripts/dev.sh test --filter save       # its tests (ctest label save)
scripts/dev.sh test --filter ...save    # and those of everything that depends on it
```
