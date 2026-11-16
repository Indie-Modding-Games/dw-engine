# 0013. A native PC game

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** accepted because the code already follows it, in the game (`apps/game`).

## Context

The target platform is PC. We do not want it to feel like a ported PS1 game, but **as if it had come out only on PC**.

## Decision

All the interface, controls and experience design starts from PC conventions:

| Area | What it means |
|---|---|
| Controls | Keyboard and mouse at the same level as the gamepad: menus that work with the mouse, *tooltips* on hover, keyboard shortcuts and every key remappable. Any gamepad that connects to the PC ([ADR 0014](0014-gamepads-and-input.md)) |
| Button prompts | They change according to the device in use (key, mouse, gamepad), never the fixed PS1 symbols |
| Screen | Any resolution and aspect ratio (16:9, 16:10, ultrawide), scalable UI, windowed, borderless or fullscreen, multiple monitors |
| Performance | Free frame rate, with interpolation over the fixed-step logic; configurable VSync and limit |
| Saving | Files with several slots and autosave. No memory cards or blocks |
| Loading | No loading screens inherited from the CD, and no disc swaps |
| Text | Vector fonts, no character limits per line and with full localization |
| Options | Graphics, audio, controls and accessibility (text size, subtitles, color blindness) |
| Mods | Mods folder and manager in the launcher |

## Consequences

- The UI is designed for mouse and gamepad at the same time. This affects every menu in the GDD.
- The visual style (prerendered backgrounds with 3D models on top) is kept, with HD and widescreen backgrounds.
- macOS, consoles and mobile are not targets ([ADR 0003](0003-vulkan-and-sdl3.md)).

## Alternatives rejected

- **A faithful port with the console experience:** it contradicts the goal.
