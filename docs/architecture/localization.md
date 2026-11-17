# Localization

All text the player sees comes from the packages' language tables, never from the code. The game ships English and
Spanish; a package can add more languages or change individual texts. The language is chosen in the main menu and
changes without restarting.

## Where it lives

`packages/engine/localization`, in the **systems** layer, next to `assets`, which it depends on (and `core`). Reading
and checking the tables (`dwe/assets/text_table.hpp`) are in `assets`.

- It has to be below `ui` and `script` (services), which use it, and within reach of `packages/game/*`, which will be
  able to ask for texts without knowing the UI. That is why it cannot be a service.
- It only needs to read files from the VFS: it does not draw or touch the platform. It fits with the other systems
  that read data from the packages.
- The tables and their checking are in `assets` because `dwx validate` can only use `formats`, `assets` and `core`
  ([dependency rules](dependency-rules.md)). The catalog (current language, fallback, placeholders) belongs to the
  runtime and stays outside `dwx`.
- The system language is read by `platform` (`platform::systemLanguage()`, with SDL) and used by the app.
  `packages/game/` does not see it.

## Two sources of text

| Folder | What it has | Who writes it |
|---|---|---|
| `localization/<language>/<area>.yaml` | The engine's texts: menus, HUD, button prompts, save messages | The team (`content/packs/game`), in `en` and `es` |
| `text/<language>/<source>.yaml` | The original game's text: map names, techniques, items, messages | `dwx extract` the English (base package); translations go in a translation package |

Both use the same format: a flat mapping from key to text, with no metadata.

```yaml
# localization/es/pause_menu.yaml
title.main: "Pausa"
ui_scale.value: "{percent} %"
```

## Keys

A key is segments of `a-z`, `0-9` and `_` joined by dots. The full key is prefixed with the file name:

| Origin | Full key | Example |
|---|---|---|
| Engine | `<area>.<key>` | `pause_menu.title.main`, `prompt.key.enter`, `common.yes` |
| Original game | `<source>.<block>.<4-digit index>` | `main.map_names.0003`, `btl.menu.0087` |

In the engine the area is the document or the part that uses the text (`main_menu`, `pause_menu`, `hud`, `prompt`),
and `common` holds the words that several share. The original's are stable disc ids
([texts](../formats/texts.md#text-tables)): `localization::gameTextKey(source, block, index)` and, for a map's
name, `localization::mapNameKey(loadingName)`. A key cannot be in two tables of the same language.

## Placeholders

A text carries values between braces: `{percent}`, `{count}`. Whoever asks for it gives the values by name; the
numbers that the original wrote with `%d` are `{0}`, `{1}`... and are given by their number. Numbers are written
without decimals if they are integers (`3`, not `3.0`). `{{` and `}}` are literal braces; an unclosed `{` is a table
error.

The original's control codes are readable tags in all languages, also in the extracted English
([full list](../formats/texts.md#tags)). The text returned by `text`, `tr`, `t()` and `text-key` is plain text
for the UI: `{br}` and `{wait}` are a line break, `{color:N}` disappears and the other tags (`{digimon:pstat=3}`)
stay written, so you can see what is missing. Whoever draws the text with colors and waits (the dialog box) reads the
text as is with `Catalog::find`.

### Plurals

A word that changes with a count is written `{count:singular|plural}`: `"{days} {days:día|días}"` gives "1 día" and
"2 días", and `"{days} {days:day|days}"` "1 day" and "2 days". The singular form is used only when the count is
exactly 1 (`localization::takesOne`), the rule of English and Spanish; 0, 1.5 or a value that is not a number take the
other. A form can be empty (`card{n:|s}`). The placeholder counts as that of its argument (`days`) when comparing
languages, so each one puts its own words (`assets::pluralPlaceholderOf`, `localization::pluralWord`). It is used in
the partner's age, the bits and lives in the battle's final panel, and the packs and cards of the bulk purchase.

Each language has to have, in each key, the same set of placeholders as English. This way the word order can change
between languages without touching the code.

## How a text is requested

There is a single lookup, by key, the same everywhere:

| From | How |
|---|---|
| C++ | `localization::tr("pause_menu.title.main")` or `catalog.text(key, arguments)`, with `TextArgument{.name = "percent", .value = "150"}` |
| RML | `<span text-key="main_menu.new_game"></span>`: the UI sets the text when the document loads and again when the language changes |
| UI Lua | `t("pause_menu.ui_scale.value", {percent = 150})`; `ui.languages()` gives the available languages |
| Game Lua (`ScriptVm`) | `t(key[, arguments])` and `texts.language()`, after `bindTexts(catalog)` |
| Minimap | `ui.setMinimap(grid, nameKey)`: the UI looks up the name and looks it up again when the language changes |
| Map name on a map change | The app publishes the key in the `map_title_key` setting (from `FieldScene::nameKeyOf`, the only place that looks up a map's name) and the HUD writes it with `t()`, also in its `on_language` |

A key that the chosen language does not have comes out in English. On load, the log warns about each missing table
and about the missing keys, with a maximum of five keys per warning. The keys that a translation's lock deliberately
leaves untranslated (`kept_english` and `kept_original` of `text/<language>/.sources.lock`) do not count as missing:
the complete Spanish does not warn about anything (it used to count 311). A key that does not even have English comes
out as the key itself (`menu.nothing`), so it shows up on screen. The log messages for developers are not translated:
they are English constants in each module's `messages.hpp`.

## Choosing the language

1. `--lang en|es` on the command line. It is only for autotests: it is not saved.
2. The language saved in the user's settings (`settings.yaml` in the user's data folder, the one from
   `SDL_GetPrefPath`), if the packages still have it.
3. The system language (`SDL_GetPreferredLocales`), if the packages have it. If not, English.

The player chooses it once, on the first start's screen or in the language box that the original look shows until
one is chosen, and changes it later in the
"Language" row of the Interface tab of the options, from the main menu or from the pause menu (next to
"Original-look text", [original look](ui.md#original-look), which is saved in the same settings). It is used with
mouse, keyboard and gamepad. Left and right, or confirm, move to the next language. That choice changes the whole
game, also the original's text when there is a translation package, and is saved in the settings. An autotest neither
reads nor writes the user's settings.

The menu sends the `language` command with the language. The app (`LanguageChanger`) does this:

1. It loads the new language's catalog. If that fails, the game stays in the previous language.
2. It publishes the `language` setting.
3. It calls `Ui::refreshTexts()`: it sets each `text-key` and the map name again, and calls
   `on_language(document)` on the visible documents so that their Lua rewrites what it set with `t()`.
4. It saves the settings.

A hidden document is updated when it is shown, in its `on_show`.

## Packages, new languages and mods

- **New language:** a package with `localization/<language>/` and all of English's areas appears in the main menu's
  selector. `localization/<language>/language.yaml` has `name`, the language's name in that language.
  `text/<language>/` translates the original's text and can be partial: what is missing comes out in English.
- **Changing a text:** a package higher in the stack with the same file and only the keys it changes. The tables are
  merged key by key from the bottom up (`assets::readTextTable`), unlike the rest of the assets, where the whole file
  wins.
- **Translation of the original:** the Spanish of the game text goes in `content/packs/es-translation`
  (`text/es/<source>.yaml`). The Digimon names stay in English and are not repeated in those files: the translation's
  lock (`text/es/.sources.lock`, see [texts](../formats/texts.md#a-translations-lock)) lists them and stores the
  SHA-1 of the English of each translated string. How to translate: [translate](../guides/translate.md). The style
  decisions of the design lead (2026-10-20: items are translated, the player is addressed informally, `HP` and `MP`
  do not change, stages in Spanish, the screens in columns use the engine's own keys, the Spanish letters on the name
  screen arrive with the engine's UI, and where the original's English is broken the intent is translated) are in the
  [glossary](../guides/spanish-glossary.md#design-lead-decisions).
- **Manifests:** the `name` and `description` of a `pack.yaml` or a `ruleset.yaml` are not keys of these tables, since
  tools read them without mounting the pack. Each is one text, or a mapping of language codes to texts that has `en`:
  `name: { en: "Game data", es: "Datos del juego" }` (`assets::LocalizedText`; `in(language)` falls back to English).

## Checks

`dwx validate` compares each language with English (`assets::checkTranslations`, in addition to `checkContent`):

- Each table in `localization/en` has to be in every language that has engine tables, with the same keys.
- An extra key, or a key with other placeholders, is an error in both sources. In `text/` a key may be missing.
- A table without its English table is an error, and so is a key repeated in two tables of the same language.
- The lock of a translation of the original: strings whose English changed since they were translated, strings
  without a SHA-1 and keys that English does not have.

The tests: `dwe_assets_tests` (`text_table_tests`) for the format, the placeholders and the check;
`dwe_localization_tests` for loading, the fallback, the merge of packages and the hot switch, and for making sure that
each language of the game package has the same keys as English in both directions (`catalog_tests`);
`dwe_ui_tests` and `dwe_script_tests` for `text-key`, `t()`, `on_language` and the main menu.

Rules so that everything comes out in the chosen language:

| Rule | How it is checked |
|---|---|
| No RML document writes words between tags (except `<title>`): it uses `text-key` | `dwe_ui_tests` (`literal_text_tests`) |
| No UI Lua passes words in quotes to `set_text`: it uses `t(key)` | `dwe_ui_tests` (`literal_text_tests`) |
| The original's drawn words (MENU, COUNT, FINISH!!, PUSH...) only come out in English: the UI puts the `use-menu-art` class on each document's `body` while the language is that of those drawings, and the look paints the key's text without it | `dwe_ui_tests` (`menu_art_tests`) |
| The banner that opens a battle draws the word of `battle.banner_shape` (`#` cells of the original's pieces); English gives the original's same pieces | `dwe_screens_tests` (`battle_intro_tests`) |
| RmlUi's `text-transform: uppercase` only raises a-z: the font also raises accented letters and ñ in an already uppercase text (`finishUppercase`) | `dwe_ui_tests` (`uppercase_tests`) |
| In C++ only the logs for developers are English literals; what the player sees goes through `tr()` | Review (there is no cheap test) | The route
`tests/autotest/route-language.yaml` runs once per language (see [vertical slice](../guides/vertical-slice.md)).

## Adding a text

1. Choose the area (the document that shows it, or `common`) and a key: `pause_menu.save_done`.
2. Add it to `content/packs/game/localization/en/<area>.yaml` and to `.../es/<area>.yaml`, with the same
   placeholders.
3. Use it: `text-key="pause_menu.save_done"` in the RML, `t("pause_menu.save_done")` in Lua or
   `localization::tr("pause_menu.save_done")` in C++.
4. `dwx validate --engine content/packs/game --with <base>` checks that both languages match.

## Legal note

The engine's texts (`localization/`) belong to the team and go in git. The original's text does not: the English is
extracted by `dwx` from the user's disc and never enters the repository, and the translation's lock only stores its
SHA-1. What has no translation comes out in English, and always the Digimon names. The translation of the original
into Spanish is a derivative ([assets](../legal/assets.md)).

## Fonts

The UI font (Noto Sans) covers Spanish: `ñ`, accented vowels, `ü`, `¿` and `¡`. A language with another alphabet
needs a font that covers it in its package.
