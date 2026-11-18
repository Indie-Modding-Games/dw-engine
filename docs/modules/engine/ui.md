# ui

The game's UI with RmlUi ([ADR 0009](../../adr/0009-rmlui.md)): RML documents and RCSS styles read by asset id
from the VFS, with its logic in Lua (a `ScriptVm` of its own, sandboxed). `Ui` opens the menu with the `menu` action, navigates it with
the actions from any device, sends the app commands (`UiCommand`) and shows the HUD with the minimap.
The texts come from a `localization::Catalog` (`text-key` in the RML, `t()` in Lua) and `refreshTexts()` switches them to another
language without reloading. `UiRenderer` draws it in `render`'s UI pass, and `minimapGridOf` + `buildMinimap` produce the minimap from the
collision of a `MapDocument`. The `FontEngine` puts on top of RmlUi's font engine the `original-look` family, which
draws with the original's bitmap font (`BitmapFont`, read from the base pack) or with Noto Sans according to the player's
`text_font` setting. Only the documents of the engine packs (`PackOrigin::Engine`) of the base and
remaster layers run their own Lua (inline scripts and event attributes such as `onclick`); a `<script src>` runs only if
the file comes from one of them. A mod that declares `layer: base` does not count: the origin comes from where it was found. A document from a
mod, or one that reads a template from a mod, is loaded without its own code and the log says why, once per document
(`CodeTrust`, with the rule of `script::checkEngineDocument`). Each load first empties RmlUi's template cache,
which keeps templates by path and by name: this way each document reads, and judges, the templates it uses, and a
mod template read by another load does not reach it unseen. `element:set_attribute` does not accept event
attributes (`on...`), so that a script cannot turn text into Lua.

The style is set by the **skin** that the player chooses: `SkinCatalog` reads the `ui/skins/<id>/skin.yaml` of
all the mounted packs (a mod adds a skin by adding a folder) and `Ui::setSkin` live-switches the stylesheets
of all the open documents without reloading them, with the same ids and scripts. The documents only link
`ui/common/base.rcss`; the rest of the style belongs to the skin. Images are requested without depending on the extension (a
remaster KTX2 answers to `.png`) and an indexed image is drawn with another palette from its metadata with
`?palette=N`. Any image is drawn as a light-gray silhouette with `tone=grey` (`?tone=grey`, or
`?palette=N&tone=grey`): each pixel takes a gray from its luminance, between 64 and 176, with its alpha; it is recolored once
on loading, so drawing it costs the same as the original (the City tab uses it for whoever is still missing). Every
image is loaded with `loadDrawnImage` (`image_reference.hpp`), which gives its pixels and the size it occupies
(`DrawnImage`): if the player has their own version (`<id>_palette<N>`, the requested palette or 0, in a pack of the
`user` layer; [custom images](../../guides/custom-images.md)), it draws that one at the original's size,
so rectangles, sprites and layout do not change and it only gains detail. One that does not measure the original times
an integer, the same on both axes, is discarded with a warning in the log; `imageSize` also gives the drawn size. The
tests: `tests/image_replacement_tests.cpp`. Details in
[docs/architecture/ui.md](../../architecture/ui.md); how to edit menus in
[docs/guides/edit-menus.md](../../guides/edit-menus.md).

The HUD shows the minimap as a disc around the player, with north up: the `minimap-disc` decorator
(`src/minimap_disc.*`, registered as `original-box`) paints the disc with the image from `buildMinimap` and its ring, plain
with a shine (`plain`, modern skin) or with the border stripes of the original's windows (`original`); the
computation of the window and the markers (`minimapDiscView`, `locateOnDisc`) is in `dwe/ui/minimap.hpp`
([ui.md](../../architecture/ui.md#minimap)). The N for north sits on the ring in an opaque badge in both skins
(black with a light cyan border in the original), so it can be read and does not cross the ring (`hud_fit_tests.cpp`).

| What | Original | dwe | Test | Status |
|---|---|---|---|---|
| Round minimap | Does not exist | Map disc centered on the player, north (−Z) up, view radius of 800 to 1800 units depending on the map; partner on the edge (90% of the radius) if outside; `plain` or `original` ring | `minimap_tests.cpp` ("the round minimap's view…", "…window is centred on the player, north-up", "a marker lands on the disc…"), `minimap_disc_tests.cpp`, `ui_tests.cpp` ("…round the player, north-up") | Improvement (option) |

`world_map.hpp` paints the [island map](../apps/game.md#island-map) with loose values (zones, paths and their
look), without knowing anything about maps or the game: `paintWorldMap` draws the sea, each zone's land (dimmed if it is not
known), the borders, the coast and the paths (`world_map_tests.cpp`).

The bitmap font is drawn at an integer multiple of its cell (11 pixels tall), so that each pixel stays
sharp: at 720 tall, the one closest to the letter size, as the boxes were laid out; at another height, that multiple
times the pixels per dp (`FontEngine::setDpRatio`), rounded down. Before, the size was rounded at each
height and, since the boxes grow with the window, at 1920x1080 a 36dp text (54 pixels) came out 5 times the
cell where the boxes grew 4.5 and spilled out of them; now it comes out 4 times larger and fits.

A box with the original skin's animation (`original_box.lua`, classes `box-animating` and `box-closed`) is hidden
with opacity 0 and also shrunk to nothing (`transform: scale(0)` in `ui/common/base.rcss`): RmlUi inherits opacity
as a value that a child can set again (the unavailable entries of the field menu, the tabs of the
player page), and those children were seen on their own during the five frames of the outline, like the ghost "Sleep" when closing the
menu. It is not hidden with `visibility`: a hidden element does not take focus, and a box takes it before opening.

`Ui::overflowingTexts()` (`text_fit.cpp`) gives the texts that do not fit in the documents that are shown: each text that the
player sees is compared with the drawn box that surrounds it (opaque background, decorator, border, or a box's piece
frame), with what clips it and with the window, and then with the edges of the boxes and the texts next to it. What
a later opaque box covers does not count. The margin is one pixel of the original (3dp). The autotest step
`expect_text_fit` and the route key `text_fit` use it ([vertical slice](../../guides/vertical-slice.md#route-format));
its cases are in `packages/engine/ui/tests/text_fit_tests.cpp` (at 1280x720, 1920x1080, 1366x768 and 1024x768).

`Ui::drawnRects(exceptDocument)` gives the rectangles of what the player sees in the shown documents except one
(texts, boxes with a background, border or decorator, and images), clipped to the window; it leaves out what is hidden, what is
transparent, what is under a transform (a box that opens or closes) and the layers that cover half the
window or more (a veil). `Ui::drawnRectsIn(document)` gives the same for a single document and `Ui::elementRect` the box of
an element. They are used by the save notice, to avoid covering any window ([game](../apps/game.md#save-notice)),
and the autotest step `expect_clear`. `Ui::drawnRectsOf(document, elementId)` gives what is seen of an element and what it contains (nothing if
it or what contains it is hidden) and `Ui::drawnRectsAround(document, elementId)` what is seen of all the documents
except that element and the boxes that surround it entirely (its panel; a box next to it does count): with them `expect_clear`
checks a part of a document against the rest, such as the HUD's time in digits.

Button prompts are glyphs of the last device used (`prompts.hpp`: `promptGlyph`, `promptMarkup`,
`promptText`; own drawings in `glyph_art.hpp`): with the keyboard, the key assigned to the action (Space to
confirm, Ctrl to cancel) in a drawn key; with a gamepad, the face button in the position the game uses
with the letters of its family, the four shapes of the original gamepad or, for an unknown gamepad, just its position.
They change live when another device is touched. The 33 texts of the original that name a button with a character (○ □ △ ×,
which the modern font does not have and which came out as boxes) go through the same system in the story box, and the
own texts write `{prompt:<action>}`. Details in [docs/architecture/ui.md](../../architecture/ui.md),
"Button glyphs".
