# assets

Content by package: the stable asset ids, each package's `pack.yaml` and the virtual file system that mounts them in
layers.

- Folder: `packages/engine/assets`; CMake targets: `dwe::assets` (`systems` layer).
- Depends on: `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/assets](https://indie-modding-games.github.io/dw-engine/modules/engine/assets).

Build and test only this package:

```sh
scripts/dev.sh build --filter assets
scripts/dev.sh test --filter assets       # its tests (ctest label assets)
scripts/dev.sh test --filter ...assets    # and those of everything that depends on it
```
