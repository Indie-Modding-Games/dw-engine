# evolution

The partner's evolution (`dwe::game::evolution`, `game` layer): when it changes form, to which one, and what it takes
along when it changes (stats, techniques, counter). What the original did is in the
[evolution sheet](../../reference/evolution.md); how the rules are read is in
[rulesets](../../architecture/rulesets.md).

It does not know the partner: it receives an `EvolutionInputs` with what it reads (form, stats, weight, care mistakes,
discipline, happiness, tiredness, battles, learned techniques, age, counter, map, whether the tamer is free, virus form,
copy of the virus form) and returns an `EvolutionResult` that the partner applies. It only keeps its own state
(`EvolutionState`): the evolution counters and the copy of the virus form belong to the partner. It does not include
other `game` modules; the session is the glue.

## Usage

```cpp
auto evolution = dwe::game::evolution::Evolution::load(vfs, ruleset);   // evolution section + the manifest's tables
dwe::game::evolution::EvolutionState state;                             // goes in the saved game
dwe::ruleset::Random random{seed};

// On each clock advance, with the frames it ran (GameClock):
const dwe::game::evolution::CheckSpan span{.frames = advance.frames, .frameInHour = frameInHour,
                                           .framesPerHour = framesPerHour};
const auto check = evolution->checkNatural(inputs, state, span, random);
partner.evolutionTimer = check.evolutionTimer;
if (check.evolution) {
    applyToPartner(*check.evolution);                                   // the session
    dwe::game::evolution::recordEvolution(state, *check.evolution);
}

// Events:
evolution->wakeUp(inputs, state, random);                               // on waking up
evolution->praiseOrScold(inputs, state, {.scolded = true, .disciplineBefore = 0, .happinessBefore = -100}, random);
evolution->fillVirusBar(inputs, state);                                 // the partner reported the full bar
evolution->useItem(inputs, state, "giga_hand");                         // item id in data/items
evolution->returnFromVirus(inputs, state);                              // script routine 2 (reads inputs.virusBackup)
evolution->evolveByScript(inputs, state, target);                       // script routine 10
evolution->rebirth(inputs, random);                                     // on dying
```

- **`checkNatural`** looks once per game clock frame, like `tickConditions`: it goes over each counter value the span
  passed through (`CheckSpan`; so that advancing the clock gives the same as playing it), with the normal choice from
  each level's threshold, and the special one at the champion's 360 hours rolls once for every frame of that hour
  until a form comes up. `CheckSpan::hourly(n)` is a once-per-hour check (simulation, tests). The virus bar does not:
  its limit and its form are in the `care` section (`toilet.virus_bar_limit`, `toilet.virus_form`), the partner reports
  with `VirusBarFull` and `fillVirusBar` leads to the form in `EvolutionInputs::virusForm`. The counter it returns
  already includes the hours after the evolution. The partner is still the one that raises the counter.
- **`EvolutionResult`**: target form (the one that is loaded), reason (`Normal`, `Special`, `Item`, `Virus`, `Script`,
  `Rebirth`), stat change or restored stats (see below), weight, care mistakes and battles, counter, extra hours of
  life, chance of raising the tamer's level, learned technique (index in `data/moves`), equipped slots (positions in the
  form's list), whether the virus bar goes back to 0, the copy to save on entering the virus form and whether the item
  mark stays.
- **Stats**: evolution caps nothing. `statChange` is what each stat changes by (negative if a scale lowers it) and the
  session adds it with `Partner::addStatGains`, which keeps each gain at 999 at most and each stat at its cap
  (`care.limits`). On leaving the virus form, in contrast, `restoredStats` brings the stats rebuilt from the copy,
  uncapped; the session sets them with `Partner::setStats` and then leaves them at each stat's cap (`care.limits`: 9999
  HP and MP, 999 the rest), like `EVL_clampBaseStats`, but without the cap of 999 per gain (decided by the design lead,
  2026-10-20); `statChange` stays at 0.
- **Virus form copy**: on entering the virus form, `EvolutionResult::virusBackup` brings the previous form and stats.
  The session saves both in `PartnerState::virusBackup` (the original saves the form in the script variable
  `PSTAT_SUKAMON_BACKUP_DIGIMON`; decided by the design lead, 2026-10-20: it goes with the partner), and returns them in
  `EvolutionInputs::virusBackup`, which `returnFromVirus` and the stat rebuild read.
- **`EvolutionState`**: forms already raised and the used-item mark. `toYaml`/`fromYaml` save the forms by id, in the
  section version `EvolutionStateVersion` (for the `packages/game/save` migrations); the mark is not saved, as in the
  original.
- **Requirements** (`report.hpp`): `report(inputs, state, target)` gives the `RequirementReport` of a target with a
  requirement: what each of the four groups asks for (care mistakes, weight, stats, extra) and how the partner stands,
  with the points that the same `OriginalScoring` that decides the evolution adds (`score` comes from it; without the
  preference for unraised forms, which depends on the best candidate). `outlook(inputs, state)` gives the partner's
  `EvolutionOutlook` for the evolution chart: counter, the hours of its level's check, one report per target of its
  card, the form that the next check would pick with the partner as it is (`next`, the same `select` as `checkNatural`)
  and the rookie's fallback. The pause menu chart uses it
  ([game.md](../apps/game.md#evolution-requirements)); the tests in `report_tests.cpp` check that `next` is what
  `checkNatural` picks when the hour arrives.
- **Simulation** (`simulation.hpp`): `simulateRaisings(evolution, generator, n, seed)` runs `n` raisings with an input
  generator and returns a histogram of targets, for the tuning panel.

## Rules and strategies

The `evolution` section of the ruleset
([`rules/original/evolution.yaml`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/packs/game/rules/original/evolution.yaml),
[schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_evolution.schema.json)) has the numbers with their decomp citation: hours of
each level, the babies' targets, scoring, fallback, specials (as data rules: target, roll, source forms and
requirements), virus, fixed items, stat gains, techniques, the rebirth babies and the oddities in `quirks:`.
`selection` and `scoring` name the strategies (the `original` of each one, in `strategies.hpp`).

The tables come from the ruleset's manifest (`tables:`): `digimon`, `raise`, `moves`, `evolution` and `item_effects`,
read with the readers in [`assets`](../engine/assets.md#techniques-and-evolution). `EvolutionTables` resolves the
references to indexes; a card target that is a raw number outside the table is ignored.

The targets of the evolution items (`EVOLUTION_ITEM_TARGET`) come from `evolves_to` in `data/item_effects`, which `dwx`
extracts from the executable, for the items with `handler: evolution`. The optional `item_targets` list of
`data/evolution` (`dwx` does not write it) replaces those targets item by item, and is the only source if the ruleset
does not name `item_effects`. The `original` ruleset names it, so content extracted before `dwx` wrote it has to be
extracted again.

| Oddity (`quirks:`) | With `true` (original) |
|---|---|
| `evolution_item_sticks` | After using an evolution item, no evolution gives stats or extra life |
| `selection_average_carries_over` | The average of a candidate that does not improve carries over to the next |
| `unlisted_baby_restarts_timer` | A baby with no target starts its counter over (decided by the design lead (2026-10-20): faithful to the original) |
| `rookie_stat_tie_picks_last` | In a rookie stat tie the last one counts |
| `timer_special_overrides_normal` | At the champion's 360 hours, a failed special cancels the normal choice (decided by the design lead (2026-10-20): faithful to the original) |

## Hidden content

Optional improvement, off by default: the `oculto` ruleset ("Original + hidden content", `extends: original`) gives a
natural path to three forms that the disc carries complete but that the original only gives with items: `species_k`
(champion), `species_m` and `species_o` (ultimates), and to `species_q` (ultimate), which the disc only carries as a
boss. It is available through Folder Mode's `oculto` ruleset: the new-save mode selector defines the save type and
replaces the old hidden-content activation ([mode selector](../../architecture/game-variants.md#choosing-a-variant)).
The chosen ruleset stays in the saved game; the `original` ruleset stays the same. The items that give them
(`noble_mane`, `giga_hand`, `metalbanana`) keep working with both.

All of it is in `rules/oculto/species_extra.yaml` (the section for [per-species
patches](../../architecture/rulesets.md#per-species-patches)), with design numbers of our own, not game data:

| Form | From | Requirement (3 of 4 groups, like the original) | Gate |
|---|---|---|---|
| `species_k` | `species_ak`, `species_l` | attack 150, defense 100, speed 120; 0–2 mistakes; weight 25 ± 5; 30 techniques or coming from `species_l` | `species_aj` lives in the city (trigger 248) |
| `species_m` | `species_bb`, `species_r`, `species_ay`, `species_n` | HP 4000, MP 4000, attack 500, defense 400, speed 300, brains 300; 0–5 mistakes; weight 60 ± 5; 60 battles | Metalic Cup (16) won at some point |
| `species_o` | `species_p`, `species_ar` | attack 500, speed 500, brains 400; 0 mistakes; weight 20 ± 5; 100 battles or coming from `species_p` | `species_s` defeated and in the city (trigger 242) |
| `species_q` | `species_r`, `species_bk` | MP 5000, speed 400, brains 500; 0–5 mistakes; weight 30 ± 5; discipline 80 | `species_q` rescued in the lab of Grey Lord's Mansion (trigger 129, end of script 64) and at night (20 to 6) |

- **Gates.** `SpeciesGate` (story triggers, time of day, cup won) is checked before scoring: if it is not met, the
  target does not score (`OriginalScoring` gives 0 points and `RequirementReport::gateMet` is `false`), so the check
  does not pick it and the tree's requirements panel says so in the first row ("Special condition"). `EvolutionInputs`
  carries `trigger` (a trigger reader), `hourOfDay` and `cupWon`; without a reader, a trigger or hour gate is closed.
- **Slots.** Each new target takes the first free slot of `to` **after** the origin's last target, so in a tie the
  original's target wins (the plan said slot 0 for `species_ak`; it was changed so as not to touch the priority of the
  usual targets).
- **Cups.** The `cup` gate is the cup number in `data/tournaments`. `EvolutionAdapter::cupReader` reads it from
  `TournamentAdapter::cupWon` (the trigger 15 + cup that the won cup sets and that is saved with the game); without
  tournaments (`setCupSource` not called, as in the adapter's tests alone) `cupWon` is empty and the gate is open. The
  prizes go in `extra_prizes` of the same file, which the tournaments add to those of `tournament.yaml`: Noble Mane (126)
  in the Animal Cup (20, grade Z), Giga Hand (125) in the Metalic Cup (16, grade M) and Metalbanana (127) in the Filth
  Cup (17, grade T).
- **`species_q` as a partner.** It has no rows on the disc, so the patch designs them: gains 6000/7000/600/500/700/700,
  raising copied from `species_r` with Meat as favorite food and the finisher of `species_r` (`death_claw`) in its first
  empty slot (the fifth). `effect_type: devi` gives its techniques the launch points of `species_r` (`BattleAdapter`
  puts it in `effects.original_types`). Its model is an enemy's and has no care or gym clips: `clips` says which one
  plays in place of each (walks badly → walks, happy → victory, eating → technique 0, sleeping and statuses → idle in a
  bad mood, tired → battle tiredness, gyms → guard and block; the finisher, clip 51, plays technique 0).
  `FieldScene::loadActor` adds them when loading the partner with `assets::addClipStandIns`, which copies the clip that
  exists under the missing id and never overwrites one of its own; this way loading the partner still refuses a model
  that lacks a clip (`MissingClips::Refuse`).
- **Raised.** Species from 0x3f on do not fit in the original's raised triggers (200 + species up to 0x3e):
  `StoryAdapter` keeps them in its own list, which goes in the game's evolution section and which a new game empties.
  The tree counts them as discovered with it; the medal for the 61 still counts only those of the original.
- **Hours.** `Evolution::checkHoursFor(species)` gives a form's check hours (those of its level, or those of the Mega
  step for an ultimate when the ruleset has it).

Tests: `packages/game/evolution/tests/hidden_tests.cpp` (trigger and hour gate, cup gate, Mega step),
`clip stand-ins fill only the clips a model lacks, with copies of ones it has` (`packages/engine/assets/tests/model_data_tests.cpp`),
`the user's disc, extracted: the hidden content's paths and gates` and `the user's disc, extracted: the Mega step's
paths` (`extracted_tests.cpp`, with `DWE_CONTENT`: the
targets after the original's and each path picked by the check),
`the game pack's optional rulesets follow their schemas and read` (`packages/engine/ruleset/tests/species_extra_tests.cpp`)
and `extra_prizes: the hidden content's species patches give each new form's item`
(`packages/game/tournament/tests/cup_extras_tests.cpp`), `a species past the raised triggers is kept apart, and a new game clears it`
(`packages/session/tests/evolution_adapter_tests.cpp`). Routes: `route-hidden-content.yaml`,
`route-hidden-content-off.yaml` and `route-mega-step.yaml` ([vertical slice](../../guides/vertical-slice.md)).

### Ultimate → Mega step

Another new-game option, off by default and only with the hidden content: the `oculto_mega` ruleset
(`extends: oculto`) adds the `mega_step` section (`rules/oculto_mega/mega_step.yaml`). An ultimate checks again at 120
hours with the same scoring; reaching Mega resets the counter, gives 72 hours of life and raises the tamer level
(`tamer_level_chance: 100`). A Mega is the last step. In the other rules (training, tournaments, prosperity) a Mega still
counts as level 5.

| Form | From | Requirement | Gate |
|---|---|---|---|
| `species_o` (Mega) | `species_s` (instead of `species_p` and `species_ar`) | the same as before: attack 500, speed 500, brains 400; 0 mistakes; weight 20 ± 5; 100 battles or coming from `species_s` | the same as before (trigger 242) |
| `species_t` (Mega) | `species_u`, `species_m` | HP 8000, MP 6000, attack 700, defense 600, speed 400, brains 400; 150 battles | Mt. Infinity finished (trigger 50, which script 184 sets after the final battle; assumption) |

- **`species_t` as a partner.** Designed rows: gains 9999/9999/999/900/700/800 and raising from `species_u` with seven
  meals instead of six and weight 70. It fights as type 3 (virus; the boss is 0, no type: `partner_type`) and its
  Infinity Cannon hits for 240 with 1 iframe instead of 777 with 38 (`partner_moves`). `BattleAdapter` adds that copy to
  the technique catalog (`MoveCatalog::addVariant`: same id, after the table) and `Combatant::moveVariants` uses it only
  for the partner; the boss and any other fighter keep the table's. It has the same clip stand-ins as `species_q` and in
  the field it is drawn at 0.6 (`field_scale`: radius 400 against `species_u`'s 220).
- **Without the option** nothing changes: an ultimate does not check again and `species_t` is not reached.

Tests: `a partner's move variant replaces the table's move for it alone` (`packages/game/battle/tests/core/battle_tests.cpp`),
`a ruleset's species patches give the partner its own type and move values`
(`packages/session/tests/battle_adapter_tests.cpp`) and those of "Hidden content".

The canonical stages (Mega for `species_v`, `species_bd`, `species_o` and `species_t`) are UI only: the rules still see
level 5 ([game.md](../apps/game.md#canonical-stages)).

### Rare evolution items

Optional improvement, off by default ("Rare items" setting, [game.md](../apps/game.md#rare-items)). The two level 5
forms that have no evolution item in the original, `species_q` (base id `vand`) and `species_t` (base id `muge`), each
gain one: `game:blood_moon` and `game:steel_heart`, in `data/items.yaml` and `data/item_effects.yaml` of the `game` pack
(never in the base data), after the original's items. They are used like the original's level 5 ones (`handleEvoItems`,
the same as `red_ruby` or `beetlepearl`): only with the partner one level below the target form, the same animation and
no extra stats. At run time they reuse the icon of those two items (`icon: <id>` in the pack's row), and their
description is built at run time with the species name (`description_species`, the `{species}` key of the text
"Digivolve to {species}"), so no species name goes into the repository. The sell price, the order and the saved game use
their `game:` id.

Where they come up: the `rare_items` section of the `oculto` and `oculto_mega` rulesets (`rules/*/rare_items.yaml`,
schema `ruleset_rare_items`) gives `chance_percent` (1), the `pool` list and, per map, the spots (`spots`) of random
items in its entry section and the trigger that turns them off (`unless_trigger`). For now only `trop06` (the seven
spots of script 18, without trigger 236). With the option on, `FieldItemsAdapter::loadMap` rolls each spot on entering
the map (with the session's randomness, before the script header): if it comes up, it leaves a rare item from the group
at that spot and the script's `spawn_item` for that tile is not done. Off, nothing is rolled and the randomness does not
change.

| Ruleset | Group |
|---|---|
| `oculto` | `game:blood_moon` (`species_q` only becomes a partner with the hidden content) |
| `oculto_mega` | `game:blood_moon` and `game:steel_heart` (`species_t` only comes with the Mega step) |

A rare item does not come up while it is in the bag or already on the ground; after using it, it can come up again, like
any evolution item that is bought or won again. If a stack of tables does not carry the target species (base test
content), its effect row is skipped with a warning and the rest of the pack loads.

Tests: `packages/session/tests/rare_items_tests.cpp` (schema and rulesets, probability with a fixed seed: 1% of 140,000
rolls and the same ones with the same seed, the adapter with the option off and on, save and load with `game:` ids),
`a pack's evolution item, a row with no original number, works as an Ultimate's does`
(`packages/game/evolution/tests/specials_tests.cpp`), the icon composition and the description's species in
`table_composition_tests.cpp` and `bag_view_tests.cpp`.

## All species as a partner

Improvement on by default (an exception to the rule that the original is the default, decided on 2026-11-10): the 49
species with a model that the original does not let you raise come up by evolving in a normal game. Rulesets `todas`,
`todas_oculto` and `todas_oculto_mega`, section `all_species`; off, the game uses `original`, `oculto` or `oculto_mega`
and is the original. Design, tree and table: [extra-evolutions.md](extra-evolutions.md).

The selection goes by levels (`originalFirst` in `strategies.cpp`): first only the original's targets; those of
`all_species` (`EvolutionTables::yields`) only when none of the original's pass. Those of the `koro` line come up through
wake-up specials after the original's. A reserve form that does not learn a technique on evolving fights with the first
one in its list (`fightWithFirstMove`, the same one the tricks evolution uses).

## Parity with the decomp

| Rule | Decomp | Our code / section | Test | Status |
|---|---|---|---|---|
| Thresholds 6/24/72/144 h and the check with the tamer free | `partner_impl.c`: `tickConditions` | `Evolution::checkNatural`, `check_hours` | `each level checks from its own hours…`, `no natural evolution while the tamer is busy` | Same |
| One check per clock frame, also when advancing it | `clock.c`: `tickGameClock`; `partner_impl.c`: `tickConditions` | `checkNatural(CheckSpan)` | `a check over several hours…`, `a span checks each counter value…`, `at the timer hours the special rolls on every checked frame…` | Same |
| Baby → fixed in-training | `evolution.c`: `getFreshEvolutionTarget` | `OriginalSelection` (`babyTarget`), `baby_targets` | `a baby becomes its in-training form…` | Same |
| Baby with no target restarts the counter | `evolution.c`: `getFreshEvolutionTarget` | `quirks.unlisted_baby_restarts_timer` | `quirk unlisted_baby_restarts_timer…` | Same (the uninitialized value is not reproduced: it does not evolve); decided by the design lead (2026-10-20): faithful to the original |
| Care points, weight, stats and bonus | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `requirements` | `a target scores a point each…`, `the battles bonus…`, `the happiness and learned moves bonuses` | Same |
| Only learned techniques with an index below 64 count | `evolution.c`: `getNumMasteredMoves` (two 32-bit words) | `requirements.mastered_moves_below`, `countMasteredMoves` | `the moves requirement counts only the learned moves below…` | Same |
| Rookie's favored stat and tie | `evolution.c`: `calculateRequirementScore` | `OriginalScoring`, `quirks.rookie_stat_tie_picks_last` | `a rookie target scores…`, `quirk rookie_stat_tie_picks_last…` | Same |
| Preference for unraised forms | `evolution.c`: `calculateRequirementScore` | `OriginalScoring` (`preferNew`) | `against a best candidate…` | Same |
| In-training: most points, first on a tie | `evolution.c`: `getInTrainingEvolutionTarget` | `OriginalSelection` (`bestScored`) | `an in-training partner takes…` | Same |
| Rookie and champion: best average, accumulator | `evolution.c`: `getRookieEvolutionTarget`, `getChampionEvolutionTarget` | `OriginalSelection` (`bestAverage`), `quirks.selection_average_carries_over` | `rookies and champions take…`, `quirk selection_average_carries_over…` | Same (without dividing by 0) |
| Rookie fallback at 96 h | `evolution.c`: `getRookieEvolutionTarget` | `fallback` | `a rookie with no candidate…`, slow `the known paths` | Same |
| Specials on waking up | `evolution.c`: `handleSpecialEvolutions` (mode 2) | `Evolution::wakeUp`, `specials.wake_up` | `waking up: …`, `a special needs its roll…`, `one roll for every rule…` | Same |
| Specials on praising or scolding; scold warning | `evolution.c`: `handleSpecialEvolutions` (mode 3); `partner_impl.c`: `handlePraiseScold` | `Evolution::praiseOrScold`, `specials.praise_scold` | `praise or scold: …` | Same |
| The 360 h special that overrides the normal one | `partner_impl.c`: `tickConditions` | `praise_scold_on_timer`, `quirks.timer_special_overrides_normal` | `a champion at the timer hours…`, `quirk timer_special_overrides_normal…` | Same; decided by the design lead (2026-10-20): faithful to the original |
| Virus bar and the copy on entering | `partner_impl.c`: `tickConditions` | `care.toilet.virus_bar_limit` and `virus_form` (owner: `care`), `Evolution::fillVirusBar` with `EvolutionInputs::virusForm` (the partner's warning), `EvolutionResult::virusBackup` (the partner saves it, with the form) | `the partner's full virus bar…` | Same |
| Return from the virus form and rebuilt stats | `partner.c`: `callDigimonRoutine` (2); `evl.c`: `EVL_scaleBaseStats` | `Evolution::returnFromVirus`, `EvolutionInputs::virusBackup`, `EvolutionResult::restoredStats` | `back from the virus form…`, `the stats rebuilt from the virus backup are left to the session to clamp` | Same: they are restored with `setStats` and the session leaves them at each stat's cap, like `EVL_clampBaseStats`; without the cap of 999 per gain (decided by the design lead, 2026-10-20) |
| Evolution by script | `partner.c`: `callDigimonRoutine` (10) | `Evolution::evolveByScript` | `effects_tests.cpp` | Same |
| Fixed and table evolution items | `item.c`: `handleEvoItems` | `Evolution::useItem`, `items`, `EvolutionTables::itemTarget` (`data/item_effects`, `item_targets`) | `a fixed evolution item…`, `an item of the item table…`, `the evolution items' targets come from the item effects…`, slow `every item of EVOLUTION_ITEM_TARGET evolves a form…` | Same |
| Contagious item mark | `item.c`: `handleEvoItems`; `evl.c`: `EVL_applyEvolution` | `quirks.evolution_item_sticks` | `quirk evolution_item_sticks…` | Same |
| Stat gains and scale | `evl.c`: `EVL_applyEvolution`, `EVL_scaleBaseStats` | `stat_gains` (`EvolutionEffects`), `EvolutionResult::statChange` | `each stat that reaches the reference…`, `the listed forms…` | Same |
| Stat caps | `evl.c`: `EVL_clampBaseStats` | `Partner::addStatGains` (`care.limits`), not the evolution | `packages/game/partner`: `stat_gain_tests.cpp` | Approved deviation: in addition, each gain stays at 999 at most (`stat_gain_max`, 2026-10-20) |
| Weight, care, battles, counter, life, tamer level | `evl.c`: `EVL_applyEvolution` | `stat_gains` | `effects_tests.cpp` | Same |
| Learned technique and slots | `evl.c`: `EVL_applyEvolution`; `utils2.c`: `learnMove` | `moves`; the session gives them to `BattleAdapter::evolveMoves` | `the form learns the weakest move…`, slow `every target of every chart…`, `an evolution teaches the new form's move…` | Same |
| Rebirth | `evolution.c`: `reincarnatePartner`; `dooa.c` | `Evolution::rebirth`, `rebirth` | `rebirth turns the partner…` | Same (the stat inheritance and the lives belong to the partner) |
| Item refusal | `partner_impl.c`: `partnerWillRefuseItem` | — | — | Out of scope (item system) |

The slow tests (`the user's disc, extracted: …`) load the evolution with the extracted tables (`DWE_CONTENT`), run all the
checks over all the forms with random inputs, check known paths, check that each target of each card passes with inputs
that meet its requirement, and simulate a thousand raisings of each in-training form.
