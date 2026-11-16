# 0014. Gamepads and action-based input

- **Status:** Accepted
- **Date:** 2026-10-18
- **Note (2026-11-06):** accepted because the code already follows it: `packages/engine/input` (action map with keyboard, mouse and gamepads).

## Context

The game must work with any gamepad that connects to the PC, in addition to keyboard and mouse
([ADR 0013](0013-native-pc-game.md)). Players mix devices, connect and disconnect them in the middle of a game and
expect to be able to remap all the controls.

## Decision

**Devices.** We use SDL3's gamepad API:

| Family | Examples |
|---|---|
| Xbox | Xbox One and Series controllers, and XInput-compatible ones |
| PlayStation | DualShock 4, DualSense and DualSense Edge |
| Nintendo | Switch Pro Controller and Joy-Con |
| Steam | Steam Deck (built-in controls) and Steam Controller, also through Steam Input |
| Generic | 8BitDo and others, through SDL's mapping database; the user can extend it with their own file |

- **Hot plugging**: a gamepad can be connected or disconnected at any time. If it disconnects in the middle of a
  game, the game pauses.
- **Several gamepads at once**, so a local game mode can have more than one player.

**Action-based input.** The game never reads buttons, but **actions** (`confirm`, `cancel`, `menu`, `move`,
`camera`…), grouped in **contexts** (field, battle, menu). The `input` module translates keys, mouse and gamepads
into actions according to each device's profiles.

- All actions can be remapped per device, and the profiles are saved in the settings.
- 360° analog movement, with configurable dead zones.
- Option to swap confirm and cancel (the Nintendo layout versus the Xbox one).
- **Automatic button prompts**: the icons follow the last device used and the type of gamepad. The icons are our own
  or freely licensed (for example, Kenney's *Input Prompts* pack, CC0), never the official ones from any
  manufacturer.
- Rumble. Later, optionally: gyroscope, adaptive triggers and DualSense light, which SDL3 supports.
- The Lua API exposes actions, not buttons, so game modes and mods work with any device.

## Consequences

- The `input` module is added to the systems layer. `platform` delivers the device events; `input` converts them into
  actions.
- All the UI and all the prompts are designed for mouse and gamepad at the same time.
- CI tests input with SDL's virtual gamepads, because it has no physical gamepads.

## Alternatives rejected

- **Read buttons directly in the game:** it ties each system to a specific device and prevents remapping controls.
- **XInput only:** it leaves out PlayStation, Nintendo and generic gamepads.
