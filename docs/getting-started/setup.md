# Set up the environment

If you are looking for the full path from building to starting with your disc, see [Run the game](play.md).

Everything is built inside a Docker container ([ADR 0015](../adr/0015-container-development.md)). On your machine
you only need Docker, git and your GPU driver with Vulkan 1.3 support.

## 1. Install Docker (Ubuntu)

```bash
sudo apt update && sudo apt install -y docker.io docker-buildx
sudo usermod -aG docker $USER
```

Log out and back in so the `docker` group takes effect. Check that it works:

```bash
docker run --rm hello-world
```

> Being in the `docker` group is equivalent to having root permissions on the machine. That is normal on a
> development machine.

## 2. Build

```bash
scripts/dev.sh image       # creates the development image (once, or when the Dockerfile changes)
scripts/dev.sh configure   # the first time vcpkg builds the dependencies: it takes a while
scripts/dev.sh build
scripts/dev.sh test
```

The default preset is `linux-clang-debug`, with ASan and UBSan. For another one: `PRESET=linux-clang-release
scripts/dev.sh configure`.

Compiled objects are stored in a ccache cache (the `dwe-ccache` volume), so rebuilding something that was already
built, even in another folder or worktree of the repository, comes from the cache. The build, the tests,
clang-tidy and clang-format all use every core; `DWE_JOBS=6` limits them.

### Build and test one package

Each folder in `packages/` and `apps/` is a package, named after its folder (`packages/engine/core` is `core`,
`apps/game` is `game`; `repo` is the repository's own tests). You do not need to build everything to work on one:

```bash
scripts/dev.sh packages                          # the packages: name, folder, type and what they depend on
scripts/dev.sh build --filter core               # only core (the pkg_core target)
scripts/dev.sh test --filter core                # its tests (the ctest label core)
scripts/dev.sh test --filter 'game...'           # game and everything it uses
scripts/dev.sh test --filter '...core'           # core and everything that uses it
scripts/dev.sh test --filter 'packages/game/*'   # the packages of a folder
scripts/dev.sh affected                          # what changed since origin/main and what depends on it
scripts/dev.sh test --affected                   # only the tests for that
```

`--filter` can be repeated and combines with `--affected`. `affected` compares against the branch base and the working
tree; use `DWE_AFFECTED_BASE=<branch or commit>` for another base. Dependencies come from the `CMakeLists.txt` files
(`scripts/packages.py`). The launcher is built and tested with `scripts/launcher.sh` and the beta gateway with
`node --test`; the other packages, with their CMake `pkg_<name>` target.

### Check before a PR

```bash
scripts/dev.sh check-commit   # what each commit needs: a build with warnings as errors and the fast tests
scripts/dev.sh check-full     # more than a pull request's CI: also formatting and lint of everything, the
                              # documentation site, slow tests and clang-tidy
```

Both use the `ci` preset, save each step's log in `build/logs/` (or in `DWE_LOG_DIR`) and exit with an error
if anything fails. The slow tests are the ones that read the user's disc or the content extracted from it (the ctest
label `slow`): without `DWE_DISC` or `DWE_CONTENT` they pass instantly. Details in
[ADR 0015](../adr/0015-container-development.md).

### With your disc

The container sees nothing outside the repository. For the tools and the disc tests to read your copy, set
`DWE_DISC` to the image's path (`.bin` or `.cue`):

```bash
DWE_DISC="$HOME/games/My disc (USA).bin" scripts/dev.sh test
```

`dev.sh` mounts the image's folder **read-only** at the same path inside the container and passes `DWE_DISC` to it.
Put the path in quotes if it has spaces or parentheses. Without `DWE_DISC`, the tests that need the disc are skipped
(CI never has it). The same goes for `DWE_CONTENT` (a folder extracted with `dwx`) and `DWE_DECOMP` (a copy of the
decomp, for the tests that compare against it); the tests read them only through `tests/support`
([module page](../modules/tests/support.md)). To play with your local assets without packaging anything, see
[development assets](../architecture/development-assets.md).

## 3. Play

```bash
scripts/dev.sh run
```

The game runs on your machine, not in the container. In debug it enables the Vulkan validation layers; if they are not
installed (a build without the `validation` feature), it warns in the log (`validation layer unavailable`) and continues without them.

- Move with WASD, the arrow keys, the D-pad or the left stick of any gamepad.
- `º`/`` ` `` or `F1` opens the development console. There you can type Lua: `marker.x = 100`.
- Edit `apps/game/scripts/main.lua` with the game open: it reloads on save.
- In the overlay's Frame window, **asset viewer** opens the [asset viewer](../guides/asset-viewer.md). It mounts
  `content/base`, or the folder you pass with `scripts/dev.sh run --content <folder>`.
- `scripts/dev.sh run --content <extracted content> --model-viewer` shows, instead of the test scene, the
  player and the partner with each of their animations ([models](../architecture/models.md#model-viewer)).
- If your gamepad is not recognized, leave an SDL `gamecontrollerdb.txt` in the folder you launch the game from.
- `scripts/dev.sh run --check-content content/base` opens no window: it mounts the packs in the folder (folders and
  `.pak` files), opens each asset with the engine's readers (PNG, KTX2, glTF, YAML, Ogg Opus, WAV, SF2, MIDI,
  Y4M), lists the ones that fail and exits with an error if there are any.
- `scripts/dev.sh run --content <extracted content> --list-saves` opens no window: it writes each save slot to the log
  (empty, its header, or why it cannot be read). `--saves <folder>` uses another saves folder instead of the
  user data one ([saved games](../architecture/save.md#where-the-files-are)).

## Windows

> `.github/workflows/platform-check.yml` follows these steps, with the `windows-ci` preset, on a GitHub Windows runner (every
> Monday and by hand). The `windows` check of each pull request only cross-compiles from Linux
> ([the Windows build from Linux](#the-windows-build-from-linux)).

On Windows the container is not used: the build uses **clang-cl** and the MSVC standard library.

1. Install Visual Studio 2022 (or its Build Tools) with "Desktop development with C++", and LLVM 20 or later
   (`clang-cl` in the `PATH`).
2. Clone vcpkg at the commit of the `builtin-baseline` in `vcpkg.json`, run `bootstrap-vcpkg.bat` and set
   `VCPKG_ROOT` to point to that folder.
3. From a "Developer PowerShell for VS 2022" (it brings CMake and Ninja):

```powershell
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
ctest --preset windows-clang-debug
```

Dependencies are built with the `x64-windows-static` triplet (static libraries and static CRT, `/MT`), so
the game does not need the player to install the Visual C++ runtime. The Vulkan loader (`vulkan-1.dll`) is
copied next to the executable. To enable the validation layers in debug:
`$env:VK_LAYER_PATH = "build\windows-clang-debug\vcpkg_installed\x64-windows-static\bin"`. Windows builds
have no sanitizers.

### The Windows build from Linux

The `windows-cross` preset builds and links from the container the same thing as `windows-ci` (clang-cl, debug, static
CRT, warnings as errors), so MSVC standard library errors show up locally and not only in
CI. It uses the image's `clang-cl` and `lld-link` against the CRT, the MSVC STL and the Windows SDK that
[xwin](https://github.com/Jake-Shadle/xwin) downloads:

```bash
scripts/dev.sh windows-sdk                       # once: downloads the libraries to the dwe-xwin volume
PRESET=windows-cross scripts/dev.sh configure
PRESET=windows-cross scripts/dev.sh build
```

- **License:** Microsoft's headers and libraries come with the Visual Studio and Windows SDK license.
  `windows-sdk` shows it and asks whoever runs it to accept it; `--accept-license` treats it as accepted, only if you have already
  read it. Those files live in the `dwe-xwin` volume (or in `DWE_XWIN_DIR`) and are **never** pushed to the repository.
- Dependencies are built with the `x64-windows-static-cross` triplet: the usual `x64-windows-static`, but with
  clang-cl instead of Visual Studio's `cl`.
- The tests are not run: they are Windows executables.
- xwin brings the Visual Studio 2022 libraries (MSVC 14.44); the Windows CI uses the Visual Studio 2026 ones (14.51).
  Portability errors show up the same, but a new 14.51 warning would only show up in CI.
- The first time, vcpkg builds the dependencies for Windows (about 4 minutes); after that a cold build takes a minute
  and a half and, with a full ccache cache, seconds.

## Other commands

| Command | What it does |
|---|---|
| `scripts/dev.sh shell` | Opens a terminal inside the container |
| `scripts/dev.sh format` | Formats the C++ (clang-format), the Lua (StyLua) and the shell (shfmt), and wraps their comments to 100 columns (`--check` only checks) |
| `scripts/dev.sh lint` | Runs luacheck on the Lua and shellcheck on the shell |
| `scripts/dev.sh setup-hooks` | Installs the pre-commit and commit-msg hooks: they check the publication policy and format and review what you are about to commit |
| `scripts/dev.sh tidy` | Runs clang-tidy in parallel (needs a prior `configure`) |
| `scripts/dev.sh docs` | Builds the documentation site from `docs/` into `build/docs-site`; a broken link makes it fail |
| `scripts/dev.sh docs-dev` | Serves the site at `http://localhost:5173` (or on the `DWE_DOCS_PORT` port) and reloads it on edit |
| `scripts/dev.sh training-check --content <folder>` | Plays one session of each gym course on the extracted content and compares it with the original's limits ([checking training](../guides/check-training.md)) |
| `scripts/dev.sh packages` · `affected` | Lists the packages and what a change affects ([build and test one package](#build-and-test-one-package)) |
| `scripts/dev.sh test -LE slow` | Runs the tests without the slow ones, the ones that read the disc or the extracted content |
| `build/<preset>/apps/dwx/dwx verify <file>` | Computes the SHA-1 of an image or an executable and says which version it is |
| `build/<preset>/apps/dwx/dwx extract --list <image.cue>` | Lists the disc's files with their SHA-1 and checks the executable's file table ([Disc](../formats/disc.md)) |
| `build/<preset>/apps/dwx/dwx extract --cache <folder> <image.cue>` | Writes the folder with the content extracted from the disc into a cache (it extracts it the first time): what `--dev-assets` uses with `disc_image` ([development assets](../architecture/development-assets.md#reading-the-disc-in-the-game)) |
| `build/<preset>/apps/dwx/dwx validate --engine content/base` | Checks a pack or a folder of packs: schemas, references, model contracts and that each asset opens. Without `--engine` it checks them as mods ([details](../architecture/assets-pipeline.md#dwx-validate)) |

## Editor

The style is the same across the whole repository: 2 spaces, never tabs, 100 columns. It is set by
`.editorconfig`, `.clang-format`, `stylua.toml`, `.luacheckrc` and `.shellcheckrc`, and the editor formats on save.

- **VS Code, in the container (recommended).** Open the folder and choose "Reopen in Container". The container uses
  the development image, with pinned versions of clang-format, clangd, StyLua, shfmt, luacheck and shellcheck,
  so what formats on save is exactly what CI and the gate check. It installs the extensions on its own.
- **VS Code, outside the container.** When you open the folder, accept the recommended extensions
  (`.vscode/extensions.json`): clangd, StyLua, shell-format, ShellCheck, luacheck and EditorConfig. The clangd,
  StyLua, shell-format and ShellCheck ones download their own program; StyLua downloads the pinned version. The luacheck one
  needs `luacheck` in the `PATH`.
- **CLion, Neovim and others.** clangd reads `.clangd`, `.clang-format` and `.clang-tidy`; for Lua and shell, configure
  StyLua and shfmt with no options: StyLua reads `stylua.toml` and shfmt reads `.editorconfig`.

clangd needs the compile commands of a configured preset (`scripts/dev.sh configure`).

**Versions.** Outside the container, a clang-format or shfmt of another version may format something differently. It
does not matter: the pre-commit hook and the gate use the image's versions and fix it or flag it.

**Pre-commit hook.** `scripts/dev.sh setup-hooks` makes git use `scripts/hooks/` in this clone (and in its
worktrees). Before each commit, the hook checks the publication policy (below) and then formats the
C++, Lua and shell files that are in the commit, adds them again and runs luacheck and shellcheck on them. If a lint
error remains, the commit stops and says why. A file with unstaged changes, or deleted in the working
tree, is not touched: the hook names it and the gate checks it. Without Docker, without the image or with an image without the
formatters (the warning names the missing ones and suggests `DWE_IMAGE=dwe-dev-speed`), it warns in one line and lets
the commit through, because the gate checks the same thing. clang-tidy does not run in the hook, only in `check-full` and the gate. To skip the
hooks for one commit, use `DWE_SKIP_HOOK=1 git commit ...`: it warns in one line and lets the commit through without checking the
policy or formatting.

**Publication policy.** The hooks check, without Docker and before formatting, the
[legal policy](../legal/assets.md) and the [commit conventions](../conventions/commits.md). The rules and the
word list are in `scripts/hooks/policy.sh`:

- **Forbidden words in files** (pre-commit, only warns): the hook names with a `WARNING` each
  `file:line: word` of the added or changed lines that name the franchise, a console or a company,
  and lets the commit through, because the internal code and the content API keep the original names (decision
  of 2026-10-22). It counts the bare word, case-insensitive; inside a longer identifier
  (letters, digits or `_` around it) it does not count. Lines the commit does not touch are not looked at.
- **Exception list** (`allow` in `policy.sh`): cuts noise from the warnings. Each entry allows a text in
  some files and says why. Today: the list itself and its test; the story API's word list and its
  tests; the decomp file that is cited by its name; a hardware reference and a disc file; the
  gamepad families; and the manufacturer of the Windows tools. A new entry is justified in review.
- **Forbidden paths** (pre-commit, stops the commit): disc images and game files (`*.iso`, `*.bin`,
  `*.cue`, `*.img`, `*.chd`, textures, models, sound and video from the disc, the executable), `content/base/` and
  `content/cooked/` even if added with `-f`, emulator saved games and memory cards (`*.sav`,
  `*.mcd`…), traces (`*.jsonl`), `_work/` folders and everything under `.claude/`.
- **Author** (pre-commit, stops the commit): `git var GIT_AUTHOR_IDENT` has to be `Rivarux
  <2211799+Rivarux@users.noreply.github.com>`.
- **Message** (commit-msg, stops the commit; it is public text): it has no `Co-Authored-By`, no "Generated with" line, no mention of AI assistants, and does not
  use the forbidden words. The template's comment lines do not count.

`scripts/style/tests/test_policy_hooks.sh` tests each rule in a temporary repository.

With `git commit <paths>`, git commits those paths from a temporary index: the commit comes out formatted, but your index
keeps the unformatted version. It is better to `git add <paths>` and then `git commit` without paths; if you already did it,
add those paths again with `git add <paths>`.
