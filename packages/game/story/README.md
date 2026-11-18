# story

The story: its state and the runtime that runs the sections of the map scripts.

- Folder: `packages/game/story`; CMake targets: `dwe::story` (`game` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/story](https://indie-modding-games.github.io/dw-engine/modules/game/story).

Build and test only this package:

```sh
scripts/dev.sh build --filter story
scripts/dev.sh test --filter story       # its tests (ctest label story)
scripts/dev.sh test --filter ...story    # and those of everything that depends on it
```
