# ui

The game's UI: documents and styles read by asset id, their scripts and their skins.

- Folder: `packages/engine/ui`; CMake targets: `dwe::ui` (`services` layer).
- Depends on: `assets`, `core`, `input`, `platform`, `render`, `script`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/ui](https://indie-modding-games.github.io/dw-engine/modules/engine/ui).

Build and test only this package:

```sh
scripts/dev.sh build --filter ui
scripts/dev.sh test --filter ui       # its tests (ctest label ui)
scripts/dev.sh test --filter ...ui    # and those of everything that depends on it
```
