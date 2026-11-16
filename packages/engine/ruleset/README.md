# ruleset

The ruleset format: it loads the packages' `rules/<id>/`, follows `extends` and validates the rule tables that the
game's systems read.

- Folder: `packages/engine/ruleset`; CMake targets: `dwe::ruleset` (`services` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/ruleset](https://indie-modding-games.github.io/dw-engine/modules/engine/ruleset).

Build and test only this package:

```sh
scripts/dev.sh build --filter ruleset
scripts/dev.sh test --filter ruleset       # its tests (ctest label ruleset)
scripts/dev.sh test --filter ...ruleset    # and those of everything that depends on it
```
