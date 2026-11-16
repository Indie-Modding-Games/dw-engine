# DWE Studio

DWE Studio is a desktop workbench separate from the game. It brings together a code editor, the engine tree, a command terminal and controls to build and run `dwe-game`.

## Running in development

It needs Node.js and npm. From this folder:

```sh
npm install
npm run dev
```

To create the app bundle:

```sh
npm run build
npm start
```

By default it opens the repository root that contains `apps/studio`. You can select another DWE root with the folder button; it must have `scripts/dev.sh`.

## Language

The interface and its messages are in English or Spanish. `DWE_STUDIO_LANG=es` or `en` chooses; without it Studio follows the system's language (English if it is neither). The texts are in `src/shared/i18n.js`: the source texts are English and the Spanish ones are listed there by them.

## Workflow

- The editor opens and saves files of the repository. `Ctrl/Cmd+P` searches for files and `Ctrl/Cmd+S` saves.
- The explorer watches for changes on disk. Changes saved from another tool, such as Codex or Claude Code, appear in "Recent changes" and update the tabs that have no local changes.
- The build button runs `scripts/dev.sh build`; "Run" builds `dwe-game` and `dwx`, updates the cache with `disc_image` and creates a run content root with that base and the project's packages. It does not modify `content/base`. Then it starts the game with `--tuning --dev-assets` and opens the debug overlay: FPS, inputs, Lua console and a status panel. `F1` hides it or opens it again. The output of the build and of the game appears in the bottom panel. The disc path is set in `dev-assets.yaml` inside the project or in the user's data folder.
- The console keeps a `bash` session from the selected root; you can change directory and the commands keep their state.
- The `DW_DECOMP` explorer opens the local decompiled tree as a read-only reference. It looks for `DWE_DECOMP` or, by default, `~/code/dw_decomp`. DWE Studio does not copy or modify that repository.
- The "Compare" tab shows two windows side by side: DuckStation with the user's disc and DWE Engine with the project's run. Studio refreshes the windows when it opens and while it starts the game; it recognizes titles such as `dwe`, `dwe-game`, `dw1` and `digimon world`. "Find windows" forces a refresh of the list. On Linux/X11, click a screen to give focus to that game; the keyboard and the controller go to the chosen window while the capture stays visible in Studio.
- If it detects both windows at startup and the loop was not stopped before, DWE Studio selects them again by their title, starts the captures and resumes the loop automatically. Every 20 seconds it saves in `.dwe-studio/observer/` three sequential frames per game (half a second between frames) and the recent logs; `runtime.json` records windows, captures and errors so that Codex can diagnose failures even if there are no samples yet. The Codex chat of this project reviews those files with its recurring monitor and can write short button sequences into `control/pending.json`, which Studio sends to the selected windows with X11. "Send sample now" forces a capture; "Stop loop" turns off the sending and removes any pending sequence. `control/result.json` records the buttons that were executed.
- When DWE Studio is closed or reloaded, the games that Studio launched stay open. Their PIDs, arguments and chosen disc are saved in the user data; when it opens again, Studio checks those processes, reattaches their logs and detects their windows to resume the capture. The stop buttons only end processes that Studio launched. If a game was started outside Studio, it attaches to its window without launching an extra copy.

The game's Lua scripts are read from the source tree and can be reloaded when edited. C++ changes need a new build; the game view in this first delivery is a native-window indicator, not yet an embedded video capture.

## Comparing the original and the remake

DuckStation is the reference for the original PS1 game; DWE is the remake. Each sample keeps the frames of both windows and a joint image `comparison.jpg`, with PS1 on the left and DWE on the right, keeping the proportions of each capture.

After a sequence in `control/pending.json`, Studio automatically captures three frames of each game and links the sample to the control's identifier. Wait for `control/result.json → observation.status: "captured"` and open `observation.sampleId` before deciding the next input. While that capture is pending, the queue does not run another sequence. If it fails or takes longer than 15 seconds, the loop stops and records the reason; check the windows before turning it on again. The periodic captures every 20 seconds continue as follow-up.

To review animations, use **Capturar movimiento (2 s)** (capture motion). It tries to get 40 images per game at 50 ms intervals, with a maximum width of 640 pixels. `sample.json → frameTimes` stores the real times: the desktop load can alter the cadence. The files `motion-duckstation.jpg` and `motion-engine.jpg` summarize twelve positions of the sequence with their time; the full 40 frames stay available for detailed inspection. The motion sample allows up to 16 MB; the normal samples, up to 8 MB.

Before blaming a difference on the remake, check the same dialogue, menu or battle phase in both windows. For an irregular movement, follow the character's position and facing along the sequence and compare the duration of its cycles. The joint image and the time summaries are evidence for the review; the observer does not yet classify failures by itself or modify the game's code.

## Limits of this delivery

The app does not include the Codex or Claude chats. Agents can modify the same file tree and DWE Studio reflects the saved changes; Claude Code only appears as available if its CLI is installed. The console keeps the shell and shows its output, though it does not yet offer a PTY terminal for interactive full-screen programs.

The DWE run turns on the controller's SDL events when the window is in the background, so you can play from DuckStation with a physical controller and both processes receive their inputs. DuckStation must have SDL input enabled and «Disable Background Input» off. The keyboard only controls the focused window. The captures are video: DWE Studio does not yet stream the game to an AI agent automatically nor apply code changes from each frame.

Some Linux desktops with PipeWire only deliver one window in Electron's listing; in that case it is not possible to capture both processes at the same time from DWE Studio and it depends on the compositor.

Line debugging and inspection of the game's state require a debugging protocol in the DWE runtime. The structure of panels and run flow takes ideas from VS Code and Godot; it does not include or copy their full applications.

The window uses context isolation, a narrow preload and the renderer sandbox. Monaco Editor is distributed under MIT; the dependencies are pinned in `package-lock.json`.

## Review by component and history

The current flow reviews one piece at a time (the menu first), using the decompilation as a read-only behavior reference and DuckStation as visible evidence. The automation of full walkthroughs was retired; the actions and captures are requested for the active piece.

Before analyzing or recapturing, check `odd/tasks/comparacion-por-componentes.md` and `.dwe-studio/observer/processed.json`. Recapture only for an implementation change, a specific doubt or a pending validation. Do not repeat sequences that are already explained.

The manifest keeps 30 recent samples. When a sample leaves that window, the samples recorded in `processed.json → processed` and those linked to an action are moved to `.dwe-studio/observer/history/<id>/`, keeping images, times, logs and metadata. The other periodic captures are deleted. These files are local and ignored by Git; do not add images or data of the original disc to the repository. An invalid review record stops the cleanup without deleting evidence. Files deleted by earlier versions are not recovered automatically.
