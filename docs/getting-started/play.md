# Run the game

The usual flow is **build → extract your disc once → start with that content folder**. The native game runs on your machine; Docker provides the build environment. You do not need the private instructions repository or the decomp to run the engine.

This page is for people who build the game. To play the beta without building anything, using the launcher, see the [beta for Linux](../guides/beta-linux.md) or the [beta for Windows and macOS](../guides/beta-windows-macos.md).

The project is still in development: the whole story is playable, but there are gaps and bugs left to fix ([port coverage](../reference/port-coverage.md)).

## 1. Set up and build

On Linux you need Git, a working Docker, and a graphics driver compatible with Vulkan 1.3. From the repository root:

```bash
scripts/dev.sh image
scripts/dev.sh configure
scripts/dev.sh build
```

The first configuration builds the dependencies and can take a while. The details of the environment and the preset are in [Set up the environment](setup.md).

## 2. Prepare your disc's content

If you already have a folder extracted with `base/pack.yaml`, go to the next section and reuse it. You do not need to extract it on every launch.

For a new extraction, use your own image of a recognized version. It is better to keep the complete CUE/BIN pair: an ISO of data sectors can lose information the movies need. Keep the disc outside the repository.

Open the environment with the disc folder mounted read-only. Replace the path with a real absolute path:

```bash
DWE_DISC="/absolute/path/to/my-disc.cue" scripts/dev.sh shell
```

Inside that container terminal, run:

```bash
build/linux-clang-debug/apps/dwx/dwx extract --out build/local-content/base "$DWE_DISC"
exit
```

These examples use the default `linux-clang-debug` preset. If you built another one, replace its name in the binary path. The output will be a content folder with this structure:

```text
build/local-content/
└── base/
    ├── pack.yaml
    ├── data/
    ├── digimon/
    ├── map/
    └── ...
```

Pick a new output for your first extraction; do not use `--force` on a folder that has your own files. See [dwx extract](../formats/extract.md) for the output rules and the coverage report. Do not add the extracted content to Git.

## 3. Start the game

Back in your machine's normal terminal, from the repository root:

```bash
scripts/dev.sh run --content build/local-content
```

`--content` points to the folder that **contains** the `base` pack, not to `model.glb` or to an individual map. The project's `game` pack provides its rules and menus; its dependencies must be resolvable in the mounted content.

If you already have content somewhere else:

```bash
scripts/dev.sh run --content /path/to/my-content
```

Useful options:

```bash
# Spanish and the classic look with the original font
scripts/dev.sh run --content build/local-content --lang es --ui-skin original --text-font original

# English
scripts/dev.sh run --content build/local-content --lang en

# Window size
scripts/dev.sh run --content build/local-content --size 1280x720
```

### Play the latest version of main with one command

Once you have the content ready in `build/studio-content` (DWE Studio leaves it there with
`scripts/studio-run.sh`), `scripts/dwe-play` does everything in one go:

```bash
scripts/dwe-play
```

1. It checks that the checkout is on `main` and has no changes; if not, it refuses and says so.
2. If a `dwe-game` from that checkout is already open, it says so and exits without launching another one.
3. `git pull --ff-only`.
4. It builds `dwe-game` in Docker with `scripts/dev.sh build --target game` on the usual directory
   (`build/linux-clang-debug`, or the one from `PRESET`).
5. It copies the repository's `game` pack to `build/studio-content/packs/game` (`rsync --delete-after`). The game reads
   the packs from that copy; an old copy made the game fail to start ("The game could not start").
6. It starts `dwe-game --content build/studio-content --lang es --ui-skin original --text-font original` with the output
   in `.dwe-studio/user-game-run.log`.

Options: `--no-pull` (does not update), `--no-build` (uses the binary that is already there) and `--` to pass more options to the
game, for example `scripts/dwe-play --no-pull -- --size 1920x1080`. The variables `DWE_ROOT` (another checkout),
`PRESET`, `DWE_PLAY_BRANCH` and `DWE_PLAY_CONTENT` change the checkout, the preset, the required branch and the content
folder.

In the menu, start a **New game** in an empty slot. Existing saves are managed from the interface; you do not need to delete files or reuse an occupied slot to start.

## Basic controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | WASD or arrow keys | D-pad or left stick |
| Confirm / use | Space, Enter or `F` | South button |
| Cancel or go back | Ctrl or Backspace | East button |
| Walk (hold) | Shift | Upper right button (R1) |
| Open or close the menu (in battle: the bag) | Tab | North button (triangle) |
| Charge the finisher (in battle, during the aura) | Confirm (Space, Enter or `F`) | South button |
| Fire the finisher (in battle, with the gauge full) | Q | West button (square) |
| Secondary action (hold: camera down when fishing, 10 packs in the shop) | E | North button (hold; pressing it opens the menu) |
| Pause | P | Start |
| Options during the game | Esc or `F10` (they open the menu straight on Options; another press closes it) | Select (Back) |
| Island map | M | Left shoulder (L1) |
| Save a bug report | F12 | |

Button names depend on the gamepad and controls may be context-dependent. The on-screen help shows the ones for the device in use. These are the engine's mappings, not the keys configured in DuckStation.

## Report a bug

If you see something odd while playing, press **F12**: the game saves a report with the game state, a screenshot and what
was happening, and shows "Bug report saved". Reports go to the `reports` folder of your user data
(`~/.local/share/indie-modding-games/dwe/reports/` on Linux). What each one contains and how to reproduce it is in
[reporting a bug](../guides/vertical-slice.md#reporting-a-bug-f12).

## Check a piece without starting a game

To open the isolated UI scene or the player profile:

```bash
scripts/dev.sh run --content build/local-content --ui-demo --lang es --ui-skin original --text-font original
scripts/dev.sh run --content build/local-content --ui-demo-player --lang es --ui-skin original --text-font original
```

The demo uses sample values and does not represent a loaded game. To inspect models and their animations:

```bash
scripts/dev.sh run --content build/local-content --model-viewer
```

More options in [UI](../architecture/ui.md#demo) and [models](../architecture/models.md#model-viewer). To understand what each file contains, see [Extensions and files](../formats/extensions.md).

## If it does not start

| Symptom | What to check |
|---|---|
| It cannot find `base` or assets are missing | That `--content` is the parent folder of `base/`, that `base/pack.yaml` exists, and that the extraction finished correctly. |
| The extractor does not recognize the version | The report and the image's version; renaming the file does not change its contents. |
| The extracted movies fail | That the image keeps the original audio and video sectors; see [STR](../formats/video.md). |
| It cannot find the built executable | That `scripts/dev.sh build` finished and that you use the same preset to build and to run. |
| Error creating the Vulkan device | Vulkan 1.3 support and the GPU driver. |
| The requested language is not available | That the mounted content includes its tables; start with `--lang en` or `--lang es`. |

Errors are written to the terminal. To save them to a local file:

```bash
scripts/dev.sh run --content build/local-content > build/game-run.log 2>&1
```

Do not delete saves or content to fix a startup error. The optional [development assets](../architecture/development-assets.md) mode offers another flow for working with local discs and folders, but it is not needed for the startup described here.
