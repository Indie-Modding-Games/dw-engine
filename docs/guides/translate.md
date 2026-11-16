# Translating the original's texts

The project has its own Spanish translation of the original game's texts. This guide says where it lives, how it is
written and how it is kept up to date when the extracted English changes. The terms that repeat are in the
[translation glossary](spanish-glossary.md).

## Where it lives

In the [`content/packs/es-translation`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/packs/es-translation/pack.yaml) pack (id `es_translation`),
which depends on `base`. It is a **delta**: it only carries Spanish. The English comes from each user's disc
(`dwx extract`), and no text from the original goes into git ([legal policy](../legal/assets.md)).

```text
content/packs/es-translation/
├─ pack.yaml
└─ text/es/
   ├─ main.yaml       one per text/en source: main, btl, std, vs, kar, fish, trn
   ├─ ...
   ├─ story_049.yaml  one per event script: the story dialogue (story_000 to story_221)
   └─ .sources.lock   generated: which English each translation comes from and what is left in English
```

Each file is a flat mapping from key to text, with the same keys as `text/en/<source>.yaml`:

```yaml
"item_names.0013": "Venda"
```

The key is `<block>.<slot>`: the block from the string manifest and the index of the slot within it, with four
digits. The full id has the source in front (`main.item_names.0013`, see [Texts and font](../formats/texts.md)). In
the story tables the key is `text.<offset>`, the offset of the script's order in four hexadecimal digits
(`"text.3b66"`, full id `story_000.text.3b66`, see [Event scripts](../formats/scripts.md#lua)). A missing key is
shown in English.

## What is not translated

They are not written, because they would be copies of the original. The `.sources.lock` lists them, so that coverage
tells "left this way on purpose" from "still to translate":

- `kept_english`: the Digimon names (`main.digimon_names`), which stay in English. The technique names
  (`main.move_names`) are translated (the design lead's decision, 2026-10-21). The titles in English words in that
  block, which are not names, are translated (`translated_name_ids`): `Market Manager` and `main character`, the
  name that appears in place of the player's while they have none (an unnamed game).
- `kept_original`: what is not English words and stays as it is: the blank strings, those that, without the spaces on
  the sides, are the name of a Digimon (the speaker's line in curling), single letters, the Japanese leftovers the US
  version does not show, the abbreviations `HP`, `MP` and `Bits` and the proper name `Overdell`.

The rules are at the top of `scripts/translation.sh` (`kept_english_blocks`, `kept_original_blocks`,
`kept_name_block` and `kept_original_ids`).

## How to translate

1. Extract the disc (`dwx extract`) and point `DWE_CONTENT` to the content folder.
2. See what is missing: `scripts/translation.sh missing main` writes each id with its English **in the terminal**.
   That English is never pasted into the repository. `missing story_049` gives a story table and `missing story`,
   all of them.
3. Write the Spanish in `text/es/<source>.yaml` following the [glossary](spanish-glossary.md) and the rules below.
4. `scripts/translation.sh lock` records in the `.sources.lock` the SHA-1 of the English of each new id.
5. `scripts/translation.sh check` has to end with `0 problems`.

`status` gives the coverage per source (the story tables add up to one row; `status story` gives one per table) and
`long` lists the translations that are longer than their English, to check that they fit. In the story, `long` lists
instead the lines of more than 30 characters (or wider than the widest line of their English) and the texts with more
lines than their English and than the box (4 rows); tags do not count.

### Text rules

- Neutral Latin American Spanish, addressing the player informally (tuteo).
- When the original's English is broken, the intent is translated and all the control codes are kept (the design
  lead's decision, 2026-10-20; the other decisions are in the [glossary](spanish-glossary.md)).
- Tags and markers are copied as they are and do not change in number: the tags in
  [Texts and font](../formats/texts.md#tags) (`{color:N}`, `{br}`, `{wait}`...) and the numbers `{0}`, `{1}`
  that the original wrote with `%d`, which can change order. `check` and `dwx validate` verify it.
- Strings with space padding (the map names centered in 24 characters) keep the width.
- Strings that are one line of a box (those of fishing or curling) do not exceed the length of the original's lines,
  about 29 characters.
- Story dialogue is split into lines with `\n` like the original: up to 30 characters per line not counting tags, and
  if possible the same lines as the English (the box has 4 rows). The first line that names the speaker
  (`{color:7}<name> {color:1}\n`) is copied the same, with the space before the tag.

### Checking that it fits

The engine draws these texts with its own fonts (Noto Sans in RmlUi), which have accents, `ñ`, `¿` and `¡`; the
original's font does not have them, so no translated text can go through it. With `--lang es` it is checked with
captures that these fit:

- the item names in the inventory and in the "item found" box (about 96 px in the original);
- the item descriptions (one line);
- the end-of-battle text (two lines of about 156 px: «soltó», «se lastimó», «¡aprendida!» and the orders);
- the shouts and names of the battle orders;
- the technique names in the battle orders menu and in the end-of-battle box;
- the map names of the `species_e` transport and the jukebox's themes (25 characters in the original);
- the curling help pages and the fishing boxes (lines of about 244 px);
- the names and descriptions of the medals, the tamer titles and the memory card menus.

The strings the original splits into columns by pixel position (`kar.menu.0000`, `main.game_menu_year_day.0000`,
`main.main_menu_save_labels.0001`, `main.script_menu_trade_label.0000` and the labels of the status, techniques and
player views) are translated keeping each column's width in characters, but they only make sense with the original's
drawing. The engine's UI uses its own localization keys for those screens, and these translations are left only as a
fallback (the design lead's decision, 2026-10-20).

## How it is updated

The `.sources.lock` keeps, for each translated id, the hexadecimal SHA-1 of the English text it came from (the UTF-8
bytes of the text as it is in `text/en`, with its tags). If a new extraction changes that English, the hash no longer
matches and the translation becomes **stale**:

1. `scripts/translation.sh stale` lists the stale ids with their new English.
2. The Spanish of each one is reviewed.
3. `scripts/translation.sh accept <id>...` records the new hash.

`lock` never changes the hash of an id that is already in the lock, so a stale translation stops being stale only with
`accept`. `dwx validate` reads the same lock and warns about what is stale.

## Script text

The dialogue of the event scripts (`show_textbox` and `set_selection`) appears in `text/en/story_<n>.yaml`, one table
per script, since the scripts moved to Lua; it is translated with the same flow in `text/es/story_<n>.yaml`. The
names of the characters who speak or are named stay in English, letter by letter.

## Map signs

The signs painted on the backgrounds (the arena, the posts, the shops...) are also translated. Their texts are keys
in `localization/<language>/signs.yaml` of the `game` pack, like the engine's other texts; what changes is where and
how they are drawn, which goes in an overlay per map. A sign that the original already has in English and is only
needed in Spanish goes in `content/packs/es-translation/signs/es/<map>.yaml`. How to find a sign and write its
overlay: [signs guide](signs.md).

## A new language

The game is prepared for more languages than Spanish and English, but a language is only published when there is a
translation made by a person: nothing is machine-translated. Whoever translates into a new language (code `fr`, for
example) needs:

1. **The engine's texts**: a `content/packs/game/localization/<code>/` folder with a copy of each table in
   `localization/en/` (`common.yaml`, `pause_menu.yaml`, `shop.yaml`, `first_run.yaml`...) with the same keys and the
   `{name}` arguments as they are. A missing key is shown in the fallback language.
2. **The language name**: `name` in its `language.yaml`, and `native.<code>` and `flag.<code>` in the `language.yaml`
   of **all** the languages (the language box lists them from there).
3. **The flag**: its image in `ui/flags/<code>`, which `scripts/art/make_flags.py` generates.
4. **The original's texts**: a delta pack like `content/packs/es-translation` with `text/<code>/`, its
   `.sources.lock` and the same flow as above (`scripts/translation.sh`); without it, the game's dialogue appears in
   English.
5. **The signs**: the keys in `localization/<code>/signs.yaml` and, if needed, overlays per map
   ([signs guide](signs.md)).

On first start, the disc screen asks for the language if there is more than one: it lists them from the
`localization/` folders that have `first_run.yaml`, so a new language appears as soon as it brings that table. With a
single language it does not ask.

The classic font composes letters with acute, grave or circumflex accent, diaeresis, cedilla and tilde, the signs
`¡ ¿ « » ‹ › „ ‚ ß º ª … ·` and the ligatures `æ œ` ([Texts and font](../formats/texts.md#font)). What it does not
compose (Æ, Œ, other alphabets) comes out with the fallback font: it is visible, but in a different style. A language
with its own alphabet also needs glyphs or a new font; that is not done.
