# dwe beta for Linux

dwe is a new PC engine for a classic creature-raising RPG from the late 1990s. It plays the original game with its
own code, and reads the game's data from your own disc.

This is an early test build. The whole story is open, start to end; versus mode, full fishing and mods are not in yet.
Expect bugs, and please report them (see below).

## What you need

- **Your own copy of the original game**, as a disc image of the North American release: a `.cue` file with its
  `.bin`, or an `.iso`. Nothing from the original game comes in this package, and nothing is downloaded.
- 64-bit Linux with glibc 2.38 or newer (for example Ubuntu 24.04, Debian 13, Fedora 39 or later).
- A graphics card and driver with Vulkan 1.3, and the Vulkan loader (`libvulkan1` on Debian and Ubuntu,
  `vulkan-loader` on Fedora and Arch).
- About 1.5 GB of free disk space.

## How to run

```sh
tar -xf dwe-beta-*.tar.xz
cd dwe-beta-*/
./dwe
```

The first time, the game asks for your disc image. Type or paste its path, pick it with **Browse...**, or drop the
file on the window, then press **Enter**. The game reads the disc once (a minute or a few) and starts. After that it
goes straight to the title screen.

Useful options: `./dwe --fullscreen`, `./dwe --lang en` or `./dwe --lang es`, `./dwe --size 1920x1080`.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | WASD or arrow keys | D-pad or left stick |
| Confirm | Enter or Space | Bottom face button |
| Cancel, back | Esc or Backspace | Right face button |
| Open or close the menu | Tab | Top face button |
| Pause | P or Pause | Start |
| Save a bug report | F12 | |

The help on screen shows the keys or buttons of the device you are using.

## Language

The game starts in your system's language when it is English or Spanish, and in English otherwise. You can change it
on the first screen and in the game's options.

## Where your files go

Everything the game writes is in `~/.local/share/indie-modding-games/dwe/` (or under `$XDG_DATA_HOME` when it is
set):

- `content/`: what the game read from your disc. Delete it to read the disc again. When an update of the game
  reads the disc differently, the game asks for it again by itself; your saves and settings stay as they are.
- `saves/`: your saved games.
- `settings.yaml`: your choices (language, look, minimap).
- `reports/`: your bug reports.

## Reporting bugs

When something looks wrong, press **F12**. The game saves a report and shows "Bug report saved". Each report is a
folder in `~/.local/share/indie-modding-games/dwe/reports/` with your game state, a screenshot and the last lines of
the log. Send that folder to whoever gave you this beta, with a few words on what you were doing.

If the game does not start, run `./dwe` from a terminal and send what it prints.

## Licences

The `licenses/` folder has the licences of the libraries and the font this build uses.
