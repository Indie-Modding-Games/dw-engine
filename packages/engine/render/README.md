# render

The game's rendering, on top of `rhi`: maps, models, the UI and the scene's grading.

- Folder: `packages/engine/render`; CMake targets: `dwe::render` (`systems` layer).
- Depends on: `assets`, `core`, `rhi`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/render](https://indie-modding-games.github.io/dw-engine/modules/engine/render).

Build and test only this package:

```sh
scripts/dev.sh build --filter render
scripts/dev.sh test --filter render       # its tests (ctest label render)
scripts/dev.sh test --filter ...render    # and those of everything that depends on it
```
