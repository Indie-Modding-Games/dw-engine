# launcher

The `dwe` desktop launcher: it reads the player's disc, keeps the game up to date with the published releases and opens
it. It is a desktop app in Rust (`src-tauri/`) with the web interface in `ui/`, apart from CMake.

- Folder: `apps/launcher`; `addons/` holds the example add-ons and `tools/` the fake release server for its
  tests.
- Depends on: no CMake package. Its end-to-end tests use `apps/beta-gateway`.
- Documentation: [modules/apps/launcher](https://indie-modding-games.github.io/dw-engine/modules/apps/launcher).

Build and test only the launcher (in its own image, `scripts/launcher.sh`):

```sh
scripts/dev.sh build --filter launcher
scripts/dev.sh test --filter launcher
```
