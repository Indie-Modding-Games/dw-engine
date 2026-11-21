# Game variants: Classic Enhanced and Folder Mode

A player picks, at each new game, between two experiences that share one engine and one set of packs:

- **Classic Enhanced**: the original game as it was (its story, rules, balance and progression) with the engine's
  technical improvements, comfort options and bug fixes.
- **Folder Mode**: the expanded game: hidden content, every species as a partner, the Ultimate to Mega step, the rare
  items and every option, the ones that change the rules included.

The decision and the alternatives are in [ADR 0026](../adr/0026-game-variants-as-modes.md). This page says what each
variant turns on, how it is chosen and saved, and how co-op, mods and new variants fit.

## What a variant is

A variant is a [game mode](../modules/game/modes.md) with a preset: a `modes/<id>/mode.yaml` manifest that names the
rulesets a new game may start with and, with its optional `options` key, the options of the options page its games
fix at one value. There is no second build, no branch of code per variant and no flag in the systems: the systems read
their [ruleset](rulesets.md) and the player's options as always, and the variant only decides which rulesets the new
game offers and which options stay at the original's value while one of its games is played.

```yaml
# content/packs/game/modes/classic/mode.yaml
id: classic
name: modes.classic_name
description: modes.classic_description
ruleset: original
rulesets: [original]
order: 0
options:
  difficulty: normal
  rare_items: false
  free_exploration: false
  extra_toilets: false
  choose_egg_after_lives_lost: false
```

Folder Mode (`modes/folder/mode.yaml`, order 1) admits the six rulesets of the rules screen and fixes nothing.

## What each one has

Everything the engine does is shared; the variants only differ in the rules a game may use.

| Area | Classic Enhanced | Folder Mode |
|---|---|---|
| Story, maps, systems (field, care, battle, evolution, tournaments, shop job...) | Shared | Shared |
| Engine and technical improvements (HD scenery, smooth motion, lighting, weather, widescreen, sound) | Yes, the player's options | Yes, the player's options |
| Comfort options (autosave, island map, minimap, quick battles, skip evolutions, bulk trades, long names, shop extras...) | Yes, the player's options | Yes, the player's options |
| Bug fixes as options (the four "fair" quirks, the art fixes, Angemon's and Whamon's story fixes) | Yes, the player's options | Yes, the player's options |
| Rules | `original` only | `original`, hidden content (`oculto`), Mega step (`oculto_mega`), every species as a partner (`todas` and its two mixes) |
| Easy difficulty | Fixed at normal | The player's option |
| Rare items in the field | Fixed off | The player's option |
| Free exploration | Fixed off | The player's option |
| Extra toilets | Fixed off | The player's option |
| Choose the egg after losing every life | Fixed off | The player's option |
| Online co-op | Yes (the host's variant rules the shared world) | Yes |
| Online versus | The same for both: it is not a story game | The same |

The line between the two follows the issue's split: an option that fixes the original's bugs or makes it more
comfortable without changing what the player can obtain or how hard it is belongs to both; one that changes the
balance, the progression or the content is Folder Mode's. Every feature players have today stays reachable: it is in
Folder Mode, or in both.

The extras that are not options (hidden content, Mega step, every species as a partner) are rulesets, so Classic
Enhanced leaves them out simply by not admitting those rulesets, and its new game skips the rules screen.

## Choosing a variant

The new game asks the mode first, with the list every skin already draws (`ui/new_game_mode`): Classic Enhanced first,
then Folder Mode, each with a line that says what it is. Classic Enhanced starts right away with the original's rules;
Folder Mode goes on to the rules screen as the new game always did, with every species as a partner on by default.
Back from either list returns to the slot list.

The variant belongs to the save, not to the player or the installation, so the launcher does not choose it: one build,
one download, one set of packs, and each playthrough carries its own variant. A new game the command line starts
without the title (`--ruleset`, the test routes that skip the menus) plays Folder Mode, which fixes nothing.

## Fixed options while playing

When a game of a mode with `options` starts, the game (`apps/game/src/mode_options.hpp`, `ModeOptions`):

- applies those values instead of the player's own: `PlaySession::playSettings()` is the player's settings with the
  mode's values on top, read through the settings file's own parser, since its keys are the options' names;
- publishes them over the player's values, with `options.locked.<option>` set and the mode's name key in
  `options.variant`; the [UI API](../modding/ui-api.md) answers them with `options.locked(name)` and
  `options.variant()`, and `options.set` refuses a fixed option; a fixed value reaches the UI as the option's own
  kind (`true` and `false` as booleans, a whole number as a number, anything else as text);
- refuses any command that would change one (the value is published again);
- when the game ends, publishes back what the UI showed before, so the title's options page shows the player's own
  values again.

The player's settings file never changes: a player who turned the easy difficulty on finds it on in their next Folder
Mode game. The options page groups the fixed rows last in their tab, greyed and without arrows, with the tag "Fixed in
Classic Enhanced" and a help line that says where it can be changed. At the title nothing is fixed.

A mode may only fix the options a game applies from its own settings: the difficulty, the rare items, free
exploration, the extra toilets, the egg after losing every life, the four "fair" quirks, the generous talk range, the
quick battle start, skipping evolutions, a cancel that closes a choice, Angemon's and Whamon's story fixes, the shop
job's mode and the co-op difficulty (`FixableOptions`). A pin on another option, or with a value the setting cannot
take, is left out with its reason in the log, and the others stay.

## Saves

- The session saves the mode in its `mode` section (`id`), as before. A save from before the variants recorded
  `original`, the only mode there was, which fixed nothing: it loads as Folder Mode (`game::modes::currentModeId`), so
  it keeps every option and ruleset it had. A save with no `mode` section loads the same way.
- Save format 4 adds `mode` to the header's summary, so the slot list names each save's variant without reading its
  sections. The migration from format 3 leaves it empty, which the slot list reads as Folder Mode, and the next save
  fills it ([save files](save.md)).
- A save always plays its own variant: there is no way to load a Folder Mode game as Classic Enhanced, or the other way
  round, so the two cannot be mixed by accident. The slot list shows the variant next to each save (after the date in
  the modern layout, after the partner in the summary box of the original layout).
- A save whose mode no pack has any more (a mod's mode, its mod left out) loads with no option fixed, with a warning
  in the log.

## Co-op

A co-op guest plays the host's save, so the guest's game has the host's mode: the host's variant rules the shared
world, and the guest's options page shows the same fixed rows. When the save the guest brought plays the other
variant, nothing is mixed silently: the guest's start notice says the world plays another variant, and the host hears
which player brought it. Both sides compare the saves they already exchange, so the protocol does not change. The
guest's partner and bag go home to its save only when its variant takes everything the host's can give
(`game::modes::takesProgressFrom`: the same mode, or one that fixes nothing and admits every ruleset of the host's).
A Folder Mode guest in a Classic Enhanced world takes its progress home; a Classic Enhanced guest in a Folder Mode
world takes nothing home, and its notice says its save stays as it was, so a Classic Enhanced save never holds what
Folder Mode's rules made. The room list does not name the host's variant yet: that needs the room directory to carry
it (follow-ups). The issue placed multiplayer in Folder Mode; co-op stays
in both variants instead, since it changes no rule of a single game, and taking it from Classic Enhanced would take a
feature from its players. The co-op difficulty (helpers make battles harder) is an option of both.

## Mods and content packs

- The base pack (what `dwx` extracts from the disc) and the game pack are shared by both variants. Content only Folder
  Mode uses comes as rulesets or ruleset sections that only Folder Mode admits (the hidden content, the Mega step, every
  species as a partner, the rare items' `rare_items` section), so there is no second copy of any asset.
- A mod adds a variant with its own `modes/<id>/mode.yaml`: its rulesets and the options it fixes. It replaces one of
  the game's by giving the same folder (`modes/folder/mode.yaml`), like any asset, for example to admit its own
  ruleset in Folder Mode.
- A mod that changes the story or the balance for every game still applies to both variants, as packs are mounted for
  the whole game. Such a mod should ship its rules as a ruleset that only its mode, or Folder Mode, admits.

## Adding a variant

A new variant is a new `mode.yaml` with its rulesets, its fixed options and its two texts (`localization/<language>/`),
nothing else: the list, the save, the slot list and the options page follow. The list shows the first six modes.

## Follow-ups

- Turning a Classic Enhanced save into a Folder Mode one (Folder Mode admits everything Classic Enhanced does, so the
  conversion is safe one way). It needs a choice on the slot list and a save that rewrites its `mode` section.
- The room list and the co-op lobby naming the host's variant before joining (a protocol change).
- Expansions the launcher mounts (the planned story expansion) declaring the modes they play in, so a Classic Enhanced
  game never sees them.
- Fixing graphics or interface options from a mode, which today the game applies outside a game's settings.
