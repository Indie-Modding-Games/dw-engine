# versus

The online versus protocol: identity, series, line-ups, results and walkouts between two machines.

- Folder: `packages/game/versus`; CMake targets: `dwe::versus` (`game` layer).
- Depends on: `core`, `net`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/versus](https://indie-modding-games.github.io/dw-engine/modules/game/versus).

Build and test only this package:

```sh
scripts/dev.sh build --filter versus
scripts/dev.sh test --filter versus       # its tests (ctest label versus)
scripts/dev.sh test --filter ...versus    # and those of everything that depends on it
```
