# input

Input by actions: the game asks about actions (`confirm`, `move_left`...), never about keys or buttons.

- Folder: `packages/engine/input`; CMake targets: `dwe::input` (`systems` layer).
- Depends on: `core`, `platform`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/input](https://indie-modding-games.github.io/dw-engine/modules/engine/input).

Build and test only this package:

```sh
scripts/dev.sh build --filter input
scripts/dev.sh test --filter input       # its tests (ctest label input)
scripts/dev.sh test --filter ...input    # and those of everything that depends on it
```
