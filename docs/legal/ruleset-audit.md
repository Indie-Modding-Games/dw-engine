# Audit of the original's values: rules or tables

> Date: 2026-10-21. Package `p4-ruleset-audit`, phase A (classification only: it changes no value or code).
> Complements the [legal asset policy](assets.md).
>
> Status (2026-11-06): [phase B](#phase-b-plan) is still pending. The (b) tables in the list are still in the
> game pack's rulesets (for example `life.yaml: rebirth.babies`, `evolution.yaml: baby_targets`,
> `battle.yaml: moves.buffs`); `dwx` does not read them from the disc yet.

## What this document is for

The engine is written by looking at the original game (its *decomp*), and the project's policy is that **there is
nothing from the original in git**: no code, no graphics, no texts, no data tables. But for the game to play the same
you have to know *how it works*: how many hours a Digimon lives, how often it gets hungry, how damage is calculated.
This document reviews one by one the numbers that the repository today has taken from the original and decides, for
each one, whether it can stay in git or has to come from the player's disc (executable and overlays).

There are two classes:

- **(a) Rule or mechanic.** A loose number, or a small structure, that says *how the game works*: a cap, a threshold,
  a divisor of a formula, how many frames something takes to happen, a probability, the size of a block. It is the
  game's "physics", what anyone would describe in a guide. **It stays in git**, in the ruleset, with its citation to
  the decomp.
  *Example:* `life.yaml: lifetime_hours: 360`, the partner lives 360 game hours. It is a rule; a mod can change it.
- **(b) Per-entity table.** Data with one entry per Digimon, per item, per technique or per map: each species' stats,
  which species evolves into which, what each item does. It is the game's *content*, just like its graphics. **It
  comes from the player's disc (executable and overlays) with `dwx`** into the base pack, and the ruleset only says
  which table to use.
  *Example:* `life.yaml: rebirth.babies`, the starting stats of each of the four babies on rebirth. It is data of
  four specific species, not a rule.
- **(?) Doubtful.** A value that could go to either. It carries a recommendation and its reason. All those in this
  audit are already decided (see "Coordinator decisions"): in the tables they appear with their final class.

**Decision on the (a)/(b) distinction:** "Decided by the coordinator (2026-10-21)". Reason: the rules are necessary
to reimplement the game and are descriptions of how it works, not copies of its content; the per-entity tables are
game content, and the "bring your own data" model ([ADR 0011](../adr/0011-asset-legal-policy.md)) requires them to
come from the user's disc.

The names of decomp functions and files in the comments are allowed: they say where to check a rule. Copied tables are
not.

### Criteria this audit applies

These criteria are the interpretation of the previous decision for the cases it does not cover; they are accepted as
they are written: "Decided by the coordinator (2026-10-21)".

1. **Classify by meaning, not by syntax.** Item and technique numbers are ids
   (`item: <item id>`, `map: <map id>`). The class does not depend on the form: a per-item list of effects is (b)
   whether it is written with numbers or with ids.
2. **A reference is not a table.** Rule: "a single reference to an entity is (a); a list of two or more entities is
   (b)". "Decided by the coordinator (2026-10-21)". A loose id says *to whom* a rule applies (the form a rookie falls
   to with no candidate, the counterattack technique, the item that attracts enemies): it names the subject of a
   mechanic and does not copy data of that entity. A list of entities, or one that **assigns values** to several
   (stats, effects, evolution targets), is (b).
3. **A category is not an entity.** The tables indexed by specialty (fire, ice...), type (data, vaccine, virus), level
   (baby... ultimate), item category or intelligence bracket are mechanics: (a). Exception (D3, decided): those that in
   the executable are **a data table with its own symbol and nine values or more** (`TYPE_FACTORS`, `FLEE_CHANCES`,
   `SLEEP_PATTERN`, `ITEM_SORT_ORDER`) go to (b), because extracting them costs little (`dwx` already reads tables by
   address) and they are the closest thing to a copied table.
4. **The engine's values** (invented, measured or approved deviations) are (a): they do not come from any table.

## How to read the table

One line per value: the key inside the file, the value or a summary (tables are summarized, never copied here), the
class and the reason. `quirks.*` are quirk flags: all are (a), because they say whether a behavior is reproduced, they
do not store data.

---

## `content/packs/game/rules/original/`

### `ruleset.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `id` | `original` | a | Project metadata. |
| `name` | `Original` | a | Project metadata. |
| `version` | `0.1.0` | a | Project metadata. |
| `description` | `en`: `The original game's rules, quirks included, as the starting point and the reference.`, and its `es` | a | Project metadata. |
| `tables` | 8 roles → `data/*` | a | References to the base pack's tables: the mechanism the (b) class uses. Phase B adds roles. |
| `sections` | 12 sections | a | Structure of the ruleset. |

### `clock.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `frames_per_second` | 20 | a | Timer or duration. |
| `frames_per_minute` | 20 | a | Timer or duration. |
| `days_per_year` | 30 | a | Calendar. |
| `year_limit` | 100 | a | Calendar. |
| `start.year` | 0 | a | Start date of a new game. |
| `start.day` | 0 | a | Start date of a new game. |
| `start.hour` | 8 | a | Start time of a new game. |
| `start.minute` | 0 | a | Start time of a new game. |

### `life.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `lifetime_hours` | 360 | a | Timer or duration. |
| `lives` | 3 | a | Life rule. |
| `unhappy_lifetime.every_hours` | 4 | a | Timer or duration. |
| `unhappy_lifetime.happiness_below` | 80 | a | Threshold. |
| `unhappy_lifetime.happiness_divisor` | 50 | a | Divisor of a formula. |
| `unhappy_lifetime.extra_hours` | 1 | a | Term of the life formula. |
| `rebirth.baby_choice` | `original` | a | Strategy name. |
| `rebirth.babies` | table of 4 entries per species (id + 6 stats) | **b** | Fixed stats per species. In the executable: `REINCARNATE_BABY_TYPE` (8-byte datum; dooa.c) and the immediate arguments of the four calls to `setReincarnateStats` in `initializeReincarnatedPartner` (partner_impl.c). Target: `data/rebirth`. |
| `rebirth.inherit_percent_per_tamer_level` | 1 | a | Factor of the inheritance formula. |
| `rebirth.random_egg_without_lives` | `true` | a | Game rule (yes or no). |
| `scenes.lost_life.*` | 35, 95, 135, 145, 185 frames; shadows 160 and 255 | a | Timer or duration; fade level. |
| `scenes.wake_map.story_map`, `early_map`, `exit` | 205, 218, 1 | a | Reference to maps by their index. |
| `scenes.wake_map.story_triggers`, `story_variable`, `story_variable_at_least` | [220, 214], 1, 50 | a | Story condition (threshold). |
| `scenes.wake_map.story_map_start_x` | 28 | a | Screen offset in pixels (0xBC − 0xA0). |
| `scenes.death.sound_bank`, `sparkle_sound` | 19, 7 | a | Reference to a sound bank and to a sound. |
| `scenes.death.*_frame`, `burst`, `sink`, `vanish`, `rise`, `glow`, `hatch` | frames 10-439 with their sound | a | Timer or duration. |
| `scenes.death.clips.*` | 1, 44, 12, 33, 28, 11 | a | Reference to animations by their id. |

### `new_game.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `partner` | a species (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `stats` | 6 fallback stats | a | Invented fallback values (not from the original); the real ones are extracted by `dwx` from script 193 into `data/new_game_stats`. |
| `tamer_level` | 0 | a | Initial value. |

### `new_game_scene.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `lines` | 1238 (a MAPHEAD table) | a | Reference to the base pack's texts; the sentences are not copied. |
| `partners` | 2 species (references) | a | References (criterion 2): the species each answer chooses. |
| `fade_in_frames`, `fade_out_frames`, `frame_ticks` | 20, 40, 3 | a | Scene timings (`fadeFromBlack`, `fadeToBlack`, `VSync(3)`). |
| `camera_distance`, `camera_projection`, `camera_centre_line` | 4200, 1000, 185 | a | The scene's camera parameters. |
| `host` | a species, its position, two clips, a frame and two turns | a | Staging: an entity and its animation numbers. |
| `naming` | 6 letters, 5 columns, 18 rows of letters | a | The Latin alphabet and digits in a grid; they are not data from the original. |

### `partner_moves.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `new_game_slot` | 0 | a | Position in the species' technique list, not a specific technique. |
| `new_game_species_slots` | 1 species (reference) and its position 2 | a | Reference to a species (criterion 2) and a position in its technique list, not a specific technique. |
| `reborn_slot` | 0 | a | Position in the species' technique list, not a specific technique. |
| `quirks.forget_mask_16_bits` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

### `calendar.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `new_day.day_counters` | 4 story variables | a | Variable numbers (references): script_common.c: dailyPStatTrigger adds 1 to each at the start of the day. |
| `new_day.stopped_value` | 255 | a | Sentinel value of the rule: a variable with it does not count. |
| `new_day.daily_marks` | 1 variable and its bit | a | Reference and mask: the bit that the new day clears. |

### `field_items.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `ground_slots` | 11 | a | Capacity (DROPPED_ITEMS). |
| `chest_slots` | 8 | a | Capacity (a map's chests). |
| `reach_cells` | 1 | a | Distance of the rule (tamer.c: checkItemPickup, checkChestCollision). |
| `pickup_sound`, `toilet_sound`, `sleep_sound`, `praise_sound`, `scold_sound` | slot and sound | a | References to sounds (slot, number), not data. |
| `pickup_player_clip` | 12 | a | Animation id (reference; tamer.c: tamerTickPickupItem). |
| `feed.sound` | slot and sound | a | Reference to a sound (partner.c: partnerTickFeedItem, playSound(0, 12)). |
| `feed.range_base`, `feed.range_per_distance` | 160, 20 | a | Formula of the rule (partner.c: checkEatDistance, distance × 200 / 10 + 160). |
| `feed.take_distance` | 10 by default; 15 or 25 in 13 species (`ITEM_TAKE_DISTANCE`, 66 bytes) | **b** | Per-species table. Like those of D3, it is extracted as a data table (`TableLocation`); until then the ruleset writes only the values different from 10. |
| `feed.offer_frame`, `feed.hand_down_frame` | 7, 15 | a | Animation frames of the scene (time references). |
| `feed.clips` | 5, 4, 0, 8, 13 | a | Animation ids (references). |
| `toilet_walk.camera_frames` | 20 | a | Duration (partner.c: tickPartnerToilet, createCameraMovement). |
| `toilet_walk.partner_clip` | 10 | a | Animation id (reference). |
| `praise_scold.praise`, `praise_scold.scold` | 38, 39, 11; 7, 12, 25 | a | Animation ids (references; tamer.c: tamerTickPraiseScold). |
| `praise_scold.tall_from_level`, `praise_scold.short_species` | 4; 3 species | a | Threshold and exceptions of the rule (tamer.c: tamerTickPraiseScold, types 0xd, 0x1b and 0x38). |

### `poop_piles.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `look.model` | `model/etcna/unti` | a | Reference to the model that dwx extracts (partner_impl.c: initializePoop); the model is not copied. |
| `look.model_size` | 10 | a | Constant of the drawing rule (renderPoop: size × 4096 / 10). |
| `wild.squat_clip`, `wild.after_clip` | 10, 12 | a | References to partner animations (partner.c: tickWildPoop). |
| `eat.large_from` | 11 | a | Threshold of the rule (partner_impl.c: tickPoopDetection). |
| `eat.small_reach`, `eat.large_reach` | 200, 300 | a | Distances of the rule (tickPoopDetection). |
| `eat.eat_clip` | 8 | a | Reference to a partner animation (partner.c: partnerTickEatShit). |

### `cards.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `max_copies` | 9 | a | Limit of the rule (script_menu.c: showCardTextbox). |
| `listed.first`, `listed.last` | 1, 64 | a | Range of card numbers (references). |
| `pack.draws` | 3 | a | Quantity of the rule (rollCardPack). |
| `pack.rarity_roll`, `pack.rarity_below` | 100; 4 bounds | a | Pack rarity probabilities (rollCard): code constants, not card data. |
| `lines.*` | line numbers | a | References to the lines of MAPHEAD text 0x4d3; the text is not copied. |

### `fishing.yaml`

The fishing tables (the fish classes, `FISH_BITE_CHANCE`, `FISH_BAIT_CHANCES`, the zones, the shores and the maps) are
(b) and are not here: they are `data/fishing`, which `dwx` extracts from the disc's FISH overlay.

| Key | Value | Class | Reason |
|---|---|---|---|
| `sea_serpent.*` | 20 %, every 8 and every 20 frames | a | Probability and frequencies of the rule (fish.c: FISH_findFishToBite, FISH_tickFishPopulation). |
| `bites.*` | every 20 frames; 200 and 600 | a | Frequency and distances of the rule (FISH_findFishToBite, FISH_tickFish). |
| `population.*` | 33, 32; 600 to 1200; 30 | a | Caps, life and pause of the rule (FISH_tickFishPopulation, FISH_spawnFish, FISH_tickFish). |
| `hook.*` | 150; 20 to 40 and 30; 40 to 60 and 60; 150, 100 | a | Threshold, timings and strength of the rule (FISH_hookFish). |
| `tension.*` | 5000 and 10000; ranges of ±500 or less; 1 to 3, 5 | a | Limits and deltas of the formula (FISH_beginTension, FISH_updateTension). |
| `junk.*` | from 31, every 20, 1000 of 10000, 0 to 99 | a | Frequency and probability of the rule (FISH_tickRod); which item comes out is (b), from `data/fishing`. |
| `cast.*` | 5, 20; 100, 200 and 400 | a | Strength and speeds of the cast (FISH_tickRod). |
| `shore_by_y.*` | 429, 6 | a | Code constants of the water test (FISH_isFishableWater2). |
| `sounds.*` | slot 8, sounds 0 to 10 | a | References to sounds (playSound). |

### `save.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `autosave.on_map_change` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_battle_end` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_partner_sleep` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_new_day` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_evolution` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_tournament_result` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_recruit_join` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_key_item` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_dangerous_area` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_story_progress` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.on_partner_reborn` | `true` | a | Approved deviation (2026-10-20); the engine's. |
| `autosave.coalesce_seconds` | 2 | a | Engine value (approved deviation, 2026-10-20). |

### `tamer.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `level_min` | 0 | a | Cap (minimum). |
| `level_max` | 10 | a | Cap. |
| `evolution_level_gain` | 1 | a | Increment. |

### `story.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `ticks_per_frame` | 3 | a | Pace: the original advances 20 frames per second (`VSync(3)`), the engine ticks at 60 Hz. |
| `variable_max` | 255 | a | Cap (a variable is a byte). |
| `frozen_variable`, `frozen_value` | 0, 3 | a | Variable number and value of a state mark (`scriptPauseGame`). |
| `talk_facing_trigger` | 44 | a | Trigger number of a rule. |
| `show_name_variable` and its values | 6; 0, 1, 2 | a | Variable number and values of a rule (`consumeMapChangeShowName`). |

### `dialogue.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `open_frames` | 5 | a | Pace: frames a box takes to grow (`ui.c: renderUIBoxAnimated`). |
| `no_cancel_trigger` | 49 | a | Trigger number that blocks canceling a selection. |
| `speech_box.columns`, `speech_box.rows` | 21, 4 | a | Size of the dialog box in characters and rows. |
| `sounds.*` | 5 slot/sound pairs | a | System sound numbers that play on opening, advancing, canceling, moving the cursor and refusing. |
| `name_colors.*` | 6, 10, 7, 5 | a | Color indices of the speaker's name. |

### `story_values.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `stat_limits` | 21 caps | a | Caps of the stat commands (`STATS_LIMIT_ARRAY`); `lives` 255: the original reads outside the table. |
| `happiness_floor` | -100 | a | Floor of `reduce_stat` for happiness. |
| `date_year_limit`, `date_overflow` | 255; 25, 14, 23:59 | a | Quirk of `secondsToDate`. |

### `item_use.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `refusal.never_taken` | 3 ranges of item numbers | **b** | D5. "Decided by the coordinator (2026-10-21)": per-item mark in `data/item_effects`; `meals` is derived from the items with a food handler if it matches. |
| `refusal.discipline_checked_below` | 1 item number (range limit) | **b** | D5. "Decided by the coordinator (2026-10-21)": per-item mark in `data/item_effects`. |
| `refusal.fixed_level_evolutions` | 3 items with their level | **b** | Per-item value: the level at which each fixed-evolution item is accepted (comparisons of `partnerWillRefuseItem`, partner_impl.c). Target: `data/item_effects` (`accepted_level`). |
| `refusal.refused_level_differences` | [0, 2, -1] | a | Rule on the level difference. |
| `meals` | 2 ranges of item numbers | **b** | D5. "Decided by the coordinator (2026-10-21)": per-item mark in `data/item_effects`; `meals` is derived from the items with a food handler if it matches. |
| `shop_discount_levels` | [1, 2] | a | By level (criterion 3). The original uses `BABY_IDS` (8 species; script_common.c: `isPartnerBaby`), which are exactly those of level 1 and 2: the by-level rule does not copy the list. |
| `training_boost_bits` | 6 bits, one per stat | a | Bit layout (structure). |

### `inventory.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `bag.max_slots` | 30 | a | Cap. |
| `bag.slot_counts` | [10, 20, 30] | a | Possible bag sizes. |
| `bag.new_game_slots` | 10 | a | Initial value. |
| `bag.new_game_money` | 0 | a | Initial value. |
| `bag.new_game_items` | [] | a | Empty. Filling it with `DEFAULT_INVENTORY_TYPES/AMOUNTS` would be (b): that data would come from the executable. |
| `bag.max_stack` | 99 | a | Cap. |
| `bag.stacking` | `original` | a | Strategy name. |
| `money.max` | 999999 | a | Cap. |
| `sort.orders` | 3 orders of 6 categories (`ITEM_SORT_ORDER`, 18 bytes) | **b** | D3. "Decided by the coordinator (2026-10-21)": it is extracted as a data table (`TableLocation`). |
| `use.field_colors` | [0, 2] | a | By use category (criterion 3). |
| `use.battle_colors` | [1, 2] | a | By use category (criterion 3). |
| `use.discard_droppable` | 1 | a | Value of a `data/items` mark. |
| `use.key_item_droppable` | 0 | a | Value of a `data/items` mark. |
| `defeat.lost_percent` | 30 | a | Percentage or probability. |
| `shop.stock_trigger_base` | 384 | a | Base of a trigger range (structure). |
| `shop.sell_divisor` | 2 | a | Divisor of a formula. |
| `shop.baby_discount_percent` | 90 | a | Percentage or probability. |
| `shop.max_amount` | 99 | a | Cap. |
| `effects.stat_limit` | 999 | a | Cap. |
| `effects.hp_mp_limit` | 9999 | a | Cap. |
| `effects.conscious_only` | [hp_healing, mp_healing, double_floppy, status, battle_disk] | a | By handler (code category), not by item. |
| `effects.double_floppy` | HP and MP it heals (one value each) | **b** | D6. "Decided by the coordinator (2026-10-21)": `data/item_effects`, next to `HEALING_VALUES`. |
| `effects.status` | 5 items with their effect | **b** | Per-item effects (cases of `handleStatusItems`, item.c). Target: `data/item_effects` (`battle_status_cure`, `protects`, `injury_cure`, `sickness_cure`). |
| `effects.injury_roll` | 3 | a | Range of a roll. |
| `effects.sickness_roll` | 10 | a | Range of a roll. |
| `effects.revive` | 2 items with their HP % | **b** | Per-item effects (`handleRestore`, item.c). Target: `data/item_effects` (`revive_hp_percent`, `battle_status_cure`). |
| `effects.battle_disk.limit_percent` | 130 | a | Percentage or probability. |
| `effects.battle_disk.limit_cap` | 999 | a | Cap. |
| `effects.battle_disk.disks` | 7 items with the boost of 3 stats | **b** | Per-item effects (constant switch of `handleBuffDisks`, main.c). Target: `data/item_effects` (`battle_buff`), read with `ConstantSwitch` like `handleFood`. |
| `effects.chip.script` | 1 item → a script section | **b** | Per-item effect (`handleChips`, case 0x16). Target: `data/item_effects` (`script_section`). |
| `effects.chip.toilet` | an item (reference) | a | Criterion 2, "Decided by the coordinator (2026-10-21)": a single entity. The "toilet" effect is still the `handleChips` handler's. |
| `effects.chip.tamer_level.items` | 3 items | **b** | D7. "Decided by the coordinator (2026-10-21)": a list of items; per-item `chip_effect` in `data/item_effects`. |
| `effects.chip.tamer_level.chance_percent` | 10 | a | Percentage or probability. |
| `effects.chip.tamer_level.amount` | -1 | a | Increment. |
| `effects.food.favourite_energy_percent` | 140 | a | Percentage or probability. |
| `effects.food.favourite_happiness` | 2 | a | Increment. |
| `effects.food.poop_level_change` | -1 | a | Increment. |
| `effects.evolution.requires_previous_level` | `true` | a | Rule. |
| `effects.evolution.fixed` | 3 items → species | **b** | Item → species map (`handleEvoItems`, item.c, items ≥ 0x7d). Target: `data/item_effects` (`evolves_to`, which already exists for `EVOLUTION_ITEM_TARGET`). Duplicates `evolution.yaml: items.fixed`. |
| `quirks.stack_overflow_is_lost` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.baby_discount_limited_by_full_price` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.omni_disk_ignores_limit` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.useless_items_consumed` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

### `evolution.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `selection` | `original` | a | Strategy name. |
| `scoring` | `original` | a | Strategy name. |
| `check_hours.baby` | 6 | a | Timer or duration. |
| `check_hours.in_training` | 24 | a | Timer or duration. |
| `check_hours.rookie` | 72 | a | Timer or duration. |
| `check_hours.champion` | 144 | a | Timer or duration. |
| `baby_targets` | 4 species → species | **b** | Species → species evolution map (switch of `getFreshEvolutionTarget`, evolution.c). Target: `data/evolution` (`fresh_target` per species). |
| `requirements.pass_points` | 3 | a | Threshold. |
| `requirements.weight_margin` | 5 | a | Threshold. |
| `requirements.hp_mp_divisor` | 10 | a | Divisor of a formula. |
| `requirements.mastered_moves_below` | 64 | a | Limit of the bit mask (structure). |
| `fallback.rookie_hours` | 96 | a | Timer or duration. |
| `fallback.rookie_target` | a species (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `specials.roll_limit` | 100 | a | Range of a roll. |
| `specials.wake_up` | 4 rules: target, sources, roll, conditions | **b** | D1. "Decided by the coordinator (2026-10-21)": `data/evolution: specials`. |
| `specials.praise_scold` | 3 rules | **b** | D1. "Decided by the coordinator (2026-10-21)": `data/evolution: specials`. |
| `specials.praise_scold_on_timer.level` | 4 | a | Threshold. |
| `specials.praise_scold_on_timer.hours` | 360 | a | Timer or duration. |
| `items.fixed` | 3 items → species | **b** | Same as `inventory.yaml: effects.evolution.fixed` (duplicated: phase B leaves a single source). |
| `items.level_step` | 1 | a | Rule. |
| `stat_gains.gain_divisor` | 10 | a | Divisor of a formula. |
| `stat_gains.scale_divisor` | 10 | a | Divisor of a formula. |
| `stat_gains.scaled_targets` | 4 species | **b** | D2. "Decided by the coordinator (2026-10-21)": per-species mark in `data/digimon`. |
| `stat_gains.scaled_below_level` | 3 | a | Threshold by level. |
| `stat_gains.ultimate_lifetime_hours` | 96 | a | Timer or duration. |
| `stat_gains.champion_tamer_level_chance` | 20 | a | Percentage or probability. |
| `stat_gains.ultimate_tamer_level_chance` | 100 | a | Percentage or probability. |
| `moves.specialty_blocks` | 7 indices, one per specialty | **b** | D4. "Decided by the coordinator (2026-10-21)": derived on load from `data/moves` (`special` field); it is not stored. |
| `moves.block_size` | 9 | a | Block size. |
| `moves.basic_slot_below_level` | 3 | a | Threshold. |
| `moves.finisher_first` | 58 | a | Range limit as a mechanic; a range over the original's indices. |
| `moves.finisher_end` | 113 | a | Range limit as a mechanic; a range over the original's indices. |
| `rebirth.babies` | 4 species | **b** | `REINCARNATE_BABY_TYPE` (dooa.c, 8-byte datum). Target: `data/rebirth`. Duplicates the list in `life.yaml`. |
| `scene.sound_bank` | 18 | a | Reference to a sound bank (`loadMapSounds2`). |
| `scene.wait_frames`, `scene.dim_frames` | 56, 32 | a | Timer or duration. |
| `scene.camera_aim_end`, `scene.camera_move_end`, `scene.burst_frame` | 64, 96, 106 | a | Timer or duration. |
| `scene.orbit_ease`, `scene.grow` | 164-194, 204-264 | a | Timer or duration. |
| `scene.roar_frame`, `scene.swap_frame`, `scene.end_frame` | 214, 284, 354 | a | Timer or duration. |
| `scene.burst_sounds`, `scene.roar_sound` | [1, 0], 5 | a | Reference to sounds of the bank. |
| `scene.camera.*` | 8 integers (angle, step, distance, rise, projection, drop) | a | Term of a camera formula. |
| `scene.clips.*` | 0, 1, 12, 11 | a | Reference to animations by their id. |
| `quirks.evolution_item_sticks` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.selection_average_carries_over` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.unlisted_baby_restarts_timer` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.rookie_stat_tie_picks_last` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.timer_special_overrides_normal` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

### `training.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `aptitude_tenths` | 6 stats × 5 aptitude types | a | By category: each species' `training_type` already comes from `data/raise`; these are code constants of `TRN_calculateTrainingMultiplier`, not a data table. |
| `level_tenths` | [9, 9, 10, 11, 12] | a | Factors by level (criterion 3). |
| `upgrade_bonus` | 6/5 | a | Factor 6/5. |
| `upgrade_flags` | 6 triggers, one per stat | a | Which story trigger upgrades each machine: references to flags (criterion 2). |
| `practice_bonus` | 6/5 | a | Factor 6/5. |
| `practice_sessions` | 10 | a | Threshold. |
| `practice_level_below` | 3 | a | Threshold. |
| `manual_bonus` | 11/10 | a | Factor 11/10. |
| `tired_out_tiredness` | 100 | a | Threshold. |
| `tired_out_gain` | 1 | a | Fixed value of the gain. |
| `slot_default_tenths` | 10 | a | Default value. |
| `overwork_tiredness` | 70 | a | Threshold. |
| `overwork_happiness` | -3 | a | Increment. |
| `overwork_discipline` | -3 | a | Increment. |
| `gain_formula` | `original` | a | Strategy name. |
| `places` | 2 training modules: timers and 15 courses | a | D8. "Decided by the coordinator (2026-10-21)": `trn` and `trn2` are code modules (TRN_REL and TRN2_REL), not maps; their timers and courses are gym rules, per stat (category). |
| `quirks.brain_training_uses_mp_aptitude` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

### `care.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `limits.happiness_min` | -100 | a | Cap (minimum). |
| `limits.happiness_max` | 100 | a | Cap. |
| `limits.discipline_max` | 100 | a | Cap. |
| `limits.tiredness_max` | 100 | a | Cap. |
| `limits.weight_min` | 1 | a | Cap (minimum). |
| `limits.weight_max` | 99 | a | Cap. |
| `limits.hp_mp_max` | 9999 | a | Cap. |
| `limits.stat_max` | 999 | a | Cap. |
| `limits.stat_gain_max` | 999 | a | Approved deviation (2026-10-20). |
| `limits.stat_min` | 1 | a | Cap (minimum). |
| `start.discipline` | 50 | a | Initial value. |
| `start.happiness` | 50 | a | Initial value. |
| `hunger.baby_interval_hours` | 2 | a | Timer or duration. |
| `hunger.in_training_interval_hours` | 3 | a | Timer or duration. |
| `hunger.window_minutes` | 90 | a | Timer or duration. |
| `hunger.window_step_frames` | 10 | a | Timer or duration. |
| `hunger.starving_weight_loss_minutes` | 10 | a | Timer or duration. |
| `hunger.tired_hunger_tiredness` | 50 | a | Threshold. |
| `hunger.tired_hunger_frames` | 50 | a | Timer or duration. |
| `hunger.tired_hunger_minutes` | 3 | a | Timer or duration. |
| `hunger.fed_happiness` | 5 | a | Increment. |
| `hunger.fed_discipline` | 1 | a | Increment. |
| `hunger.overfed_happiness` | -3 | a | Increment. |
| `hunger.overfed_discipline` | -2 | a | Increment. |
| `hunger.favorite_energy_percent` | 140 | a | Factor. Duplica `inventory.yaml: effects.food`. |
| `hunger.favorite_happiness` | 2 | a | Increment. Duplica `inventory.yaml: effects.food`. |
| `refusal.discipline_base` | 110 | a | Term of a formula. |
| `refusal.discipline_bonus_min` | 10 | a | Term of a formula. |
| `refusal.discipline_bonus_range` | 10 | a | Range of a roll. |
| `refusal.favorite_refusal_percent` | 20 | a | Percentage or probability. |
| `sleep.tiredness_percent_per_minute` | 30 | a | Factor of a formula. |
| `sleep.tiredness_threshold` | 180 | a | Threshold. |
| `sleep.awake_cut_minutes` | 10 | a | Timer or duration. |
| `sleep.baby_awake_hours` | 3 | a | Timer or duration. |
| `sleep.in_training_awake_hours` | 7 | a | Timer or duration. |
| `sleep.young_awake_variation_hours` | 1 | a | Timer or duration. |
| `sleep.patterns` | 8 schedules (`SLEEP_PATTERN`, 48 bytes) | **b** | D3. "Decided by the coordinator (2026-10-21)": it is extracted as a data table (`TableLocation`). |
| `sleep.baby_sleepy_penalty.every_frames` | 200 | a | Timer or duration. |
| `sleep.baby_sleepy_penalty.happiness` | -1 | a | Increment. |
| `sleep.baby_sleepy_penalty.discipline` | -1 | a | Increment. |
| `sleep.in_training_sleepy_penalty.every_frames` | 300 | a | Timer or duration. |
| `sleep.in_training_sleepy_penalty.happiness` | -1 | a | Increment. |
| `sleep.in_training_sleepy_penalty.discipline` | -1 | a | Increment. |
| `sleep.adult_sleepy_penalty.every_frames` | 1200 | a | Timer or duration. |
| `sleep.adult_sleepy_penalty.happiness` | -2 | a | Increment. |
| `sleep.adult_sleepy_penalty.discipline` | -4 | a | Increment. |
| `sleep.missed_sleep_tamer_level_percent` | 1 | a | Percentage or probability. |
| `sleep.regeneration.hp_mp_percent` | 70 | a | Percentage or probability. |
| `sleep.regeneration.hp_mp_percent_range` | 10 | a | Percentage or probability. |
| `sleep.regeneration.tiredness_percent` | 80 | a | Percentage or probability. |
| `sleep.regeneration.tiredness_percent_range` | 20 | a | Percentage or probability. |
| `sleep.regeneration.weight_divisor` | 10 | a | Divisor of a formula. |
| `sleep.regeneration.pillow_percent` | 120 | a | Percentage or probability. |
| `sleep.regeneration.liked_area_percent` | 120 | a | Percentage or probability. |
| `sleep.regeneration.disliked_area_percent` | 80 | a | Percentage or probability. |
| `sleep.sick_penalty.happiness` | -20 | a | Increment. |
| `sleep.sick_penalty.discipline` | -10 | a | Increment. |
| `sleep.sick_penalty.tiredness` | 10 | a | Increment. |
| `sleep.injured_penalty.happiness` | -10 | a | Increment. |
| `sleep.injured_penalty.discipline` | -5 | a | Increment. |
| `sleep.injured_penalty.tiredness` | 5 | a | Increment. |
| `sleep.hungry_weight_loss` | 1 | a | Increment. |
| `sleep.fade.out_frames` | 40 | a | Timer or duration. |
| `sleep.fade.in_frames` | 40 | a | Timer or duration. |
| `sleep.fade.control_frame` | 35 | a | Timer or duration. |
| `sleep.scene.*_clip` | 8, 0, 11, 9 | a | Animation ids (references). |
| `sleep.scene.fade_lead_frames` | 5 | a | Animation frames of the scene (time reference). |
| `toilet.poop_step_minutes` | 10 | a | Timer or duration. |
| `toilet.urge_discipline_offset` | 20 | a | Term of a formula. |
| `toilet.urge_frames_per_hundred` | 1200 | a | Timer or duration. |
| `toilet.toilet_happiness` | 2 | a | Increment. |
| `toilet.toilet_discipline` | 2 | a | Increment. |
| `toilet.wild_happiness` | -10 | a | Increment. |
| `toilet.wild_discipline` | -5 | a | Increment. |
| `toilet.wild_tamer_level_percent` | 1 | a | Percentage or probability. |
| `toilet.weight_loss_roll` | 4 | a | Range of a roll. |
| `toilet.weight_loss_divisor` | 4 | a | Divisor of a formula. |
| `toilet.virus_bar_limit` | 16 | a | Threshold. |
| `toilet.virus_form` | a species (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `toilet.poop_eaters` | 2 species | **b** | D2. "Decided by the coordinator (2026-10-21)": per-species mark in `data/digimon`. |
| `toilet.eating` | 2 brackets | a | Brackets by poop size, not by entity. |
| `toilet.eating_largest.hp_divisor` | 2 | a | Divisor of a formula. |
| `toilet.eating_largest.mp_divisor` | 10 | a | Divisor of a formula. |
| `toilet.eating_largest.weight` | 10 | a | Increment. |
| `toilet.eating_largest.cure_percent` | 20 | a | Percentage or probability. |
| `toilet.piles_per_map` | 16 | a | Cap. |
| `toilet.piles_total` | 100 | a | Cap. |
| `sickness.missed_sleep_hours` | 10 | a | Timer or duration. |
| `sickness.tries_offset` | 10 | a | Term of a formula. |
| `sickness.tries_percent` | 5 | a | Percentage or probability. |
| `sickness.tiredness_offset` | 50 | a | Threshold. |
| `sickness.disliked_area_hours` | 10 | a | Timer or duration. |
| `sickness.injury_hours` | 12 | a | Timer or duration. |
| `sickness.untreated_hours` | 12 | a | Timer or duration. |
| `sickness.on_sick_happiness` | -20 | a | Increment. |
| `sickness.hourly_penalty.happiness` | -10 | a | Increment. |
| `sickness.hourly_penalty.discipline` | -5 | a | Increment. |
| `sickness.hourly_penalty.tiredness` | 5 | a | Increment. |
| `mood.scold_discipline` | 2 | a | Increment. |
| `mood.scold_happiness` | -10 | a | Increment. |
| `mood.boosted_scold_discipline` | 8 | a | Increment. |
| `mood.boosted_scold_happiness` | 0 | a | Increment. |
| `mood.praise_discipline` | -5 | a | Increment. |
| `mood.praise_happiness` | 2 | a | Increment. |
| `mood.praise_discipline_divisor` | 10 | a | Divisor of a formula. |
| `mood.sulk_check_frames` | 200 | a | Timer or duration. |
| `mood.sulk_recovery_frames` | 15 | a | Timer or duration. |
| `mood.sulk_stat_loss_frames` | 300 | a | Timer or duration. |
| `mood.sulk_stat_loss_percent` | 2 | a | Percentage or probability. |
| `mood.area_effect_frames` | 1200 | a | Timer or duration. |
| `mood.liked_area.happiness` | 1 | a | Increment. |
| `mood.liked_area.tiredness` | -2 | a | Increment. |
| `mood.disliked_area.happiness` | -1 | a | Increment. |
| `mood.disliked_area.tiredness` | 1 | a | Increment. |
| `mood.area_timer_limit_frames` | 28800 | a | Timer or duration. |
| `tiredness.tired_threshold` | 80 | a | Threshold. |
| `tiredness.tired_happiness` | -2 | a | Increment. |
| `tiredness.tired_happiness_frames` | 100 | a | Timer or duration. |
| `tiredness.sub_tiredness_threshold` | 60 | a | Threshold. |
| `year.happy_happiness` | 100 | a | Threshold. |
| `year.happy_tamer_level_percent` | 5 | a | Percentage or probability. |
| `year.unhappy_tamer_level_percent` | 10 | a | Percentage or probability. |
| `strategies.care_mistakes` | `original` | a | Strategy name. |
| `strategies.sickness_roll` | `original` | a | Strategy name. |
| `strategies.item_refusal` | `original` | a | Strategy name. |
| `quirks.sleeping_hungry_keeps_weight` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.sleep_adds_default_hours_to_evolution` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.sickness_chance_wraps` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.young_sleep_without_variation` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.battle_midnight_skips_age` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.toilet_doubles_poop_interval` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.food_clears_training_boost` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.skipped_hunger_window_counts_twice` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.skipped_hours_add_sickness_tries` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.skipped_hours_unhappiness_only_at_midnight` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.wake_searches_from_last_meal` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

### `battle.yaml`

| Key | Value | Class | Reason |
|---|---|---|---|
| `strategies.partner_ai` | `original` | a | Strategy name. |
| `strategies.enemy_ai` | `original` | a | Strategy name. |
| `strategies.damage` | `original` | a | Strategy name. |
| `strategies.obedience` | `original` | a | Strategy name. |
| `limits.max_enemies` | 3 | a | Cap. |
| `limits.pending_damage_cap` | 9999 | a | Cap. |
| `limits.stat_cap` | 999 | a | Cap. |
| `damage_tick` | 4 steps | a | Steps of the damage reserve mechanic. |
| `affinity.factors` | 7 × 7 matrix by specialty (`TYPE_FACTORS`, 49 bytes) | **b** | D3. "Decided by the coordinator (2026-10-21)": it is extracted as a data table (`TableLocation`). |
| `affinity.empty_slot` | 10 | a | Neutral value. |
| `affinity.move_weight_bonus` | 5 weights | a | Code constants (`BTL_calculateElementBonus`) by affinity value (category). |
| `affinity.target_score` | 5 scores | a | Code constants (`BTL_calculateTargetScore`) by affinity value (category). |
| `moves.mp_cost_multiplier` | 3 | a | Factor of a formula. |
| `moves.finisher_first_index` | 58 | a | Range limit as a mechanic; a range over the original's indices. |
| `moves.finisher_last_index` | 112 | a | Range limit as a mechanic; a range over the original's indices. |
| `moves.learnable_below_index` | 58 | a | Range limit as a mechanic; a range over the original's indices. |
| `moves.counter` | a technique (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `moves.flattened_shot` | a technique (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `moves.speed_reserve_floor` | -155 | a | Cap (minimum). |
| `moves.shared_cooldown_frames` | 110 | a | Timer or duration. |
| `moves.buffs` | 5 techniques with the boost of 3 stats | **b** | Per-technique effect (switch of `BTL_applyBuffMove`, battle_main.c). Target: `data/moves` (`buff`), read with `ConstantSwitch`. |
| `moves.buff_uses.per_brain` | 100 | a | Threshold. |
| `moves.buff_uses.base` | 1 | a | Term of a formula. |
| `moves.buff_uses.tiers` | 2 brackets | a | Intelligence brackets (category). |
| `moves.learned_together` | 2 pairs of techniques | **b** | Per-technique list (`learnMove`, utils2.c). Target: `data/moves` (`learned_with`). |
| `moves.slot_techniques` | 1 species slot | **b** | Per-species exception (`entityGetTechFromAnim`, utils2.c). Target: `data/digimon` (the row's slot). |
| `speed_reserve.start` | 100 | a | Initial value. |
| `speed_reserve.cap` | 100 | a | Cap. |
| `speed_reserve.gain_every_frames` | 2 | a | Timer or duration. |
| `speed_reserve.gain_per_speed` | 100 | a | Term of a formula. |
| `speed_reserve.gain_base` | 1 | a | Term of a formula. |
| `pacing.after_attack_frames` | 40 | a | Timer or duration. |
| `pacing.no_move_frames` | 80 | a | Timer or duration. |
| `pacing.ranged_patience_frames` | 80 | a | Timer or duration. |
| `pacing.ranged_band_percent` | 30 | a | Percentage or probability. |
| `pacing.flattened_shot_every_frames` | 40 | a | Timer or duration. |
| `pacing.flattened_shot_frames` | 30 | a | Timer or duration. |
| `pacing.flattened_shot_release_frame` | 2 | a | Timer or duration. |
| `pacing.flattened_shot_speed` | 35 | a | Distance per frame. |
| `pacing.flattened_shot_life_frames` | 58 | a | Timer or duration. |
| `pacing.flattened_shot_half_width` | 45 | a | Distance. |
| `pacing.flattened_shot_half_height` | 200 | a | Distance. |
| `spacing.contact_margin` | 200 | a | Distance. |
| `spacing.standing.near_squared` | 0 | a | Distance. |
| `spacing.standing.far_squared` | 480000 | a | Distance. |
| `spacing.walking.near_squared` | 160000 | a | Distance. |
| `spacing.walking.far_squared` | 320000 | a | Distance. |
| `spacing.keep_away_margin_squared` | 640000 | a | Distance. |
| `spacing.keep_away_walking_squared` | 160000 | a | Distance. |
| `obedience.brain_below` | 301 | a | Threshold. |
| `obedience.roll_brain_divisor` | 2 | a | Divisor of a formula. |
| `obedience.roll_period_frames` | 20 | a | Timer or duration. |
| `obedience.partner_chance_base` | 70 | a | Percentage or probability. |
| `obedience.enemy_chance_brain` | 300 | a | Term of a formula. |
| `obedience.enemy_chance_divisor` | 4 | a | Divisor of a formula. |
| `obedience.idle_frames` | 100 | a | Timer or duration. |
| `obedience.retarget_chances` | [70, 40, 30, 0] | a | By charge mode (category); 4 bytes of `TARGET_ATTACKER_CHANCE`, fewer than 9 values (criterion 3). |
| `obedience.enemy_follow_attacker_percent` | 71 | a | Percentage or probability. |
| `obedience.command_delay.low_discipline_below` | 70 | a | Threshold. |
| `obedience.command_delay.low_base` | 160 | a | Term of a formula. |
| `obedience.command_delay.low_divisor` | 10 | a | Divisor of a formula. |
| `obedience.command_delay.high_base` | 10 | a | Term of a formula. |
| `obedience.command_delay.high_divisor` | 10 | a | Divisor of a formula. |
| `obedience.command_delay.high_scale` | 10 | a | Factor of a formula. |
| `commands.menus` | 6 brackets → commands | a | By intelligence bracket (category); `BRAIN_TO_COMMAND_MAP` (5 bytes, battle_setup.c), which is the one the ruleset models, and code. `STD_BRAIN_TO_COMMAND_MAP` (5 bytes, std_main.c, used by the STD_REL code) is not a copy: it is a variant with other values, and the ruleset does not model it. |
| `partner_ai.status_target_brain` | 300 | a | Threshold. |
| `partner_ai.switch_margin` | 40 | a | Threshold. |
| `partner_ai.scored_target_brain` | 150 | a | Threshold. |
| `partner_ai.power_weights` | [50, 20, 5, 0] | a | Weights by rank (`BTL_YOUR_CALL_POWER_PRIO`, 4 bytes): category, fewer than 9 values. |
| `partner_ai.mp_weights` | [50, 20, 10, 0] | a | Weights by rank (`BTL_YOUR_CALL_MP_PRIO`, 4 bytes): category, fewer than 9 values. |
| `partner_ai.reach_weights` | [10, 5, 0, 0] | a | Weights by rank (`BTL_YOUR_CALL_WIDE_PRIO`, 4 bytes): category, fewer than 9 values. |
| `partner_ai.even_weight` | 20 | a | AI weight. |
| `partner_ai.empty_slot_weight` | 5 | a | AI weight. |
| `partner_ai.area_brain` | 200 | a | Threshold. |
| `partner_ai.area_min_enemies` | 2 | a | Threshold. |
| `partner_ai.area_weight_per_enemy` | [10, 15, 15] | a | Weights by mode (category). |
| `partner_ai.low_hp_percent` | 31 | a | Percentage or probability. |
| `partner_ai.buff_priority_brain_divisor` | 10 | a | Divisor of a formula. |
| `partner_ai.buff_priority_base` | 5 | a | Term of a formula. |
| `partner_ai.buff_priority_decay_frames` | 20 | a | Timer or duration. |
| `partner_ai.confused_air_percent` | 70 | a | Percentage or probability. |
| `partner_ai.confused_swing_percent` | 5 | a | Percentage or probability. |
| `partner_ai.confused_turn_roll` | 10 | a | Range of a roll. |
| `partner_ai.confused_turn_from` | 8 | a | Threshold. |
| `enemy_ai.area_weight` | 20 | a | AI weight. |
| `hit.always_hit_below_level` | 3 | a | Threshold. |
| `hit.accuracy_divisor` | 2 | a | Divisor of a formula. |
| `hit.attacker_speed_divisor` | 10 | a | Divisor of a formula. |
| `hit.speed_scale` | 999 | a | Factor of a formula. |
| `hit.standing_evasion_percent` | 120 | a | Percentage or probability. |
| `hit.block_frames` | 3 | a | Timer or duration. |
| `hit.blocked_melee_min_percent` | 10 | a | Percentage or probability. |
| `hit.blocked_melee_max_percent` | 30 | a | Percentage or probability. |
| `hit.counter_min_mp` | 165 | a | Threshold. |
| `hit.big_hit_percent` | 20 | a | Percentage or probability. |
| `damage.affinity_divisor` | 30 | a | Divisor of a formula. |
| `damage.difference_cap` | 500 | a | Cap. |
| `damage.power_scale` | 500 | a | Factor of a formula. |
| `damage.variance_min` | 90 | a | Range of a roll. |
| `damage.variance_max` | 110 | a | Range of a roll. |
| `damage.counter_defense_percent` | 30 | a | Percentage or probability. |
| `damage.finisher_charge_divisor` | 40 | a | Divisor of a formula. |
| `damage.enemy_finisher_min` | 100 | a | Range of a roll. |
| `damage.enemy_finisher_max` | 200 | a | Range of a roll. |
| `damage.min` | 1 | a | Cap (minimum). |
| `damage.max` | 9999 | a | Cap. |
| `status.poison_every_frames` | 100 | a | Timer or duration. |
| `status.poison_min_percent` | 1 | a | Percentage or probability. |
| `status.poison_max_percent` | 3 | a | Percentage or probability. |
| `status.confusion_min_frames` | 200 | a | Timer or duration. |
| `status.confusion_max_frames` | 300 | a | Timer or duration. |
| `status.stun_min_frames` | 200 | a | Timer or duration. |
| `status.stun_max_frames` | 240 | a | Timer or duration. |
| `status.flatten_transition_frames` | 65 | a | Timer or duration. |
| `status.flatten_min_frames` | 224 | a | Timer or duration. |
| `status.flatten_max_frames` | 304 | a | Timer or duration. |
| `finisher.goal_base` | 3000 | a | Term of a formula. |
| `finisher.per_frame` | 1 | a | Timer or duration. |
| `finisher.per_attack` | 2 | a | Term of a formula. |
| `finisher.per_block` | 3 | a | Term of a formula. |
| `finisher.per_blocked_attack` | 4 | a | Term of a formula. |
| `finisher.progress_divisor` | 50 | a | Divisor of a formula. |
| `finisher.aura_frames` | 80 | a | Timer or duration. |
| `finisher.charge_per_press` | 2 | a | Term of a formula. |
| `finisher.charge_cap` | 80 | a | Cap. |
| `condition.stat_penalty_percent` | 20 | a | Percentage or probability. |
| `condition.mp_cost_extra_percent` | 50 | a | Percentage or probability. |
| `condition.mp_discounts` | 4 brackets | a | Intelligence brackets (category). |
| `ending.flee_frames` | 41 | a | Timer or duration. |
| `ending.flee_ignores_hits_from` | 19 | a | Threshold. |
| `ending.revive_frames` | 171 | a | Timer or duration. |
| `ending.revive_countdown_shown_at` | 20 | a | Threshold. |
| `item_throw.release_frame` | 7 | a | Timer or duration. |
| `item_throw.flight_frames` | 24 | a | Timer or duration. |
| `hud.enemy_hp_bar_frames` | 21 | a | Timer or duration. |
| `hud.end_count_delay_frames` | 100 | a | Timer or duration. |
| `hud.end_wait_frames` | 60 | a | Timer or duration. |
| `consequences.victory.happiness` | 2 | a | Increment. |
| `consequences.victory.discipline` | 0 | a | Increment. |
| `consequences.victory.tiredness` | 0 | a | Increment. |
| `consequences.victory.battles` | 1 | a | Increment of a mechanic. |
| `consequences.flee.happiness` | -10 | a | Increment. |
| `consequences.flee.discipline` | -6 | a | Increment. |
| `consequences.flee.tiredness` | 2 | a | Increment. |
| `consequences.flee.battles` | 0 | a | Increment of a mechanic. |
| `consequences.defeat.happiness` | -30 | a | Increment. |
| `consequences.defeat.discipline` | -20 | a | Increment. |
| `consequences.defeat.tiredness` | 0 | a | Increment. |
| `consequences.defeat.battles` | 0 | a | Increment of a mechanic. |
| `consequences.after_battle_tiredness` | 5 | a | Increment. |
| `consequences.after_battle_food` | -15 | a | Increment. |
| `consequences.mp_tiredness_below_percent` | 80 | a | Percentage or probability. |
| `consequences.mp_tiredness_scale` | 10 | a | Factor of a formula. |
| `consequences.defeat_lives` | 1 | a | Increment. |
| `consequences.elapsed_minutes` | 20 | a | Timer or duration. |
| `rewards.enemy_factors` | [10, 12, 16] | a | By number of enemies (`STAT_GAIN_ENEMY_FACTORS`): category, fewer than 9 values. |
| `rewards.partner_scale` | 10 | a | Factor of a formula. |
| `rewards.fallback.hp_lost_percent` | 100 | a | Percentage or probability. |
| `rewards.fallback.attacks` | 10 | a | Increment. |
| `rewards.fallback.big_hits` | 10 | a | Increment. |
| `rewards.fallback.blocks` | 10 | a | Increment. |
| `rewards.fallback.speed_hp_lost_percent` | 50 | a | Percentage or probability. |
| `rewards.fallback.brain_attacks` | 5 | a | Threshold. |
| `rewards.fallback.brain_big_hits` | 5 | a | Threshold. |
| `rewards.no_drop_maps` | list of maps (one entry today) | **b** | Criterion 2, "Decided by the coordinator (2026-10-21)": it is a list of maps, which grows per map; it becomes a per-map mark. Target: per-map mark in `data/maps` (`no_drops`), read from `battleStatsGainsAndDrops` (battle_ui.c, main executable). |
| `rewards.tracked_moves` | 12 | a | Size. |
| `joining.chances` | 3 × 3 matrix by type (`FLEE_CHANCES`, 12 bytes) | **b** | D3. "Decided by the coordinator (2026-10-21)": it is extracted as a data table (`TableLocation`). |
| `joining.untyped_chance` | 70 | a | Percentage or probability. |
| `joining.bell_item` | an item (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `joining.bell_roll` | -50 | a | Range of a roll. |
| `joining.repel_item` | an item (reference) | a | Reference (criterion 2): a single entity, subject of a rule. |
| `joining.repel_roll` | 20 | a | Range of a roll. |
| `timing.attack_frames` | 30 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.hit_frame` | 12 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.hit_reaction_frames` | 20 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.block_animation_frames` | 15 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.faint_frames` | 40 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.walk_speed` | 22 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.projectile_speed` | 60 | a | Engine value (approximate, pending measurement), not from the original. |
| `timing.arena_radius` | 2400 | a | Engine value (approximate, pending measurement), not from the original; it only counts without a camera or map (simulation, tests). |
| `timing.combat_area_width` | 280 | a | Number from the original's logic (map.c: entityCheckCollision, 0x118), not disc data. |
| `timing.combat_area_height` | 200 | a | Number from the original's logic (map.c: entityCheckCollision, 0xc8), not disc data. |
| `timing.combat_area_corner_height` | 200 | a | Number from the original's logic (map.c: entityCheckCombatArea), not disc data. |
| `timing.retreat_step` | 50 | a | Per-frame step of the tamer's run when the battle starts, measured in the original (battle_setup.c: BTL_handleBattleIntro, animation 3), not disc data. |
| `timing.retreat_radius` | 100 | a | The tamer's radius in the area test (DIGIMON_DATA[0].radius), a logic number, not disc data. |
| `timing.retreat_trail_area_width` | 310 | a | Number from the original's logic (battle_setup.c: BTL_handleBattleIntro, 0x136), not disc data. |
| `timing.retreat_trail_area_height` | 230 | a | Number from the original's logic (battle_setup.c: BTL_handleBattleIntro, 0xe6), not disc data. |
| `timing.retreat_entry_area_width` | 300 | a | Number from the original's logic (battle_setup.c: BTL_handleBattleIntro, 0x12c), not disc data. |
| `timing.retreat_entry_area_height` | 220 | a | Number from the original's logic (battle_setup.c: BTL_handleBattleIntro, 0xdc), not disc data. |
| `quirks.broken_mp_tiredness` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.command_delay_discarded` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.sickness_always_hits` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.smart_menu_drops_attack_and_moderate` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.counter_needs_mp` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.low_hp_weights_loop_bug` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.target_score_uses_own_stats` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.weakest_reports_last_score` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.confusion_waits_for_partner_attack` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.scripted_enemies_free_mp` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.injury_counts_any_hit` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.scripted_battle_everyone_joins` | `true` | a | Quirk flag: says whether a behavior is reproduced. |
| `quirks.mp_check_and_payment_round_differently` | `true` | a | Quirk flag: says whether a behavior is reproduced. |

---

## Other constants of the original outside the rulesets

A search in `packages/engine/`, `packages/game/`, `apps/`, `packages/formats/`, `content/` (outside `rules/original/`)
and `tests/` for comments that cite the decomp, for constant tables and for test asserts that repeat values of the
`original` ruleset. An assert that repeats a (b) value is (b); one that only looks at a size or at something existing
is (a). Most of the citations in `packages/game/` and `packages/session` explain behavior and do not store values;
those are not listed. One line per constant or group of constants of the same kind.

**How the test asserts were searched.** For each (b) table, all its references were taken from the ruleset (species,
technique, item and map ids, item numbers and ranges) and searched in all the files of `*/tests/`, in `tests/` and in
`apps/sim/scenarios/`: as text, as the named constants of the fixtures that equal those item numbers (`Bun`, `Charm`,
`BigCrest`…) and by the names of the fields that read them (`babyTargets`, `scaledTargets`, `eatsPoop`, `sleepyHour`,
`sortOrders`, `noDropMaps`, `scriptedWarps`…). Each result was reviewed by hand: it is (b) if the test passes only
because the `original` ruleset brings that value or that entity in the list (also if it depends on an entity **not**
being there); it is (a) if the value is set by the test itself, if it comes from an invented table of the fixture, or
if it only looks at sizes or at something existing. The fixtures that repeat a (b) list count as asserts. In addition,
the formula tests that run with `originalRules()` were reviewed in case their expected result depends on a cell of a
matrix (`TYPE_FACTORS`, `FLEE_CHANCES`): `battle_tests.cpp:250-260` (damage) and `battle_tests.cpp:448-457` (joining
the battle) do; those that only count results of whole battles with unequal stats (`runBattles`) do not.

**Criterion for addresses and sizes** (D9, "Decided by the coordinator (2026-10-21)"): the addresses of tables and
functions, record sizes, opcodes and register positions are **format knowledge** that is needed to read the user's
executable; they do not contain its data. They are (a), like `ExecutableLayout`, which `dwx` already uses per version.

### How `dwx` locates the tables by version

The version comes from the executable's SHA-1 (`packages/formats/src/disc/known_releases.cpp`: us, eu, jp, jp_rev1).
With it, `formats::exe::findExecutableLayout` (`packages/formats/src/exe/executable_layout.cpp`) gives the address and
the number of entries of each table (`TableLocation`), and `apps/dwx/src/game_tables.cpp: readGameTables` reads them.
The values that are in the code and not in a data table are read by executing a constant switch (`ConstantSwitch`,
`packages/formats/include/dwe/formats/exe/constant_switch.hpp`: `handleFood`, `handleChips`). The overlays have their
own layout (`packages/formats/src/exe/overlay.cpp`, `overlay_tables.cpp`). Only the American version is located; the
others fail with a clear error until someone fills in their addresses from the decomp's `config/<version>/symbols.txt`.

| Where | What | Class | Reason |
|---|---|---|---|
| `packages/formats/src/exe/executable_layout.cpp` | addresses and number of entries of some 37 tables, 9 item handlers and the indices of the first item of each effect table (US) | a | Format (D9). |
| `packages/formats/src/exe/executable_layout.cpp: UsFoodValues, UsChipValues` | where the local values of `handleFood` and `handleChips` are (18 and 7 registers or stack slots) | a | Format (D9): it says where to read, not what is worth. |
| `packages/formats/src/exe/tables.cpp` | size of each record (DigimonPara, Move, Item, EvoRequirements...) | a | Format. |
| `packages/formats/src/exe/overlay.cpp`, `overlay_tables.cpp` | headers, load addresses, ranges of `SHOP_REL.BIN`, the overlays' tables (US) | a | Format (D9). |
| `packages/formats/src/disc/file_table.cpp`, `packages/formats/src/video/movie.cpp`, `packages/formats/src/text/font.cpp`, `packages/formats/src/image/tim_packs.cpp`, `packages/formats/src/audio/vhb.cpp` | addresses, capacities and steps of file, video, font, TIM and sound tables | a | Format. |
| `packages/formats/src/effect/efe_script.cpp`, `efe_subops.cpp`, `efe_file.cpp`, `efe_analysis.cpp` | 18 opcodes and 0x61 effect sub-commands with their arguments; VRAM layout | a | Format of the effects language. |
| `packages/formats/src/script/disassembly.cpp` | 105 script opcodes with their operands | a | Format of the scripts language. |
| `packages/formats/src/script/new_game_stats.cpp` | script 193 and 4 opcodes | a | Format: where to read the initial stats (the values come from the disc). |
| `packages/formats/src/text/game_text.cpp` | text control bytes | a | Format. |
| `packages/formats/src/map/map_file.cpp` | caps of a map (35 tiles, 188 instances, 8 NPCs) and the path `MAP<id/15+1>` | a | Format. |
| `packages/formats/src/text/string_manifest.cpp` | US: 25 string blocks and some 24 lists of addresses of specific messages | a | D9. "Decided by the coordinator (2026-10-21)": per-message locators (format). |
| `packages/formats/src/text/charset.cpp: NonKanjiRows` | cp932 table | a | Encoding standard, not the game's. |
| `packages/formats/include/dwe/formats/exe/item_effects.hpp` and its copies in `packages/engine/assets`, `packages/game/inventory` | names of 10 handlers, 18 food values and 7 chip values | a | Own names of fields (schema). |
| `apps/dwx/src/audio_extractors.cpp` | VAB slots, base note, types with enemy music, tracks that play once | a | Format of the sound. |
| `apps/dwx/src/model_extractors.cpp: KnownScenes, RuntimeTextures` | some 30 models → TIM, map or routine where the original shows them | a | D9. "Decided by the coordinator (2026-10-21)": per-file locators (format). |
| `apps/dwx/src/model_extractors.cpp`, `map_extractors.cpp`, `image_extractors.cpp`, `padding_extractor.cpp` | CLUT rows, sectors per model, stacked TIMs, padding ranges of `MMM.DAT` | a | Format. |
| `packages/engine/assets/include/dwe/assets/map_images.hpp`, `map_document.hpp` | VRAM pages and rows, map order, 100 × 100 collision, 3 lights, 10 warps | a | Format. |
| `packages/engine/ui/src/minimap.cpp`, `packages/game/field/src/field.cpp` | warp triggers 110 to 119 | a | Mechanic range. |
| `packages/engine/render/include/dwe/render/map_camera.hpp`, `map_layers.hpp`, `map_renderer.hpp` | 320 × 240, near plane 100, OT step 16, 20 fps, fade of 25 | a | Presentation mechanic. |
| `packages/engine/ruleset/include/dwe/ruleset/game_clock.hpp: ClockRules` | 20 fps, 20 frames per minute, 30 days, 100 years, 8:00 | a | Repeats `clock.yaml` as default values. |
| `packages/game/field/include/dwe/game/field/rules.hpp:27-29` | default radius and walk and run speeds | **b** | D11. "Decided by the coordinator (2026-10-21)": values of a species; the header enters the D11 step of the migration. |
| `packages/game/field/include/dwe/game/field/rules.hpp` (the rest) | distances in radii, fades, hours of the day, default lights | a | Repeats the (a) part of `field/rules.yaml`. |
| `packages/game/battle` (`combatant.hpp`, `state.hpp`, `moves.hpp`, `rules.hpp`, `battle.hpp`, `movement.cpp`, `targeting.cpp`, `original_partner_ai.cpp`, `fighter_actions.cpp`, `enemy_decisions.cpp`, `move_plans.cpp`) | 3 + 1 slots, 16 techniques per species, animation 0x2E, numbering of commands, technique bits, 7 specialties, sentinels | a | Structure and mechanic. |
| `packages/game/partner/src/poop_piles.cpp`, `packages/game/field/src/field.cpp` | orientation bands | a | Mechanic. |
| `packages/game/evolution/include/dwe/game/evolution/tables.hpp` | requirement sentinel and bits 0x01/0x10 | a | Format. |
| `packages/screens/src/partner_status_view.cpp`, `bag_view.cpp`; `apps/game/src/field_scene.cpp` | bar caps 999 and 9999, color 255, ambient light 0.5 | a | Presentation mechanic. |
| `packages/session/src/battle_adapter.cpp`, `evolution_adapter.cpp` | 0x2E/0xFF on rebirth; caps 9999/999 | a | Structure; the caps come from `care.limits`. |
| `apps/sim/scenarios/care_scenario.cpp: MealEnergy, MealWeight` | the energy and weight the original's meat gives | **b** | Effect of a specific item copied into C++ (`handleFood`). Target: read it from `data/item_effects` or use invented values. |
| `apps/sim/tests/stand-in/*.yaml`, `tests/mods/example_mod/data/*.yaml` | invented species and techniques, with the original's ids and indices | a | Invented. "Decided by the coordinator (2026-10-21)": a test with a disc checks that no value of a fixture matches its entity's real table. |
| `content/packs/game/field/rules.yaml: partner.radius, walk_speed, run_speed`; `player.radius, walk_speed, run_speed` | radius and speeds of the initial partner and the player | **b** | D11. "Decided by the coordinator (2026-10-21)": radius from `data/digimon`, speeds from the model's root motion. |
| `content/packs/game/field/rules.yaml` (the rest) | distances in radii, fades, hours of the day, lights | a | Mechanic. |
| `content/packs/game/field/warps.yaml: start` | start map and exit | a | Reference (criterion 2). |
| ~~`content/packs/game/field/warps.yaml: scripted_warps`~~ | 1 script warp (map, trigger, destination, flag) | **b** | D10. "Decided by the coordinator (2026-10-21)". **Deleted** in p4-scripted-warps (2026-10-21): replaced by `field/script_warps`, which `dwx` extracts from the user's disc. |
| `content/packs/game/models/partner.yaml`, `tamer.yaml` | animation ids 0 to 4 | a | Structure. |
| `content/packs/game/save/settings.yaml: manual_slots` | 15 | a | Size. |
| `content/packs/game/ui/battle/logic.lua`, `ui/partner_menu/logic.lua` | 11 commands at most, 30 slots | a | Sizes; they repeat the ruleset. |
| `content/packs/es-translation/text/es/*.yaml` | own translation, by block and index of the original, with the source text's SHA-1 | — | Outside this audit: derived text. "Decided by the coordinator (2026-10-21)": it moves to a separate content repository (package `p4-translation-split`). It does not count in the tally. |
| Tests that repeat US addresses and counts (`packages/formats/tests/exe_tables_tests.cpp`, `file_table_tests.cpp`, `overlay_tables_tests.cpp`, `packages/game/evolution/tests/extracted_tests.cpp`) | addresses, counts (44 items from 0x47) | a | Format (D9). |
| `content/schemas/*.json` | descriptions that cite decomp functions | a | No values. |
| `packages/game/partner/tests/rules_tests.cpp:51-52, 60-63` | hours of four schedules of `SLEEP_PATTERN` | **b** | Repeats (b) values (D3). |
| `packages/game/partner/tests/rules_tests.cpp:65` | stats of the first baby on rebirth | **b** | Repeats (b) values. |
| `packages/game/partner/tests/rules_tests.cpp:50, 58` | sizes (8 schedules, 4 babies) | a | Sizes only. |
| `packages/game/evolution/tests/rules_tests.cpp:42` | a baby's target | **b** | Repeats (b) values. |
| `packages/game/evolution/tests/rules_tests.cpp:64` | the 7 blocks by specialty | **b** | Repeats (b) values (D4). |
| `packages/game/evolution/tests/rules_tests.cpp:41, 54-55, 60, 65` | sizes of the lists | a | Sizes only. |
| `packages/game/evolution/tests/specials_tests.cpp:53-142` | targets and sources of the special evolutions | **b** | Repeats (b) values (D1). |
| `packages/game/evolution/tests/effects_tests.cpp:141` | the four rebirth species | **b** | Repeats (b) values. |
| `packages/game/battle/tests/rules/rules_tests.cpp:17` | a cell of `TYPE_FACTORS` | **b** | Repeats (b) values (D3). |
| `packages/game/battle/tests/rules/rules_tests.cpp:99-100` | that one technique has a boost and another a pair | a | Existence and size only. |
| `packages/game/battle/tests/core/battle_tests.cpp:419` | a pair of techniques that are learned together | **b** | Repeats (b) values. |
| `packages/game/inventory/tests/rules_tests.cpp:55` | size of `sortOrders` | a | Size only. |
| `packages/game/inventory/tests/effects_tests.cpp:32-33, 42, 44, 48, 54, 84` | double floppy, cures, revive, disk and script section | **b** | Repeats (b) values (D6 and per-item effects). |
| `packages/game/partner/tests/sleep_tests.cpp:20-21, 31` | `SleepyHour` and `WakeHour` (a schedule of `SLEEP_PATTERN`) and its awake hours; they are checked at :29-30, :59, :85, :88 and :91, and the rest of the file plays until `SleepyHour` | **b** | Repeats (b) values (D3). |
| `packages/game/partner/tests/sleep_tests.cpp:41-45` | the sleep hours of a baby and of an in-training, which come from their schedules | **b** | Repeats (b) values (D3). |
| `packages/game/partner/tests/hunger_tests.cpp:172-185` | the wake-up hour of a schedule, on which the next meal depends | **b** | Repeats (b) values (D3). |
| `packages/game/partner/tests/life_tests.cpp:180` | the first baby of the rebirth list | **b** | Repeats (b) values. |
| `packages/session/tests/session_tests.cpp:373-374` | the four rebirth babies | **b** | Repeats (b) values. |
| `packages/game/partner/tests/partner_fixture.hpp:129`, `packages/session/tests/session_fixture.hpp:103`, `packages/session/tests/example_mod_tests.cpp:110` | fixtures that create the species of the four rebirth babies, in their order | **b** | Repeats (b) values. |
| `packages/game/partner/tests/toilet_tests.cpp:152-153, 167` | that one species eats poop and another does not | **b** | Repeats (b) values (D2). |
| `packages/game/evolution/tests/selection_tests.cpp:62-67, 108-116` | a baby's target, and a baby that is removed from `baby_targets` | **b** | Repeats (b) values. |
| `packages/game/evolution/tests/state_tests.cpp:150-160` | that a baby has an in-training target | **b** | Repeats (b) values. |
| `packages/game/evolution/tests/effects_tests.cpp:58-66` | the stat changes of two species of `scaled_targets` | **b** | Repeats (b) values (D2). |
| `packages/session/tests/evolution_adapter_tests.cpp:91-99` | a baby's target (`baby_targets`) | **b** | Repeats (b) values. |
| `packages/session/tests/evolution_adapter_tests.cpp:201-203, 231-243` | the fixed target of an evolution item (`items.fixed`) | **b** | Repeats (b) values. |
| `packages/game/inventory/tests/effects_tests.cpp:77-83, 86` | a chip from the tamer level list, and one that is outside the chip lists | **b** | Repeats (b) values (D7). |
| `packages/game/inventory/tests/effects_tests.cpp:111-114` | the fixed target of an evolution item | **b** | Repeats (b) values. |
| `packages/game/inventory/tests/use_tests.cpp:91, 100-101` | a chip that is outside the chip lists and does nothing | **b** | Repeats (b) values (D7). |
| `packages/game/inventory/tests/bag_tests.cpp:114-129` | two orders of `ITEM_SORT_ORDER`, in comments and in the expected order | **b** | Repeats (b) values (D3). |
| `packages/session/tests/inventory_adapter_tests.cpp:162-171` | an item of `refusal.never_taken` | **b** | Repeats (b) values (D5). |
| `packages/session/tests/inventory_adapter_tests.cpp:173-190` | the acceptance level of a fixed-evolution item (`refusal.fixed_level_evolutions`) | **b** | Repeats (b) values. |
| `packages/game/battle/tests/core/battle_tests.cpp:245-251, 355-360` | a technique of `moves.buffs` and its boost percentage | **b** | Repeats (b) values. |
| `packages/game/battle/tests/core/battle_tests.cpp:439-442` | the map of `rewards.no_drop_maps` | **b** | Repeats (b) values. |
| `packages/game/battle/tests/core/battle_tests.cpp:256-266` | the expected damage range, which comes from a diagonal cell of `TYPE_FACTORS` | **b** | Repeats (b) values (D3). |
| `packages/game/battle/tests/core/battle_tests.cpp:454-463` | how many enemies join, which comes from two cells of `FLEE_CHANCES` | **b** | Repeats (b) values (D3). |
| `packages/game/field/tests/rules_tests.cpp:150-152` | the script warp: how many there are, its trigger and its exit | **b** | Repeats (b) values (D10). |

---

## Summary

### Tally by class

After the coordinator's decisions no (?) is left. Before deciding there were 21 (16 in the rulesets and 5 outside).
The translation row does not count.

| Scope | (a) rule | (b) table | Lines |
|---|---|---|---|
| Rulesets (`rules/original/*.yaml`) | 439 | 27 | 466 |
| Other constants of the original (code, content and tests) | 38 | 36 | 74 |
| **Total** | **477** | **63** | **540** |

Almost everything is (a): the rulesets were written from the start with the idea that the per-entity tables belong to
the base pack (`tables:` in `ruleset.yaml`). The (b) that remain are small tables that in the original are not in a
data table but **inside the code** (call arguments, switch cases, comparisons), and that is why they were copied into
the ruleset. Decisions D1 to D11 add to (b) small tables of categories with their own symbol, sets of species or
items with a behavior and the field values of a species.

### (b) tables and their target

None is copied here. "File" says which file of the user's disc it comes from (the main executable or an overlay);
"How", which reading mechanism `dwx` needs (see phase B). Functions and symbols are named only as locators.

| Value | File | Where | How | Target in the base pack |
|---|---|---|---|---|
| `life.yaml: rebirth.babies` (stats) | main executable | arguments of `setReincarnateStats` in `initializeReincarnatedPartner` (partner_impl.c) | code constants | `data/rebirth`: stats per baby |
| `life.yaml: rebirth.babies` (species) and `evolution.yaml: rebirth.babies` | main executable (small data; declared by dooa.c and read by the DOOA_REL code) | `REINCARNATE_BABY_TYPE` (8 bytes) | data table | `data/rebirth`: species per baby |
| `evolution.yaml: baby_targets` | main executable | switch of `getFreshEvolutionTarget` (evolution.c) | constant switch | `data/evolution`: `fresh_targets` |
| `evolution.yaml: items.fixed` and `inventory.yaml: effects.evolution.fixed` | main executable | `handleEvoItems` (item.c), items ≥ 0x7d | code constants | `data/item_effects`: `evolves_to` |
| `item_use.yaml: refusal.fixed_level_evolutions` | main executable | `partnerWillRefuseItem` (partner_impl.c) | code constants | `data/item_effects`: `accepted_level` |
| `inventory.yaml: effects.status` | main executable | cases of `handleStatusItems` (item.c) | constant switch | `data/item_effects`: `battle_status_cure`, `protects`, `injury_cure`, `sickness_cure` |
| `inventory.yaml: effects.revive` | main executable | `handleRestore` (item.c) | code constants | `data/item_effects`: `revive_hp_percent`, `battle_status_cure` |
| `inventory.yaml: effects.battle_disk.disks` | main executable | switch of `handleBuffDisks` (main.c) | constant switch | `data/item_effects`: `battle_buff` |
| `inventory.yaml: effects.chip.script` | main executable | `handleChips`, case 0x16 (item.c) | code constants | `data/item_effects`: `script_section` |
| `battle.yaml: moves.buffs` | BTL_REL, with copies in STD_REL and VS_REL | switch of `BTL_applyBuffMove`, `STD_applyBuffMove`, `VS_applyBuffMove` (battle_main.c and its twins) | constant switch, with the overlay layout | `data/moves`: `buff`; `dwx` checks that the three copies give the same |
| `battle.yaml: moves.learned_together` | main executable | `learnMove` (utils2.c) | code constants | `data/moves`: `learned_with` |
| `battle.yaml: rewards.no_drop_maps` (criterion 2) | main executable | `battleStatsGainsAndDrops` (battle_ui.c) | code constants | `data/maps`: `no_drops` |
| `apps/sim/scenarios/care_scenario.cpp: MealEnergy, MealWeight` | main executable | `handleFood` (already extracted) | already exists | read `data/item_effects`, or invented values |
| `evolution.yaml: specials.*` (D1) | main executable | `handleSpecialEvolutions` (evolution.c) | code constants | `data/evolution`: `specials` |
| `evolution.yaml: stat_gains.scaled_targets` (D2) | EVL_REL | `EVL_applyEvolution` (evl.c) | code constants, with the overlay layout | `data/digimon`: `evolution_scales_stats` |
| `care.yaml: toilet.poop_eaters` (D2) | main executable | `tickPoopDetection` (partner_impl.c) | code constants | `data/digimon`: `eats_poop` |
| `battle.yaml: affinity.factors`, `joining.chances` (D3) | main executable | `TYPE_FACTORS`, `FLEE_CHANCES` | data table | `data/battle`: `affinity`, `join_chances` |
| `care.yaml: sleep.patterns` (D3) | main executable | `SLEEP_PATTERN` | data table | `data/raise`: `sleep_patterns` |
| `inventory.yaml: sort.orders` (D3) | main executable | `ITEM_SORT_ORDER` | data table | `data/items`: `sort_orders` |
| `evolution.yaml: moves.specialty_blocks` (D4) | — | — | derived on load | from `data/moves` (`special`); it is not stored |
| `item_use.yaml: refusal.never_taken`, `refusal.discipline_checked_below`, `meals` (D5) | main executable | `partnerWillRefuseItem`, `partnerHandleFoodFeed` (partner_impl.c) | code constants, or derived | `data/item_effects`: `refusal`, `meal` (`meal` is derived from the items with a food handler if it matches) |
| `inventory.yaml: effects.double_floppy` (D6) | main executable | `handleDoubleFloppy` (item.c) | code constants | `data/item_effects`, next to `HEALING_VALUES` |
| `inventory.yaml: effects.chip.tamer_level.items` (D7) | main executable | `handleChips` (item.c) | constant switch | `data/item_effects`: `chip_effect` |
| ~~`field/warps.yaml: scripted_warps`~~ (D10) | the map's script | — | `field/script_warps` (dwx) | deleted in p4-scripted-warps |
| `field/rules.yaml` and `packages/game/field/include/dwe/game/field/rules.hpp:27-29`: radius and speeds (D11) | main executable and the models | `DIGIMON_DATA.radius`; root motion of the animations | already extracted | `data/digimon` and the models |
| Test asserts that repeat (b) values (see "Other constants") | — | — | — | invented values in the stand-in packs, or tests with a disc |

The symbols and functions in these tables are named only to locate the data on the user's disc: neither the decomp's
code nor the tables' values are in this document or in git.

## Coordinator decisions

They were made by the coordinator, to whom the user delegated these decisions. Each one: "Decided by the coordinator
(2026-10-21)".

- **Interpretation criteria** (the four above): accepted as they are written.
- **A single entity or a list** (rule of criterion 2): "a single reference to an entity is (a); a list of two or more
  entities is (b)". That is why `effects.chip.toilet` (one item) is (a), the tamer level chips (three items) are (b),
  `rewards.no_drop_maps` becomes (b) as a per-map mark in `data/maps`, and `bell_item` and `repel_item` stay in (a).
- **D1. Special evolutions** (`evolution.yaml: specials.wake_up`, `specials.praise_scold`; 7 rules in
  `handleSpecialEvolutions`): **(b)**, to `data/evolution: specials`. Reason: they are a species → species evolution
  map. The ruleset keeps `roll_limit`, the moment of each mode and the strategy.
- **D2. Sets of species with a behavior** (`stat_gains.scaled_targets`, `toilet.poop_eaters`): **(b)**, per-species
  marks in `data/digimon` (`evolution_scales_stats`, `eats_poop`). Reason: they assign a behavior to several species,
  and a mod's new species declares the mark in its row.
- **D3. Category tables with their own symbol** (`TYPE_FACTORS`, `FLEE_CHANCES`, `SLEEP_PATTERN`,
  `ITEM_SORT_ORDER`): **they are extracted** to `data/battle` (`affinity`, `join_chances`), `data/raise:
  sleep_patterns` and `data/items: sort_orders`. They do not stay as (a). Reason: it is the cleanest legally and costs
  one `TableLocation` each.
- **D4. Technique blocks by specialty** (`moves.specialty_blocks`): **derived on load** from `data/moves` (the
  `special` field of each learnable technique). Reason: that way they are not stored anywhere.
- **D5. Item ranges** (`refusal.never_taken`, `refusal.discipline_checked_below`, `meals`): **(b)**, per-item marks in
  `data/item_effects` (`refusal: never | discipline`, `meal`). First it is checked whether `meals` matches the items
  whose handler is food; if it matches, it is derived and nothing is read. Reason: they are per-item properties.
- **D6. Double floppy** (`effects.double_floppy`): **(b)**, in `data/item_effects` next to `HEALING_VALUES`. Reason: it
  is the amount an item heals.
- **D7. Tamer level chips** (`effects.chip.tamer_level.items`, three items): **(b)**, per-item `chip_effect` in
  `data/item_effects`; the probability and the amount stay in the ruleset (a). Reason: it is a list of items. The
  toilet chip (`effects.chip.toilet`) is a single reference and stays in (a) by criterion 2.
- **D8. Training courses** (`training.yaml: places[].courses`): **(a)**. Reason: `trn` and `trn2` are code modules
  (TRN_REL and TRN2_REL), not maps; a course is the rule of a gym machine, per stat (category), not per species, item,
  technique or map.
- **D9. Locators** (addresses, sizes, opcodes, the per-message lists of `string_manifest.cpp`, `KnownScenes`):
  **(a)**. Reason: they say where each datum is on the user's disc, not what it is worth; it is the status of
  `ExecutableLayout`.
- **D10. Script warps** (`field/warps.yaml: scripted_warps`): **(b)**. **Deleted** in p4-scripted-warps (2026-10-21):
  the scripts' warps are extracted by `dwx` into `field/script_warps` with their conditions.
- **D11. Field radius and speeds** (`field/rules.yaml`, initial partner and player): **(b)**. The radius comes from
  `data/digimon` and the speeds from the model's root motion. Reason: they are values of a species, and that way a new
  partner walks at its own speed.
- **Test fixtures** (`apps/sim/tests/stand-in/`, `tests/mods/example_mod/`): accepted as invented, with a test with a
  disc that checks that no value of a fixture matches its entity's real table.
- **Spanish translation** (`content/packs/es-translation/`): moves to a separate content repository (package
  `p4-translation-split`).
- **Phase B plan and migration order**: accepted.

## Phase B plan

Goal: that no (b) table is in git, without changing the game.

### 1. Reading in `dwx`, per table

Three mechanisms, all per version. The addresses come from the decomp's `config/<version>/symbols*.txt`; the values,
from the user's disc. What is in the main executable is located with `ExecutableLayout`
(`packages/formats/src/exe/executable_layout.cpp`); what is in an overlay (EVL_REL, BTL_REL, STD_REL, VS_REL), with the
overlay layout of `packages/formats/src/exe/overlay_tables.cpp` (`findOverlayTablesLayout`), which gains the entries of
EVL_REL and BTL_REL. A version that is not located fails with a clear error, as today.

1. **Data table** (`TableLocation`, already exists): `REINCARNATE_BABY_TYPE` and (D3) `TYPE_FACTORS`,
   `FLEE_CHANCES`, `SLEEP_PATTERN` and `ITEM_SORT_ORDER`.
2. **Constant switch** (`ConstantSwitch`, already exists for `handleFood` and `handleChips`): `handleBuffDisks`,
   `BTL_applyBuffMove` (and its copies `STD_applyBuffMove` and `VS_applyBuffMove`), `getFreshEvolutionTarget`,
   `handleStatusItems`. Each with its `ValueLocation` per version. The three copies of the support boost are read and
   `dwx` fails if they do not give the same.
3. **Code constant** (new, `CodeConstant`): the address of an instruction and which operand carries the value
   (`addiu`, `ori`, `slti`...). `dwx` decodes the instruction, checks that it is the expected one and reads the
   immediate; if it is not, it fails and does not guess. It is used by:
   - the arguments of `setReincarnateStats`, the fixed targets of `handleEvoItems`, the level of
     `partnerWillRefuseItem`, `handleRestore`, case 0x16 of `handleChips`, `learnMove` and
     `battleStatsGainsAndDrops`;
   - decisions D1, D2, D5, D6 and D7.

   It lives in `formats/exe` next to `constant_switch`, with its page in `docs/modules/` and its tests.

### 2. Format in the base pack

YAML documents in `data/`, each with its schema in `content/schemas/` and references by id (as `tablesToYaml` does
today), never by number:

- `data/rebirth` (new): `babies: [{ digimon, hp, mp, offense, defense, speed, brain }]`.
- `data/evolution`: adds `fresh_targets: { <baby>: <target> }` and `specials` (D1).
- `data/item_effects`: adds per item `evolves_to` (also for the fixed ones), `accepted_level`, `battle_status_cure`,
  `protects`, `injury_cure`, `sickness_cure`, `revive_hp_percent`, `battle_buff: { offense, defense, speed, omni }`,
  `script_section` and the marks of D5, D6 and D7.
- `data/moves`: adds per technique `buff: { offense, defense, speed }` and `learned_with`.
- `data/maps`: adds the `no_drops` mark per map.
- D3: `data/battle` (`affinity`, `join_chances`), `data/raise: sleep_patterns`, `data/items: sort_orders`.

Each new field is optional in the schema (additive change): dw1fe and the tools that read `data/` keep working
([external tools](../architecture/external-tools.md)).

### 3. How the rulesets use it

- `ruleset.yaml: tables` adds the new roles (`rebirth: data/rebirth` and `battle: data/battle`).
- Each section loses its (b) keys; the system reads that data from its role's table, as `evolution` already does with
  `data/evolution`. The rules that remain keep their citation to the decomp.
- The duplication is resolved at the same time: the fixed item targets and the list of babies end up in a single
  place.
- A mod's ruleset brings its own tables per role (`extends` replaces whole tables), and a new species or item
  declares its fields in its row, without touching the ruleset.

### 4. Tests with invented data

- `formats/exe`: a test executable (`makeExecutableFixture`) with invented values at the layout's addresses and, for
  the switches and the code constants, hand-written MIPS instructions with invented immediates. Error cases: version
  not located, unexpected instruction, a switch that does more than assign.
- `apps/dwx`: the generated document validates against its schema and the ids are those of the tables.
- `packages/game/*` and `apps/sim`: the stand-in packs (`apps/sim/tests/stand-in/`) gain the new tables, with invented
  values that do not match the real ones.
- A test with a disc (name `the user's disc, extracted: ...`, label `slow`) per table: the extracted table has the
  expected entries and ids and the ruleset loads without error.
- The test asserts that repeat (b) values (see "Other constants") move to invented values of the stand-in packs or to
  tests with a disc that compare with the extracted table; those that only look at sizes stay.
- A test with a disc of the fixtures: no value of a fixture (`apps/sim/tests/stand-in/`, `tests/mods/example_mod/`)
  matches that of its entity's real table.

### 5. Acceptance criterion: the game does not change

"Decided by the coordinator (2026-10-21)": moving the per-entity tables to extraction **must not change the game**. A
test with a disc (`DWE_DISC` / `DWE_CONTENT`) runs the `original` ruleset before and after the migration on the real
data and demands the same behavior.

How it is built:

1. **Fixed, deterministic scenarios** in `apps/sim` (which already guarantees the same result with the same seed and
   any number of threads), with a `--trace` mode that writes one line per event (evolution, rebirth, item use, hit,
   technique learned) with its values:
   - each baby raised with fixed care plans until its first, second and third evolution (targets from
     `fresh_targets`, from the specials and from the items);
   - one death and rebirth for each seed of a fixed list (chosen baby and inherited stats);
   - each item with an effect used on a partner with fixed stats, in the field and in battle (cures, revive, disks,
     chips, evolution, refusals);
   - fixed battles that use each support technique and each pair of affinities;
   - the complete `loop` scenario of phase 3.

   Each (b) entry has at least one scenario that exercises it:

   | (b) entry | Scenario |
   |---|---|
   | `life.rebirth.babies` (stats and species), `evolution.rebirth.babies` | death and rebirth with each seed of the list |
   | `evolution.baby_targets` | each baby raised until its first evolution |
   | `evolution.items.fixed`, `inventory.effects.evolution.fixed` | each fixed-evolution item on a partner of each level |
   | `item_use.refusal.fixed_level_evolutions` | the same items with the partner one level below and above |
   | `item_use.refusal.never_taken`, `refusal.discipline_checked_below` | each item given with discipline 0 and 100 and a fixed roll |
   | `item_use.meals` | each item given hungry and not hungry |
   | `inventory.effects.status` | each status item in the field and in battle, sick and injured partner |
   | `inventory.effects.revive` | each revive item during the defeat countdown |
   | `inventory.effects.battle_disk.disks` | each disk used twice in a battle (boost and cap) |
   | `inventory.effects.chip.script` | the script chip with the script paused |
   | `inventory.effects.chip.tamer_level.items` | each chip of the list with a fixed roll |
   | `inventory.effects.double_floppy` | the double floppy in the field and in battle |
   | `inventory.sort.orders` | the full bag sorted with the three orders |
   | `evolution.specials.wake_up` | each rule, on waking up, with its source form and a fixed roll |
   | `evolution.specials.praise_scold` | each rule after praising or scolding, and the counter one |
   | `evolution.stat_gains.scaled_targets` | an evolution to each target that scales and to one that does not |
   | `evolution.moves.specialty_blocks` | an evolution per specialty: the technique it learns |
   | `care.sleep.patterns` | a whole day with each sleep schedule |
   | `care.toilet.poop_eaters` | each species of the list next to a poop, and one that is not on it |
   | `battle.affinity.factors` | a battle per pair of specialties |
   | `battle.joining.chances` | the battle start per pair of types with a fixed roll |
   | `battle.moves.buffs` | each support technique in a normal, tournament and versus battle (BTL, STD and VS) |
   | `battle.moves.learned_together` | learning the first technique of each pair |
   | `battle.rewards.no_drop_maps` | winning a battle in each map of the list and in one that is not on it |
   | `apps/sim` (care scenario meat) | the `care` scenario |
   | `field/rules.yaml` and `rules.hpp` (radius and speeds) | the partner follows the player along a fixed route |
   | `field/script_warps` (replaces `scripted_warps`) | the `room08` door with trigger 203 set and cleared (`packages/game/field/tests/field_tests.cpp`) |

   **Coverage:** the `--trace` mode notes each (b) entry that is read. The test fails if a migrated entry is not read
   at least once during the scenarios, so a new table without a scenario does not pass.
2. **Record before, compare after.** In the commit before the migration, `scripts/dev.sh parity-record` runs the
   scenarios with the current extraction and stores the trace outside git (`_work/parity/<sha>.trace`; the trace
   contains game values and is never versioned). After each migration commit, `parity-check` runs the same and
   compares line by line; the first difference fails the test with the scenario, the seed and the event.
3. **Comparison in the same process** when migrating each table, in two commits. In the first, the system learns to
   read the extracted table when its section does not carry the key, and the test with a disc writes in the build a
   copy of the `original` ruleset without that table's (b) keys; it runs the scenarios with both rulesets (the git
   one, with the values in the YAML, and the copy, which reads the extracted table) and demands identical traces. The
   second commit deletes the keys from the YAML. That way parity is checked without depending on a hand-recorded
   trace.
4. The test is part of `check-full` (which already receives `DWE_DISC` and `DWE_CONTENT`), and re-extraction is done in
   a new directory (`_work/content-f5-<tag>`), as the package's rules require.

### 6. Migration order

Each family of tables goes in its two commits (point 3
of the acceptance criterion), with its extractor, schema, tests, ruleset change and parity test green. The test
asserts that repeat (b) values of that family (see "Other constants") change **in those same commits**, at the latest
in the second, because the second deletes the YAML keys they read. Order:

1. Record the parity trace and add the `--trace` mode and the scenarios (without changing data).
2. `CodeConstant` in `formats/exe`, with its tests of invented instructions.
3. `data/rebirth` (table `REINCARNATE_BABY_TYPE` + constants of `setReincarnateStats`): `life.yaml` and
   `evolution.yaml`.
4. `data/evolution: fresh_targets` (`getFreshEvolutionTarget`).
5. `data/item_effects`: statuses, revive, disks, script chip, fixed targets and acceptance level.
6. `data/moves`: support boosts and techniques that are learned together.
7. `apps/sim`: the care scenario's meat comes from `data/item_effects`.
8. `data/maps: no_drops` (criterion 2).
9. Decisions D1 to D11, from the cheapest to the most expensive: D4 (derive), D3 (data tables), D6, D7, D5 (first
   check whether `meals` is derived), D2, D11 (also `packages/game/field/include/dwe/game/field/rules.hpp:27-29`), D1;
   D10 with the Lua scripts.
10. Final review of the test asserts: none repeats a (b) value any more; those that remain move to invented values or
    to tests with a disc.
11. The fixtures test.
