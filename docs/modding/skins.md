# Skins

A skin is the style of the whole interface: colors, fonts, sizes, frames and images of each screen. The game ships
two, the original and the modern one, and the player switches them in "Interface style". A mod adds another with a
`ui/skins/<id>/` folder. The full contract is in [UI, skins](../architecture/ui.md#skins); where each screen is
and how it is tested, in [edit menus](../guides/edit-menus.md).

## A minimal skin

```text
my_skin/
├─ pack.yaml                         (layer: mods, api: "1.4", depends on game)
├─ localization/en/my_skin.yaml      skin_name: "My skin"
└─ ui/skins/my_skin/
   ├─ skin.yaml
   └─ theme.rcss
```

```yaml
# ui/skins/my_skin/skin.yaml
id: my_skin
name: my_skin.skin_name
extends: modern                 # starts from the modern one and changes only its own parts
theme:
  - theme                       # relative to the skin's folder → ui/skins/my_skin/theme
cursor: cursor                  # optional: PNG with straight alpha, installed as the system cursor
cursor_hotspot: [1, 1]          # optional: the point that clicks (x, y in pixels from the top-left corner)
screens:
  pause_menu:
    styles: [pause_menu/style]  # → ui/skins/my_skin/pause_menu/style
```

The sheets of `theme`, of `screens.<screen>.styles`, the `document` and the `cursor` are written **relative to the
skin's folder** (`parseSkin` prefixes them with `ui/skins/<id>/`); the full absolute path also works, so skins written
before this form keep working. The images of the `sprite_sheets` are **not** made relative: they name assets of the
base layers.

To avoid starting from scratch, `python scripts/new_skin.py fork <id> --from <skin>` copies an existing skin to a new
folder, rewrites its paths and leaves **an empty `style.rcss` sheet for each screen** that the source does not style;
`--flatten` dissolves the `extends:` chain into a self-contained skin.

The tests' pack [`tests/mods/ui_skin/test_contrast`](../modules/tests/mods.md) is a complete example.

## Layout

Some screens have two arrangements in the same document and the same script: that of the modern look and that of the
original. A skin chooses with `layout:` in its `skin.yaml`, per screen; if it extends another, it inherits what it does
not set:

```yaml
layout:
  pause_menu: {menu: original, box: original}
  main_menu: {menu: original}
```

`menu` is the screen's arrangement and `box`, whether its boxes open at once (`modern`) or grow from a gray outline as
in the original (`original`). Others are finer, so that a skin that extends the original keeps part of it: `pages`
(the partner's pages as full screens, or `compact`, a single page with the status and the techniques), `picture` (the
widened cover with the boxes in their places from the original) and `heading` (the save machine's title also over its
question). And a few are only used for now by the DevVago skin: `tabs: minimal` and `medals: profile` in the pause
menu, `pool: current` in the partner's. The full list of screens and options is in
[UI, skins](../architecture/ui.md#skins); the document also receives the class `layout-<option>-<value>` on its
`<body>`, to write rules that only apply with one arrangement.

## Rules

- **Style, not behavior.** A skin does not write `nav-*` or its own scripts: each screen's navigation and logic belong
  to the game. A replacement document keeps the ids and links its screen's script.
- **Images by id.** Sprite sheets and images name assets by their id; a mod does not hand out images from the
  original.
- **The mouse cursor belongs to the system, not to RmlUi.** With `cursor:` and `cursor_hotspot:` the skin installs an
  image as the **OS cursor** while it is active: it always stays on top (modal menus included) without fighting the
  stacking. Without `cursor:`, the system arrow stays; switching skins restores it.
- **If something fails**, the skin is left out with the reason in the log and the game goes on with the one it had.

## Test while editing

With `--tuning`, the game watches the interface files every half second:

- on saving an RCSS sheet, an RML or a screen's script, that screen is loaded again;
- on saving a `skin.yaml` (or `ui/ui_skin.yaml`), the game reads all the skins again and reloads each screen with the
  skin in use. If the edit leaves the skin in use broken (an `extends` that does not exist, a sheet that is missing),
  it is not applied: the log says why and the previous skin stays until the next edit.

The sprite sheets' images and a skin in a new folder still need the game to be restarted.
