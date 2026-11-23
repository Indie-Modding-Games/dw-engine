# Beta for Windows and macOS

The Windows or Mac player does the same as the Linux one: installs the launcher, picks their disc and plays. They
install nothing else by hand: no libraries, no Vulkan, no C++ runtime. All that is needed is the normal driver for
their graphics card (on Windows it brings Vulkan; on macOS Vulkan is inside the game, on top of Metal). Before
playing, the launcher asks the game whether any card is usable (`dwe-game --check-system`); if none is, it shows what
to do with a button to the manufacturer's drivers page and, on Windows, to Windows Update
([machine requirements](../modules/apps/launcher.md#computer-requirements)).

| | Windows | macOS |
|---|---|---|
| Launcher | `dwe-launcher_<version>_x64-setup.exe` (NSIS, per user, no administrator) | `dwe-launcher_<version>_universal.dmg` (Apple silicon and Intel) |
| Game package | `dwe-game-<version>-windows-x86_64.tar.xz` | `dwe-game-<version>-darwin-aarch64.tar.xz` and `-darwin-x86_64.tar.xz` |
| Launcher data | `%LOCALAPPDATA%\indie-modding-games\dwe` | `~/Library/Application Support/indie-modding-games/dwe` |
| Game saves and settings | `%APPDATA%\indie-modding-games\dwe` | The same folder as the data |
| Tested | With Wine: the game package (starts up to the title, `dwx` identifies the disc), the launcher install and its tests | Nothing yet: a Mac is missing |

The launcher picks its computer's package in `game-manifest.json` by the key `<system>-<processor>`
(`linux-x86_64`, `windows-x86_64`, `darwin-aarch64`, `darwin-x86_64`, the same names as the updater's `latest.json`).
On Windows the data goes to the local folder and not the roaming one, because the installed versions and the content
take hundreds of megabytes; the game still keeps saves and settings in its usual folder, so `dwe-game.exe` started by
hand finds them (the content it does not: it is passed with `--content` or the disc is read again).

## Windows

### The game package

```bash
scripts/dev.sh windows-sdk --accept-license   # once: the MSVC libraries and the Windows SDK (xwin)
scripts/package_windows.sh --out <folder>
```

It builds, in the development container, from Linux, the `windows-cross-package` preset (clang-cl with the static
CRT, release, `DWE_PACKAGE`) and leaves `dwe-beta-<version>-<commit>-windows-x86_64.tar.xz` with the same shape as the
Linux one: `bin/dwe-game.exe`, `bin/dwx.exe`, `bin/vulkan-1.dll`, `lib/lavapipe/`, `packs/`, `licenses/`, `LICENSE`,
`THIRD_PARTY_NOTICES.txt`, `README.md`, `VERSION`, `EXTRACTOR_VERSION` and `dwe.cmd` to start it without a launcher.
The `.pdb` files are kept apart, for crash reports.

- `packs/` holds the team's packs cooked and packaged as `.pak`. Since `dwx.exe` does not run on Linux, the script
  cooks them with the `dwx` from the `linux-clang-package` preset (it builds it if needed); cooking gives the same
  bytes on both systems within the scope of [ADR 0017](../adr/0017-cooking-and-pak-files.md#determinism).
- The CRT and the C++ standard library are inside the programs (`/MT`), so no redistributable needs to be installed.
  In `bin/` the only DLL that is shipped is the Vulkan loader; the script reads its imports
  (`llvm-readobj --coff-imports`) and refuses to package if any DLL is neither in `bin/` nor part of Windows.
- `lib/lavapipe/` carries a fixed build of lavapipe (Mesa 26.3.1, checked by SHA256) for when no card works: the
  launcher and `dwe.cmd` check the computer and switch to that driver only then. The script downloads the `7z` and
  unpacks it with `7z` (`p7zip-full` in the development image). `dzn` is not included, because its current build
  offers neither `descriptorIndexing` nor `bufferDeviceAddress`.
- The game in the package is a windowed program (`/SUBSYSTEM:WINDOWS`): it does not open a console next to it. Its
  output goes to the log the launcher gives it (`<data>\logs\game.log`). `dwx.exe` is still a console program, and the
  launcher runs it without a window.
- As on Linux, the script refuses if a program names a folder of the machine that builds it or if there is a `base`
  pack.

### Testing it with Wine

```bash
Xvfb :125 -screen 0 1280x720x24 -nolisten tcp &
scripts/wine_smoke.sh <tarball or folder> --display :125 --out <folder> \
  --disc <.cue image> --content <extracted content>
```

It runs the package in the `dwe-wine-smoke` image (`.devcontainer/wine.Dockerfile`: Ubuntu with Wine, Mesa and
lavapipe, no network): `dwx.exe identify` with the disc and `dwe-game.exe --content` in the Xvfb it is given, with
captures before and after pressing Enter. Vulkan goes through `winevulkan` to lavapipe, so no GPU is needed. Under
Wine, Wine's own `vulkan-1` is used (`WINEDLLOVERRIDES=vulkan-1=b`): Wine only registers its driver for its own
loader, and the package's `vulkan-1.dll` would not find any; on Windows the package's one reads the drivers from the
registry like any game.

Wine is not Windows: the installer, the keychain and SmartScreen are not tested this way.

### The launcher

```bash
scripts/launcher.sh bundle-windows
```

It builds, in the launcher image (`.devcontainer/launcher.Dockerfile`, with Rust's MSVC target, `cargo-xwin`, clang
and NSIS), the installer `build/launcher/x86_64-pc-windows-msvc/release/bundle/nsis/dwe-launcher_<version>_x64-setup.exe`
and, with the signing key, its `.sig` for the updater. `tauri.windows.conf.json` configures it:

- **Per user** (`installMode: currentUser`): it installs to `%LOCALAPPDATA%\dwe-launcher` and registers itself in the
  user's "Installed apps", without asking for administrator permissions.
- **WebView2 included** (`embedBootstrapper`): if the player's Windows does not have the web engine the interface
  uses (a Windows 10 that is not up to date), the installer carries its small installer and runs it; this downloads
  the engine, so that first install needs internet (the launcher needs it anyway to download the game). Windows 11
  already has it. If it cannot be installed, the installer stops without leaving anything half done.
- With Wine the silent install (`/S`) was checked: the program ends up in `%LOCALAPPDATA%\dwe-launcher` with its
  uninstaller and the user's entry. The launcher's interface cannot be tested with Wine (WebView2 does not work
  there).
- In Spanish or English, depending on Windows' language.

### What the player sees: SmartScreen

The installer and the game are not signed with a publisher certificate, so the first time Windows shows "Windows
protected your PC". The player presses **More info → Run anyway**. The antivirus may scan `dwe-game.exe` the first
time it starts; it takes a few seconds.

With a code-signing certificate (from a recognized authority, or Windows' cloud signing service) the warning changes
to show the publisher's name and, with accumulated reputation, goes away. The installer, `dwe-game.exe` and
`dwx.exe` need to be signed in the release flow; Tauri's updater does not change.

## macOS

There is no Mac on the team's machine and Apple's SDK is not downloaded, so everything about macOS is prepared but
**untested**: GitHub's macOS runners build and package it when the release flow is launched.

### The compiler

Apple's Clang and the system's libc++ do not have all the C++26 the code uses (`std::print`, `std::ranges::to`,
`std::views::zip`, `std::jthread` without `-fexperimental-library`, floating-point `std::from_chars`) and also tie
parts of the library to the system version. That is why the `macos-arm64-*` and `macos-x64-*` presets use Homebrew's
LLVM (`tooling/cmake/toolchains/macos-llvm-libcxx.cmake`) and link its libc++ and libc++abi statically, as on Linux.
Minimum: macOS 13.3 (libc++ formats floating-point numbers, `std::to_chars`, from that version). Homebrew lives in
`/opt/homebrew` on Apple silicon and in `/usr/local` on Intel Macs; the toolchain uses `DWE_LLVM_ROOT` if it is set
(the release flow sets it with `brew --prefix llvm@22`), otherwise `HOMEBREW_PREFIX`, and otherwise the first of those
two folders that has the LLVM. Without LLVM it stops with a clear message instead of vcpkg's misleading error
("unable to find a build program corresponding to Ninja") that brought down the first run on Intel.

Homebrew installs libc++ and MoltenVK only for the Mac's processor. To build the Intel package on an Apple silicon
Mac (as the release flow does), `scripts/package/macos_cross_libs.sh` builds libc++ and libc++abi for x86_64 from the
sources of the same LLVM version, with Homebrew's options. It checks that its `__config_site` is identical to that of
the headers the Homebrew clang compiles with. It also downloads MoltenVK from its official release (universal, with
the SHA-256 sum pinned) and keeps the Intel part. The toolchain links that libc++ (`DWE_LIBCXX_DIR`), supplies the
Xcode SDK (the Homebrew clang only ships configuration for the Mac's processor) and stops with a clear message if the
libc++ is for another processor.

### The game package

```bash
scripts/package_macos.sh --arch arm64 --out <folder>   # or --arch x64, on an Intel Mac or an Apple silicon one
```

It needs `brew install llvm@22 molten-vk ninja`, CMake and `VCPKG_ROOT`. `--arch x64` on Apple silicon uses the
`macos-x64-cross-package` preset (vcpkg's tools, such as `slangc`, for `arm64-osx`) and needs Rosetta
(`softwareupdate --install-rosetta --agree-to-license`): the package's Intel `dwx` cooks the team's packs. The Intel
libc++ and MoltenVK are kept in `DWE_MACOS_CROSS_DIR` (by default `build/macos-cross-x64`); they are built once per
LLVM version (a few minutes). At the end the script starts the package's `dwe-game` with `--check-system` (the Intel
one, with Rosetta). It requires it to print its header and a result other than `no-vulkan`: that proves the program
starts and reaches MoltenVK through the Vulkan loader and the package's ICD file. It does not judge whether the card
is usable, because GitHub's runners are virtual machines with a paravirtual Metal device: `ok`, `unsuitable` and
`no-device` pass. `no-vulkan` does not depend on the card (the loader does not find the driver), and an unexpected
exit or a missing header are package failures. It leaves
`dwe-beta-<version>-<commit>-darwin-aarch64.tar.xz` (or `-darwin-x86_64`) with:

| Path | What it is |
|---|---|
| `dwe-game.app/Contents/MacOS/` | `dwe-game` and `dwx`. |
| `dwe-game.app/Contents/Frameworks/` | The Vulkan loader (`libvulkan.1.dylib`) and MoltenVK, which implements Vulkan on top of Metal. |
| `dwe-game.app/Contents/Resources/packs/` | The team's packs as `.pak` (`game.pak`, `es-translation.pak`); `content::teamPacksFolder` looks for them there. |
| `dwe-game.app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json` | Where the loader looks for drivers inside an application bundle. |
| `README.md`, `VERSION`, `EXTRACTOR_VERSION`, `licenses/`, `LICENSE`, `THIRD_PARTY_NOTICES.txt` | As on Linux. |

The launcher runs `dwe-game.app/Contents/MacOS/dwe-game` when the package has the application, and `bin/dwe-game`
otherwise. The game finds `dwx` and the packs from the real path of its program (`_NSGetExecutablePath`), because SDL
gives `Contents/Resources` inside an application.

Unmeasured risks: the game asks for Vulkan 1.3 with `bufferDeviceAddress` and descriptor indexing; MoltenVK provides
it on Apple silicon with Metal 3, but Intel Macs with old graphics cards may be left out.

### The launcher

A single universal `.dmg` (`tauri build --target universal-apple-darwin`) for Apple silicon and Intel, with its
`.app.tar.gz` and `.sig` for the updater. `tauri.macos.conf.json` asks for macOS 13.3, like the game, and the ad hoc
signature (`signingIdentity: "-"`).

### What the player sees: Gatekeeper

The launcher carries an **ad hoc** signature (`codesign -s -`), with no developer identity, and is not notarized.
Apple silicon requires some signature to run a program; the ad hoc one is enough, but Gatekeeper warns that "the
developer cannot be verified" the first time something downloaded from the internet is opened. The player:

1. Drags `dwe-launcher` from the `.dmg` to Applications.
2. **Right-clicks (or Control-clicks) → Open** and confirms with **Open**. On macOS 15 and later that shortcut is no
   longer enough: open the launcher once, close the warning and go to **System Settings → Privacy & Security →
   Open Anyway**, and confirm with the password.

It is only needed once. The game does not ask for it: the launcher itself downloads it, and what a program like that
downloads does not get marked as quarantined.

With an Apple Developer account (paid, yearly) it is signed with a "Developer ID Application" certificate and
notarized (`notarytool`) in the release flow: Gatekeeper then opens the launcher without a warning. Tauri does it
with the variables `APPLE_CERTIFICATE`, `APPLE_SIGNING_IDENTITY`, `APPLE_ID`, `APPLE_PASSWORD` and `APPLE_TEAM_ID`, and
the game package would be signed with the same identity in `package_macos.sh`.

## Release and its cost

The game packages are built on every pull request (the `package` jobs of `.github/workflows/ci.yml`): Linux and
Windows on Ubuntu (Windows by cross-compilation), Apple silicon and Intel both on `macos-15`, which is Apple silicon
(the Intel one by cross-compilation, about 2.5 times faster than on `macos-15-intel`).
`.github/workflows/release.yml` runs on every merge into `main` that changes code (not with documentation-only
changes) and publishes those same packages; only if it does not find them does it build them itself, the same way. The
launcher for Linux, Windows (on Ubuntu) and macOS (the universal `.dmg`, `macos-15`) is built only when it changed
since the previous version. The manifests always name the four computers. Each computer is built on its own: if one
fails, the others finish, the run's summary says which one is missing and nothing is published until *Re-run failed
jobs* completes it. What it checks and how a test version becomes stable: [Publishing a
version](../modules/apps/launcher.md#publishing-a-release). `scripts/release_local.sh` is still the fallback for Linux
and Windows from the maintainer's PC: [Publishing from this
PC](../modules/apps/launcher.md#publishing-from-this-pc).

Pull requests do not really test on Windows: their `windows` check only builds by cross-compilation.
`.github/workflows/platform-check.yml` builds and runs the unit tests of the engine and the launcher on
`windows-latest` every Monday and by hand, and also on `macos-latest` if the `macos` option is checked. With the
`macos_x64_package` option it only builds the game package for Intel Mac the same way the release does, without
publishing anything, and leaves it as a run artifact; this is how a branch that touches that build is tested before
merging it:

```bash
gh workflow run platform-check.yml --ref <branch> -f macos_x64_package=true
```

The repository is public, so the minutes of GitHub's standard runners (Linux, Windows and macOS) are not charged.
Approximate duration (no caches → with full caches and a small change):

| Flow | Linux and Windows | macOS |
|---|---|---|
| `release.yml`, full build | ~30 → ~5 min | ~22 → ~8 min (Intel, built on Apple silicon; the first time it also builds its libc++) |
| `release.yml` with the pull request's packages | 2–4 min | — |
| `platform-check.yml` (default: Windows) | ~70 → ~35 min | — |
| `platform-check.yml` with `macos` | ~70 → ~35 min | ~60 → ~30 min |

## Pending

- Test the launcher on a real Windows (installer, keychain, update) and everything on a Mac.
- Publisher signatures (the maintainer's decision: they have a cost).
