# Controls

Which actions the engine offers, what triggers them on each device and how they are used depending on the context. It
is the reference for the action map (`defaultActionMap`) and for the per-document behaviors. The bag in battle is
opened with `menu` (same as in the field) and the keyboard is on the `WASD` cluster; `Esc` opens the options and
`cancel` is on `Ctrl` ([below](#esc-opens-the-options-cancel-is-on-ctrl)).

## Principles

The same physical button should always mean the same thing within a family:

- **`confirm`** — accept / choose / advance. Never opens new menus.
- **`cancel`** — go back / close / undo. Never opens anything.
- **`menu`** — the context's menu. In the field, the full pause menu (bag, partner, options, save,
  quit). In battle, the battle menu: only bag and options, with no save or quit.
- **`pause`** — freezes time. It is available wherever a game is running.
- **`walk`** — held modifier. It must not share a button with any pressed action.
- **`alt` / `extra`** — secondary actions tied to the original controller (○ and □). They are only used where the
  context reads them. On the gamepad `alt` shares north with `menu` (in fishing and the card shop it is read held
  —`isDown`— and there `menu` opens nothing); it is a deliberate exception. On the keyboard they are in the `WASD`
  cluster, away from `Home`/`Delete`.

In addition, two practical rules for the keyboard:

- **Modern keys, playable with one or two hands.** Frequent actions go in the `WASD` cluster and its
  neighbors (`Q` `E` `R` `F` `C` `V` `X` `Z`, `Tab`, `Space`, `Shift`, `Ctrl`), not on function rows or on document
  navigation keys (`Home`, `End`, `Delete`, `PageUp`).
- **Careful with keys that type text.** The action map uses single keys, with no modifiers. The name box
  (`naming_box.cpp`) processes the text **before** the actions (`line 137`: *"its keys are also the
  movement and confirm actions"*), so a letter assigned to an action **will be typed** when naming and
  will not trigger the action on that screen. That is why `E`/`Q` do not
  trigger `alt`/`extra` in that box (see the note under the map).

On-screen prompts are generated from the action (`ui::promptControl`), not from the button, so reassigning
does not break the player's hints. The device used last decides which glyph is shown. A
modifier assigned on both sides (cancel with both Ctrl keys, walk with both Shift keys) is named as the pair:
the prompt says "Ctrl", not "Left Ctrl", which also fits in the narrow prompt rows (the evolution tree's).

## Current action map

Single source: `packages/engine/input/src/action_map.cpp:defaultActionMap`. The first binding of an action is the
one the prompts show.

| Action | Keyboard | Mouse | Gamepad | Use |
|---|---|---|---|---|
| `confirm` | Space, Enter, **F** | Left click | South | Choose a command (battle), accept, advance text boxes, click of the focused element. `F` is the "use/interact" alias next to `WASD`; `Space` is the one the prompts show. |
| `cancel` | Left/Right Ctrl, Backspace | Right click | East | Go back / close. `Esc` was freed for `settings` (the classic PC pause menu); the prompts show Ctrl. |
| `menu` | Tab | — | North | Pause menu in the field; **in battle it opens the bag**. |
| `pause` | P | — | Start | Freezes time; the pause box also opens from battle. The "Pause" media key was removed. |
| `settings` | Esc, F10 | — | Back | In-game options: they open the pause menu directly on its options page, and close it if it was already there. The prompt under the pause box names the device's key. |
| `world_map` | M | — | LeftShoulder | Island map. A dwe improvement; unused in the original. |
| `move_left/right/up/down` | A/D/W/S or arrows | — | D-pad or left stick | Player movement and UI navigation. |
| `walk` | Left/Right Shift | — | **RightShoulder (R1)** | Hold to walk instead of run. It no longer shares west with `extra`. |
| `alt` | **E** | — | **North (△)** | The original's ○ button. Lowers the camera while fishing, buy 10 in the card shop, cursor to "Back" in the name box (gamepad). Shares north with `menu`. |
| `extra` | **Q** | — | West (□) | The original's □ button. Triggers the finisher in battle; clears everything in the name box (gamepad); advances phases in shops. |
| `tab_previous` / `tab_next` | **Q** / **E** | — | LeftShoulder (L1) / RightShoulder (R1) | Previous / next tab of a page with tabs (the [in-game options](../modules/apps/game.md#in-game-options)); they arrive as `NavCommand::TabPrevious`/`TabNext` and a page without tabs does not use them. They share a key with `extra`/`alt` and a button with `world_map`/`walk`, which do nothing while the menu is open. |
| `help` | H | — | Left trigger | On-screen rules of the minigame (the shop job: opens and closes its panel while its bar is visible). A dwe improvement; F1 belongs to the console. |
| `trade` | T | — | RightStick (R3) | Co-op only: asks the nearest player in reach for an item trade ([multiplayer, §6.2](multiplayer.md)). The hint under that player's tag names the key. |
| `debug_console` | ` or F1 | — | — | Development console. |
| `report_bug` | F12 | — | — | In-game bug report. |
| `debug_time_day/dusk/night` | 1 / 2 / 3 | — | — | Development views (`dwe-game --map`). |
| `debug_next_map` | N | — | — | Development views. |

> **Keyboard and the name box.** `E`/`Q` are letters, so when typing a name they are typed and do not trigger
> `alt`/`extra` (the text is processed before the actions, `naming_box.cpp:137`); those shortcuts remain on the
> gamepad only.

Gamepad buttons are identified by position (`south`, `east`, `west`, `north`, `left_shoulder`,
`right_shoulder`, `start`, `back`, `dpad_*`, stick). The glyph the player sees (X/Cross, ○/Circle, □/Square,
△/Triangle, A/B/X/Y, etc.) is decided by the family detected in `packages/engine/ui/src/prompts.cpp`.

## How each action is delivered to the game

There are two paths. The full map matters for understanding where to make changes when we change an assignment.

### 1. The UI (modal documents)

`packages/engine/ui/src/ui.cpp:Ui::takeCommands` converts the actions `move_left/right/up/down`, `confirm`, `cancel`
and `menu` into navigation commands (`up`, `down`, `left`, `right`, `confirm`, `cancel`, `menu`) and delivers them
to the modal document on top through its `on_action(document, action)`. If it returns `true`, the UI does not
apply the default action (move focus, confirm the focused element, go back, open the game menu).

Direct access to the bag and to the boxes outside the UI: the shop screens, `naming_box`,
`item_keeper_screen`, `discard_screen`, etc. read specific actions (`wasPressed("alt")`,
`wasPressed("extra")`, `isDown("alt")`) in their `tick`.

### 2. The game and battle

In the field:

- `menu` opens the full pause document (`pause_menu`), with its pages: partner, bag, options,
  save, quit.
- `pause` (Start) shows the simple pause box; `settings` (`Esc` or `F10` on the keyboard, Select on the gamepad) jumps
  directly to the options page, and closes it if it was already open on it.
- `cancel` (`Ctrl` on the keyboard, East on the gamepad) closes the menu.

In battle (`content/packs/game/ui/battle/logic.lua:on_action` and `apps/game/src/play_session.cpp:pressBattle`):

- `confirm` chooses the focused command, charges the finisher meter while the aura lasts (`charging` phase) and
  advances the result text boxes.
- `menu` **opens the bag** of the tamer (`open_bag`); in the final box, `confirm` and `cancel` advance the text.
- `cancel` does **not** open the bag: it is consumed
  because otherwise the default `cancel` would close the battle document and end it.
- `extra` (**Q**) triggers the finisher (`BattleScreen::pressFinisher`) when the meter is full.
- `up`/`down` are consumed for navigation and do not end up opening the pause.
- The battle document does not respond during `intro`, `victory` or `closing` (the poses and the closing).

## `Esc` opens the options; `cancel` is on `Ctrl`

Classic PC uses `Esc` for the pause menu; here `Esc` is the `settings` action — the in-game options menu, which from
the Start pause box or from the field opens `pause_menu` directly on its "Options" page, and closes it if it was
already open on it. So that one press does not trigger two actions at once, `cancel` is on the `Ctrl` pair (left and
right, like the two `Shift` keys of `walk`); `Backspace`, the right click and the gamepad's East also work. The
`{prompt:cancel}` prompts draw the "CTRL" keycap on the keyboard, and the pause box hint ("Esc Options") is shown on
the keyboard too.
