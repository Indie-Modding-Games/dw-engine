# localization

The texts the player sees in their language: each package's catalog and its templates.

- Folder: `packages/engine/localization`; CMake targets: `dwe::localization` (`systems` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/localization](https://indie-modding-games.github.io/dw-engine/modules/engine/localization).

Build and test only this package:

```sh
scripts/dev.sh build --filter localization
scripts/dev.sh test --filter localization       # its tests (ctest label localization)
scripts/dev.sh test --filter ...localization    # and those of everything that depends on it
```
