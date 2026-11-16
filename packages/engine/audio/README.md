# audio

Sound: the output to the device, the mixer, the voices, the music and the volumes.

- Folder: `packages/engine/audio`; CMake targets: `dwe::audio` (`systems` layer).
- Depends on: `assets`, `core`, `platform`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/audio](https://indie-modding-games.github.io/dw-engine/modules/engine/audio).

Build and test only this package:

```sh
scripts/dev.sh build --filter audio
scripts/dev.sh test --filter audio       # its tests (ctest label audio)
scripts/dev.sh test --filter ...audio    # and those of everything that depends on it
```
