# signs

The signs painted in the maps' images, written again in the player's language. The American version
left many signs in Japanese (the arena, the Freezeland posts, the shops...) and others in English
(the house's doormat, the tool shop's sign). `signs` erases the old text and draws the new one
**when the map loads**, from the textures that the player extracted from their disc and from its own data: the repository
carries not one pixel of the original, neither cropped nor edited.

How to find a sign and write its overlay: [signs guide](../../guides/signs.md).

## Pieces

| Piece | What it does |
|---|---|
| `sign_overlay.hpp` | The format: `Sign` (image, region, erasing, text, font, effects), `parseSignOverlay`, `mergeSignOverlays` |
| `sign_font.hpp` | `SignFont`: a TrueType font from the pack rasterized with FreeType, without hinting and with kerning |
| `sign_painter.hpp` | `paintSign` / `paintSigns`: erases and draws on a copy of the images and gives patches; `applySignPatches` applies them |
| `map_signs.hpp` | `MapSigns`: reads the overlays from the VFS, loads the fonts (with the CJK faces as fallbacks, `SignFont::addFallback`) and keeps the patches by map, language and revision |

## Overlays

- `signs/<map>.yaml` of the `game` pack: a map's signs in all languages (their texts, in English and
  Spanish, in `localization/<language>/signs.yaml`).
- `signs/<language>/<map>.yaml` of any pack (the Spanish one is `es-translation`): replaces signs with the same
  `id`, adds the ones only that language needs (the doormat, which already reads fine in English) and removes with `remove` the
  ones that that language leaves as in the original.
- The schema is `content/schemas/sign_overlay.schema.json`.

A sign says:

| Field | What it is |
|---|---|
| `target` | `background` (default) or a sprite texture page: `{page, bits, clut}` as the sprite names it |
| `region` | `[x, y, width, height]` in image pixels: the only thing the sign may change, and the text's box if there are no `corners` |
| `corners` | The four corners of the text (top left, top right, bottom right, bottom left), for a sign in perspective |
| `rotation`, `skew` | Degrees of rotation (clockwise) and slant of the letters, when there are no `corners` |
| `clear` | How the old text is erased (below) |
| `text` | The text key, like `signs.trai00_title_top`; `\n` marks where a line may break (see Fitting) |
| `font` | A font from `ui/fonts/` (`noto_sans_bold` by default); the letters it lacks (Chinese, Japanese, Korean) are drawn by `ui/fonts/cjk/noto_sans_cjk_*`, the sign's language first |
| `size` | Optional: the letters' size in image pixels instead of the one the box gives (see Fitting); shipped signs leave it out |
| `letter_spacing`, `line_spacing` | Extra pixels between letters and a factor on the line height (0.85 by default) |
| `align`, `valign` | `left`/`center`/`right` and `top`/`middle`/`bottom` |
| `vertical` | One letter under another (a vertical banner) |
| `lines` | How many lines the board holds (1 to 4, 1 by default): the text may wrap at spaces and hyphens up to that many |
| `group` | Signs of one map with the same group are drawn at the size of the smallest among them (the two halves of a title, the rules of a board, the posts of a crossroads) |
| `fit` | Fits the text to its box (yes by default); with `no`, the size is used as is and nothing wraps |
| `color`, `outline`, `glow` | The letters' color, an outline `{color, width}` and a neon halo `{color, radius, strength}` |
| `colors` | `all` (any opaque color of the palette) or `region` (only those the original uses around it) |

The `clear` modes:

| Mode | What it does |
|---|---|
| `inpaint` | Each pixel blends those from outside the rectangle in its row and its column, by distance |
| `fill` | One color: `color`, or the average of the rectangle's border |
| `diffuse` | Removes only the pixels that move away from the board's color (`color`, or the most common) by more than `tolerance`, grows them `grow` pixels and fills them from the surrounding board, ring by ring, with the board's grain; `quad` limits the erasing to a quadrilateral |
| `none` | Does not erase: the text is drawn on top (the second line of an already erased sign) |
| `transparent` | Leaves the area transparent (the palette color with alpha 0), so that what is behind the sprite shows: the letters of a sprite page that is painted over the map (the lit signposts). The new letters are drawn without soft edges, because there is nothing to blend them with |

## Fitting

One rule sizes every sign, in every language (`layOutSignText` and `layOutSigns` in
`sign_painter.cpp`):

1. **Margins.** Along the text, each side keeps 15% (`SignMarginRatio`) of a line's height free (of
   the box's height divided by its lines; for vertical text, of the box's width). Across the text
   the whole box is used: the margin there is the font's own.
2. **Base size.** The size whose letters fill the box's height: a capital `H` (or `国` for
   Chinese, Japanese and Korean text) measured in the font, with the lines stacked at the line
   spacing. A small box gives small letters, a big one big letters, so a sign looks as large as the
   original text it covers. `size` overrides it.
3. **Shrinking.** When the widest line (outline and halo included) does not fit the room between
   the margins, the whole text shrinks uniformly until it does. Nothing ever crosses the box.
4. **Lines.** Each way of breaking the text into at most `lines` lines (or as many as the text's
   `\n` give) is tried; the one with the largest letters wins, and fewer lines win a tie. With
   `\n` in the text, only those breaks are used (joined with a space, or with nothing after a
   hyphen and between ideographs); without, spaces and hyphens are. So a translation only has to
   say where its words may break, never where they must.
5. **Groups.** Signs that share a `group` take the smallest size among them, so two halves of one
   board never look different.
6. **Floor.** A text may not shrink below letters 4 image pixels tall (`SignLegibleLetter`), or
   60% of its base size when that is smaller (`SignFloorRatio`). A sign whose base size is already
   below 4 pixels (a tiny board) has no floor. A text that would need to go under the floor must
   be reworded, not squeezed.
7. **Vertical text** keeps one letter under another; the Japanese long-vowel mark `ー` turns into
   the vertical bar `｜`, as in printed vertical Japanese.

`packages/engine/signs/tests/sign_content_tests.cpp` lays out every shipped sign of every map in
every language with the real fonts and fails when one crosses its box, falls under its floor,
lays out nothing or has letters no font draws. It runs with the rest of `dwe_signs_tests`, in
the CI of every PR, without a disc: `build/ci/packages/engine/signs/dwe_signs_tests
--test-case='every shipped sign*'` runs only this one.

## How it is painted

1. The text is rasterized with FreeType at 4x, with the outline (dilation) and the halo (three-pass blur).
2. The text box is brought to the image with a homography (square to quadrilateral): the `corners`, or the region
   with its rotation and slant.
3. Each pixel blends the layers (halo, outline, letters) over the already erased board and is written as the nearest
   **palette index** (weighted RGB). No new color enters the texture and the time-of-day palettes
   keep shading the sign.
4. `paintSigns` gives one patch per sign (the pixel rectangle that changed); `applySignPatches` writes them to
   the images and, if a texture page changed, mounts the sprites' textures again.
5. Many maps draw over the background sprites cut out of the same background (a counter in front of a banner,
   an awning): with the map's objects, each pixel that a background sign changed is also copied into the
   8-bit sprites with the background palette (`clut` -1) that show it, if their texel had the same index as the background (some
   sprites were cut out a couple of pixels offset: the offset is searched for, up to 4 pixels, at which the most
   texels match the background). A
   sprite with its own palette (a sign that blinks with a lit copy) needs its own sign with
   `target: {page, bits, clut}`, like `shop_lit` in `gcan09`.

## In the game

`FieldScene` keeps a copy of the map's images as it read them, paints the patches before
`MapRenderer::setMap` and, if the language (`Catalog::revision`) or the option changes, uploads the background and the
sprites' textures again with `MapRenderer::updateImages`. It checks this on every frame (`FieldScene::update`), also with
the game paused: the language chosen in the pause menu changes the signs seen behind the menu right away. `MapSigns` keeps the patches by map, language and revision, so
going back to a map paints nothing again.

The **Translated signs** option in the pause menu (options; `translated_signs` in the player's settings) is
on by default; when off, the map shows the original textures.

## Tools and tests

- `dwe-game --dump-signs <map>[,<map>...] --shots <folder> [--lang <language>]`: writes the background and the texture
  pages with a grid, the list of sprites, the painted signs and a `place(...)` to go and see each sign
  (`apps/game/src/sign_dump.cpp`). The images come from the player's disc: they go to `_work/tmp`, never to the repository.
- `packages/engine/signs/tests`: overlay reading, erasing, text on a synthetic texture (without game data), rotation,
  perspective, effects, patches and `MapSigns`'s language change.
- `packages/engine/signs/tests/sign_content_tests.cpp`: every shipped sign fits its box at no less than its floor, in
  every language (see Fitting).
- `tests/autotest/route-signs.yaml`: captures a map in the game's language and another with the option off.
- `tests/autotest/route-signs-language.yaml`: changes the language in the pause menu of `gcan09` and captures the sign behind
  the open menu (already in the new language) and then on the field.

## Signs done

| Map | Sign | en | es |
|---|---|---|---|
| `betl02` | Arena neon | ARENA | ARENA |
| `betl02` | Reception banner | RECEPTION | RECEPCIÓN |
| `frzl01` | Notice next to the stove (illegible lettering in the original: the meaning is written) | NOTICE / Beware of the cold | AVISO / Cuidado con el frío |
| `frzl06` | Crossroads signposts | ICEBERG BEACH / MISTY TREES | PLAYA ICEBERG / BOSQUE BRUMOSO |
| `frzl12` | Ridge signpost, sideways (the same sign as the east one of `frzl06`) | ICEBERG BEACH | PLAYA ICEBERG |
| `gias00` | Lit signpost (sprite page, `clear: transparent`) | (already in English) | PUEBLO FACTORIAL / MTE. INFINITO / MTE. BASURA |
| `gias08` | Sign on the toilet booth (pun) | PUBLIC POO | POPÓ PÚBLICO |
| `gcan09` | Shop sign | MONOCHROME SHOP | TIENDA MONOCROMO |
| `gias06b` | Recycling shop title | RECYCLED GOODS & CARDS | RECICLAJE Y CARTAS |
| `gias06b` | Subtitle and SALE banner | (already in English) | TIENDA DE RECICLAJE Y CARTAS / REBAJAS |
| `mayo01_2`, `mayo02_2` | Arrows to the mountain | MT. PANORAMA | MTE. PANORAMA |
| `mayo00`, `mayo02_2` | Lit signposts (sprite page) | (already in English) | CASA DE JIJIMON / CIUDAD FILE / GIMNASIO VERDE / BOSQUE NATIVO / MTE. PANORAMA / TÚNEL TALADRO |
| `mayo10`, `mayo11` | The anglers' board (the blue scribbles stay) | ANGLER'S CODE and its two rules | CÓDIGO DEL PESCADOR |
| `mayo11` | Tunnel name above the entrance | DRILL TUNNEL | TÚNEL TALADRO |
| `omoc08` | Red graffiti on the brick wall | THE GANG / WAS HERE | LA BANDA / ESTUVO AQUÍ |
| `room02` | Doormat | (already in English) | Bienvenido |
| `room13`, `room19` | Waiting room banner and arena reception sign | WAITING ROOM / RECEPTION | SALA DE ESPERA / RECEPCIÓN |
| `trai00` | Battle course board | BATTLE COURSE | CURSO DE COMBATE |
| `trai00` | Plaque on the stand below | GREEN GYM | GIMNASIO VERDE |
| `twnb10`…`twnb17` | Tool shop plaque and open sign (each phase of the plaza) | (already in English) | TIENDA DE OBJETOS / ESTAMOS ABIERTOS |
| `twnb03`…`twnb06`, `twnb11`…`twnb14`, `twnb18`…`twnb21` | Restaurant name on the counter | (already in English) | RESTAURANTE |
| `twna02`…`twna13`, `topn01`, `tend01`, `tend02` | Item bank plaque next to the farm | (already in English) | BANCO DE OBJETOS |

Pending and gaps: the complete inventory of signs per map (what is left and why) is in the translation
work tracking; the words of the lit map in `frzl03` are cut into many sprite pieces and the overlay
does not yet know how to write a text split up like that.