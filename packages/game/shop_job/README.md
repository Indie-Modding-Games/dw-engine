# shop_job

The canyon shop job, improved version (option `shop_job.mode`).

- Folder: `packages/game/shop_job`; CMake targets: `dwe::shop_job` (`game` layer).
- Depends on: `assets`, `core`, `ruleset`.
- Layout: `CMakeLists.txt` (the package and its dependencies, with `dwe_add_module`), `include/` (the public part, as
  `<dwe/...>`), `src/` and `tests/`.
- Documentation: [modules/game/shop_job](https://indie-modding-games.github.io/dw-engine/modules/game/shop_job).

Build and test only this package:

```sh
scripts/dev.sh build --filter shop_job
scripts/dev.sh test --filter shop_job       # its tests (ctest label shop_job)
scripts/dev.sh test --filter ...shop_job    # and those of everything that depends on it
```
