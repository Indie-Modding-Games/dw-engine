# Game variants: Classic Enhanced and Folder Mode

A player picks, at each new game, between two experiences that share one engine and one set of packs:

- **Classic Enhanced**: the original game as it was (its story, rules, balance and progression) with the engine's
  technical improvements, comfort options and bug fixes.
- **Folder Mode**: the expanded game: hidden content, every species as a partner, the Ultimate to Mega step, the rare
  items and every option, the ones that change the rules included.

The mode is chosen when the new save is created. That choice replaces the old step of activating hidden content and
defines the **save type**: Classic Enhanced or Folder Mode, recorded in the save from the first file onward.

The decision and the alternatives are in [ADR 0026](../adr/0026-game-variants-as-modes.md). This page says what each
variant turns on, how it is chosen and saved, and how co-op, mods and new variants fit. It also records the module and
resource layout, the build and distribution strategy, the systems that still need refactoring and the task plan.

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

## Feature matrix

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

In short: Classic Enhanced is the original experience in everything that is gameplay — story, rules, balance and
progression — with only technical, comfort and fix improvements on top. Folder Mode is the home of everything that
changes gameplay: extra content, rule changes, balance and progression changes, and any new system or mechanic.

The extras that are not options (hidden content, Mega step, every species as a partner) are rulesets, so Classic
Enhanced leaves them out simply by not admitting those rulesets, and its new game skips the rules screen.

## Choosing a variant

**Design requirement:** the mode selector replaces the old "activate hidden content" step of new-save creation. The
player chooses the mode when the new save file is created, and that single choice defines the save type: Classic
Enhanced or Folder Mode. Hidden content is not a separate switch; it is part of Folder Mode's rulesets. The mode is
written to the new save at once and cannot be changed later, so every save always plays the variant it was created
with.

The new game asks the mode first, with the list every skin already draws (`ui/new_game_mode`): Classic Enhanced first,
then Folder Mode, each with a line that says what it is. Classic Enhanced starts right away with the original's rules,
which have no hidden content. Folder Mode is the expanded save; its ruleset list, where it is still shown, only picks
which Folder Mode ruleset the save starts with. In the intended flow it never asks whether hidden content is on: the
mode has already decided that. The current `ui/new_game_rules` still carries the legacy hidden-content rows; folding
them into the mode choice is [task 10](#task-plan). Back from either list returns to the slot list.

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

- The mode and the save type are decided in the same step as the new save's creation. The mode selector replaces the
  old "activate hidden content" question: choosing Classic Enhanced or Folder Mode writes that id to the first save,
  and no later step asks to activate hidden content or switches an existing save's type.
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

## Module and resource layout

The variant logic is deliberately thin: it lives at the edges of the game, not in the systems that play it.

| Layer | Where | What it owns |
|---|---|---|
| Rules and data | `content/packs/game/modes/<id>/mode.yaml`, `content/schemas/mode.schema.json` | Which rulesets a mode admits, its default ruleset, its position in the list and its fixed options. No code per mode |
| Mode catalog | `packages/game/modes` (`ModeCatalog`, `parseGameMode`) | Reads and validates manifests, checks that every ruleset exists and sorts the list. A broken mode is skipped and logged |
| New game | `apps/game` (`title_screen`, `NewGameModes`) | Asks the mode at new-save creation: that choice defines the save type and replaces hidden-content activation; the legacy rules screen still needs to be folded into it (task 10) |
| Play session | `apps/game` (`PlaySession`, `ModeOptions`) | Applies the mode's pins over the player's settings, publishes them as locked, refuses changes and restores the player's values on exit |
| Saves | `packages/session`, `packages/game/save` | Writes the mode in the `mode` section and in the format-4 summary; migrates older files. `save` knows no variant names |
| Co-op | `apps/game` (`coop_*`, `online_rooms`) | Compares the guest's save with the host's mode and decides which progress may go home |
| Systems | `packages/game/*` (field, care, battle, evolution, story, shop...) and `packages/engine/*` | Read their ruleset and their settings as always; they never ask which variant is active |

Content is organized by what it is, not by variant:

```text
content/packs/game/
├─ modes/classic/mode.yaml      # original only, five pinned options
├─ modes/folder/mode.yaml       # six rulesets, no pins
├─ rules/original/...           # shared rules, including the approved deviations
├─ rules/oculto/...             # Folder Mode's hidden content
├─ rules/oculto_mega/...        # Folder Mode's Mega step
├─ rules/todas*/...             # Folder Mode's every-species rulesets
└─ save/settings.yaml           # shared slots; autosave is a ruleset's rule
```

The base pack extracted by `dwx` and the rest of the game's assets are shared. A pack higher in the
[stack](vfs.md) can replace a mode manifest or add a ruleset; because the stack composes tables entry by entry, two
variants do not need two copies of a table. An exclusive resource is one that only a Folder Mode ruleset references,
not a duplicate folder.

## Build and distribution

There is one binary, one set of packs and one download. The variant is data inside the game pack and an id inside each
save, not a build target, a launcher setting or an install-time choice
([ADR 0026](../adr/0026-game-variants-as-modes.md)).

- **Build:** the same CMake targets and presets produce the game for every platform. There is no compilation flag,
  preprocessor symbol or variant-specific target: adding a mode does not rebuild the systems.
- **Content:** the `base`, `game`, remaster and translation packs are mounted for both variants. Folder Mode's extra
  rulesets are additional data, not a second copy of the base pack. A mod ships its `modes/<id>/mode.yaml` and its
  rulesets in its own pack.
- **Install and update:** the [launcher](../modules/apps/launcher.md#publishing-a-release) publishes one artifact for
  every merge into `main` (Testing) and promotes the tested one to Stable. Both variants are updated together; they
  have no independent version numbers. Pack `version` and `dependencies`, together with `dwx validate`, keep a mod's
  ruleset and the manifest that uses it in step.
- **Visual identity:** the new-game list names and describes each variant, the slot list shows the variant from the
  save's summary, the options page marks pinned rows as fixed in the active variant, and mods and skins read
  `options.variant()`. There is no different icon or executable.
- **Independent distribution:** it is not needed today and is deliberately not built. If a variant ever needs its own
  release cadence, the seam is its mode manifest and the packs it names: ship those separately and mount them on the
  same binary. No game code is copied or forked.

## Systems that need refactoring

The first version met the design's goals without changing the systems. The limits below are where a third variant or
an expansion would be felt; they are ordered by dependency.

| # | Current system | Limit | Refactor |
|---|---|---|---|
| 1 | `FixableOptions` and `ModeOptions::withPins` (`apps/game/src/mode_options.hpp`) | The options a mode may pin are a C++ array and their values are checked with a YAML round-trip through the settings parser; a new fixable option needs code | Give each setting its own fixability and value-domain metadata (schema or declaration) and validate pins once against it. The mode manifest stays the only description of a variant |
| 2 | Pack manifest (`content/schemas/pack.schema.json`) | A pack cannot declare which modes it plays in, so an expansion aimed at Folder Mode is still mounted in a Classic Enhanced game | Add an optional mode compatibility list to `pack.yaml`, resolve it with the pack's `dependencies` and hide a pack whose modes do not include the save's variant |
| 3 | Room directory and co-op lobby (`apps/game/src/online_rooms.cpp`) | The host's variant is only known from the save after joining, so the room list cannot show it | Carry the mode id in the room's directory entry and in the join request, and check compatibility before joining. This is a protocol change ([co-op](multiplayer.md)) |
| 4 | `currentModeId` and `ModeNotMounted` (`packages/game/modes`, `apps/game/src/play_session.cpp`) | A missing, legacy or now-unmounted mode silently becomes Folder Mode with no options fixed: right for old saves, indistinguishable from a typo or a removed mod | Log the reason for the fallback and offer a one-way Classic Enhanced → Folder Mode conversion on the slot list; keep the load itself permissive |
| 5 | `ShownModes` (`apps/game/src/new_game_mode.hpp`) and the command line | The list shows only the first six modes and there is no `--mode` switch: enough for two, not for a catalog of variants | Paginate the list and add `--mode` for routes and tests, keeping Folder Mode as the default when the title is skipped |
| 6 | Graphics and interface options (`apps/game/src/graphics_options.hpp` and the skins) | They are applied outside a game's settings, so a mode cannot pin them | Put their values under the same option metadata as the game's options, then allow pins when the manifest's value domain covers them |
| 7 | Mode manifest (`GameMode`) and session mounting | A mode chooses rulesets and pins, not which systems run: an arena still mounts the story's field and care systems (#118) | Extend the manifest with the systems a game activates and have the session mount that set. Keep the key optional so today's modes do not change |
| 8 | New-game rules screen (`ui/new_game_rules`) | It still asks to activate hidden content after the mode has been chosen, so the mode selector does not yet replace that step | Fold hidden-content activation into the mode choice; keep at most the Folder Mode ruleset pick, and write the chosen mode to the first save |

## Migration from the old architecture

Before variants the game had one implicit mode (`original`): the title went straight to the rules screen, options were
global to the player and a save only carried `ruleset`. The migration was done in layers, so old games keep playing.

| Step | Then | Now | Compatibility rule |
|---|---|---|---|
| Identify the variant | No id in the save; `original` was the only mode | The session writes the mode id in its `mode` section, which the save already had | An empty or `original` id loads as Folder Mode (`currentModeId`) and keeps the game's rulesets and options |
| Name it in the slot list | The summary had no variant | Save format 4 adds `summary.mode` | 3 → 4 fills it empty; the slot list reads empty as Folder Mode and the next save writes it |
| Keep the player's settings | Options changed globally | `ModeOptions` overlays pins for the length of a game and never writes the settings file | The player's own choices come back when the game ends; a new Folder Mode game sees them |
| Separate rules from comfort | One mixed options page | The mode manifest only pins balance, progression and content options; bug fixes and comfort options stay in both | Every feature remains reachable in at least one variant |
| Replace hidden-content activation | A new game asked whether hidden content was on | The new-save mode selector defines the save type; hidden content comes from Folder Mode's rulesets | The legacy rules screen is kept only until task 10, never as a way to switch an existing save's type |
| Add new variants | A new game meant a new build or a new branch | A new `mode.yaml` plus its two localization keys | No code is duplicated: the catalog, save, slot list and options page follow the data |

The chains are forward only: a current game reads old saves, but an older game does not read a format-4 save or a
section it does not know ([save files](save.md#versions-and-migrations)). A player who returns to an old build needs
the newer one again, not a downgraded save.

## Task plan

The work is split so that each step can be reviewed and released on its own. The first four steps are in the current
code; the rest are the open follow-ups, mapped to the refactors above.

| # | Task | Deliverable | Status |
|---|---|---|---|
| 1 | Define the two experiences and their boundary | This page, the feature matrix and [ADR 0026](../adr/0026-game-variants-as-modes.md) | Done |
| 2 | Shared mode infrastructure | `mode.schema.json` with `options`, catalog validation, new-game mode list, per-game mode id | Done |
| 3 | Classic Enhanced preset and pinned options | `modes/classic/mode.yaml`, `ModeOptions`, locked rows in the options page, `--ruleset` routes | Done |
| 4 | Saves and co-op safety | Format 4 summary, legacy mapping, slot label, host's variant in co-op, one-way progress rule | Done |
| 5 | Harden option pins (refactor 1) | Option metadata, pins validated in one place, a mod can pin a new option without a code change | Open |
| 6 | Mode-aware packs (refactor 2) | `pack.yaml` compatibility list and validation at mount; expansions cannot leak into Classic Enhanced | Open |
| 7 | Announce the variant before joining (refactor 3) | Mode id in the room directory and join request, with protocol tests | Open |
| 8 | Slot conversion and save diagnostics (refactor 4) | One-way Classic Enhanced → Folder Mode action that copies the save, with an explicit fallback reason in the log | Open |
| 9 | Scale the mode list and tooling (refactors 5–7) | Pagination, `--mode`, system activation in the manifest, pin support for graphics and interface options | Open |
| 10 | Mode selector defines the save type (refactor 8) | New-save mode choice replaces `ui/new_game_rules` hidden-content activation; the chosen mode is written to the first save and no later step asks to activate hidden content | Open |

Task 5 unblocks mods that add variants; tasks 6 and 7 are independent of each other; task 8 can ship once the
conversion rules are agreed. Task 10 closes the gap between the current rules screen and this page's required new-save
flow. Every task keeps the two rules of this design: systems stay variant-agnostic and a save always plays the variant
it was started with.
