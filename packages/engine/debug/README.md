# debug

The development tools: the console with the log and a Lua command line, the frame statistics and the asset viewer.

- Folder: `packages/engine/debug`; CMake targets: `dwe::debug` (`services` layer).
- Depends on: `assets`, `core`, `input`, `platform`, `rhi`, `ruleset`, `script`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/debug](https://indie-modding-games.github.io/dw-engine/modules/engine/debug).

Build and test only this package:

```sh
scripts/dev.sh build --filter debug
scripts/dev.sh test --filter debug       # its tests (ctest label debug)
scripts/dev.sh test --filter ...debug    # and those of everything that depends on it
```
