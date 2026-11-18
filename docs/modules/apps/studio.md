# studio

DWE Studio (`apps/studio`): a desktop workbench for developing the engine, separate from the game and the
launcher. It brings together a code editor, the repository tree, a terminal and buttons to build and launch
`dwe-game`, and a tab to view the original and the remake side by side. It is a development tool under testing:
it is not published and nobody needs it to build or play. Detailed usage, limits and the comparison flow: the
[Studio README](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/studio/README.md).

## Stack

Electron with `electron-vite` and the Monaco editor; `chokidar` watches the disk. It is not a CMake module: it is
installed and launched with npm from `apps/studio`:

```sh
npm install
npm run dev          # development; npm run build && npm start for the bundle
```

`scripts/build-native-input.mjs` first builds two small C programs for Linux/X11 (`src/main/x11_input.c`,
`x11_focus.c`) that give focus and send key presses to a game window.

| Folder | What it has |
|---|---|
| `src/main/` | The main process: the project (tree, reading and writing files), the processes (build, launch, stop), the terminal, the window captures and the observer |
| `src/shared/` | The texts of the interface in English and Spanish (`i18n.js`), used by the main process and the window |
| `src/preload/` | The bounded bridge between the window and the main process (context isolation, sandboxed renderer) |
| `src/renderer/` | The interface: editor, explorer, panels and the "Compare" tab |

## What it does

- **Project:** opens the repository root (or any other with `scripts/dev.sh`), edits and saves files, searches
  with `Ctrl/Cmd+P` and reflects the changes that another tool saves to disk.
- **Build and run:** "Build" is `scripts/dev.sh build`. "Run" is `scripts/studio-run.sh`: it builds
  `dwe-game` and `dwx`, updates the disc cache named by `disc_image` in `dev-assets.yaml`, mounts a content
  root with that base and the repository packs (without touching `content/base`) and launches the game with
  `--tuning --dev-assets` and the debug overlay ([development assets](../../architecture/development-assets.md)).
- **Reference:** opens the local copy of the decomp read-only (`DWE_DECOMP`); it never copies or changes it.
- **Compare:** captures the window of an emulator with the player's disc and that of `dwe-game`, side by side. Every
  20 s it saves samples (frames and logs) in `.dwe-studio/observer/`, which stays out of git, and it can send the
  windows short button sequences written in `control/pending.json`. "Capture motion" saves 2 s of
  frames from each game to compare animations.
- **Language:** the interface and its messages are in English or Spanish. `DWE_STUDIO_LANG` (`es` or `en`) chooses;
  without it Studio follows the system's language (English if it is neither).

The games it launched stay open when Studio is closed; when it comes back, it finds them by their PID and reattaches
their logs. Only for processes that it launched itself.

## Limits

- Capturing and sending buttons to another window only work on Linux with X11.
- The terminal is a shell without a PTY: it is not suitable for interactive full-screen programs.
- It does not debug by line or inspect the game state; for that there are the [remote control](../../guides/remote-control.md)
  and the [state trace](../../guides/state-trace.md).
- It has no automated tests.

`apps/studio-vscode/index.html` is the HTML mockup of the interface it started with (proof of concept).
