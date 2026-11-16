# game

`dwe-game`, the game: it opens the window, mounts the packages, starts the game and draws the field, combat and the menus.

- Folder: `apps/game`; CMake targets: the `game` program (`apps` layer).
- Depends on: `animation`, `assets`, `audio`, `battle`, `content`, `core`, `curling`, `debug`, `evolution`, `field`,
  `fishing`, `gym`, `input`, `inventory`, `localization`, `partner`, `platform`, `render`, `rhi`, `ruleset`, `save`,
  `scene`, `screens`, `script`, `session`, `signs`, `story`, `tournament`, `ui`, `video`, `weather`.
- Layout: `CMakeLists.txt` (the program and its dependencies), `src/` and `tests/`.
- Documentation: [modules/apps/game](https://indie-modding-games.github.io/dw-engine/modules/apps/game).

Build and test only this package:

```sh
scripts/dev.sh build --filter game
scripts/dev.sh test --filter game       # its tests (ctest label game)
```
