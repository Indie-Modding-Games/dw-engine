# dwx

The asset tool: it reads the player's disc image, extracts the `base` package, checks it, cooks it and packs it.

- Folder: `apps/dwx`; CMake targets: `dwe::dwx`, the `dwx_cli` program (`apps` layer).
- Depends on: `assets`, `core`, `formats`.
- Layout: `CMakeLists.txt` (the program and its dependencies), `src/` and `tests/`.
- Documentation: [modules/apps/dwx](https://indie-modding-games.github.io/dw-engine/modules/apps/dwx).

Build and test only this package:

```sh
scripts/dev.sh build --filter dwx
scripts/dev.sh test --filter dwx       # its tests (ctest label dwx)
```
