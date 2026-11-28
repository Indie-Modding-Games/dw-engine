# launcher

The desktop launcher of `dwe` (`apps/launcher`): it reads the player's disc, keeps the game up to date with the
releases published in the repository and starts it. It is a separate program from the engine, built with
[Tauri 2](https://v2.tauri.app/): the backend in Rust (`src-tauri/`) and the interface in HTML, CSS and JavaScript without a
framework (`ui/`). It is tested on Linux; for Windows there is an installer (built from Linux) and a game package
tested with Wine, and macOS is prepared but untested ([beta for Windows and macOS](../../guides/beta-windows-macos.md)).

## What the player sees

The main screen is dark and minimalist: the game's art full screen, blurred and darkened toward
the left (an image from the player's own extracted content, local only; before extracting, the launcher's abstract
art), nearly black surfaces and a single accent color, teal, reserved for "Play", the active
element and the focus ring.

- **Sidebar** (narrow, on the left): the logo; "Home", "Games", "Images", "Add-ons" (opens Settings →
  Add-ons) and "Settings"; below a line, the icons that add-ons add; at the very bottom, "About"
  (versions and keyboard). Each icon shows its name on mouse hover or when reached with the keyboard.
- **At the top, the status pills:** the installed version with "Closed beta" (while the versions are 0.x or
  prereleases), "Playing" while the game runs, the download or disc read in progress with its bar, or
  "New version: X" (opens the news).
- **In the center left:** the big title, a short description and two buttons. **The accent button**
  changes with the state: "Install" → "Downloading…"/"Extracting…" (with "Stop" next to it) → "Update (n MB)"
  when there is a new version (with "Play without updating" below) → "Play". **Configure** (ghost) opens a menu:
  Settings, read the disc again, go back to the previous version (asks first) and open the logs.
- **First run:** the same spot says "Welcome", "Install" picks the disc and below it explains which formats are
  read and that everything is local (the disc is processed on the player's computer and nothing is uploaded). The
  disc can be dropped anywhere on the window.
- **Bottom left, "Add-ons":** the list of installed add-ons with their state, "Open" for those that
  have a page and "Manage add-ons".
- **Bottom right:** "News", a collapsible panel with the notes of the latest version ("Read all" shows them
  in full) and the add-on cards, and up to three links with generic icons (repository, community and
  website). The links are set at build time with `DWE_LAUNCHER_LINKS`, a JSON object with one `https` URL per type
  (`{"repo": "https://…", "community": "https://…", "web": "https://…"}`; in the release flow, the repository
  variable `DWE_LAUNCHER_LINKS`). A missing type is not shown and, without the variable, there are none: nothing is
  linked by default.
- **Images:** export the images from the player's disc, load improved versions in bulk (folder or `.zip`)
  or one by one in the list of maps and images, and turn them on or off (see
  [Custom images](#custom-images)).
- **Games:** four cards: the base game, active, and three more in gray with "Coming soon" (card expansion,
  second part and third part).
- An add-on's page takes the center of the screen when opened from its icon or from "Add-ons".
- **Settings**, in tabs (vertical list on the left; ↑/↓ in the list, Ctrl+Tab and Ctrl+Shift+Tab or
  Ctrl+PageDown/PageUp from anywhere in the dialog):
  - **General:** language, data folder and computer requirements. The language box lists English, Español,
    Português (Brasil), Français, Deutsch, Русский, 简体中文, 한국어 and 日本語. A new install (no language
    saved) takes the system's language when it is one of them, else English, and saves it; a saved choice is
    never changed. The texts are one module per language in `ui/js/i18n/` (`en.js` is the reference: the
    others have the same keys and `{placeholders}`); a key a language lacks shows in English, and
    `node apps/launcher/tools/i18n_check.mjs [code]` (run by CI) fails on missing keys, extra keys or changed
    placeholders. The codes are kept in two lists that must match: `LANGUAGES` in `ui/js/i18n.js` (with the
    native names) and in `src-tauri/src/settings.rs`.
  - **Updates:** the channel, check on open, check now, automatic updates, the installed
    versions and going back to the previous one.
  - **Disc:** the remembered disc and reading it again.
  - **Add-ons:** install, the list of installed ones (turn on, turn off, remove) and each
    add-on's settings section (see [Add-ons](#add-ons)).
  - **Advanced:** the logs folder and safe mode.

The four cards, the icons and the background are original art (shapes and gradients, `ui/js/art.js`, `ui/art/`); no
text, name or image of the interface comes from the original game. With the disc already read, behind it there is also a
map background from the player's own content (never included or uploaded): it reads it from `content/base/` if the pack is
loose or from `content/base.pak` (`src-tauri/src/pak.rs` reads one entry, decompresses it with zstd and checks its SHA-1);
the map backgrounds stay as PNG inside the `.pak`. The whole interface can be used with the keyboard (Tab and
Shift+Tab; ↑/↓ in the sidebar and in the "Configure" menu, which also opens with ↓; Esc closes menus, panels
and dialogs), has a visible focus ring, AA contrast and respects "reduce motion". It adapts from 960×600 to
2560×1440: the text grows with the window width.

## The disc

The launcher does the same as the game's first-run screen ([beta for Linux](../../guides/beta-linux.md#first-start)),
with the installed game's `dwx`:

1. It classifies what the player picked (`src-tauri/src/disc.rs`). `dwx` reads `.cue` (with its `.bin` files), `.bin` and
   `.iso`. A `.zip` or `.7z` is first unpacked into `<data>/tmp/disc` (with a size limit and no paths that
   leave the folder) and the `.cue`, `.iso` or `.bin` it holds is used. ECM, PBP, RAR, CHD, MDF/MDS, CCD/IMG,
   NRG, CDI, CSO and `.gz` are **not** read: the launcher says which format it is and that it must be converted first, instead of
   pretending to support it.
2. It checks the files the CUE sheet names and runs `dwx identify`: `supported` goes on; another region or another
   game is explained to the player.
3. It runs `dwx extract --progress --out <data>/content.partial/base` and shows the percentage and the step. If it
   fails, the notice shows `dwx`'s error with each file it names (in a scrolling box; without the `dwx: ` line,
   its last lines), and `launcher.log` keeps the exit code and the last 60 lines of `dwx`.
4. It runs `dwx cook-pak <data>/content.partial/base -o <data>/content.partial/base.pak --progress`, which cooks the
   base pack into a single `base.pak` and checks that every entry reads back and that the engine opens every asset
   ([`dwx cook-pak`](dwx.md#cooking-to-a-pak-in-one-go-dwx-cook-pak)). The bar gives 20 % to extraction and 80 % to
   this, which is the long part, with the steps "cooking", "packing" and "checking". Only if it finishes well is the
   `base/` folder of `content.partial` deleted. A game whose `dwx` has no `cook-pak` (older than `base.pak`; the launcher
   looks at the usage `dwx` prints with no arguments) skips this step and leaves the pack loose, as before.
5. It moves the packs to `<data>/content/` (each one replaces the one with the same name, and then the other form of
   the same pack is removed: the loose `base/` from an earlier install when `base.pak` arrives, renamed away in
   one step and then deleted, so the game never mounts half a pack) and writes `install.yaml` with the installed game's
   `ExtractorVersion` and the path of the chosen disc, with the same keys as the game, so the
   game mounts the result without asking. The launcher also remembers the file the player picked
   (`disc_source`, which may be the `.zip`).

A from-scratch install ends with `<data>/content/` like this: `base.pak`, `ui_original/` and `install.yaml`.

### Installs with the loose base pack

Installs from before `base.pak` have a loose `<data>/content/base/`. When opened, the launcher cooks it into
`base.pak` without reading the disc again (`convert_content`, `content::convert`), with the task "Preparing the content…"
and its percentage in the status pill and the Stop button:

1. Nothing to do if there is no installed game, if the content is out of date (the next disc read
   already leaves `base.pak`), if the game's `dwx` cannot cook a file or if less than 1.5 GB is free: the loose pack
   keeps being played and it is tried again at the next open.
2. `dwx cook-pak <data>/content/base -o <data>/content.convert/base.pak --progress`. The work goes in
   `content.convert/`, outside `content/`, because the cooked folder is a pack with the same id.
3. With the file checked, it moves it to `content/base.pak` and only then renames `content/base/` to
   `content.convert/base.retired` (in one step) and deletes `content.convert/`.

`install.yaml` does not change: the disc read is the same, only the shape of the pack changes. A failure or a Stop at
any point leaves `content/` with a whole base pack: the loose one until `base.pak` is in place, and then
`base.pak` (while both exist, the folder wins, as in the game). If it stopped right between the two steps of point 3,
the next open finishes removing the folder. The tests in `content.rs` check it with a fake `dwx`.

If there is no installed game yet, before step 2 it downloads the latest published version.

## Updates

Each release in the repository (GitHub Releases of `Indie-Modding-Games/dw-engine`, prereleases included) carries what is
below. While the beta is closed, the launcher does not talk to GitHub: it talks to the
[beta gateway](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/beta-gateway/README.md), which asks for the beta key and answers like the GitHub
API for that single repository.

| File | What it is |
|---|---|
| `dwe-game-<version>-<computer>.tar.xz` | The game package for each computer (`linux-x86_64` from `scripts/package_linux.sh`, `windows-x86_64` from `scripts/package_windows.sh`, `darwin-aarch64` and `darwin-x86_64` from `scripts/package_macos.sh`): game, `dwx`, the team's packs; no disc data. |
| `game-manifest.json` | Semantic version, commit, `extractor_version` and, per computer, the package's name, size, SHA-256 and signature. |
| `latest.json`, `*.AppImage`, `*.deb`, `*-setup.exe`, `*.app.tar.gz` and their `.sig` | The launcher itself, for the Tauri updater; plus the `.dmg` to install it by hand. `latest.json` names the AppImage with `linux-x86_64` and `linux-x86_64-appimage`, and the `.deb` with `linux-x86_64-deb`: the updater looks first for the key of its install form, so a launcher installed from the `.deb` updates with another `.deb` (with the AppImage it would fail with "invalid updater binary format"). |

The launcher asks `GET <server>/repos/<repo>/releases` and takes, according to the **update channel** (below), the
newest version whose `game-manifest.json` has a package for its computer, using the `<system>-<processor>` key of
`latest.json` (`manifest::platform_key`: macOS is called `darwin`); a version made only for other computers is
skipped. It compares it with the installed one (semver) and never installs an older one. To update:

1. It downloads the package by its URL on the gateway (`Accept: application/octet-stream`; the gateway answers with a
   302 to a signed GitHub link), to `<data>/downloads/*.part`.
2. It checks the size, the SHA-256 and the minisign signature with the launcher's public key. If anything does not
   match, it deletes it and the game stays as it was.
3. It unpacks it into `<data>/game/versions/.staging-<version>`, checks that it has `bin/dwe-game` and `bin/dwx`
   (`.exe` on Windows; on macOS inside `dwe-game.app/Contents/MacOS/`),
   renames it to `versions/<version>` and rewrites `<data>/game/state.json` (current and previous) with a single `rename`.
   Only the current and the previous version are kept; "Go back to the previous version" swaps them.
4. If the new game's `ExtractorVersion` is different (the package's `EXTRACTOR_VERSION` file or the manifest),
   it reads the remembered disc again without asking. If the disc is gone, it says so and asks to pick it again.
   Going back to the previous version does the same.

The launcher updates itself with the Tauri updater (`latest.json`, the same signing key); on Linux
it only works when it runs as an AppImage, on Windows with the NSIS installer and on macOS with the app. The updater asks the gateway for `latest.json` and the AppImage with the beta key
in the header (never in the URL); the gateway points the `latest.json` URL to itself and the launcher
refuses to download one that is on another server.

### Update channel

In Settings, "Update channel" picks which versions the game and launcher updates come from (its `latest.json` is
the one of the same version):

| Channel | What it receives |
|---|---|
| Stable | Only stable versions (the promoted ones, [Stable version](#stable-version-promoting-a-prerelease)): the newest in the list with a package for this computer or, if none is among the 20 newest, `releases/latest`. While there is no stable one, it receives the same as "Testing". |
| Testing | The newest version of any kind: every change that lands in `main`. |

A new install starts on "Stable"; a launcher that was already installed before the channel existed stays
on "Testing", so those who were already testing do not lose versions. Going from "Testing" to "Stable" does not downgrade the game: the
installed version stays until a newer stable one comes out. The beta gateway already answers `releases/latest` (404
while there is no stable one), so the channel needs nothing else from the server.

The launcher keeps the version list from the last check for the update that follows. If a download using that
list fails in a way that may come from an old list (network, "not found", `latest.json` with no update), it
reads the list again once and retries before showing the error: a file uploaded again has another address.

### Automatic updates

In Settings → Updates there are three switches, saved in `settings.json`:

| Option | Default | What it does |
|---|---|---|
| Check for updates on open (`check_updates`) | yes | Checks when the launcher opens. "Check now" checks at any time. |
| Update the launcher automatically (`auto_update_launcher`) | yes | Downloads the new launcher, checks its signature and installs it without asking. The notice "The launcher was updated to X.Y.Z" appears with "Restart now" and "When I close it": the new version opens on restart, now or the next time. |
| Update the game automatically (`auto_update_game`) | yes | Downloads, checks and installs the new game (the atomic swap above) and reads the disc again if the `ExtractorVersion` changes. |

What each check does with what it finds is decided by `src-tauri/src/auto_update.rs` (`plan`), separately for the
launcher and for the game:

- Nothing new: nothing.
- With the option off, or if this copy cannot install it by itself (a build without a signing key, a launcher
  that was not opened from its AppImage, a game that has not yet been installed from the disc): it is offered with the usual
  button ("Update the launcher", "Update (n MB)").
- With the option on, if the game is running (or there is another task): it waits for it to finish and then installs it.
  It never interrupts a game or restarts the launcher while playing.
- Otherwise: it installs it.
- A launcher already installed in this session is not downloaded again: the restart notice stays.

If an update fails (network, signature, SHA-256, space), the current version stays: the game only changes with the
final `rename` and the launcher is only replaced after checking the signature. An automatic failure opens no dialog:
a short notice appears and the "Update" button stays there to retry. The tab shows the time of the last
check and its result ("Up to date", "There is a new version", "Updated to…" or the error with "Retry"); it is saved
in `settings.json` (`last_check`).

Tested end to end on Linux (AppImage): a launcher 0.1.0 finds 0.1.1 on the fake server, downloads it,
checks the signature, replaces its AppImage and, with "Restart now", starts as 0.1.1. To repeat it
you need a disposable signing key (`tauri signer generate`), two AppImages signed with it
(`scripts/launcher.sh bundle --bundles appimage` with `DWE_LAUNCHER_PUBKEY` and `TAURI_SIGNING_PRIVATE_KEY`) and, only in
that test build, `--config '{"plugins":{"updater":{"dangerousInsecureTransportProtocol":true}}}'`, because the
Tauri updater asks for `https` and the fake server is `http`. The repository's configuration does not carry it.

On Windows the launcher's installer closes the program when it runs, so the launcher downloads and checks it
right away and runs it on restart or on close.

The server is the gateway URL the launcher was built with (`DWE_LAUNCHER_GATEWAY` at build time; in the
release flow, the repository variable `DWE_BETA_GATEWAY`). Without it, it points to
`https://beta-gateway.invalid`, which does not exist. `DWE_LAUNCHER_API` changes it for one run (tests); it is not saved
in `settings.json`. Only `https` is accepted, or `http` to this same computer (for tests). The key is never
sent to any address other than the server's: a file whose URL points elsewhere is rejected, and when redirecting a
download to another server `reqwest` strips the `Authorization` header (`tests/fake_release.rs` tests it).

### The beta key

Testers do not need a GitHub account or to type anything: the launcher carries the single shared beta key
**inside** and sends it to the gateway (how to create and change it:
[beta gateway](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/beta-gateway/README.md)). There is no
field, notice or tab to paste it. The key only opens the releases: it gives no permission on the repository.

**It is a light barrier, not a secret.** The repository and its releases are public and the key ships in the program that
is distributed: anyone with the launcher can extract it. It is there so the gateway does not answer just anyone, nothing more.

- **Where it comes from:** `release.yml` builds it in from the repository secret `DWE_BETA_KEY`, which has the
  **same value** as the gateway's `BETA_KEY` secret (24 characters or more, no spaces). The plan's check
  step fails if it is missing or shorter, and each launcher checks that its program carries it; neither
  writes it to the log. By hand: `DWE_LAUNCHER_BETA_KEY=<key> scripts/launcher.sh build` (the container
  receives it by name, never on the command line) or `scripts/release_local.sh` with that variable set.
- **Which one it sends**, the first that exists: `DWE_LAUNCHER_BETA_KEY` in the environment when run (tests, like
  `DWE_LAUNCHER_API`), the one built in and, only in a manual build without a key, the one an old launcher
  saved in the system keyring (service `dwe-launcher`, account `beta-key`). A launcher with the key inside
  deletes that keyring entry once, without notice (`old_beta_key_removed` in `settings.json`).
- **It is never shown:** it does not reach the page, it is not written to the log and, if it appeared in an error message,
  it is replaced by `[beta key]`.
- **If the gateway answers `401`** (the key changed and this launcher is older), the launcher says "The beta server no longer accepts this launcher. Update the launcher or reinstall it from the latest release." and
  does not ask for any key. To keep that from happening, when the key changes the previous one stays valid for a while
  (`BETA_KEY_PREVIOUS` on the gateway), while the launchers update themselves.
- **Changing it:** see "Changing the key" in the gateway's README. In short: new key in `DWE_BETA_KEY` and in
  `BETA_KEY`, the old one in `BETA_KEY_PREVIOUS`, publish a release and, once the testers have the new
  launcher, delete `BETA_KEY_PREVIOUS`.

### The signing key

The repository has **no** secret key; `tauri.conf.json` carries a placeholder and a build without a public
key rejects all updates (it says so in the settings). To create the real key, once and outside the
repository:

```bash
scripts/launcher.sh shell "tauri signer generate -w /path/outside/the/repo/dwe.key"
```

It asks for a password. Keep `dwe.key` and the password in a password manager: if they are lost, the launchers already
distributed will not accept more updates. Then:

- on GitHub, the secret `DWE_SIGNING_KEY` with the text of `dwe.key`, `DWE_SIGNING_KEY_PASSWORD` with the password and the
  variable `DWE_SIGNING_PUBKEY` with the text of `dwe.key.pub`;
- to build by hand, `DWE_LAUNCHER_PUBKEY="$(cat dwe.key.pub)" scripts/launcher.sh build` and, to sign,
  `TAURI_SIGNING_PRIVATE_KEY="$(cat dwe.key)" TAURI_SIGNING_PRIVATE_KEY_PASSWORD=...` (the text, not the path: the
  container only sees the repository).

The tests create their own throwaway key on each run.

## Playing

"Play" runs `bin/dwe-game --content <data>/content` from the installed version's folder (the game finds
its packs in `packs/` next to `bin/`), with the output in `<data>/logs/game.log`, and hides the launcher. On Windows
it starts it without a console window. If the player chose another data folder, it adds
`--saves <data>/saves --reports <data>/reports`; with the default one the saves stay in the game's folder. When the game
ends, the launcher comes back; if it ended with an error, it shows the last 30 lines of its log.
Two exit codes get their own message. Exit 113 (from 0.1.0-beta.1133, or a build that is not a version) means the
graphics card stopped responding: it asks to update the driver and, when they apply, suggests Vulkan 1.2 mode (off and
known by that game) and the package's software driver (not used in that run). An older game's 113 gets the generic
message, since it never returned it on purpose. Exit 0xCFFFFFFF means Windows closed the game because it hung.
Exit 114 means no Vulkan path of the card could make the device ([rhi](../engine/rhi.md)). The dialog shows a plain
sentence first and the log's end below it, with a "Copy details" button for a report; no Vulkan code is the message
itself.

**Graphics steps.** In the automatic launch mode a start that fails on the graphics moves the next one down a step
without asking (`graphics_step.rs`): the card as the game picks (1.4 or 1.3), the card on 1.3 (`--vulkan13`, only when
the card offers 1.4 and the game knows the flag, from 0.1.0-beta.1136), the card in the 1.2 compatible mode
(`--compatible`), then the package's software driver. A start fails on the graphics when it exits with 114, or with
113 or a Windows hang in its first 90 s; a device lost after that is a crash during play and keeps the step. The
dialog says which step the next start uses and where to go back. The step is saved in `settings.json`
(`graphics_step`, with the card and driver it happened on, `graphics_step_gpu`); Settings › General › Computer
requirements shows it with "Use the graphics card again", and a check that finds another card or driver (an update)
puts it back on the card by itself. The manual launch modes ignore it.

## Computer requirements

The player installs nothing: the game package carries what it needs and the launcher checks the computer before it
is needed (`src-tauri/src/system_check.rs`). It looks when it opens, after each task (install, update, going back to
the previous version), before reading the disc and before playing, and when the player presses "Check again" in
Settings › General › Computer requirements.

| What | How it is checked | What the player sees |
|---|---|---|
| Free space | That of the disk where `<data>` is (the nearest existing folder): `statvfs` on Linux and macOS, `GetDiskFreeSpaceExW` on Windows. Reading the disc needs about 3 GB (the partial copy, the unpacked archive and the game download); playing, about 200 MB. | If it is short, "Not enough disk space" on the home screen with how much is needed, where and how much there is, and the buttons "Check again" and "Pick another folder". Reading the disc and "Play" refuse with `no-space` instead of failing halfway. |
| Graphics card | `dwe-game --check-system` of the installed game (below), first with the computer's drivers. | If it works, the card, its driver and the Vulkan version it reports (`api`); the game itself picks the 1.3 or 1.4 path when it starts. If the card works only in the 1.2 compatible mode (an old driver), the game uses that mode on its own: a "Compatible graphics mode" notice that says nothing needs changing and offers the maker's driver page, and a "Compatible graphics" label at the top. If no card works, the game starts anyway in **compatibility mode** (below): a notice that closes with "Got it", a "Compatibility mode" label at the top and the maker's driver button. |
| Nothing can run the game | Neither the computer's cards nor the package's software driver (or the package does not carry it: macOS for now). | "Your graphics card cannot run the game": what to do, the button to the maker's driver page (NVIDIA, AMD or Intel, according to the card's `vendor`; all three if unknown), on Windows also "Open Windows Update", and "Check again". Pressing "Play" still tries to start: the game writes the reason to its log and the launcher shows the end. |

**`dwe-game --check-system`** (`packages/engine/rhi/include/dwe/rhi/system_check.hpp`) creates a Vulkan instance and picks a
GPU with the same version (1.3) and the same features as `Device::create` (`renderer_requirements.hpp`), with no window,
no content and without creating the device; it takes less than a second with a normal driver. It writes to standard
output `key=value` lines under the header `dwe-check-system 1`: `result` (`ok`, `no-vulkan`, `no-device`,
`unsuitable`), `detail`, `devices` and, if there is a GPU, `gpu`, `vendor` (PCI id), `kind` (`discrete`, `integrated`,
`virtual`, `software`, `other`), `api` and `driver`; it exits with 0 only if it is `ok`. When none works it names the best of
those there are, to know which maker the driver is from. The branch that limits the options of the
distribution builds has to let `--check-system` through. The launcher gives it 20 s; with no answer (a game older than the
option, which says "bad argument") it plays as before. With `--compatible` it accepts Vulkan 1.2 when the driver has the
dynamic rendering and synchronization2 extensions. Without it, a GPU that fails the 1.3 floor is checked again on that
one, as the game itself falls back to it: `result=ok` with `floor=1.2` (and the 1.3 failure in `detail`) means the
card works in the compatible mode. For a game older than that fallback the launcher probes `--compatible` itself and,
when it passes, adds the flag to Play: the card in the 1.2 mode comes before the software driver.

**Compatibility mode.** The package carries the Vulkan loader (on Linux, `lib/libvulkan.so.1`, which the game
looks for first; on Windows, `bin/vulkan-1.dll`) and Mesa's software Vulkan driver, lavapipe, in
`lib/lavapipe/` ([Linux beta guide](../../guides/beta-linux.md), [Windows and macOS
guide](../../guides/beta-windows-macos.md)). If the first check finds no GPU that works, the launcher repeats
`--check-system` with `VK_DRIVER_FILES` (and `VK_ICD_FILENAMES`) pointing to lavapipe's manifest:
`lvp_icd.json` on Linux, `lvp_icd.x86_64.json` on Windows. On Linux it also adds `LD_LIBRARY_PATH` with that
folder; on Windows, `PATH`. If it works that way, it starts the game with that same environment. It is slower (the
processor draws it), but the game starts on a virtual machine or with an old or missing driver. The package's
scripts (`dwe` on Linux, `dwe.cmd` on Windows) do the same for those who play without the launcher. A computer
whose only Vulkan is a system software one (llvmpipe) also sees the notice, without switching drivers. Inside the
game, [software mode](../../architecture/software-mode.md) takes over from there: a cheap preset, automatic quality,
its own notice and the minimum requirements measured on lavapipe.

**Graphics switches.** Settings › General › Computer requirements has two switches, off by default, that add a
game flag to every start:

| Switch | Flag | What it does |
|---|---|---|
| Vulkan 1.2 mode | `--compatible` | Forces the 1.2 compatible mode on every start, even on a card that runs 1.3. It is no longer needed for an old card: the game and the launcher pick that mode on their own when only it works. |
| Low-performance mode | `--low-performance` | Every start begins from the lightest graphics preset; the game's options can still change each one during the run. |

A game older than a flag refuses the whole command line ("bad argument"), so the launcher only passes each flag to
games that know it: both from 0.1.0-beta.1127 (a development build gets them). Going back to a previous version
keeps Play working. Older launchers may still pass `--rdna2-safe`, which the game accepts and ignores; this launcher
no longer has that switch, and a `rdna2_safe_mode` left in `settings.json` is kept but unused.

**Manual launch options.** In Settings › Advanced › Starting the game you can stop relying on that check: "Launch
mode" picks Automatic (the one above), Without checking (start with the system's driver, no probe and no driver
change) or the package's software driver (use lavapipe without probing, to try it even when detection says
otherwise; a package without lavapipe, such as the macOS one, starts with the system's driver). You can also write
extra game arguments and environment variables (`NAME=value`, one per line, up to 32 of each); the player's ones are
applied after the software driver's, so they can correct a wrongly detected `VK_DRIVER_FILES`. Lines that are not
understood are ignored, never keep you from playing, and none of this is passed to the probe: it only changes how
the game starts. It is saved in `settings.json`, which keeps the fields a newer launcher wrote, and an unknown launch
mode there reads as Automatic.

**AppImage.** The AppImage's startup puts its folders ahead of `XDG_DATA_DIRS`, where the Vulkan loader
looks for the computer's drivers; the launcher removes them from the environment of `--check-system` and of the game
(`outside_appimage`).

**.deb.** It recommends `mesa-vulkan-drivers | vulkan-icd`, which apt installs by default: on a computer with an AMD or
Intel card without Mesa's Vulkan driver, this way the game plays with the card and not in compatibility mode. The AppImage cannot
install anything and relies on compatibility mode.

**Windows.** The installer brings WebView2 and the game package `vulkan-1.dll`; card drivers cannot be installed
safely from the launcher. The package carries lavapipe in `lib/lavapipe/`, so without a card that works the game
plays in compatibility mode (slower) and the notice above is shown. Updating the card's driver (or Windows) brings
the performance back. `dzn` is not shipped: the available build does not offer `descriptorIndexing` or
`bufferDeviceAddress`, which the game requires even in that mode.

`DWE_LAUNCHER_FREE_BYTES=<bytes>` fakes the free space, to see the space screens without filling a disk; with
`VK_DRIVER_FILES=/does/not/exist.json` in the launcher's environment the first check finds no GPU (compatibility mode
if the game carries `lib/lavapipe`; if not, notice and start attempt, or a direct start with the manual "Without
checking" mode).

## Folders

| Path | What it is |
|---|---|
| `~/.config/indie-modding-games/dwe-launcher/settings.json` | The launcher's settings (data folder, language, remembered disc). Never the key or the server. On Windows `%APPDATA%\indie-modding-games\dwe-launcher\`, on macOS `~/Library/Application Support/indie-modding-games/dwe-launcher/`. |
| `<data>` | By default the game's, `~/.local/share/indie-modding-games/dwe` (`$XDG_DATA_HOME`; on macOS `~/Library/Application Support/indie-modding-games/dwe`), so the game started without the launcher sees the same. On Windows, `%LOCALAPPDATA%\indie-modding-games\dwe`: what is installed does not go to the roaming profile, and saves stay in the game's folder (`%APPDATA%\indie-modding-games\dwe`). |
| `<data>/game/versions/`, `state.json` | The installed versions of the game. |
| `<data>/content/` | What was read from the disc, as the first-run screen leaves it: `base.pak`, `ui_original/` and `install.yaml`. |
| `<data>/content.partial/`, `content.convert/` | Work area of a disc read and of the conversion of a loose base pack; they are deleted when done. |
| `<data>/logs/launcher.log`, `game.log` | The launcher's log and the output of the last game. |
| `<data>/addons/<id>/`, `addons.json` | The installed add-ons and their state (active, accepted permissions). |
| `<data>/addon-storage/<id>.json` | Each add-on's store (256 KB at most). |
| `<data>/custom-images/originals/`, `originals.stamp` | The images from the disc written by `dwx export-images`, and which game version and disc read they come from. |
| `<data>/custom-images/pack/`, `state.json` | The player's image pack (`user` layer) and whether it is on. `incoming/` is the staging folder when copying. |

`DWE_LAUNCHER_GAME_DIR=<folder>` uses an already unpacked package (for example the one from `package_linux.sh`) instead of
the installed versions: it is for testing without publishing anything.

## Building and testing

Everything runs in the `dwe-launcher-dev` image (`.devcontainer/launcher.Dockerfile`: stable Rust, Node LTS with the Tauri
CLI, WebKitGTK 4.1 and libsoup 3); the host only runs the result.

```bash
scripts/launcher.sh image    # the image
scripts/launcher.sh test     # cargo test (unit and fake server) and clippy
scripts/launcher.sh build    # build/launcher/release/dwe-launcher
scripts/launcher.sh bundle   # AppImage, .deb and their signatures (needs the private key)
scripts/launcher.sh bundle-windows   # the Windows installer (NSIS) and its signature, built from Linux
```

The macOS `.dmg` is only built on a Mac (the release flow).

With `DWE_BUILD_SLOTS=<lock folder>` each build waits for one of the machine's two build slots, shared with the other heavy
builds. The fonts are copied from the game package (`content/packs/game/ui/fonts`) at build time.

Each launcher package carries `LICENSE` (the project's MIT license) and `THIRD_PARTY_NOTICES.txt`: the licenses
of the Rust crates it is built with (those from `cargo metadata` for that system, with their `LICENSE*`,
`COPYING*` and `NOTICE*` files) and the font's. `scripts/launcher.sh` writes them to `src-tauri/notices/` (outside git)
before building, with `scripts/package/third_party_notices.py`, and Tauri copies them into the package
(`bundle.resources`: next to the program on Windows and Linux, in `Contents/Resources` on macOS). The interface uses no
npm packages. The release flow fails if a package does not carry them.

The interface is not minified: its HTML, CSS and JavaScript take about 200 KB (55 KB gzipped), Tauri puts them in the
program compressed with brotli and serves them from memory, so minifying would save a few KB of an installer
of several MB and would force adding npm and a bundler, and debugging unreadable code in the browser
tools. It will be reviewed if the interface grows a lot.

The Rust tests cover the manifest, version comparison, the SHA-256 and the signature, installation with an
atomic swap and rollback, unpacking `.zip` and `.7z`, the CUE sheet, the `install.yaml` record (with the
game's format) and an extraction with a fake `dwx`; `tests/fake_release.rs` starts a server that answers like
the gateway and tests the download without a key, with a key, with a tampered file, with another signature and redirected to another
server (which does not receive the key); `through_the_beta_gateway` does the same through the real gateway
(`apps/beta-gateway` with node) in front of `fake_release_server.py` acting as GitHub. `tests/addons_bridge.rs` tests
that an add-on reaches no privileged command and not the key. The gateway has its own tests
(`node --test apps/beta-gateway/`).

To test by hand, `apps/launcher/tools/fake_release_server.py <folder> --port 8765` serves the subfolders of `<folder>`
as releases (their files and `notes.md`); start the launcher with
`DWE_LAUNCHER_API=http://127.0.0.1:8765`, or put the gateway in front (see its README, "Testing locally").

## Publishing a release

Nobody picks versions or starts workflows for a test release: every change that lands in `main` (a merged pull
request) is built and published on its own as a **prerelease**, the "Testing" channel. A pull request brings together a batch of
features and fixes, so each test release is a batch
([Workflow](../../contributing.md#workflow)). A test release that has been tried becomes the
**stable** one by hand, without building anything again. The pieces:
`scripts/package_release.sh --platform <computer> --out <repository folder>` builds the package (or takes one with
`--tarball`), signs it and adds it to `game-manifest.json` (one per version, with all the computers);
`scripts/package/release_manifest.py launcher` writes `latest.json`. The repository is public, so the Actions
minutes (macOS ones too) cost nothing. If Actions is not available, `scripts/release_local.sh` is the
fallback for Linux and Windows ([Publishing from this PC](#publishing-from-this-pc)).

### The versions

- The game's version is `<CMakeLists.txt version>-beta.<N>`, with `N` the number of commits on `main`
  (`scripts/ci/release.py version`). Each merged pull request adds one commit (*squash* merge), so each
  version from `main` has a greater `N` than the previous one. The tag is `v<version>`, for example `v0.1.0-beta.1031`.
- The `CMakeLists.txt` version (`project(dwe VERSION 0.1.0 ...)`) only changes by hand, in a pull request, to
  start another series (`0.2.0-beta.<N>` comes after all the `0.1.0-beta.*`).
- The programs do not carry the channel inside: the stable version is the same prerelease already tried, with the same
  files, signatures and manifests. That is why a player on the "Testing" channel does not download anything again when that version
  becomes stable, and one on the "Stable" channel receives exactly what was tried.
- The launcher has its own version (`tauri.conf.json`), which is bumped by hand when the launcher changes (see below).

### Test release: every merge into `main`

`.github/workflows/release.yml` runs on every push to `main` that touches more than documentation, tests or CI
(`paths-ignore`), and also by hand
(*Actions → Release → Run workflow*). If a new merge arrives while another is being published, the new one cancels the old one:
only the newest commit goes out as a release.

**It is built once, in the pull request.** The `package (<computer>)` jobs of `ci.yml` build the real game packages
on every pull request (the same steps as the `game` job of `release.yml`,
`.github/actions/game-package`), unsigned, from the pull request's head commit. `main` only accepts pull
requests up to date with `main` and merges them with *squash*, so the new commit on `main` has **the same tree**
(`git rev-parse <commit>^{tree}`) as the pull request's head, though not the same commit. That is why the packages are
identified by the tree: each artifact is called `package-<computer>-<tree>` and carries a `source.json` with the tree,
the pull request number, its head commit and the package's SHA-256 (`scripts/ci/packages.py`). They are kept
14 days. Pull requests from a fork do not build them (they have no access to anything in the repository); their merge uses the
full path.

What `release.yml` does, in order:

1. **Plan** (seconds; up to a minute more if it waits for packages): checks that the secret `DWE_SIGNING_KEY`, the variable
   `DWE_SIGNING_PUBKEY`, `DWE_BETA_GATEWAY` with an `https` URL and the secret `DWE_BETA_KEY` with 24 characters or
   more are there; computes the version and the tag; refuses if that commit already has a published version, if the game's version
   is not greater than the newest release's, or if the **launcher version** is not valid (see below). Then it decides:
   - **Game packages.** It looks in the GitHub API for a `ci.yml` run of a pull request from the same
     repository with the four packages of `main`'s tree and its four `package` jobs finished successfully
     (`scripts/ci/packages.py find`). If a package is still being uploaded, it asks again for a minute. If
     it finds them, they are **promoted**; if not (fork, pull request merged before its packages finished,
     expired artifacts, the GitHub API fails), the **full build** is done.
   - **Launcher.** It is built only if something that makes it up changed since the newest published version
     (`apps/launcher` without its tests, `scripts/launcher.sh`, its image, the fonts, `LICENSE`, the notices script or
     `release.yml`), if that version does not have all its signed files or `latest.json` has another version, or if its
     program does not carry this run's gateway, public key, beta key and links
     (`scripts/ci/launcher_reuse.sh`; changing one of those variables or secrets rebuilds the launcher on the next
     merge). If not, the launcher files of that version are published again, with the same version and the
     signatures they already had, so installed launchers see nothing new.

   The run summary says which path it took.
2. **Full build** (only if needed): one `game` job per computer (Linux, Windows, macOS Apple silicon
   and macOS Intel, the latter built on an Apple silicon runner and tested with Rosetta) and, if the launcher changed, one
   `launcher` job per system (Linux, Windows and macOS), in parallel and independent: if one fails, the others
   finish anyway. None signs anything: the launcher is built without the signed Tauri update files
   (`DWE_LAUNCHER_UNSIGNED=1`; on macOS the `.app.tar.gz` is packaged separately, as Tauri does). Each launcher
   checks that its program carries the gateway, the public key and the beta key inside.
3. **Publish**, the only job with the signing key: it gathers the packages (those from the pull request or the freshly
   built ones) and the launcher (the new one or the previous version's). If any computer is missing, the summary marks it
   as `MISSING` and **nothing is published**; *Re-run failed jobs* repeats only what failed. If all are there: it checks
   that each package is from `main`'s tree and is the file that was built (`scripts/ci/packages.py verify`); it tests
   that the key signs and that the public key verifies that signature; it signs the game packages and the launcher
   files that have no signature; it writes `game-manifest.json` with `main`'s commit; it checks each signature, the
   sizes, the computers, the commit and that no package carries disc data; it writes the notes (the titles of the
   pull requests since the previous version, with a header in Spanish); it creates the release as a draft, uploads
   the files (several at once), writes and checks `latest.json`, and only then publishes it as a prerelease. A
   launcher never sees a half-uploaded release (the gateway does not show drafts); the draft of a canceled
   run is deleted by the next one.

With the pull request's packages it takes about 2–4 min of wall clock. The full build, with full caches,
about 10–12 min (the Mac jobs are the longest); cold (empty caches, or after changing `vcpkg.json`, the
triplets or the images), about 25 min. The game for Intel Mac, in the `package` jobs and in `game`, is built on
the Apple silicon runner (`macos-15`), about 2.5 times faster than `macos-15-intel`. To force it by hand:
*Run workflow* with **full build** checked (it also builds the launcher). Every Monday `release.yml` does the
full build of the game and the launcher without publishing, so that path does not break unseen and
the caches the `package` jobs start from are up to date.

**Which commit a package names.** `main`'s commit only exists at merge time, after building. That is why, in CI,
the package (its `VERSION` file, its folder name and the F12 failure reports) names the **tree** it was
built from, `tree-<12 digits>` (`DWE_PACKAGE_SOURCE`, read by `package_linux.sh`, `package_windows.sh` and
`package_macos.sh`; on a PC they keep using `git describe`), and `game-manifest.json` names `main`'s commit
(`package_release.sh --commit`). To go from a tree in a report to its commit:
`git log --format='%h %T' origin/main | grep ' <tree>'`. Rewriting `VERSION` when publishing would force unpacking and
repacking each package and would not change what the game carries compiled inside, so it is not done.

### Stable version: promoting a prerelease

When a test release has been tried: *Actions → Promote → Run workflow* with its tag
(`.github/workflows/promote.yml`). In a couple of minutes:

1. It checks that it is a published prerelease and newer than the current stable one.
2. It downloads its files and checks, as a launcher would, each signature, `game-manifest.json` and the URLs of
   `latest.json`.
3. It marks it as a normal release and as the repository's latest (`releases/latest`), gives it the title
   `<tag> (estable)` and notes with the changes since the previous stable one.

It builds and signs nothing: the files, signatures and manifests are the same ones that were tried. An
urgent fix follows the same path: a pull request to `main`, its test release, and promoting it.

**Caches.** Each `main` job saves to the Actions cache what the next run can reuse, and
pull requests read them. Pull requests never save: their caches would take up the repository's 10 GB and `main`
could not use them. The package ones (vcpkg, ccache and xwin for each computer) are saved by the `game` job, which with
promotion runs in the full builds and every Monday:

| Cache | Key | What it saves |
|---|---|---|
| vcpkg dependencies, per computer | `vcpkg.json`, `tooling/vcpkg-ports/`, `tooling/triplets/`, toolchains and the image (on macOS, also the pinned LLVM) | 6–8 min per computer; it is saved even if packaging fails. Before saving it, `scripts/ci/prune_vcpkg_cache.sh` deletes the packages the build no longer uses, so it does not grow with each dependency change. The Windows one is also read by the pull requests' `windows` check |
| vcpkg dependencies of the Linux package, again after eviction | those of the vcpkg row | `pr-caches (linux-clang-package)` installs them (only the configure, without building the game) and saves them on each merge when its exact key is missing, instead of waiting for the next full build; while they were missing, each pull request built them from source (6–8 min more). Before saving a new vcpkg cache the earlier copies of its prefix are deleted (`.github/actions/trim-caches`), so two copies for one computer do not coexist |
| vcpkg dependencies of the `linux` check (`vcpkg-ci-linux-x86_64`) | the same | its triplet (`x64-linux-libcxx-release`) is not the package's: `pr-caches` saves it so each pull request does not build the 44 dependencies |
| Game objects (ccache), per computer | one per run; the earlier ones are deleted before saving it, and it only saves the objects that run built or reused (those from earlier commits filled the limit and evicted the vcpkg ones) | 12–20 min of building, except the files that changed. The package's tree (`DWE_BUILD_COMMIT`) is received only by `bug_reporter.cpp`, so it does not invalidate the rest of the game. The pull requests' `package` jobs start from it |
| MSVC libraries and Windows SDK (xwin) | the image's Dockerfile | ~1 min of download |
| libc++ and Intel MoltenVK for the Intel Mac package (`macos-cross-x64`) | the LLVM version and `scripts/package/macos_cross_libs.sh` | building Intel's libc++ on every run; `.github/actions/macos-tools` saves it on `main`, the `package` jobs only read it |
| Objects of the pull requests' checks (`ccache-ci-linux`, `ccache-ci-windows`) | one per run; the `pr-caches` job saves them, only with the objects that run used | so that `linux` and `windows` build only what the pull request changed |
| Cargo downloads and cargo-xwin Windows files of the launcher, per system | `Cargo.lock` and the launcher's Dockerfile | the downloads. The compiled dependencies (`build/launcher`, ~600 MB per system) are not saved: the launcher is only built when it changed, and they took the place of the game's vcpkg caches |

When the vcpkg cache is missing (the 10 GB limit evicts it), the job downloads the dependency sources again from their
servers. vcpkg downloads them with `scripts/ci/fetch_source.sh` (`X_VCPKG_ASSET_SOURCES`, set by
`scripts/dev.sh` and by the vcpkg installs on macOS): it retries on any error and, for kernel.org, also tries
its other servers, because `mirrors.edge.kernel.org` went down twice in November 2026 and the Linux package
failed because of it. If everything fails, vcpkg still tries the port's addresses.

*Publish* signs with npm's `tauri` (pinned version, from the npm cache) instead of building the launcher's
image (2–3 min less). The Docker images
(the development one and the launcher's) are stored in GitHub's container registry
(`ghcr.io/indie-modding-games/dwe-dev` and `dwe-launcher-dev`, tagged with the hash of `.devcontainer/`):
`scripts/ci/image.sh` pulls them instead of building them, and builds and pushes them when `.devcontainer/` changes.

**The launcher version.** An installed launcher only updates to a version **greater** than its own (the one in
`apps/launcher/src-tauri/tauri.conf.json`, which goes in `latest.json`). That is why, if anything changed in `apps/launcher/ui` or
`apps/launcher/src-tauri` (not counting `tests/`) since the newest published version, the version must be bumped in
`tauri.conf.json`, `Cargo.toml` and `Cargo.lock` in the same pull request. `scripts/ci/launcher_version.sh` checks it
on every pull request (the `launcher-version` check) and again before publishing: it fails if the launcher changed and
its version is the same as the `latest.json` of that release, if it is lower, or if the three files do not match.
`scripts/release_local.sh` does not check it: it must be looked at by hand.

## Publishing from this PC

The flow's fallback. `scripts/release_local.sh` does on the PC the same as `release.yml` for Linux and Windows (macOS
needs a Mac), with the same scripts and Docker images: the game packages (`package_linux.sh`,
`package_windows.sh`) signed with `package_release.sh` into a single `game-manifest.json`; the launcher's AppImage, `.deb` and
Windows installer (`launcher.sh bundle` and `bundle-windows`) with the gateway and the public key
inside, and their signatures; and `latest.json`. It needs Docker, `gh` signed in and, once, the Windows
libraries (`scripts/dev.sh windows-sdk`, which asks to accept their license).

From a clean copy of `origin/main`:

```bash
git fetch origin && git switch --detach origin/main
scripts/release_local.sh --tag v0.1.0-beta.1 --notes-file notas.md --upload
```

- It asks **once** for the key's password, in the terminal and without showing it. The private key
  (`~/.dwe-signing/dwe-signing.key` by default, `--key` for another) and the password only reach the containers
  that sign, through the variables `TAURI_SIGNING_PRIVATE_KEY` and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`: never on a
  command line, in a file or in the log, and they are deleted when finished.
- Before building it checks that the key signs and that its `.pub` (`--pubkey`, by default `<key>.pub`) is the
  repository's `DWE_SIGNING_PUBKEY` variable: a wrong password fails in seconds, not after the build.
  The gateway is the `DWE_BETA_GATEWAY` variable (`--gateway` for another) and the main screen's links are the
  optional variable `DWE_LAUNCHER_LINKS` (`--links` for others; without it, none), as in the flow. The beta
  key comes from `DWE_LAUNCHER_BETA_KEY` in the environment (the value of `BETA_KEY`; a GitHub secret cannot be
  read): without it, it does not build.
- Everything goes to `build/release-local/<tag>/` (`--out`, always inside `build/`): what is uploaded in `dist/`, the
  log with the timings in `logs/`.
- Before uploading it checks that each file has its `.sig` and that the public key verifies it (the `.deb` too),
  reasonable sizes, that the two manifests read and name only `linux-x86_64` and
  `windows-x86_64`, that no package carries disc data (the same check as the packagers) and that the
  two launchers carry the gateway and the public key.
- With `--upload` it creates the **draft** release and prerelease `<tag>` in `Indie-Modding-Games/dw-engine` on the commit
  of `origin/main` (it refuses if the copy has changes or is not on `origin/main`), uploads all of `dist/` (game
  packages and their `.sig`, `game-manifest.json`, AppImage and its `.sig`, `.deb` and its `.sig`, Windows installer and its
  `.sig`) and lastly `latest.json`, with the files' download URLs by version and name, as the flow does (the
  gateway swaps them for its own). Unlike a file's number, that URL stays valid if the file is
  uploaded again. Nothing is published: the owner reviews the draft and publishes it by hand.
- If the tag already exists, or there is a published release with that name, it always refuses; a draft with that name
  is only deleted with `--replace-draft`. Without `--upload` it only builds and checks (`latest.preview.json` carries example
  URLs); `--upload --no-build` uploads, after checking again, what an earlier run left.

Measured on this PC with the test key (2026-11-04, fresh copy with the vcpkg and ccache caches already full):
33 min in total; images 12 min (the development one was rebuilt entirely; with cache, seconds), Linux game 3 min,
Windows game 11 min, Linux launcher 4 min, Windows launcher 3 min, signatures and checks 7 s. It takes about 10 GB
in `build/`. `--dry-run-key <folder>` is only for testing the script: it creates a throwaway key there and builds
with it without asking anything (it never uploads).

## Interface mount points

The interface is organized into four named slots (`ui/js/slots.js`): `sidebar` (the sidebar), `tabs` (the
pages that take the center: today, the add-ons'), `news` (the cards of the news panel) and `settings` (the settings sections). The launcher's pieces
register there just as an add-on will. The privileged commands (disc, downloads, extraction,
starting programs) are only in `ui/js/api.js` and in the Rust commands (`src-tauri/src/commands.rs`); what a
piece receives to paint itself (`context()`) carries neither the beta key nor the disc path.

## Add-ons

Community add-ons use those same slots. How to make one, the v1 API and its limits are in the
guide [Launcher add-ons](../../guides/launcher-addons.md); here, how the launcher hosts them:

- **Backend** (`src-tauri/src/addons/`): the manifest (`manifest.rs`), the safe unpacking of folders and `.zip`
  (`package.rs`: paths that escape, links, size and number of files), the two-step install with the permissions
  notice (`mod.rs`, with the commands in `src/addon_commands.rs`: `addon_inspect` prepares and shows,
  `addon_install` installs only if the player accepts), the
  store (`storage.rs`), `net.fetch` (`net.rs`, a separate client with no key or cookies) and the bridge (`bridge.rs`),
  which only knows the v1 API calls and checks again on each one that the add-on is active and has
  the permission.
- **Own scheme** `dweaddon` (`protocol.rs`): it serves only the folder of each active add-on, with a strict
  per-add-on CSP. The launcher's CSP only allows loading frames from that scheme.
- **Interface** (`ui/js/addons.js`): each add-on runs in an `<iframe sandbox="allow-scripts">` (opaque origin, no Tauri
  IPC, which only exists in the main page). Its messages are recognized by the frame that sends them, are
  limited to 40 per second and only the API calls pass to the backend. The frames are never moved in the DOM (that would
  reload them): tabs and sections are hidden and shown.
- **Safe mode:** `--safe-mode`, Settings → Advanced or the notice that appears if the launcher did not close properly the last
  time with active add-ons (detected by a `running` file in the settings folder that is deleted on exit).
- The examples: `apps/launcher/addons/example-notes/` ("Notes") and `apps/launcher/addons/test-fetch/` (a test of
  `net.fetch` and of isolation against the fake server). The bridge client is in `apps/launcher/addons/sdk/`.

## Custom images

The Images section (`src-tauri/src/custom_images/`, `src/image_commands.rs`, `ui/js/custom-images.js`) manages a
single `user`-layer pack in `<data>/custom-images/pack`. While it is on and has images, "Play"
starts the game with `--mods` pointing to it. The rules, the names and how the game draws them are in the guide
[Custom images](../../guides/custom-images.md).

- **Prepare:** the first time the section is opened, or when the game version or the disc read change,
  `images_prepare` runs `dwx export-images` on the content (with the team's packs, for the names in
  the launcher's language) into `originals/`. It needs 400 MB free.
- **Check:** each file is recognized by its path from the first `map/` or `texture/` folder (case-insensitive,
  `\` counts as `/`) and must be one of the originals. Only the PNG header is read: the same
  aspect ratio, from 1 to 8 times, and a single scale per map. Anything that is not a PNG is ignored in the bulk load; what is
  equal to the original is skipped. Paths that escape the folder are rejected.
- **Load:** `images_inspect` shows the summary without writing anything; `images_apply` copies through `incoming/`. A folder
  is walked up to 16 levels and 20000 files; a `.zip` only with names that do not escape it; 256 MB per file.
- **The list:** `images_overview` groups the maps by the letters of their id (the game has no zone data) with the
  most repeated name of the group, and the images by folder. The thumbnails are `data:` URLs from `images_thumbnail`.
- **Export:** it copies `originals/` into a new folder `dwe-images` (or `-2`, `-3`...) with `README.txt`
  (`src-tauri/src/custom_images/readme_es.txt` in Spanish, `readme_en.txt` in any other language) and `maps.tsv`.

The tests: `custom_images/tests.rs` and `the_players_images_are_mounted_while_they_are_on` in `launch.rs`.

## To do

- Test the launcher on a real Windows and everything on a Mac (the keyring, paths and each system's installer).
- Test the add-ons on Windows (there the scheme is `http://dweaddon.localhost/`) and macOS.
