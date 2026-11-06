# dwe beta for Windows

dwe is a new PC engine for a classic creature-raising RPG from the late 1990s. It plays the original game with its
own code, and reads the game's data from your own disc.

This is an early test build. The whole story is open, start to end; versus mode, full fishing and mods are not in yet.
Expect bugs, and please report them (see below).

The easiest way to play is the dwe launcher, which installs and updates this package for you. This package is
for running the game without it.

## What you need

- **Your own copy of the original game**, as a disc image of the North American release: a `.cue` file with its
  `.bin`, or an `.iso`. Nothing from the original game comes in this package, and nothing is downloaded.
- 64-bit Windows 10 (version 1903 or later) or Windows 11.
- A graphics card and driver with Vulkan 1.3 (current drivers of all the common graphics cards have it).
- About 1.5 GB of free disk space.

Nothing else needs installing: the runtime libraries are inside the programs.

## How to run

Unpack the archive (for example with 7-Zip) and double-click `dwe.cmd`, or run `bin\dwe-game.exe`. Windows may warn
that the publisher is unknown: choose **More info**, then **Run anyway**.

The first time, the game asks for your disc image. Type or paste its path, pick it with **Browse...**, or drop the
file on the window, then press **Enter**. The game reads the disc once (a minute or a few) and starts. After that it
goes straight to the title screen.

Useful options (from a terminal): `bin\dwe-game.exe --fullscreen`, `--lang en` or `--lang es`, `--size 1920x1080`.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | WASD or arrow keys | D-pad or left stick |
| Confirm | Enter or Space | Bottom face button |
| Cancel, back | Ctrl or Backspace | Right face button |
| Open or close the menu | Tab | Top face button |
| Pause | P or Pause | Start |
| Save a bug report | F12 | |

The help on screen shows the keys or buttons of the device you are using.

## Language

The game starts in your system's language when it is English or Spanish, and in English otherwise. You can change it
on the first screen and in the game's options.

## Where your files go

Everything the game writes is in `%APPDATA%\indie-modding-games\dwe\`:

- `content\`: what the game read from your disc. Delete it to read the disc again.
- `saves\`: your saved games.
- `settings.yaml`: your choices (language, look, minimap).
- `reports\`: your bug reports.

The launcher keeps its own copy of the game and of what it read from your disc in
`%LOCALAPPDATA%\indie-modding-games\dwe\`; your saves stay in the folder above.

## Reporting bugs

When something looks wrong, press **F12**. The game saves a report and shows "Bug report saved". Each report is a
folder in `reports\` (above) with your game state, a screenshot and the last lines of the log. Send that folder to
whoever gave you this beta, with a few words on what you were doing.

## Licences

dwe is under the MIT License (`LICENSE`). The `licenses/` folder has the licences of the libraries
and the font this build uses; `THIRD_PARTY_NOTICES.txt` has them all in one file, with their credits.
