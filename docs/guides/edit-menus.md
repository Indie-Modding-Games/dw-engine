# Editing menus

The menus and the HUD are RmlUi documents in the `game` pack (`content/packs/game/ui/`). They are edited as text and,
in a development build, the game reloads them by itself when they are saved. Architecture in
[ui.md](../architecture/ui.md).

## Where everything is

```
content/packs/game/ui/
  common/base.rcss           the common structure, the same in all looks: display, size, navigation
  common/ui_skin_choice.lua  the "Interface style" row of the options
  skins/modern/skin.yaml     the modern look: which sheets it puts on each screen
  skins/modern/theme.rcss    its common style: font, buttons, button prompts
  skins/modern/<screen>/style.rcss
  skins/original/skin.yaml   the original look (the default one): extends the modern one, its sprite sheets
                             (images from the disc by asset id and palette) and document_classes
  skins/original/<screen>/style.rcss
  fonts/                     fonts (.ttf/.otf) and their licenses; all are loaded. original_look.yaml
                             describes the original-look family (see docs/architecture/ui.md)
  pause_menu/document.rml    the pause menu
  pause_menu/structure.rcss  its behavior in all looks: the lists wrap around
  pause_menu/logic.lua
  sleep_menu/                the sleep menu (sleep, or save and sleep)
  save_slots/, dialog/       the cover's slot list and the save dialog
  hud/document.rml           the HUD with the minimap
  hud/presenter.lua          what the HUD shows (from the game's data)
  hud/view.lua               how it draws it
  ui_sounds.yaml             which sound each menu prompt plays (move, confirm, cancel): a bank from
                             sound/ and a sound from it (see docs/architecture/audio.md)
```

Each document goes in its own folder because files are read by asset id, which is the path without the extension:
`menu.rml` and `menu.rcss` in the same folder would clash. A mod can replace any of these files with one with the same
id in its pack, or add a whole look with its `ui/skins/<id>/skin.yaml` (see
[Looks](../architecture/ui.md#skins)). A replaced document does not receive the entries the game adds later to its
own: a replaced pause menu has no "Sleep" (`#sleep`, which sends `sleep` and is grayed out with
`pause_menu.sleep_enabled`) until it adds it. A look that needs to mark elements of a shared document (for example,
that the HUD bars fill in little squares with the `pips` class) does not copy the document: it says so in
`document_classes` in its `skin.yaml`, by document and element id, and the game adds and removes those classes when
the look changes.

A look's sprite sheets name images of the base pack by their asset id (`texture/etcdat/system_w`), their palette and
their size in the original (`original_size`); if a remaster layer pack brings the same image larger (its `_meta.yaml`
says `scale`), the look uses it without changing anything and it looks sharper. This is how the original look's
arrive: `dwx extract` enlarges them ×4 with the included recipe `content/enhance/ui_original.recipe.yaml`. A sheet
with `upscale: smooth` (the two of the cover) is drawn smoothed instead by the models' texture upscaler
(`render::upscaleRgba`, the alpha interpolated like a color, so the cut-out edge does not make steps), from the
original's pixels up to 2560 wide, when the player's texture filter is "upscaled"; with the other filters it is drawn
sharp like the rest. A sheet with `mirror_sides: [left, right]` draws its image wider, with those columns of each edge
mirrored: this way the cover continues in a window wider than it (`ui/common/title_picture.lua`).
A mod's look that wants its own enlarged images writes a recipe the same way (the images it draws, `scale` and the
`index_scale` mode) and passes it by hand through `dwx upscale <recipe> <packs> -o <folder>`, which writes a remaster
pack to mount alongside ([dwx page](../modules/apps/dwx.md#dwx-upscale-and-the-bundled-recipes)). Art from the
original is never distributed: each player gets it from their own disc. The measurements of the original look are in
[UI](../architecture/ui.md#the-original-skin).

Style goes in the look, not in the document: a document only
links `../common/base.rcss` and, if the screen has a behavior of its own that does not change with the look (for
example, that a list wraps around), its `structure.rcss`.

## RML and RCSS

RML is almost HTML and RCSS almost CSS; the reference is the [RmlUi documentation](https://mikke89.github.io/RmlUiDoc/).
What is specific to this project:

- Measurements, in `dp` (one dp is one pixel at 720 lines; the UI scales by itself with the window and the player's
  option).
- Paths are relative to the document (`href="../common/base.rcss"`) or to the root of the packs
  (`/ui/skins/modern/theme.rcss`). Images are requested without depending on the extension (`.png` finds the `.ktx2`
  of a remaster) and with `?palette=N` they use another palette of an indexed image.
- An element can be selected with the keyboard and the controller if it has `tab-index: auto; focus: auto; nav: auto;`
  (`button`s already have it in `base.rcss`). `nav-up: #id` and friends set where each direction goes, for example to
  wrap around in a list. The selected element's style is `:focus`; the mouse also moves the focus.
- `autofocus` marks the element that receives the focus when the document opens.
- The HUD must not block the mouse: its `body` has `pointer-events: none`.
- There are no default styles: a new element needs its `display` in `base.rcss`.

## Logic in Lua

A document loads its scripts with `<script src="logic.lua"></script>` (or inline). They run in a Lua environment of
the document's own, in the UI sandbox (no files, no `os`, no loading code). Event attributes are Lua and see `event`,
`element` and `document`:

```xml
<button id="quit" onclick="ui.send('quit')" text-key="pause_menu.quit"></button>
<button id="open-options" onclick="open_page(document, 'options')" text-key="pause_menu.options"></button>
```

A document carries no written text: the `text-key` attribute sets the text of that key in the game's language, on
load and when the language changes. The keys are in `content/packs/game/localization/<language>/` and each one has to
exist in English and in Spanish ([localization](../architecture/localization.md#adding-a-text)).

The document can define these functions, which the UI calls with the document as the first argument:

| Function | When |
|---|---|
| `on_show(document, args)` | When shown; `args` is the table `ui.show` passed (empty if nothing was passed) |
| `on_hide(document)` | When hidden |
| `on_action(document, action)` | For each navigation step while it is the top modal document: `up`, `down`, `left`, `right`, `confirm`, `cancel`, `menu`. If it returns `true`, the UI does not do the default |
| `on_setting(document, name)` | When the app changes a setting |
| `on_device(document)` | When the player changes device (for the button prompts) |
| `on_language(document)` | When the language changes: the `text-key`s are already in the new one; here what the script set with `t()` and the button prompts are rewritten |
| `on_tick(document, seconds)` | On every logic tick while it is visible |

Available API:

| Call | What it does |
|---|---|
| `ui.send(name, value)` | Sends a command to the app (see the command table in [ui.md](../architecture/ui.md)) |
| `ui.setting(name)` | The current value of a setting the app publishes, or `nil` |
| `t(key[, values])` | The text of a key in the game's language, with its placeholders filled in: `t("pause_menu.ui_scale.value", {percent = 150})` |
| `ui.languages()` | The languages the packs have (`{"en", "es"}`) |
| `ui.skins()`, `ui.skin_name_key(id)` | The ids of the looks the packs offer (`{"modern"}`) and the key of one's name, for `t()` |
| `ui.prompt(action)` | The name of that action's control on the current device (`"Enter"`, `"A"`, `"Cross"`), in the game's language; the texts are in `localization/<language>/prompt.yaml` |
| `ui.device()`, `ui.gamepad_family()` | `"keyboard_mouse"` or `"gamepad"`; `"xbox"`, `"playstation"`, `"nintendo"`, `"steam"` or `"generic"` |
| `ui.show(name[, modal[, args]])`, `ui.hide(name)`, `ui.is_visible(name)` | Shows or hides a loaded document; with `modal` true the document receives navigation (like the menu the game opens). `args`, a table of booleans, numbers and named texts, reaches its `on_show` ([UI API](../modding/ui-api.md)) |
| `ui.cue(name)` | Plays `box_open` or `box_close` (the original look's boxes) or `confirm` (a press that does not go through navigation, like the extra key), from `ui_sounds.yaml` |
| `document_name` | The name of the document itself |
| `element:get(id)` | A descendant element, or `nil` |
| `element:set_text(text)`, `element:text()` | Changes the text (always as text, never as RML) or reads the content |
| `element:set_class(class, yes)`, `element:has_class(class)` | Classes |
| `element:attribute(name)`, `element:set_attribute(name, value)` | Attributes (`set_attribute` does not accept the event ones, `on...`) |
| `element:set_property(name, value)`, `element:remove_property(name)` | A style property, like `display`; removing it brings back the stylesheet's |
| `element:focus()`, `element:has_focus()`, `element:click()` | Focus and click |
| `element:scroll_to_top()` | Takes a scrolling list to its start (a list that fills again) |
| `element:rect()` | Left, top, width and height of the element in dp from the window's corner |
| `element.id`, `element:valid()` | Its id; whether it still exists |
| `event.type`, `event:stop()` | In an event attribute: its type; stop propagation |
| `event.wheel` | In `onmousescroll`: how much the wheel turned, positive downward (0 in other events) |

The original look's boxes are opened and closed with the original's gray outline with `ui/common/original_box.lua`
(`original_box.open`, `close`, `tick`, `cancel`, `busy`): the document links the script, puts
`<div id="box-outline" class="box-outline hidden"></div>` under the `body` and calls `tick` from `on_tick` and
`cancel` from `on_hide`. Outside the original look everything finishes at once. The three-line border is the
`original-box(3dp)` decorator, or the `.original-box` class of `skins/original/theme.rcss`. If the box adds a fill of
its own (`vertical-gradient`), it goes at the end of the decorator list: RmlUi paints the first one on top, like CSS
with backgrounds, and a fill placed in front covers the border.

A menu does not change settings by itself: it sends the command and shows what the app publishes in `on_setting`.
This way its labels always tell the truth (for example, if full screen could not be turned on).

## Testing

- `scripts/dev.sh run -- --ui-demo --content <extracted content>` opens the UI demo with the minimap of a map
  (`--map <id>`) and a player who walks around; the `menu` action (Tab, north button) opens the menu. Editing and
  saving a `.rml`, `.rcss` or `.lua` in `content/packs/game/ui/` reloads it at once.
- RML, RCSS and Lua errors appear in the log with the file and line.
- `dwe_ui_tests` walks through the pause menu with keyboard, controller and mouse; if the ids of its elements are
  changed, its tests have to be updated.
