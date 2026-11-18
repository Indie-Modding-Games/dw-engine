# Translating a sign on a map

The signs painted on the maps are written again in the player's language when the map loads (the
[signs](../modules/engine/signs.md) module). A new sign needs two things: its text in the language tables and an
overlay that says where it is and how it is drawn. No image from the game goes into git: everything is painted at
run time from the textures that each player extracts from their own disc.

## 1. Find the sign

```sh
dwe-game --content <folder> --dump-signs gcan09 --shots _work/tmp/carteles --lang es
```

It writes to the folder, for each map:

| File | What it is |
|---|---|
| `<map>-background.png` | The background, in image pixels (the `region` coordinates) |
| `<map>-background-grid.png` | The same with a grid every 16 pixels (stronger every 64) and the signs' regions in cyan |
| `<map>-page<bits>-<n>-clut<c>.png` | Each texture page of the sprites with each palette they use (with its `-grid`) |
| `<map>-sprites.txt` | The sprites (page, palette and texels) and the instances that place them on the background |
| `<map>-signs.txt` | The overlay's signs and a `place("player x z 0")` to go see them in the game |
| `<map>-<language>-background.png` | The background with the signs already painted, to check |

Open the grid image in any editor and note the text's rectangle and, if the sign is in perspective, its four corners.
Most signs are in the background; if it does not appear, look for it in the texture pages.

If in the game the sign is still in the old language, a sprite covers it: look in `<map>-sprites.txt` for the
instance whose rectangle (`at` plus the sprite's size) covers the sign. With palette `-1` the engine already copies
the sign onto it; with another palette, add a sign with `target: {page: <n>, bits: 8, clut: <c>}` and the same
coordinates moved to the page (the sprite's texel plus the distance from `at`).

## 2. Write the text

In `content/packs/game/localization/en/signs.yaml` and `.../es/signs.yaml`, one key per sign (or per line, if the
lines have different colors):

```yaml
gcan09_shop: "TIENDA\nMONOCROMO"
```

`\n` splits the line. A sign that only exists in Spanish still needs the key in English, with the original's text.

## 3. Write the overlay

`content/packs/game/signs/<map>.yaml` for all languages; `content/packs/es-translation/signs/es/<map>.yaml` for a
single one (a sign that already reads fine in English, or one that is drawn differently in Spanish, with the same
`id`).

```yaml
signs:
  - id: shop
    region: [544, 69, 79, 25]
    clear:
      mode: diffuse
      quad: [[545, 69], [623, 75.5], [622, 94], [543, 89]]
    corners: [[547, 71], [621, 77], [620, 92], [545, 87]]
    text: signs.gcan09_shop
    size: 9
    color: "#302828"
```

- `region` is the only thing the sign may touch. `corners` puts the text in perspective; without them the text goes
  straight in the region (with `rotation` and `skew` if needed).
- `clear: diffuse` is the erase that best preserves the board: it removes what does not look like the board's color
  and fills it in from around. If the board has several colors, give its own with `color` and adjust `tolerance`; if
  the erase eats the frame, limit it with `quad`.
- Copy the original's colors (the letters' color, the outline, a neon's halo) and use `colors: region` so that only
  colors the sign already had come out.
- The text shrinks on its own until it fits (`fit`); if it ends up very small, look for a shorter translation.
- A glowing sign made with semi-transparent sprites over a dark board (the posts in `mayo00` or `gias00`) is erased
  with `clear: transparent` on its page (`target`) and `colors: region`.

All the fields are in the [signs module](../modules/engine/signs.md) and in
`content/schemas/sign_overlay.schema.json`.

## 4. Check

1. Run `--dump-signs` again and compare `<map>-background.png` with `<map>-<language>-background.png`.
2. In the game: `--start <map> --lang es` and, in the console, the `place(...)` from `<map>-signs.txt`. The
   **Translated signs** option in the pause menu shows the original for comparison.
3. The route `tests/autotest/route-signs.yaml` takes both captures on its own (add a `script: place(...)` step in a
   copy if the sign is not visible at the start):

   ```sh
   dwe-game --content <folder> --lang es --start gcan09 --autotest tests/autotest/route-signs.yaml --shots _work/tmp/carteles/gcan09-es
   ```

4. `dwx validate` checks the overlay against the schema.

Captures and dumps stay in `_work/tmp`: they are never uploaded.
