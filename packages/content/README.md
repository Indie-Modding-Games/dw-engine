# content

How the apps assemble packages: the content folder, the team's packages and the install from the player's disc.

- Folder: `packages/content`; CMake targets: `dwe::content` (`apps` layer).
- Depends on: `assets`, `core`, `platform`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/apps/content](https://indie-modding-games.github.io/dw-engine/modules/apps/content).

Build and test only this package:

```sh
scripts/dev.sh build --filter content
scripts/dev.sh test --filter content       # its tests (ctest label content)
scripts/dev.sh test --filter ...content    # and those of everything that depends on it
```
