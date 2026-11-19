# Community interfaces

The game shows its screens (the HUD, the pause menu, the partner's sheet...) with its own interface. A **community
interface** is a folder that changes how some of those screens look, or all of them, without touching the game: you
copy the folder (or its `.zip`) and choose it in the menu.

There are three different things that can be changed:

- The **skin** (colors, images, typography) of the game's interface: that is a [skin](skins.md).
- The **screens** themselves (what they show, where and how): that is an **interface** (`type: ui`).
- Something **extra** on top of any interface, like a clock at the top of the screen: that is a **plugin**
  (`type: plugin`).

Interfaces and plugins are installed the same way and written the same way; this page explains both.

## Install an interface

1. Copy the interface's folder or `.zip` (for example `minimal/` or `minimal.zip`) into the user's interfaces folder:
   - Linux: `~/.local/share/indie-modding-games/dwe/ui/addons/`
   - Windows: `%APPDATA%\indie-modding-games\dwe\ui\addons\`
   - macOS: `~/Library/Application Support/indie-modding-games/dwe/ui/addons/`

   With a folder you should end up with `ui/addons/minimal/addon.yaml`. A `.zip` does not need to be unzipped: the game
   reads it as is (see [Interfaces in a .zip](#interfaces-in-a-zip)).
2. Start the game and open **Options → Interface → Interface**. Choose the new one with left and right. The row only
   shows up when there is an interface to choose.
3. Close the menu: the interface is applied when you close it.

Plugins turn on by themselves. The **Options → Interface → Plugins** row turns all of them off or on together.

A pack (a mod) can also bring interfaces in its `ui/addons/` folder, as folders (not as `.zip`).

### Interfaces in a .zip

A `.zip` in `ui/addons/` (with any name, for example `Clock-1.2.zip`) is read without unzipping it, as if it were its
interface's folder:

- It carries the `addon.yaml` at the root of the zip or inside its only folder (`minimal.zip` →
  `minimal/addon.yaml`). The extras macOS adds (`__MACOSX/`, `.DS_Store`) do not count.
- Its id is the `id` of its `addon.yaml`; the zip's name and its folder's do not matter.
- If a folder and a `.zip` give the same id, the folder is used. If two `.zip` give the same id, the first by name is
  used. The one left out shows up in the game's log.
- An interface from a zip, like one from a folder, only reads its own files.
- To change it, replace the `.zip` and start the game again: zips are looked at on startup.

For safety, a `.zip` is rejected whole, without reading anything from it, if:

- it has absolute paths, paths with `..`, with `\` or with `:`, or more than 12 folders deep;
- it has links or other special files, or encrypted files;
- it has the same file twice, or any file that cannot be an asset (for example with capitals);
- it weighs more than 20 MB, has more than 1000 entries, takes up more than 40 MB unzipped or any file expands more
  than 100 times its size (a "zip bomb");
- it has no `addon.yaml` at the root or in its only folder, or its `addon.yaml` has no valid `id`.

These are the same limits the launcher uses for content plugins. A rejected zip is not offered and its reason shows
up in the log; if it was the chosen interface, the game's own comes back and the notice says why.

### If something goes wrong

- If the chosen interface or a plugin cannot be used (it is broken, it is for another game version or a file is
  missing), the game's own comes back and a notice at the top of the screen says why for a few seconds.
- **Safe mode:** hold **Shift** while the game starts, or start it with `--safe-ui`. Only the game's own interface is
  used and the user's interfaces folder is not read. It is useful for getting back to a working game and choosing
  another interface.

## Make an interface

An interface is a folder with an `addon.yaml` and the documents of the screens it changes. The smallest example is in
`examples/ui-addons/minimal/`: it changes only the pause box.

```text
minimal/
  addon.yaml
  pause_box/
    document.rml
    style.rcss
    logic.lua
```

### addon.yaml

```yaml
id: minimal              # lowercase, digits and underscores; the folder name
name: Minimal pause      # what is seen in Options → Interface
version: 1.0.0           # your interface's version
ui_api: "0.12"           # the version of the interface API it works with
type: ui                 # a whole interface
screens:                 # the document that shows each screen you change
  pause_box: pause_box/document
```

A plugin changes `type` and, instead of `screens`, gives the list of its documents (`examples/ui-addons/clock/`):

```yaml
id: clock
name: Clock on top
version: 1.0.0
ui_api: "0.11"
type: plugin
documents:              # the documents it shows on top of the interface
  - document
```

- `id` must equal the folder name; otherwise the interface is not offered. In a `.zip` the `id` rules.
- `ui_api` says which versions of the [interface API](ui-api.md) it works with. `"0.12"` means "0.12 or any later
  version before 1.0". A range can also be given: `">=0.12 <0.14"`. If the game has a version outside the range, the
  interface is not used and the notice says so.
- `screens` names, for each screen you change, its document without the `.rml` extension, relative to the folder. The
  screens you do not name keep the game's document. The list of screens is in `content/packs/game/ui/addon.yaml` and
  in the [interface API](ui-api.md).
- `documents` (plugins only) names its documents the same way. They are always shown, on top of the interface and
  without taking focus from the menu; the document itself decides when to hide its own part (the clock hides when the
  game's time is stopped). Its `.rcss` places it: `position: absolute` wherever you want, with `pointer-events: none`
  so the mouse still reaches the game. The minimap hides while something sits on top of it, so leave free the corners
  that each skin's HUD uses (the example clock goes at the top center).
- The file's schema is in `content/schemas/ui_addon.schema.json`.

### The documents

Each document is an [RmlUi](https://mikke89.github.io/RmlUiDoc/) `.rml` with its `.rcss` style and its Lua code, like
the game's (`content/packs/game/ui/`). They can link the game's common styles (`ui/common/base.rcss`) with a relative
path.

The screen's Lua code reads the game with the [interface API](ui-api.md): `ui.setting(...)`, the functions `partner`,
`clock`, `bag`, `player`, `field`, `options`, `battle`, `versus`, `story`, `dialog` and `naming`, the `hooks.on`
events and the `on_show`, `on_tick`, `on_action`... handlers. To ask the game for something use the actions of those
namespaces (`bag.use(3)`, `battle.command(2)`, `dialog.answer(0)`) or `ui.send(name, value)`.

For your editor to complete the names, point the Lua server (LuaLS) at `content/schemas/ui_api/ui_api.d.lua`.

### What an interface can do

An interface's code runs isolated:

- It reads everything the interface API publishes.
- It can only send the game the commands and options that the API declares (`ui.send`). Any other name raises a Lua
  error and the game does not receive it. The namespaces' actions (`bag.use`, `battle.command`, `versus.step`...) send
  with the same filter: what would not pass with `ui.send` does not pass through them either.
- Battle commands only pass when the battle accepts them, the same as from the game's interface: those of the strip,
  the finisher's charge and opening the bag in the `fight` phase; closing the bag with the bag open; those of the end
  box in the `end` phase. Outside their moment, `ui.send` raises an error and the action (`battle.command`...)
  returns `false` and why.
- The story boxes' commands only pass while their box is waiting for the player: `story_advance` and `story_cancel`
  with a page or a choice waiting, `story_choose` with a choice, those of `naming_*` with the name box open and
  `dialog_answer` with the dialog open. Outside their moment, `ui.send` raises an error and the action
  (`story.advance`...) returns `false` and why.
- It does not touch files, the network or the rest of the game: the interface's Lua has no `io`, `os`, `require` or
  `load`, and only the documents, styles and scripts of its own folder (or its `.zip`) are loaded.

### The screens that can be changed

This version covers the menus, the HUD, the notices, the battle, online versus and the story boxes. These screens have
everything they read and send in the [interface API](ui-api.md):

| Screen | What it is |
|---|---|
| `hud` | The field HUD: clock, gauges, needs, minimap |
| `pause_menu` | The pause menu and the options |
| `partner_menu` | The partner's sheet and the bag |
| `skin_picker` | The skin question at startup until one is chosen, after the language box |
| `pause_box` | The pause box |
| `field_item_notice` | The notice of an item on the ground or in a chest |
| `award_notice` | The notice of a prize |
| `fishing_notice` | The notice of what was fished |
| `save_indicator` | The save indicator |
| `money_box` | The money box |
| `lives_box` | The lives box |
| `ui_notice` | The notice that an interface could not be used |
| `coop_hud` | The co-op game: the other player's name over their head, and its notices |
| `battle` | The battle: fighters, command strip, finisher, opening, numbers and end box |
| `tournament_match` | What a cup battle shows on top: the name, the attributes, the time up, the damage |
| `versus` | Online versus: the room, the code, the game and the partners, the battle, the scoreboard |
| `story_textbox` | The story's text box: what the characters say, the questions and their answers |
| `dialog` | The save dialog: a yes or no question, or a notice, over the saved games |
| `naming_box` | The box that asks for a name, the player's or the partner's |

The rest of the screens in `content/packs/game/ui/addon.yaml` can also be named in `screens`, but what they read is not
in the contract yet and may change between game versions.

### Battle

The whole battle is read with the `battle` namespace ([interface API](ui-api.md#battle)): `battle.state()` (the phase,
the bag, the counts, whether it is a cup battle), `battle.fighters()`, `battle.commands()`, `battle.finisher()`,
`battle.intro()` (the opening and the banner), `battle.numbers()`, `battle.hp_bars()`, `battle.shout()`,
`battle.result()` (the end box) and `battle.match()` (the cup's part). The events say what to redraw:
`battle.started` and `battle.ended` at the start and end, `battle.phase` on a phase change, `battle.changed` when the
fighters, the strip or the finisher change, and one for each part that moves by itself (`battle.intro`,
`battle.numbers`, `battle.overlays`, `battle.shout`, `battle.strip_moved`, `battle.match`).

The rules belong to the game: which commands exist, which can be given, what they cost, when the finisher is ready.
The interface only shows them and asks with the actions:

- `battle.command(command)` with the `command` of an entry of `battle.commands()`; `battle.charge()` on each press
  while the finisher's aura lasts (`battle.finisher().charging`); `battle.open_bag()` opens the bag (the
  `partner_menu` screen) and `battle.close_bag()` closes it.
- The end box is run by the interface, at the pace of `battle.state().frame_seconds`: each point or bit that counts
  asks for its sound with `battle.end_count()`, the last bit with `battle.end_counted()`, and when the box has
  finished it **must** call `battle.close()`, or the battle does not close.
- The events also arrive with the screen hidden: draw only if `ui.is_visible(document_name)`.

A plugin can show something extra during the battle without changing its screen. `examples/ui-addons/battle_hp/` puts
at the bottom center, where no skin puts anything from the battle, the HP of all the fighters while the fight lasts,
and moves aside while the bag is open: it reads `battle.state()` and `battle.fighters()` and redraws with
`battle.started`, `battle.ended`, `battle.phase` and `battle.changed`. It sends nothing to the game.

### Versus

Online versus is read with the `versus` namespace ([interface API](ui-api.md#versus)): `versus.state()` says which
view is shown (`menu`, `host_setup`, `hosting`, `join_entry`, `joining`, `save_pick`, `lineup`, `waiting_lineup`,
`match`, `intermission`, `result`, `error`), where the cursor is and, in a battle, the clock, the player's side and
what the stage shows. Each view has its query: `versus.setup()`, `versus.connection()`, `versus.entry()`,
`versus.saves()`, `versus.lineup()`, `versus.fighters()`, `versus.log()`, `versus.results()`, `versus.score()` and
`versus.error()`. The banner at the start of each battle is the battle's (`battle.intro()`). Each change arrives with
the `versus.changed` event.

The game runs each view: the interface asks for the steps with `versus.step(step)` (`up`, `down`, `left`, `right`,
`confirm`, `cancel`, `menu`, `tab_previous`, `tab_next` and `finisher` for the finisher) and `versus.pick(row)` to go
to a row and confirm it. The room code is typed with the keyboard, not with the interface.

### Story

The story boxes are read with three namespaces ([interface API](ui-api.md#story)):

- `story.textbox()` is the text box as the game publishes it: whether it is open, whether it is the conversations' box
  or one of its own size, how long it has been open, who is speaking (`speaker`), the rows already translated and split
  into colored chunks, where the cursor is and whether it shows the press prompt. `story.choice()` gives the options of
  a question and `story.input()` what the box expects from the player: `"page"` (to turn the page), `"choice"` (to
  choose) or `""` (nothing).
- `dialog.state()` and `dialog.options()` are the save dialog: its text, the saved game it talks about and its options
  (yes and no, or accept).
- `naming.state()` and `naming.cell(row, column)` are the name box: what is written, the letter grid and the cursor.

The events say what to redraw: `story.changed` when the box changes, `story.text` with each new page, `story.input`
when it starts or stops waiting for the player, `dialog.changed` and `naming.changed`. They also arrive with the screen
hidden: draw only if `ui.is_visible(document_name)`.

The rules belong to the game: when a page waits, how long the box takes to open, which options a question has, which
letters a name accepts. The game also reads the player's buttons and keyboard, so an interface that only draws these
boxes does not have to send anything. If it wants to run them (with the mouse, with its own buttons), it asks with the
actions: `story.advance()`, `story.choose(option)` and `story.cancel()` while the box waits; `dialog.answer(option)`
and `dialog.cancel()`; `naming.write(letters)`, `naming.erase()` and `naming.accept()`.

A plugin can show something extra next to the story without changing its boxes. `examples/ui-addons/story_log/` keeps
the last pages that have been said, with who said them, and shows them on the right, between the minimap and the
partner's bars, where it covers nothing from any skin, while the text box is open: it reads `story.textbox()` and
redraws with `story.text` and `story.changed`. It sends nothing to the game.

### Versions

The interface API carries its own version (the table is in the [interface API](ui-api.md#versions)). A minor version
(0.12 → 0.13) only adds names: an interface made for 0.12 keeps working. A major one can remove or change names. Put in
`ui_api` the version you tested it with: `"0.12"` accepts that one and the following minor ones, and the game will not
use it with a major version it does not know.

### Test while you build

- Change the files and choose the interface again in the menu (switch to another and back): it is read again from its
  files. From the development console, `ui_addon("minimal")` sets it without opening the menu.
- Lua errors and load notices show up in the game's log.
- If the interface leaves the game in a bad state, start in safe mode (Shift).

## Distribute an interface

Share the folder as is, or a zip with the folder inside (`minimal.zip` → `minimal/addon.yaml`). Whoever uses it copies
the folder or the `.zip` into their interfaces folder, as in [Install an interface](#install-an-interface). It can
also go inside a mod, in its `ui/addons/<id>/` folder: it is offered when the mod is in place.

Before distributing it:

- Make sure the `id` is the folder name and does not clash with that of another known interface.
- If you distribute it as a `.zip`, make sure it passes the limits of [Interfaces in a .zip](#interfaces-in-a-zip): no
  links or encryption, and with lowercase file names.
- Make sure `ui_api` is the version you tested it with.
- Make sure it works with all the game's skins (Original, Modern, Pixel art...): the interface brings its own styles,
  but the game documents you do not change keep the chosen skin.
- Make sure it carries no material from other games (images, texts or music) that you cannot distribute.

## See also

- [Interface API](ui-api.md): everything a screen can read and send.
- [Skins](skins.md): change only the style of the game's interface.
- [Packs](packs.md): distribute an interface inside a mod.
