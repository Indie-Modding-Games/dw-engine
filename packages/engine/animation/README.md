# animation

Plays the animations of the models `assets` loads: it samples the clips and blends the poses.

- Folder: `packages/engine/animation`; CMake targets: `dwe::animation` (`systems` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/animation](https://indie-modding-games.github.io/dw-engine/modules/engine/animation).

Build and test only this package:

```sh
scripts/dev.sh build --filter animation
scripts/dev.sh test --filter animation       # its tests (ctest label animation)
scripts/dev.sh test --filter ...animation    # and those of everything that depends on it
```
