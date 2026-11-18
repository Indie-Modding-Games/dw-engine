# Beta for Linux

The package a player receives on Linux: a tarball that is unpacked and run, with no Docker or compiler. Windows and macOS
have their own [guide](beta-windows-macos.md).

## Building it

```bash
scripts/package_linux.sh --out <folder>
```

It builds the `linux-clang-package` preset in the container (`scripts/dev.sh`) with `DWE_BUILD_COMMIT`, so that the
player's F12 reports name the commit. That preset is the release one with `DWE_PACKAGE` and the
`x64-linux-libcxx-package` triplet: the programs carry no folder of the machine that builds them (they look for
`packs/` next to the executable, and the source file names of the project and of the libraries are relative with
`-ffile-prefix-map`), and the script refuses to package if `strings` finds the repository root or `/home/` in a
program. It leaves in `<folder>` the tarball `dwe-beta-<version>-<commit>-linux-x86_64.tar.xz` and, separately, the
debug symbols of the two programs. `--no-build` packages the build as it is. The script refuses to package if it
finds a `base` pack: **nothing read from a disc is ever distributed**.

## What it contains

| Path | What it is |
|---|---|
| `dwe` | The launcher: runs `bin/dwe-game` from where it was unpacked. First it asks `bin/dwe-game --check-system`; if no graphics card is usable, it warns and starts with the software driver in `lib/lavapipe`. |
| `bin/dwe-game`, `bin/dwx` | The game and the tool that reads the disc, installed with `cmake --install` (without the build tree's library path) and without debug information. |
| `packs/` | The team's packs (`content/packs`) cooked and packaged: `game.pak` and `es-translation.pak` (`scripts/package/cook_team_packs.sh`, with the package's `dwx`). `content::teamPacksFolder` looks for them next to the executable's folder before the source tree; the packaged game only mounts the `.pak` files. |
| `lib/libvulkan.so.1` | The Vulkan loader (vcpkg's `vulkan-loader` port). `bin/dwe-game` looks for it first in `../lib` (its library path is `$ORIGIN/../lib`), so the player does not need the system's; the loader does use the system's drivers. |
| `lib/lavapipe/` | Mesa's software Vulkan driver (lavapipe) from the development image, with its `lvp_icd.json` manifest and the libraries it asks for that a desktop may not have (LLVM, ICU, libxml2, xcb extensions...): about 183 MB, 37 MB compressed. `scripts/package/software_vulkan_linux.sh` copies it. It is only used when no card is usable (the launcher and `dwe` point `VK_DRIVER_FILES` and `LD_LIBRARY_PATH` to this folder). |
| `licenses/` | The licenses of the libraries (one per vcpkg port), of the Noto Sans font, of libc++ and, in `licenses/lavapipe/`, those of each package of the software driver. |
| `LICENSE`, `THIRD_PARTY_NOTICES.txt` | The project's MIT license and, in a single file, the credits (FreeType, and where the code of the MPL-2.0 libraries is), the Apache-2.0 NOTICEs and all the texts in `licenses/`. `scripts/package/third_party_notices.py` writes it, and fails if a new component with an Apache-2.0 license does not say where its NOTICE is or that it has none; `scripts/package_release.sh` does not sign a package without these two files. |
| `README.md`, `VERSION`, `EXTRACTOR_VERSION` | What the player reads (`scripts/package/README.md`), the commit and the `ExtractorVersion` of its `dwx` (the launcher reads the disc again when it changes). |

Requirements for the player's machine: glibc 2.38 or newer (also for the software driver) and, to run at normal
speed, a driver with Vulkan 1.3; without it the game starts anyway with the software driver. Nothing else needs to be
installed: the Vulkan loader is in `lib/`, libc++ is statically linked and SDL opens X11 or Wayland and the system
audio at run time. `scripts/package_linux.sh` checks that every library `dwe-game` and `dwx` ask for is in `lib/` or
is part of the C library.

## First start

Without `--content`, the game mounts `./content` if it has a base pack (`base.pak`, or the `base/` folder that
`dwx extract` leaves by default) and, if not, the `content/` folder in the user's data
(`~/.local/share/indie-modding-games/dwe/`, or under `$XDG_DATA_HOME`).
When that folder has no base pack, before mounting anything the game opens the first-start screen
(`apps/game/src/first_run_screen.cpp`), in English or Spanish depending on the saved language or the system's:

1. The player types, pastes, browses (system dialog) or drops onto the window their `.cue`, `.bin` or `.iso` image.
2. `content::DiscInstall` checks that the image and the files the CUE sheet names exist, and runs
   `dwx identify`: `supported us` continues; `other-version` (another region) and `unknown` (another game) are
   explained to the player, as is an image that cannot be read.
3. It runs `dwx extract --progress --out <data>/content.partial/base` and shows the step and the percentage from the
   `progress <percentage> <step>` lines. With the North American image it takes under a minute.
4. It runs `dwx cook-pak <data>/content.partial/base -o <data>/content.partial/base.pak --progress`: it cooks the
   base pack (textures to KTX2, sounds to Opus, movies to AV1) into a single file and checks that the engine opens it
   whole. This is the long part (several minutes); the bar gives it 80 % and extraction gets the first 20 %.
5. It deletes the `base/` folder from `content.partial`, moves `base.pak` and `ui_original/` to `<data>/content/` (a
   loose `base/` from a previous install is removed after `base.pak` is in place), writes
   `<data>/content/install.yaml` (the `ExtractorVersion` of the `dwx` that read it and the absolute path of the
   image) and the game continues as usual, up to the title screen.

`--content <folder>` accepts both forms of the base pack: `base.pak` or a `base/` folder (a developer's, from
`dwx extract`). If both are there, the folder wins. The game without a launcher does not convert an old loose
install: it keeps playing it that way; the launcher converts it when it opens.

If the installed content has a base pack but its `install.yaml` is missing or is from another `ExtractorVersion` (the
game was updated and reads the disc differently), the same screen opens with the title "The game was updated",
explains that the disc has to be read again and has the image from last time filled in: Enter is enough. Only the
packs in `content/` are replaced; the saved games (`saves/`) and the settings (`settings.yaml`) are outside and are
not touched. Quitting closes the game without mounting the old content.

A disc from another region (EU or JP, which `dwx` recognizes by the executable's fingerprint but whose tables it does
not know) is rejected in `dwx identify` with `other-version <version>` and the screen says which one it is ("the
European version of the game") and that for now only the North American one is read.

Closing the window or pressing Stop kills `dwx` and deletes the partial folder. The screen uses Dear ImGui because
the game's UI needs the content that does not exist yet; its texts are in `localization/<language>/first_run` in the
game pack. `dwx` runs as a separate process (`platform::ChildProcess`): the game still does not read discs (rule 2 of
[dependencies](../architecture/dependency-rules.md)).

`--content`, `--dev-assets`, `--autotest` and `--control` never open this screen.

## With the launcher (closed beta)

The tester installs the launcher and opens it: it looks for the latest version, downloads the game and asks for the
disc as usual. There is no key to type or account to create: the **beta key** is inside the launcher
([how](../modules/apps/launcher.md#the-beta-key)).

If the maintainer changed the key and the launcher is very old, it says "The beta server no longer accepts this
launcher. Update the launcher or reinstall it from the latest release.": installing the latest version of the
launcher is enough.

## Beta defaults

The `original` ruleset with its `active_stage` of the story lock, the `original` look and the original font (the ones
the packs give), and the system language when it is English or Spanish. The saved games, settings and F12 reports go
to the user's data, as in development.

## Pending

- AppImage of the game without a launcher (the launcher is already distributed as an AppImage).
