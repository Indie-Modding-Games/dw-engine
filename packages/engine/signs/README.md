# signs

The signs painted into the map images, written again in the player's language.

- Folder: `packages/engine/signs`; CMake targets: `dwe::signs` (`systems` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/signs](https://indie-modding-games.github.io/dw-engine/modules/engine/signs).

Build and test only this package:

```sh
scripts/dev.sh build --filter signs
scripts/dev.sh test --filter signs       # its tests (ctest label signs)
scripts/dev.sh test --filter ...signs    # and those of everything that depends on it
```
