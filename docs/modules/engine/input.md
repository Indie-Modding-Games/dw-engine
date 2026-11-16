# input

Input by actions ([ADR 0014](../../adr/0014-gamepads-and-input.md)). The game asks about actions (`confirm`, `move_left`…), never about keys or buttons. `ActionMap` says which inputs produce each action; `InputState` keeps its value on each tick and which device was used last.

On the field you run by default, as in the original; `walk` (Shift, or on the gamepad the top right button R1) is held to walk. The gamepad's square no longer walks: it is only `extra`, so holding it does not move the player (docs/architecture/controls.md).

`confirm` has two keyboard keys: **Space** (the one the prompts show) and **Enter**, and **F** is added as a modern alias for "use/interact", next to `WASD`; on the gamepad it is South. In the name box `F` types "f" instead of confirming (the text goes before the actions), so there it is confirmed with Enter.

`alt` and `extra` are the other two face buttons of the original gamepad (○ and □; `dw/input.h: ALT_BUTTON` and 0x80). On the gamepad `alt` is on the north button, because east is `cancel`, and `extra` on the west; on the keyboard they use **`E`** (`alt`) and **`Q`** (`extra`), both inside the `WASD` cluster. In the name box those letters are typed instead of triggering the action (the text is processed first, `naming_box.cpp:137`), so the "Back" and "clear all" shortcuts are only available on the gamepad; on the keyboard they are covered by the grid's arrows and `Backspace`. See [Controls](../../architecture/controls.md).

`defaultActionMap` also brings actions for the development views (`dwe-game --map`): `debug_time_day`, `debug_time_dusk` and `debug_time_night` (keys 1, 2 and 3) change the map's time of day and `debug_next_map` (N) moves to the next map in the list. `report_bug` (F12) saves a [bug report](../../guides/vertical-slice.md#reporting-a-bug-f12) of the game. `debug_map_overlay` (F3) turns the [map debug layer](../../guides/remote-control.md#map-debug-layer) on and off.

`menu` (Tab, or the north button on the gamepad) opens the pause menu on the field. **In battle it directly opens the Tamer's bag** (before, it was launched with `cancel`): the same button, same action, different context (docs/architecture/controls.md).

`settings` (`Esc` or `F10` on the keyboard, Select/Back on the gamepad, which the original did not use on the field) opens the
pause menu directly on its options page, also from the Start pause, and closes it if it was already open on it
([in-game options](../apps/game.md#in-game-options)). `Esc` came from `cancel`, which moved to the `Ctrl` pair (with
`Backspace`, the right click and the gamepad's East as it was): see [Controls](../../architecture/controls.md).

Other default map actions: `pause` (P, Start on the gamepad), `world_map` (M or the top left button
L1: opens the [island map](../apps/game.md#island-map)), `tab_previous` and `tab_next` (Q and E, L1 and R1) to switch
tabs in the menus, `battle_speed` (R or the right trigger: switches a fight between the original's speed and double, with the [quick battles option](../apps/game.md#in-game-options)), `help` (H or the left trigger: the rules of the [shop job](../../reference/shop-job.md)) and `debug_console` (`º` or F1) for the [development console](debug.md).
