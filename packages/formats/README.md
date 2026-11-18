# formats

The readers for the original disc's formats. Only `dwx` uses it: the runtime never depends on this package.

- Folder: `packages/formats`; CMake targets: `dwe::formats` (`base` layer).
- Depends on: `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/formats](https://indie-modding-games.github.io/dw-engine/modules/formats).

Build and test only this package:

```sh
scripts/dev.sh build --filter formats
scripts/dev.sh test --filter formats       # its tests (ctest label formats)
scripts/dev.sh test --filter ...formats    # and those of everything that depends on it
```
