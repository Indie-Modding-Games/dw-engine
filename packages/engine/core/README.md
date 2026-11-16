# core

What every module uses: `Result` and errors without exceptions, the log, the message templates, math and the basic
types.

- Folder: `packages/engine/core`; CMake targets: `dwe::core` (`core` layer).
- Depends on: no other package.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/core](https://indie-modding-games.github.io/dw-engine/modules/engine/core).

Build and test only this package:

```sh
scripts/dev.sh build --filter core
scripts/dev.sh test --filter core       # its tests (ctest label core)
scripts/dev.sh test --filter ...core    # and those of everything that depends on it
```
