# session

The game session: the library that joins the `packages/game` systems, which do not use each other, into a game that is
played and saved.

- Folder: `packages/session`; CMake targets: `dwe::session` (`apps` layer).
- Depends on: `assets`, `audio`, `battle`, `core`, `curling`, `evolution`, `field`, `fishing`, `gym`, `inventory`,
  `partner`, `ruleset`, `save`, `scene`, `script`, `story`, `tournament`, `weather`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/apps/session](https://indie-modding-games.github.io/dw-engine/modules/apps/session).

Build and test only this package:

```sh
scripts/dev.sh build --filter session
scripts/dev.sh test --filter session       # its tests (ctest label session)
scripts/dev.sh test --filter ...session    # and those of everything that depends on it
```
