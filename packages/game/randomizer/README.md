# randomizer

The seeded randomizer of a new game: the run's settings and seed, the rules that say what it may change, and the
deterministic picks the systems ask it for (evolutions, starter, technique learning, field enemies, items, drops).

- Folder: `packages/game/randomizer`; CMake targets: `dwe::randomizer` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/randomizer](https://indie-modding-games.github.io/dw-engine/modules/game/randomizer).

Build and test only this package:

```sh
scripts/dev.sh build --filter randomizer
scripts/dev.sh test --filter randomizer       # its tests (ctest label randomizer)
scripts/dev.sh test --filter ...randomizer    # and those of everything that depends on it
```
