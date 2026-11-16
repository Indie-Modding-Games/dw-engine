# scene

The world's entities and their components (ECS).

- Folder: `packages/engine/scene`; CMake targets: `dwe::scene` (`systems` layer).
- Depends on: `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/scene](https://indie-modding-games.github.io/dw-engine/modules/engine/scene).

Build and test only this package:

```sh
scripts/dev.sh build --filter scene
scripts/dev.sh test --filter scene       # its tests (ctest label scene)
scripts/dev.sh test --filter ...scene    # and those of everything that depends on it
```
