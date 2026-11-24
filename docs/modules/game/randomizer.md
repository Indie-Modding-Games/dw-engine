# randomizer

The randomizer (`dwe::game::randomizer`, `game` layer): a new game of a mode that offers it can be a **randomized
run**, where evolutions, the starter, wild enemies, field items and drops change and techniques are learned at a
chance the player picks. It depends on `core`, `assets` and `ruleset`; it draws nothing and holds no random state of
its own.

It is an addition: the original has nothing like it. It belongs to Folder Mode (the
[game variants](../../architecture/game-variants.md): everything that changes the gameplay goes there), as an
overlay on top of whatever ruleset the run picked, not as a mode or a variant of its own. Classic Enhanced does not
offer it.

## The options

| Option | What changes | Where |
| --- | --- | --- |
| Evolutions | Each natural evolution gives a random form of the target's stage | `Evolution::mapNaturalTargets`, `EvolutionAdapter::mapEvolutions` |
| Technique learning | Original, 25, 50, 75 or 100 % for every technique the partner saw and can learn | `BattleSetup::learnChance`, `learnedMoves` in `aftermath.cpp` |
| Starter | The first partner is a random Rookie | `GameSession::startNewGame` |
| Wild enemies | Each listed wild enemy is a random species of its stage | `randomizeMapEnemies`, `FieldScene::attachMapNpcs` |
| Field items | Items lying on the ground or in chests | `FieldItemsAdapter::spawnItem`, `spawnChest` |
| Drops | Each species drops one random item instead of its table's | `BattleAdapter::randomizeDrops` |

The presets are **Off** (everything off) and **Chaos** (everything on, techniques always learned).

### Evolutions

The chart still decides: the selection scores the original targets with their requirements, as without the
randomizer, and the form it picks is then mapped to a form of the same stage, keyed by the form evolving and the
target (`hash(seed, "evolution", from, target)`). So the **requirements come from the original target's slot** and
the **stats come from the new form** (`evolve` loads the mapped form's reference stats). The evolution chart shows the
mapped forms with those requirements. The stage pools are the forms some chart leads to, grouped by level, without
the Megas and without forms with no reference stats: a baby still becomes an in-training form, an in-training form a
rookie, a rookie a champion, a champion an ultimate. The rookie's fallback (Numemon) is a natural evolution too and
is mapped the same way.

Not mapped: special evolutions (praise, scold, sleep, the champion's timer hour), items, scripts, the virus bar,
the developer's evolve and the Ultimate to Mega step.

### Technique learning

`learnedMoves` rolls `random.below(100)` for each technique the partner saw, as the original does, and compares it
with the fixed chance instead of the move table's. At 100 % every roll passes; with the option at Original the chance
is the table's. A battle that had no candidate and now has one makes one more roll to pick it: that is the battle's
own random number generator, which the session seeds once per battle, so nothing after the battle changes.

### Starter

The partner the opening picked becomes a random Rookie of the partner species table, keyed by the seed alone: the
interview's answer gives the same partner. It starts with the new game's stats for species without their own.

### Wild enemies

There is no flag for a wild enemy in the maps, so the list is data: `rules/original/randomizer.yaml` names, by map,
the NPC slots that are plain wild fights. It was made once from the base story's map scripts: a talk section whose
calls are only `set_dialog_owner`, `show_textbox`, `fadeout_hud`, `delay`, `start_battle` and `set_trigger`, that
starts `start_battle(0)` and sets no recruit trigger (200 to 262). That leaves out bosses, story and predefined
fights, recruits, characters and the three roaming rares. Each listed slot gets a species of its original's level
from the partner roster with a model, keyed by map and slot; it keeps the record's stats, type, scripts and level and
its moves become the new species' own (rotated by the same key). The map document is changed as it loads, before the
field places it, so the field, the battle, the minimap and co-op all see the same enemy.

### Items

Ground items are keyed by map, cell and original item; chests by map, trigger and original item. The pool is the
original's items without key items and without the run's kept and excluded ones. **Kept items** never move and never
appear anywhere else: the key items and every item the story checks for in the bag (the flute, the rods, the
Leomonstone, the mansion and fridge keys, the gear, the decoder, the meat and medicine the quests ask for...).
Excluded: the rare evolution items of the rare items option and the three unused entries of the original's table.
NPC gifts are not randomized: their lines name the item. The item keeper starts empty, so there is nothing to place in
it.

### Drops

Each species with a drop item drops the same random item, keyed by species alone; the chance stays the table's and
the roll is the same.

## Seeds and run codes

A seed is 40 bits, written as 8 characters of Crockford's base 32 (`K7QH2M9X`). The **run code** adds a dash and two
characters with the options (`K7QH2M9X-3Z`): bits 0 to 4 are evolutions, starter, enemies, items and drops, bits 5 to
7 the learning chance. `parseCode` takes either, ignores case, spaces and dashes and reads I, L and O as 1, 1 and 0.
A code pasted or typed in the setup brings its options too, so sharing a run is sharing its code.

## Determinism

Every pick is `hash % pool size`, where the hash is FNV-1a over a domain and its keys, started from splitmix64 of the
seed and finished with splitmix64, and the pools are sorted ids. It never touches the game's random numbers
(`ruleset::Random`): with the randomizer off, or on, the session, the partner and each battle draw exactly what they
draw without it, so the oracle compare still lines up. The same seed and options give the same run on every platform;
a test pins one hash value.

## Saving

The settings are fixed when the run starts and saved in the `randomizer` section (`seed`, `evolutions`,
`learn_chance`, `starter`, `enemies`, `items`, `drops`); a save without it is no randomized run. The header's summary
(format 5) has the run code, empty for any other game, and the slot list shows it next to the mode. A save of format
4 loads with an empty one.

## The setup screen

`ui/new_game_randomizer`, after the mode and its rules (`TitleScreen::finishNewGame`, `NewGameRandomizer`): the two
presets, each option, the learning chance (steps through its values), the seed (random by default; select it and
type, Enter keeps it, Esc erases), **Paste** (the clipboard's seed or run code), **New seed**, the run code and
Start. The game publishes `new_game_randomizer.*` and the document sends `new_game_randomizer` with an action
(`toggle:<option>`, `learn`, `preset:off`, `preset:chaos`, `edit`, `erase`, `commit`, `paste`, `reseed`, `start`,
`back`). A test route asks for it with `new_game_randomizer: true` (with `new_game_rules: true`); the others start no
randomized run. The pixel skin draws the presets with its shared four-corner cursor. The original look uses the rules
box, wider, higher and with closer rows (`skins/original/new_game_randomizer`), so its eleven rows fit a 720dp window.

## Ruleset data

`rules/<id>/randomizer.yaml` (schema `ruleset_randomizer.schema.json`), a section of the original ruleset that the
others extend:

```yaml
enemies:            # map id -> wild NPC slots
  trop02: [1, 3]
kept_items: [mansion_key, gear]
excluded_items: ["game:steel_heart"]
excluded_species: []
```

A mod that adds maps lists their wild slots; one that adds species can exclude them.

## Multiplayer

Co-op: the guest plays the host's save, which carries the `randomizer` section, so every peer gets the host's
settings and seed and changes each map the same way; the host's items win anyway. Versus is not randomized.

## Pending

- A spoiler log (every pick of a run, written next to the save) is not done.
- With a gamepad the seed cannot be typed letter by letter: paste it or roll a new one.
