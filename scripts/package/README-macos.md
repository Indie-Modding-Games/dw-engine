# dwe beta for macOS

dwe is a new PC engine for a classic creature-raising RPG from the late 1990s. It plays the original game with its
own code, and reads the game's data from your own disc.

This is an early test build. The whole story is open, start to end; versus mode, full fishing and mods are not in yet.
Expect bugs, and please report them (see below).

Play it through the dwe launcher, which installs and updates this package for you; the game is
`dwe-game.app`, which the launcher starts.

## What you need

- **Your own copy of the original game**, as a disc image of the North American release: a `.cue` file with its
  `.bin`, or an `.iso`. Nothing from the original game comes in this package, and nothing is downloaded.
- macOS 13.3 or later, on Apple silicon or on an Intel Mac whose graphics run Metal 3.
- About 1.5 GB of free disk space.

Nothing else needs installing: Vulkan (MoltenVK) and the runtime libraries are inside the application.

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

Everything the game writes is in `~/Library/Application Support/indie-modding-games/dwe/`: `content/` (what the
game read from your disc), `saves/`, `settings.yaml` and `reports/` (your bug reports, saved with **F12**).

## Licences

dwe is under the MIT License (`LICENSE`). The `licenses/` folder has the licences of the libraries
and the font this build uses; `THIRD_PARTY_NOTICES.txt` has them all in one file, with their credits.
