# Rulesets

The game rules are data ([ADR 0010](../adr/0010-own-rules-with-rulesets.md)): a ruleset gathers the numbers and the
choice of strategies for each system, and lives in a content package. The first one, `original`, reproduces the
original game ([ADR 0022](../adr/0022-first-ruleset-faithful-to-the-original.md)). This document describes the
format, where the code that reads it is and how each system uses it.

## Where it is

| What | Where |
|---|---|
| Code | [`packages/engine/ruleset`](../modules/engine/ruleset.md) (`dwe::ruleset`), services layer |
| `original` ruleset | [`content/packs/game/rules/original/`](https://github.com/Indie-Modding-Games/dw-engine/tree/main/content/packs/game/rules/original) |
| Schemas | `content/schemas/ruleset.schema.json` (manifest) and `ruleset_<section>.schema.json` (one per section) |
| Schema rules | `schemas:` of [`content/packs/game/pack.yaml`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/packs/game/pack.yaml) |

**Why a services module and not `packages/game/ruleset`.** All the game's systems (partner, battle, inventory, saving)
read their section of the ruleset, and rule 4 of the [dependency rules](dependency-rules.md) forbids the modules in
`packages/game/` from using each other. The ruleset core knows nothing about care or battle: it only reads YAML,
follows `extends`, counts revisions and creates strategies by name. That is a service, like `script` or `ui`, and the
modules in `packages/game/` can already use services. No exception to the rules is needed. The game clock lives in
the same module because the partner, battle, the field and the autotest share it, and its section is part of the
ruleset.

## Format

A ruleset is a `rules/<id>/` folder of a package:

```
rules/original/
├─ ruleset.yaml      manifest
├─ clock.yaml        one section per file
├─ care.yaml
└─ *.lua             strategies in Lua (later)
```

### Manifest

```yaml
id: original                 # the folder's name
name: "Original"             # one text, or one per language (see below)
version: 0.1.0
description:                 # optional
  en: "The original game's rules..."
  es: "Las reglas del juego original..."
extends: original            # optional: another ruleset it starts from
tables:                      # the tables it uses, by role; asset ids from the packages
  digimon: data/digimon
  raise: data/raise
sections: [clock, care]      # the sections this ruleset defines
sections_from:               # optional: sections it takes from another ruleset folder
  all_species: todas
scripts: [evolution.lua]     # optional: Lua files in the folder
```

- **Ids and names:** lowercase letters, digits and `_`. The id has to match the folder.
- **Name and description:** text for players and designers. Each is one text for every language, or a mapping of
  language codes (`en`, `es`, `pt_br`) to texts that has `en`: a language the mapping does not have shows the English
  text. The engine's rulesets give both English and Spanish. Logs and the tuning panel show the English one.
- **Tables:** the per-Digimon, per-technique or per-item tables are not copied into the ruleset. The ruleset says
  which table from the packages fills each role; `original` uses those from the base package that `dwx` extracts, and
  another ruleset can bring its own in its package. Each table has to exist when loading. `dwx validate` checks a
  ruleset's ids (`entryReference`) against the default id table of their role (`data/items`, `data/moves`,
  `data/maps`), not against the one the ruleset puts in its place; the session checks that one when loading.
- **Sections:** each one is `rules/<id>/<section>.yaml` and has its `ruleset_<section>` schema, with its rule in
  `schemas:` of the `pack.yaml` (`ruleset_care: [rules/*/care]`). Whoever adds a section adds both.
- **`sections_from`:** section → ruleset whose `rules/<ruleset>/<section>.yaml` is read as its own, without being in
  its `extends` chain. This way `todas_oculto` (which extends `oculto`) shares the `todas` file without copying it. It
  follows the `extends` rule: if a ruleset in the chain names the same section, the nearest one wins.

### `extends`

A ruleset can start from another and change only a part: its sections **replace whole** those of the other, its
tables replace those of the same role, and its scripts are added. What it does not define comes from the other. This
way each section file is complete and is validated on its own with its schema. The chain is followed up to 8
rulesets, and a cycle is an error.

```yaml
# rules/dificil/ruleset.yaml
id: dificil
name: { en: "Hard", es: "Difícil" }
version: 0.1.0
extends: original
sections: [care]             # its care.yaml replaces original's; the clock and the tables are original's
```

### Per-species patches

`extends` replaces whole tables, and a whole evolution or raising table is disc data that cannot be uploaded to the
repository. A ruleset that adds partners (the [hidden content](../modules/game/evolution.md#hidden-content)) says
only what it adds in the `species_extra` section (schema `ruleset_species_extra`), by species id, and
`dwe::ruleset::loadPatched{Digimon,Raise,Evolution}Table`
(`packages/engine/ruleset/include/dwe/ruleset/species_extra.hpp`) applies it on top of the tables the ruleset names:

| Field | What it does |
|---|---|
| `requirement` | Its requirements row (`EVOLUTION_REQS` format: minimum stats, errors, weight ± 5, an extra) |
| `gate` | What has to be met before scoring: `triggers` (story triggers), `hours` (`from`/`to`, time of day; wraps around midnight if `to < from`) and `cup` (cup number in `data/tournaments`, won at some point: the trigger 15 + cup that the tournaments set; open where there are no tournaments) |
| `evolves_from` | Forms that evolve into it: it takes the first free slot of `to` after each one's last destination, and the row grows if none are left |
| `evolves_to` | Its own destinations, if it has no paths row |
| `stats_gains` | Its stats reference row (`EVOLUTION_STATS_GAINS`), or `{copy: <id>}` to take another species' |
| `raise` | Raising row copied from another species (`copy`) with changed values (food, weight, energy, hunger hours) |
| `moves` | Techniques in the empty slots of its species row; or `{copy: id}`, another species' whole row (the disc's), before those slots: the form whose [partner clips](../modules/engine/animation.md#partner-clips) come from that species plays the technique clip of the same slot |
| `level` | Its level in its species row (1 Fresh to 5 Ultimate), for a species that the table leaves at 0 |
| `partner_type`, `partner_moves`, `effect_type` | The type it fights with as a partner, partner-only technique values (`power`, `iframes`) and the species whose effects its techniques use |
| `clips`, `field_scale` | Original clips it lacks → the one that plays in their place, and its size in the field and in battle (the partner fights with its radius and height at that size) |
| `chart` | Its tree cell: `x`, `y` (tree pixels; outside its box's grid, or in an occupied slot, it goes to that box's first free slot, on the next page if the box is full), `icon` (another species' cell) and `palette` |

An id that the table does not have, or a `copy` of a species without a row, is an error when loading. The paths rows
have the disc's 5 source slots and 6 destination slots, and a patch lengthens them instead of failing. The
`mega_step` section (schema `ruleset_mega_step`) is read alongside it with `readSpeciesExtra`: `check_hours`,
`lifetime_hours`, `tamer_level_chance`, the `mega` list and an optional `species` whose entries replace, field by
field, those of `species_extra`. Both are normal sections, so they follow the `extends` rule: a ruleset that names
`species_extra` replaces the whole file of the one it extends.

The `all_species` section (schema `ruleset_all_species`, from `todas` and shared by `todas_oculto` and
`todas_oculto_mega` with `sections_from`) is the one for [all species as partners](../modules/game/extra-evolutions.md):
a `species` with the `species_extra` format and a `wake_up` list with that of `specials.wake_up` in `evolution.yaml`,
which is added after the original's. `readSpeciesExtra` reads it last: a species that `species_extra` or `mega_step`
already patches keeps that patch. Its destinations are fallbacks (`SpeciesPatch::yields`): the selection only looks
at them when no destination of the original passes.

`species_extra` can also carry `extra_prizes`, with the `tournament.yaml` format (`{cup, item, amount}` or
`{cup, money}`, by number): `TournamentAdapter` adds them to those of the tournaments section, so a ruleset that
extends another gives cup prizes without copying its whole `tournament.yaml`.

The optional `rare_items` section (schema `ruleset_rare_items`, in `oculto` and `oculto_mega`) says where the
[rare evolution items](../modules/game/evolution.md#rare-evolution-items) can appear: `chance_percent` per
spot, `pool` (item ids, those of the package with its `game:`) and `maps.<id>` with their `spots` (`[x, y]` of a
tile) and an optional `unless_trigger`. `FieldItemsAdapter` reads it with `readRareItemRules`; a ruleset without it
has no rare items, and it is only used with the "Rare items" setting on. Adding a map is adding an entry to `maps`.

### Quirks: `quirks`

The `original` ruleset reproduces all the original's quirks and bugs (design lead decision, 2026-10-20), each one
behind a named flag that another ruleset can turn off. Each section groups them in its `quirks:` mapping. `true`
reproduces the original and `false` fixes it. All are mandatory, so each ruleset decides each quirk:

```yaml
# rules/original/care.yaml
quirks:
  # partner_impl.c: handleSleeping removes the hunger before looking at it to lower the weight.
  sleeping_hungry_keeps_weight: true
```

The system reads them with `readQuirks(reader, node, {"sleeping_hungry_keeps_weight", ...})` and asks
`quirks.on("sleeping_hungry_keeps_weight")`. The section's schema declares them all (`required` and
`additionalProperties: false`).

### Approved deviations

When the design lead approves that `original` departs from the original game (for example, autosave), the value
carries a comment `# Approved deviation (YYYY-MM-DD): ...` with the reason, and if it can be turned off, a flag in the
section (for example, `autosave: { on_map_change: true, ... }` in the saving section).

### Citations

Each constant of `original` carries above it the decomp file and function that it reproduces, as in
`field/rules.yaml`: `# partner_impl.c: tickHungerMechanics: ...`. Units go in the section's header comment.

### Design lead decisions (2026-10-20)

These four format rules are approved and final:

1. `extends` replaces **whole** sections and tables; there is no deep merge of values.
2. `reload()` **discards** the values tuned live: the files rule.
3. Each quirk that a section reads is **declared in every ruleset**; there are no default values.
4. The clock speed is a **real multiplier** (`setSpeed(x)`), not a list of steps.

## Ranges and ids

A ruleset names each individual item or technique **by its id** (`item: bandage`, `evolves_to: species_b`), never by
number: the number of an entry that a mod adds is set by the engine and depends on the mounted packages.

**Ranges only cover the original game; mods use ids.** The classes that the decomp defines by number intervals (the
meals of `item_use.meals`, `never_taken`, the `discipline_checked_below` threshold) remain ranges and thresholds of
the original's numbers, with their citation, but only match entries of the original, never one that the engine
numbered. Each one also accepts ids:

```yaml
meals:
  - { from: 38, to: 70 }   # the original's numbers
  - example_mod:honey_bun  # a mod's meal, by id
refusal:
  discipline_checked_below: 38
  discipline_checked_ids: [example_mod:honey_bun]
```

Techniques follow the same rule, with optional sibling id keys next to each range or threshold:

| Section | Original's range or threshold | Ids |
|---|---|---|
| `battle.moves` | `finisher_first_index`..`finisher_last_index` | `finisher_ids` |
| `battle.moves` | `learnable_below_index` | `learnable_ids` |
| `evolution.requirements` | `mastered_moves_below` | `mastered_move_ids` |
| `evolution.moves` | `finisher_first`..`finisher_end` | `finisher_ids` |
| `evolution.moves` | `specialty_blocks` + `block_size` | `specialty_block_ids` (specialty → ids) |

This way a mod adds a finisher or a technique from a specialty block without code: it defines it in `data/moves` and
names its id in its ruleset's id key.

When loading, each number in a range must be an entry of the original and each id an entry of the composed table; if
not, the session does not start and says which rule and which number or id. `dwx validate` checks the ids with the
`entryReference` annotation of the schemas.

## How a system uses it

1. **Its section.** The system defines its struct and reads it from the section with `assets::YamlReader`, with the
   errors at the file's line and column (`reader.finish(value, section.file)`).
2. **Its check.** It provides a `SectionCheck` that reads the section and discards the result. The app gathers those of
   all the systems into `SectionChecks` and gives them to the `RulesetStore`: this way a load, a reload or a tuned
   value that the system would not accept is rejected before anyone sees it.
3. **Revision.** It keeps the `revision()` with which it read its section and reads it again when it changes.

```cpp
// packages/game/partner (example)
core::Result<CareRules> readCareRules(const ruleset::Section& section);

ruleset::SectionChecks checks{{"care", [](const ruleset::Section& s) -> core::Result<void> {
    if (auto read = readCareRules(s); !read) return std::unexpected{read.error()};
    return {};
}}};
auto store = ruleset::RulesetStore::load(vfs, "original", checks);
auto care = readCareRules(*store->current().section("care"));
auto clock = ruleset::GameClock{*ruleset::readClockRules(store->current())};
std::uint64_t seen = store->revision();
// every tick:
clock.advance(dt);
if (store->revision() != seen) { care = readCareRules(*store->current().section("care")); seen = store->revision(); }
```

### Strategies

A rule that is logic and not a number (how an evolution is chosen, how the battle AI decides) goes behind an
interface. The system declares the interface and a `StrategyRegistry`, registers `original` and the others, and the
section says which to use:

```cpp
struct EvolutionSelection { virtual ~EvolutionSelection() = default; virtual int pick(/*...*/) = 0; };
ruleset::StrategyRegistry<EvolutionSelection> selections{"evolution selection"};
selections.add("original", [] { return std::make_unique<OriginalSelection>(); });

// In the section's check: an unknown name is an error on its line.
selections.check(section, "selection");
// When reading the section:
auto selection = selections.create(section, "selection");   // evolution.yaml: `selection: original`
```

The error says the file, the line, the column and the known names:
`.../rules/original/evolution.yaml: line 3, column 12: unknown evolution selection 'newest'; known: original`.
Strategies in Lua will come with their own implementation of the registry; the manifest can already name the scripts
(`scripts:`).

### Randomness

The `original` ruleset uses the original's probabilities and distributions, but not its generator: the rolls are not
reproduced bit for bit (design lead decision, 2026-10-20). The rules roll with `ruleset::Random`, a seeded PCG32:
with the same seed, a test or a simulation plays the same on all platforms. `below(n)` gives 0 to n − 1, like the
original's `randomLimit(n)` (`math.c`), and `chance(p)` succeeds p times out of 100.

## Game clock

The `clock` section describes the original's clock (`clock.c: tickGameClock`): 20 frames per second, a minute every
20 frames, 30 days per year, the year goes back to 0 at 100, and a game starts at 8:00. `ruleset::GameClock` makes it
advance with real time:

- **Frames.** It counts in the original's frames, because the care rules go "every 200 frames": `frameOfDay()` is the
  original's `CURRENT_FRAME`.
- **Advance.** `advance(seconds)` returns how many minutes, hours, days and years it has crossed. A system that does
  something every hour repeats it `hours` times, so advancing days at once gives the same as playing them.
- **Speed.** `setSpeed(x)` is the fast-forward: 1 is the original's speed (one game minute per real second), 0 stops
  it, 60 makes one game hour per second and 3600, 60 game hours per second. The tests, the autotest, the simulations
  and the debug panel use it.
- **Pause and jumps.** `setRunning(false)` is the original's `stopGameTime`. `jumpTo(hour, minute)` is the jump when
  sleeping (`advanceToTime`): it only counts the change of day, not the skipped hours.
- **One per game.** The clock belongs to the game session (`packages/session`), which advances it only on the ticks in
  which the field runs game time (`Field::gameTimeRan`) and saves it with its fractions
  (`restore(frames, pending)`), so a loaded game continues the same as the saved one.

## Hot reload and tuning

`RulesetStore` holds the active ruleset:

- `reload()` reads it again from the packages after editing its files, and `select(id)` switches to another ruleset.
- `setValue(section, path, yaml)` changes a value (`"care"`, `"hunger.window_minutes"`, `"45"`) for the tuning panel.
  The path goes by keys separated by dots, and a number picks an element of a list.
- Each change goes through the checks again. If it fails, the previous ruleset stays active and the error says why. If
  it is accepted, the revision goes up.
- A reload discards the tuned values: the files rule.

The VFS reads the files every time, so editing a section and reloading is enough. A new file needs the packages to be
mounted again.

The debug overlay's [tuning panel](../modules/engine/debug.md#tuning-panel) uses all of this: it chooses the
ruleset, edits its values, shows what a reload would change, controls the clock and saves a section to its file.
Saving does not rewrite the document with `emitYaml`, which would lose the comments (the YAML tree does not keep
them): `saveSection` changes only the text of each value that differs from the file, on its line, so the decomp
citations and the order stay. A change of shape (a value that becomes a list, a new key) is not saved: the file is
edited.

The [simulations](../guides/simulations.md) (`dwe-sim`) load a ruleset the same as the game and accept the same
changes on the command line (`--set care.hunger.window_minutes=45`) to compare variants without touching the files.

## Validation

- **When loading** (`loadRuleset`): the manifest, that each table, section and script exists, `extends`, the `clock`
  section and the systems' checks. The errors say the file, the line and the column.
- **`dwx validate --engine --with <content>/base content/packs/game`**: the manifest and each section against its
  schema, and the manifest's references (tables, sections, scripts and `extends`) with `assetReference`, so a table
  missing from the base package is a problem.
- **Disc test** (`packages/engine/ruleset`, with `DWE_CONTENT`): each `original` table is in the extracted base
  package and meets its schema.
