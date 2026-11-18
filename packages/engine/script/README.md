# script

The Lua virtual machine and the only door from mods into the engine.

- Folder: `packages/engine/script`; CMake targets: `dwe::script` (`services` layer).
- Depends on: `assets`, `core`, `input`, `localization`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/script](https://indie-modding-games.github.io/dw-engine/modules/engine/script).

Build and test only this package:

```sh
scripts/dev.sh build --filter script
scripts/dev.sh test --filter script       # its tests (ctest label script)
scripts/dev.sh test --filter ...script    # and those of everything that depends on it
```
