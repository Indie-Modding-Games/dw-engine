# rhi

The only module that talks to the graphics API: devices, buffers, textures, pipelines and the bindless table. Its
`shaders/` hold the shader modules that all the others import (`import dwe.bindless;`).

- Folder: `packages/engine/rhi`; CMake targets: `dwe::rhi` (`base` layer).
- Depends on: `core`, `platform`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/rhi](https://indie-modding-games.github.io/dw-engine/modules/engine/rhi).

Build and test only this package:

```sh
scripts/dev.sh build --filter rhi
scripts/dev.sh test --filter rhi       # its tests (ctest label rhi)
scripts/dev.sh test --filter ...rhi    # and those of everything that depends on it
```
