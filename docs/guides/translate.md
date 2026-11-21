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
`kept_name_block` and `kept_original_ids`). A language written in another script may spell the names its own way
(Korean writes them in Hangul, Japanese in katakana; Chinese keeps them in English): as soon as its `main.yaml` has a name of
`main.digimon_names` that is not a title, those names and the strings that are just a name count as translated for
that language and are no longer kept (`own_names`).

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
instead the lines of more than 30 columns (or wider than the widest line of their English) and the texts with more
lines than their English and than the box (4 rows); tags do not count. Lengths are display columns: a Chinese,
Japanese or Korean character counts two. Another language works the same with `--lang <code>`
([A new language](#a-new-language)).

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
- Story dialogue is split into lines with `\n` like the original: up to 30 columns per line not counting tags, and
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

Besides English and Spanish, the game is being translated into Brazilian Portuguese (`pt`), French (`fr`), German
(`de`), Russian (`ru`), Simplified Chinese (`zh`), Korean (`ko`) and Japanese (`ja`). These new languages have not had
a review by native speakers yet: if you play in one of them and a text reads wrong, is unclear or does not fit, you
are welcome to review it. Open an issue on the project's repository with the language, the screen or the line, the
text as it is and how you would write it, or send the fix as a pull request to the language's tables. Each language's
term decisions are in its glossary:
[Deutsch](de-glossary.md), [Français](fr-glossary.md), [Português (Brasil)](pt-glossary.md),
[Русский](ru-glossary.md), [简体中文](zh-glossary.md), [한국어](ko-glossary.md), [日本語](ja-glossary.md).

Adding a language with code `<code>` takes these pieces, all following the Spanish ones.

1. **The engine's texts**: `content/packs/game/localization/<code>/`, with a copy of each table in `localization/en/`
   (`common.yaml`, `pause_menu.yaml`, `shop.yaml`, `first_run.yaml`...) with the same keys and the `{name}` arguments
   as they are. A key the language does not have yet is shown in English, key by key, so a table can be filled bit by
   bit.
2. **The language name and flag**: `name` in its own `language.yaml`, and `native.<code>` and `flag.<code>` in the
   `language.yaml` of **every** language (the language box lists them from there). The flag image goes in
   `ui/flags/<code>`, which `scripts/art/make_flags.py` generates.
3. **The original's texts**: a delta pack `content/packs/<code>-translation` with id `<code>_translation`, like
   `es-translation`:

   ```text
   content/packs/<code>-translation/
   ├─ pack.yaml
   ├─ text/<code>/     main.yaml ... trn.yaml, story_NNN.yaml and the generated .sources.lock
   └─ signs/<code>/    the sign overlays per map, when a sign needs one
   ```

   ```yaml
   id: <code>_translation
   version: 0.1.0
   layer: base
   priority: 2
   name:
     en: "<Language> translation"
     es: "Traducción al <idioma>"
   description:
     en: "The team's own <Language> translation of the original's texts, a delta over text/en. See docs/guides/translate.md."
     es: "Traducción propia del equipo al <idioma> de los textos del original, delta sobre text/en. Ver docs/guides/translate.md."
   dependencies:
     - id: base
       min_version: 0.1.0
   schemas:
     sign_overlay:
       - signs/*/*
   ```

   Nothing registers the pack: the game mounts every folder of `content/packs` that has a `pack.yaml`, and a package
   cooks each one into its own `.pak` (`scripts/package/cook_team_packs.sh`). Without the pack, the game's dialogue
   appears in English.
4. **The translation flow**: the same as for Spanish ([How to translate](#how-to-translate) and
   [How it is updated](#how-it-is-updated)), with the language as an option:
   `scripts/translation.sh --lang <code> missing main`, `--lang <code> lock`, `--lang <code> check`... `--lang` picks
   `content/packs/<code>-translation` and `text/<code>/` (`DWE_TRANSLATION_LANGUAGE=<code>` does the same, and
   `--pack <folder>` points to another pack). `lock` needs the pack's `pack.yaml` to exist. The ids that are not
   translated ([What is not translated](#what-is-not-translated)) are the same for every language.
5. **Coverage**: `scripts/translation.sh coverage` (with `DWE_CONTENT`) prints, per language and per source, the
   strings, the translated ones, the kept ones, the missing and the stale ones and the share done; then, per
   language, its pack and the keys it has of English's in the engine's texts and in the launcher's. A language
   without a pack shows 0%. `coverage fr de` limits it to some languages.
6. **The signs**: the keys in `localization/<code>/signs.yaml` and, if needed, overlays per map in
   `content/packs/<code>-translation/signs/<code>/<map>.yaml` ([signs guide](signs.md)).
7. **The glossary**: `docs/guides/<code>-glossary.md`, with its Spanish copy in `docs/es/guides/<code>-glossary.md`,
   records the language's term decisions as the [Spanish glossary](spanish-glossary.md) does: stages, items, HP and
   MP, what is done with the Digimon names and the techniques.
8. **The launcher**: `apps/launcher/ui/js/i18n/<code>.js` with the keys of `en.js`.
   `node apps/launcher/tools/i18n_check.mjs <code>` counts the missing keys (shown in English) and fails on keys
   English does not have or on `{placeholders}` that differ.

On first start, the disc screen asks for the language if there is more than one: it lists them from the
`localization/` folders that have `language.yaml`, so a new language appears as soon as it brings that table. With a
single language it does not ask. A language with only `language.yaml` shows every other text in English and the
check lets it be; once it brings a second table, it owes all of them.

### Line lengths in Chinese, Japanese and Korean

Every length above is in display columns, as a monospaced terminal counts them: a Chinese, Japanese or Korean
character (hanzi, kanji, kana, Hangul) and the full-width punctuation (`。`, `、`, `！`, `？`) take two. A story line
holds 30 columns, so 15 of those characters, and the box keeps its 4 rows. `long` measures that way, in the story and
in the tables (a translation wider than its English). Chinese and Japanese have no spaces: a line is broken with `\n`
where a native reader would pause, never inside a word.

### Fonts

The classic font composes letters with acute, grave or circumflex accent, diaeresis, cedilla and tilde, the signs
`¡ ¿ « » ‹ › „ ‚ ß º ª … ·` and the ligatures `æ œ` ([Texts and font](../formats/texts.md#font)). Cyrillic, Chinese,
Japanese and Korean are drawn by the engine with its own fonts, which a translator does not need to touch:
[UI fonts](../architecture/ui.md#fonts). `uv run scripts/art/make_fonts.py check` lists any character of the texts
that no font draws; a character outside the fonts' sets needs them rebuilt (`make_fonts.py make`).

### Plurals and names

A word that changes with a count takes the forms of the language: Russian writes three (`{n:день|дня|дней}`), French
and Portuguese use the first for 0 and 1, and Chinese, Japanese and Korean have one (write it twice, `{n:天|天}`); see
[Plurals](../architecture/localization.md#plurals). In the naming box, Russian, Japanese and Korean get pages of their
own letters before the Latin one ([names in every language](../modules/apps/game.md#names-in-every-language)).
