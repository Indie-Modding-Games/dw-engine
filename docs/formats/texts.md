# Texts and font

How `formats/text` reads the original's texts and font, and how `dwx extract` writes them. Texts are converted to
UTF-8 with readable tags for whatever is not a character, and go back to the same bytes: the conversion loses
nothing and the tests check it in both directions.

## Two encodings

| | Byte strings | Script text |
|---|---|---|
| Where | Executable and overlays (names, menus, descriptions) | `SHOW_TEXTBOX` and `SET_SELECTION` instructions of `SCN` |
| Unit | One byte; from `0x80`, a two-byte Shift-JIS code | Two-byte cells (four for `03`) |
| Drawn by | `font.c: drawString` and `sjis.c: convertAsciiToJis` | `script_textbox.c: showTextbox` and `drawString2` |
| End | The string's NUL | A `0D` cell followed by a `00` byte |

In the US the executable's strings are single-byte ASCII and the script text is all full-width Shift-JIS:
`showTextbox` copies pairs of bytes, so single-byte ASCII does not fit in a script.

## Character set

`charset.hpp` translates between Shift-JIS codes and Unicode with the **game's set**: rows `0x81`–`0x84` of cp932
(symbols, numbers, Latin letters, kana, Greek and Cyrillic; 524 characters) with two changes that the font requires:

| Code | cp932 | Game's set |
|---|---|---|
| `0x8175` | `「` | `’` (U+2019): the font draws the apostrophe there |
| `0x8176` | `」` | `”` (U+201D): the font draws the quotation marks there |
| `0x8166`, `0x8168` | `’` and `”` | no character: their characters move to the codes above |

That leaves 522 characters and the translation is bijective: each character has one code and each code at most one
character. `「` and `」` have no code, because no glyph in the US draws them.

**Why only those rows and not all of cp932.** They cover all the glyphs of the US font and all the codes that
appear in the US data. Kanji (only in the Japanese version) and any other code are not lost: they come out
as a tag (`{sjis:889F}`, `{cell:889F}`) and come back the same. A table of the whole cp932 would have about 7,900
characters that no US text uses.

The table is 752 `u16` cells (4 rows × 188 trail bytes, from `0x40` to `0xFC` without `0x7F`), written in
`charset.cpp` as literals. It is generated deterministically from the standard code page table
(Microsoft's `CP932.TXT`, the same data as WHATWG's Shift_JIS index in these rows): for each cell, the
pair's character or 0 if it has none; the substitutions are applied in code, not in the table. The inverse table is
built on the fly by sorting the forward one.

In script text the US letters are full-width; to make them readable, the decoder writes them as
ASCII (`fullWidthOf` / `asciiOf`): `Ａ` (`0x8260`) is `A`, `0x8140` is the space, `0x8142` is `.`, `0x8175` is `'` and
`0x8176` is `"`. The rest of printable ASCII goes to U+FF01 and following. The other full stop, `0x8144`, stays as
`．` so that the round trip is exact.

## Tags

`{` and `}` are written `{{` and `}}`. What is not a character goes between braces.

Byte strings:

| Tag | Bytes |
|---|---|
| `{byte:XX}` | a control byte (`< 0x20`), `0x7F`, or a Shift-JIS first byte without a second at the end of the string |
| `{sjis:XXXX}` | a two-byte code with no character in the game's set |
| `{color:N}` | `#C` and a digit: the color of the text that follows (`battle_hud.c: BTL_drawBattleEndText`) |
| `{br}` | `#R`: line break (same) |
| `{wait}` | `#W`: wait and line break (same) |
| `{0}`, `{1}`... | each `%d` in order: a number the game writes with `sprintf` (fishing messages). A translation can change their order, because each one carries its number |

The `#` codes are only read by the end-of-battle text (`btl.battle_effect_messages`), and `%d` by the fishing
messages (`fish.messages`), but they are converted in all byte strings: no others have them. A `#` or a
`%` that do not form one of these codes stay as text.

Script text: each control cell is `code, parameter` (`showTextbox` expands names and numbers;
`drawString2` draws colors, tabs and columns):

| Code | Tag | Parameter |
|---|---|---|
| `00` | `{stop}` | ignored; the original stops drawing the page there (see below) |
| `01` | `{color:N}` | color |
| `02` | `{raw02:XX}` | no known consumer; stored as is |
| `03 a b c` | `{raw03:XXXXXX}` | the only four-byte cell; no known consumer |
| `04` | `{num:pstat=P}` | the value of script variable `P` |
| `05`, `06` | `{tamer}`, `{partner}` | ignored |
| `07`, `08`, `09` | `{digimon:pstat=P}`, `{move:pstat=P}`, `{item:pstat=P}` | name from the table at the index that `P` stores |
| `11`, `12` | `{bgm:pstat=P}`, `{tournament:pstat=P}` | same |
| `0A`, `0B` | `{bits}`, `{merit}` | ignored |
| `0C`, `0E`, `0F` | `{tab96}`, `{tab132}`, `{half}` | ignored |
| `10` | `{shopvar}` | ignored; when drawing, the shop's tally (`StoryAdapter::shopTally`) |
| `13`, `14`, `15` | `{tourn_won}`, `{tourn_wins}`, `{tourn_lost}` | ignored |
| `16`–`1C` | `{col16}`... `{col1c}` | fixed US columns; ignored |
| `0D 00` | line break (`\n`) | |
| `0D xx` | `{newline:raw=XX}` | a break with a parameter other than 0 |

An ignored parameter that is not 0 is stored: `{tamer:raw=05}`. A cell that is neither a character nor a known control
is `{cell:XXXX}`. A `00 xx` cell that does not follow a line break is `{stop}` (`{stop:raw=XX}` if `xx` is not 0;
before `ExtractorVersion` 11 it came out as `{cell:00XX}` and looked like a missing glyph): no character is missing.
When drawing it, the original stops: `drawString2` ends at byte 0 and `drawTextboxStrings` draws no more rows of the
page, although `showTextbox` keeps reading to the end and the page passes anyway. The English has eleven
`{stop}` in ten texts, in front of leftovers of a longer line (`story_054.text.0c02` shows "fades away, he'll" and
nothing more; `story_026.text.0262` shows "to get thru here," and not "?nt leave all your stuff!"); dwe leaves the
rows empty from there on (`appendPageRows`, which also accepts the `{cell:00XX}` of old content), measured on the original with
a `00 00` written in the save machine's page: only "Oh that?" comes out and the next page comes out whole.
The Spanish puts the tag at the end when it translates the intent of the line.

**End of text.** The text ends at the first `0D` cell followed by a `00` byte; that cell and the `00 xx` that
follows it are the end. Normally they are `0D 00 00 00` and are not written; if they carry other bytes they are stored with
`{end:XXYY}` at the end. Whether another page follows depends on whether the next instruction is another `SHOW_TEXTBOX`
(`pageReady`), not on the text. The encoder rejects what the original could not read the same way: a cell that starts
with `00` right after a line break, an `{end}` that is not the last tag, a `{cell}` with first byte `03` or
`0D`.

## Font

It is in the executable (`font.c`; US addresses in `findFontLocation`):

- `CHAR_TO_GLYPH_TABLE`: one Shift-JIS `u16` code per glyph; 79 in the US.
- `GLYPH_DATA`: per glyph, 11 `u16` rows and a `u16` width (24 bytes). In each row bits 15–4 are columns
  0–11 and a bit at 0 is ink; bits 3–0 are always 1.
- `FONT_CLUT`: 16 colors of 15 bits.
- The last glyph (the US's 78, code `0x817E`) is the one drawn for a code that is not in the table.

`readFont` fails if the sizes do not match, if a code is repeated or if a glyph does not have bits 3–0 set to 1 (the atlas
would not store them). `dwx extract` writes:

- `font/main.png`: indexed atlas of 12 × 11 cells in rows of 16 (192 × 55 in the US); index 1 = ink,
  0 = background, with the `FONT_CLUT` as the palette.
- `font/main_meta.yaml`: `cell_width`, `cell_height`, `columns`, `fallback`, `colors` (the 16 colors in hex) and per
  glyph `index`, `sjis`, `character`, `ascii` (if it has one), `x`, `y` and `width`.

`fontFromAtlas` rebuilds the font from the two files. The game does not use `formats`: the UI reads those two
files with its own reader (`ui::BitmapFont`) for text with the original look
([UI](../architecture/ui.md#original-look)), and when loading it composes from those glyphs the accented letters and signs
that the font lacks (á, Ñ, ¿, «, ß, æ, œ...); the base package's atlas does not change. The capitals Æ and Œ do not
fit in the 12-pixel cell and come out with the fallback font, like any character the font lacks.

## Executable and overlay strings

Strings are not indexed in any file, so `string_manifest.cpp` lists where they are for each version,
taken from the decomp (`config/us/symbols*.txt` and the blocks of `text/<dir>/<name>.yaml`). It stores only addresses and
counts, never the text. Each block has a unique name in its source and a form:

| Form | Content |
|---|---|
| `Pointers` | `char*[n]`; a null pointer has no string |
| `Records` | `n` records of `stride` bytes that start with `char name[length]` (`DIGIMON_DATA`, `ITEM_PARA`) |
| `Embedded` | `char[n][stride]` |
| `Named` | one string at each address of the list (the loose `STR_*` of a block) |

The overlays (`BTL`, `STD`, `VS`, `KAR`, `FISH`, `TRN`) are read at their load address; a pointer in an overlay
can point to the executable, and it is looked up there if it is not in the overlay. Model codes
(`DIGIMON_FILE_NAMES`) are not text and are not included.

Strings that the decomp names without a known address (three blocks of `BTL` and `VS`) come out as report notes.

## Text tables

`dwx extract` writes one table per source, `text/<language>/<source>.yaml` (`text/en/main.yaml`, `text/en/btl.yaml`...):
a flat mapping from key to text, with no other fields.

```yaml
map_names.0003: "File City"
move_names.0012: "{color:2}Spit Fire"
```

- **Key:** `<block>.<index>`, with the index in four digits (`map_names.0003`). Inside the game the full key
  carries the source in front: `main.map_names.0003`. Block and source names come from the string
  manifest and do not change between extractor versions, so the key is stable: translations use it.
- **Gaps:** a null pointer has no string and no key; the index of the others does not change.
- **Text:** UTF-8 with the tags from [above](#tags). For the language tables, each tag is a marker
  (`{color:2}`, `{digimon:pstat=3}`, `{byte:0A}`): `dwx validate` checks that a translation has the same
  set of markers as the English on each key. `{{` and `}}` are literal braces.
- **Translations:** another language goes in `text/<language>/<source>.yaml` of a translation package, with the same
  keys and nothing else; it can leave out strings (they show in English) and can be partial. Names that stay in
  English are not repeated. A package higher in the stack changes individual keys without copying the table. See
  [localization](../architecture/localization.md).

### A translation's lock

A translation package can carry `text/<language>/.sources.lock` next to its tables. It is not an asset (files
starting with a dot do not enter the VFS); it is read by `dwx validate` from the package folder.

```yaml
sources:
  main.map_names.0003: 0a1b...   # SHA-1 of the English text it was translated from
kept_english: [main.digimon_names.0001, main.digimon_names.0002]
kept_original: [main.tournament_grades.0000]
```

- `sources`: full key of each translated string → SHA-1, in 40 lowercase hexadecimal digits, of its English text
  as `dwx extract` writes it: the table's value in UTF-8, with its tags (`{color:1}`, `{br}`),
  without YAML quotes.
- `kept_english`: the keys that stay in English and are not in the translation (Digimon names).
- `kept_original`: optional; the keys that are not English words and stay as they are (gaps, names of the
  speaker, rank letters, Japanese leftovers, HP/MP/Bits). Separating them from `kept_english` keeps the count of what
  remains in English honest.
- `sources` and `kept_english` are always present; empty they are `{}` and `[]`.

`dwx validate` warns about a string whose English has changed since it was translated (a different SHA-1), a translated string
without a SHA-1 in the lock, a `kept_english` or `kept_original` key that is also translated and a key that
the English does not have.
A translation without a lock is not checked this way.

The schema is `content/schemas/text.schema.json`. The engine still reads the previous format (`source` and `strings`, a
mapping from block to a list of strings or `null`) with the same keys, for content extracted before this change;
`dwx extract` no longer writes it.

Script text does not go into these tables: it is in each script's listing (see [Event scripts](scripts.md)),
and its stable id is the script plus the offset of its instruction.
