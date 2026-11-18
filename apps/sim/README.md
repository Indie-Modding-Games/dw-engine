# sim

`dwe-sim`: plays scenarios of the game's systems many times, with no graphics and with seeds, and writes a report.

- Folder: `apps/sim`; CMake targets: `dwe::sim`, the `sim_cli` program (`apps` layer).
- Depends on: `${game_modules}`, `assets`, `content`, `core`, `ruleset`, `session`.
- Layout: `CMakeLists.txt` (the program and its dependencies), `src/` and `tests/`.
- Documentation: [modules/apps/sim](https://indie-modding-games.github.io/dw-engine/modules/apps/sim).

Build and test only this package:

```sh
scripts/dev.sh build --filter sim
scripts/dev.sh test --filter sim       # its tests (ctest label sim)
```
