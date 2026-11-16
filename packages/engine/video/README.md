# video

The game's movies: it reads frame by frame the video that `dwx extract` leaves and gives the image that is due.

- Folder: `packages/engine/video`; CMake targets: `dwe::video` (`systems` layer).
- Depends on: `assets`, `core`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/engine/video](https://indie-modding-games.github.io/dw-engine/modules/engine/video).

Build and test only this package:

```sh
scripts/dev.sh build --filter video
scripts/dev.sh test --filter video       # its tests (ctest label video)
scripts/dev.sh test --filter ...video    # and those of everything that depends on it
```
