# platform

The window, the events and the controllers: it turns the system's events into its own types, so the rest of the engine
does not see the platform library.

- Folder: `packages/engine/platform`; CMake targets: `dwe::platform` (`base` layer).
- Depends on: `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/platform](https://indie-modding-games.github.io/dw-engine/modules/engine/platform).

Build and test only this package:

```sh
scripts/dev.sh build --filter platform
scripts/dev.sh test --filter platform       # its tests (ctest label platform)
scripts/dev.sh test --filter ...platform    # and those of everything that depends on it
```
