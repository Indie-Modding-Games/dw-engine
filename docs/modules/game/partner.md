# partner

Partner care (`dwe::game::partner`, `game` layer): hunger, sleep, toilet, sickness and injuries, happiness and
discipline, tiredness, lifetime, age, death and rebirth, and training. What the original did is in the
[care sheet](../../reference/care.md); this module implements it with the `original` ruleset and says below,
rule by rule, where it matches and where it does not.

It does not draw, does not save games, does not launch scripts and does not know the inventory: the session gives it the clock, the orders as
effect structs and what it needs from the rest of the game (`CareContext`, `SleepContext`), and reads the state and the
events. It depends only on `core`, `assets` and `ruleset`.

## Usage

```cpp
auto ruleset = dwe::ruleset::loadRuleset(vfs, "original", dwe::game::partner::partnerSectionChecks());
auto rules = dwe::game::partner::loadPartnerRules(*ruleset);
auto species = dwe::game::partner::loadSpeciesTable(vfs, *ruleset);   // digimon and raise tables
dwe::game::partner::Partner partner{*rules, *species, seed};
partner.start("agu", stats, clock);

// Each tick: the clock advances and the partner goes through each frame crossed.
const auto advance = clock.advanceFrames(frames);
partner.tick(clock, advance, {.tamerIdle = idle, .scriptIdle = noScript, .area = area});
for (const auto& event : partner.takeEvents()) { /* UI, saving, scripts */ }

// The tamer's orders.
if (!partner.refuses({.item = id, .check = RefusalCheck::FavoriteOnly})) {
    partner.eat(foodEffectOf(id), clock);
}
partner.scold();
partner.praise();
if (partner.canSleep()) {
    partner.sleep(clock, {.hasPillow = pillow, .area = area});   // moves the clock to the wake-up time
}
partner.useToilet();
partner.cure({.injuryPercent = 100, .sicknessPercent = 20});    // bandage
partner.skipHours(hours, clock);                                 // rest; the session moves the clock afterwards
partner.applyBattle(consequences, clock);  // the battle already computed it (BattleAdapter) with the numbers of `battle`
auto gains = partner.train({.place = "trn", .course = "hp"}, clock);           // +1 hour
partner.addStatGains(evolutionGains);                                           // each gain, 999 at most
```

- **Time.** `tick` goes through the frames the clock crossed and applies each rule on the frame of the day on which the
  original applies it (`frameOfDay % N == 0`), so advancing a whole day at once is the same as playing it (there is a test).
  Sleeping, fighting and training move the clock with their own counts, like the original, and do not go through the
  frames they skip.
- **Orders.** Eating (`FoodEffect`), curing (`CureEffect`), scolding, praising, sleeping, the toilet and the Portable Potty, eating
  poop (`eatPoop`, for the species that do it), leaving the field scene's poop (`leaveWildPile`, with its
  effects, and `finishWildPoop`, the tamer's level), resting (`skipHours`), battle (`BattleConsequences`, with the
  numbers of the `battle` section) and training (`TrainingRequest`). The inventory owns the item table; the session translates an item into its
  effect. Stat gains that come from outside (evolution, battle) go through `addStatGains`, which applies the cap on
  each gain (`care.limits.stat_gain_max`) and the cap on each stat; `setStats` sets the stats as they are, without a cap (HP
  and MP after a battle). The lifetime that items give or take goes through `addLifetime`.
- **Events** (`EventKind`, `takeEvents`; `pendingEvents` looks at them without taking them out): needs that start, hunger window closed, care mistake (with its cause),
  poop on the ground (with its size, for `PoopPiles`), refusal, `Slept` (on waking up) and `NewDay` (each midnight
  crossed while playing, sleeping, fighting or training), trained, lifetime lost, death, rebirth, evolution (`Evolved`, when the species changes with `changeSpecies`), change of the tamer's
  level (already rolled; the session limits it to 0..10), virus bar full and scolding at the worst moment.
- **Cap on gains.** Every gain of a stat stays at 999 at most (approved deviation,
  2026-10-20): training, meals and chips (`FoodEffect::permanentGain`), the inheritance on rebirth and
  `addStatGains` all go through the same function. HP and MP still reach 9999 by adding gains. Its two
  effects are accepted (decided by the design lead, 2026-10-20): an item of +1000 gives +999, and the average with the
  reference stats of an evolution rises 999 at most. The return from the virus form does not go through here: `setStats` and then
  the cap of each stat (see below).
- **Death and rebirth.** The partner emits `Died` (value 1 if natural); the session plays the death and calls
  `rebirth(clock, tamerLevel)`.
- **Evolution.** The partner keeps the counters (`EvolutionCounters`) and warns about the virus bar; the evolution module
  decides and the session applies the result with `changeSpecies`, `addStatGains` or `setStats`. The copy of the
  virus form (`VirusBackup`) saves the previous form and stats, as the original saves the form in the script
  variable `PSTAT_SUKAMON_BACKUP_DIGIMON` and the stats in `sukaBackup*` (decided by the design lead,
  2026-10-20: the form goes with the partner). On returning from the virus form the session sets the rebuilt stats with
  `setStats` and leaves them at the cap of each stat in `care.limits` (9999 HP and MP, 999 the rest), without the cap per
  gain (`packages/session`: `EvolutionAdapter`).
- **Need deadlines.** `needDeadlinesOf(state, rules.care, clock)` (`need_deadlines.hpp`) says, for
  each need it has, how many frames are left until it does lasting damage if nobody attends to it: the toilet until
  it goes on itself, hunger until the window closes, sleep until the wake-up time, the injury until it
  turns into sickness and the sickness until it loses a life; tiredness has no deadline. No rule reads it: it is used by the HUD's
  stacked bubble (improvement, [ui.md](../../architecture/ui.md#field-status)) to sort them.
- **Poop.** `PoopPiles` keeps the world's poop like the original: in front of the partner, one random step away if the
  cell is occupied, 16 per map and 100 in total, reusing the oldest ones.
- **Saving.** `toYaml`/`fromYaml` with `PartnerStateVersion` (`state_yaml.hpp`); the session wraps them in a
  `SectionSerializer` with its migrations. Version 2 adds the species to the virus form's copy:
  `addVirusBackupSpecies` upgrades a version 1 game, which did not have it, with an empty species. `PoopPiles::slots()`/`restore()` for the poop. `toilet.wild_poop_due` (a poop waiting for its field
  scene) is optional: an earlier game does not have it and has none waiting.

## Data

- `rules/original/care.yaml` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_care.schema.json)): all the numbers of
  care, each with the decomp function it comes from, the names of the strategies and the `quirks`.
- `rules/original/life.yaml` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_life.schema.json)): lifetime, lives, unhappiness and
  rebirth (babies, fixed stats and inheritance).
- `rules/original/training.yaml` ([schema](https://github.com/Indie-Modding-Games/dw-engine/blob/main/content/schemas/ruleset_training.schema.json)): aptitudes, levels,
  bonuses, places and courses.
- Tables from the user's disc, by ruleset role: `digimon` (level) and `raise` (meal schedule, energy, toilet,
  favorite food, sleep pattern, aptitude and weight), read with `assets::loadDigimonTable` and `loadRaiseTable`.

The small tables that in the original are in the code (`SLEEP_PATTERN`, the babies' stats on rebirth) are
ruleset constants in the YAML files, with their citation.

## Strategies

The logic that can be redesigned goes behind `StrategyRegistry` (see
[rulesets](../../architecture/rulesets.md)); `original` is the game's:

| Path | Interface | What it decides |
|---|---|---|
| `care.strategies.care_mistakes` | `CareMistakeRule` | Whether an unattended need counts as a care mistake |
| `care.strategies.sickness_roll` | `SicknessRoll` | Whether the hours without sleep make it sick this hour |
| `care.strategies.item_refusal` | `ItemRefusalRule` | Whether it refuses an item |
| `life.rebirth.baby_choice` | `RebirthChoice` | Which baby it is reborn as |
| `training.gain_formula` | `TrainingGainFormula` | How much a session gives |

**Rebirth is a strategy** because it is the rule a redesign is most likely to change: the original picks a
baby at random (`rand() % 4` in `dooa.c`) and a mod may want it to depend on the dead species, on the mistakes or on
a player's choice. The stat inheritance (a % per tamer level, only with natural death) is numbers in
`life.yaml`; the choice of the egg is logic and so it goes behind an interface that receives the dead partner.
`Partner::rebirth` uses the egg from the box if lives remain; without them only if `life.rebirth.random_egg_without_lives`
is `false` (in the original it is `true`: the egg comes out at random). The option "Choose the egg after losing all lives"
turns it off ([game.md](../apps/game.md#quirks-as-options)).

## Quirks

Each quirk of the sheet that the game notices goes behind a `quirks:` flag (all `true` in `original`):

| Flag | Quirk |
|---|---|
| `sleeping_hungry_keeps_weight` | Sleeping hungry does not lower weight |
| `sleep_adds_default_hours_to_evolution` | Sleeping adds the pattern's sleep hours to the evolution counter |
| `sickness_chance_wraps` | The chance of getting sick lives in a byte and a negative gives almost 100% |
| `young_sleep_without_variation` | The baby's schedule does not vary (`randomLimit(1)`) |
| `battle_midnight_skips_age` | A battle that crosses midnight does not add age |
| `toilet_doubles_poop_interval` | After the toilet or poop on the ground the warning takes twice as long |
| `food_clears_training_boost` | Any food clears the training bonus |
| `skipped_hunger_window_counts_twice` | Skipping hours counts the hunger window's mistake twice |
| `skipped_hours_add_sickness_tries` | Skipping hours while sick adds sickness attempts |
| `skipped_hours_unhappiness_only_at_midnight` | Skipping hours only subtracts lifetime for unhappiness at midnight and in groups of 4 hours; `false` subtracts it for each multiple of 4 hours crossed |
| `wake_searches_from_last_meal` | On waking up it searches for the next meal from the last one it set before sleeping and skips the earlier ones; `false` searches from the wake-up time |
| `training.quirks.brain_training_uses_mp_aptitude` | Brains uses the MP multiplier |

Two quirks have no flag: the dead counter `subTiredness`, because nothing raises it, and the 31-day year of
training, which stays as a known gap without a flag (decision of the design lead, 2026-10-20; see the
table).

## Parity with the original

Same: does the same as the decomp. Approved deviation: different on purpose. Gap: missing.

| Rule | Decomp | Ours | Test | Status |
|---|---|---|---|---|
| Hour: evolution +1, lifetime −1; midnight: age +1 | `clock.c: tickGameClock` | `Partner::stepClock`, `startDay` | `life_tests`: spends an hour, the day turns | Same |
| Year: tamer level ±1 | `clock.c: tickGameClock` | `startDay`, `care.year` | `mood_tests`: a new year | Same |
| Order of the rules per frame | `partner_impl.c: tickConditions` | `Partner::stepCare` | `simulation_tests`: three days at once | Same |
| The care rules and the toilet only run in the companion's normal state, not in a care scene (eating, toilet, pile); the clock keeps going | `partner.c: partnerTickNormal` (`PARTNER_STATE` 1 calls `tickConditions` and `tickPoopingMechanic`) | `CareContext::partnerNormal` (the session removes it with `Field::heldForCare`) | `hunger_tests`: care scene; `toilet_tests`: the urge runs only in the normal state... | Same: checked against the decomp (2026-11-01): the urge margin does not run in a care scene or with the player held, even if it is already at −1 |
| Meal schedule | `partner_impl.c: setFoodTimer` | `setFoodTimer`, `nextAdultMealHour` | `hunger_tests`: meal hours, baby every 2 | Same |
| Meal on waking up: searches from the last one it set, without a day, and may skip meals | `partner_impl.c: handleSleeping`, `setFoodTimer` (American version branch) | `wakeUp` → `setFoodTimer` (`care.quirks.wake_searches_from_last_meal`) | `waking searches the next meal from the one set before sleeping ...` | Same (quirk with flag) |
| Hunger window and care mistake | `partner_impl.c: tickHungerMechanics` | `tickHunger`, `closeHungerWindow` | `hunger_tests`: unfed window | Same |
| Energy and weight without energy | `partner_impl.c: tickHungerMechanics` | `tickHunger` | `hunger_tests`: energy falls | Same |
| Tiredness brings hunger forward | `partner_impl.c: tickTirednessMechanics` | `tickTiredness`, `tickHunger` | `hunger_tests`: long tiredness | Same |
| Eating: fed, overfed, favorite, cap | `partner_impl.c: partnerHandleFoodFeed`; `item.c: handleFood` | `eat`, `applyFood`, `answerHunger` | `hunger_tests` | Same |
| Cheers when eating while hungry if it eats enough or its favorite (clip 0xb) | `partner_impl.c: partnerHandleFoodFeed` | `answerHunger` → `EventKind::Cheered`; `FeedScene::cheer` | `hunger_tests`: a hungry partner cheers... | Same |
| Item refusal; the favorite mark lasts one offer; the discipline check follows the favorite one | `partner_impl.c: partnerWillRefuseItem` | `OriginalItemRefusal` | `hunger_tests`, `mood_tests` | Same |
| Sleep schedule | `partner_impl.c: setSleepTimes` | `setSleepTimes`, `scheduleNextSleep` | `sleep_tests`: pattern, baby | Same |
| Tiredness brings sleep forward | `partner_impl.c: tickSleepMechanics` | `tickSleep`, `cutAwakeTime` | `sleep_tests`: cut of 10 minutes | Same |
| Penalty when sleepy, missed sleep | `partner_impl.c: tickSleepMechanics` | `tickSleep`, `missSleep` | `sleep_tests`: sleepy adult, misses its sleep | Same |
| Sleeping and recovery | `partner_impl.c: handleSleeping`, `sleepRegen` | `sleep`, `regenerateWhileAsleep`, `wakeUp` | `sleep_tests` | Same |
| Protected hour | `partner_impl.c: setImmortalHour`, `tickDeathCondition` | `inProtectedHour` | `life_tests`: death waits | Same |
| Toilet: warning, margin, on the ground | `partner_impl.c: tickPoopingMechanic`, `handleWildPoop`; `partner.c: tickWildPoop` | `tickPoop`, `poopOnGround`; with `CareContext::fieldScenes`, `wildPoopDue`, `leaveWildPile` and `finishWildPoop` for the field scene | `toilet_tests` | Same |
| Toilet and Portable Potty | `partner_impl.c: handleToilet`; `item.c: handlePortaPotty` | `useToilet`, `usePortableToilet` | `toilet_tests` | Same |
| The toilet cell only sends the companion if it needs to go and is not waiting for poop on the ground: the poop scene stops the player from its first frame (state 6), so the toilet no longer starts at the same time as it and cuts it | `tamer.c: checkMapInteraction` (condition 8, `TAMER_STATE` 0); `partner.c: tickWildPoop` (`tamerSetState(6)`) | `Partner::goesToToilet`; the app: `FieldToilet::checkCell`, and `FieldPoop::update` does not run the frame in which the toilet started | `toilet_tests`: the urge runs only in the normal state... | Same. Before, with the poop pending (urge at −1) and the player on the cell, both scenes started on the same frame: the camera went to the toilet while the poop came out next to the player, and when it finished the toilet was still without the care hold |
| Weight after poop | `partner_impl.c: handlePoopWeightLoss` | `loseWeightAfterPoop` | `toilet_tests`: the toilet gives | Same |
| Poop on the map | `partner_impl.c: createPoopPile` | `PoopPiles` | `toilet_tests`: piles | Same |
| Eating poop | `partner_impl.c: handleEatingPoop` | `eatPoop` | `toilet_tests`, `health_tests` | Same |
| Virus bar | `partner_impl.c: tickConditions` | `tickVirusBar` | `toilet_tests`: sixteenth pile | Same |
| Copy of the virus form: species and stats | `partner_impl.c: tickConditions` (`sukaBackup*`); script variable `PSTAT_SUKAMON_BACKUP_DIGIMON` | `PartnerState::virusBackup` (`VirusBackup`), state version 2 | `toilet_tests`: sixteenth pile; `state_yaml_tests`: version 1 | Same (2026-10-20): the species is saved with the partner and not in a script variable; version 1 games load it empty |
| Getting sick from not sleeping, by area, by injury | `partner_impl.c: tickSicknessMechanics` | `tickSickness`, `OriginalSicknessRoll` | `health_tests` | Same |
| Untreated sickness: lifetime | `partner_impl.c: tickSicknessMechanics` | `tickSickness`, `loseLife` | `health_tests`: twelve hours sick | Same |
| Food that makes it sick | `item.c: handleItemSickness` | `rollFoodSickness` | `health_tests`: a food | Same |
| Bandage and medicine | `item.c: handleStatusItems`, `handleMedicineHealing` | `cure` | `health_tests`: cures | Same |
| Scolding and praising | `partner_impl.c: handlePraiseScold` | `scold`, `praise` | `mood_tests` | Same |
| Limits | `partner_impl.c: tickConditionBoundaries` | `clampBoundaries` | `mood_tests`: within -100 and 100 | Same |
| Sulking | `partner_impl.c: tickUnhappinessMechanics` | `tickSulking` | `mood_tests`: sulking | Same |
| Area it likes or dislikes | `partner_impl.c: tickTirednessMechanics` | `tickTiredness` | `mood_tests`: liked area | Same |
| What it thinks of the area: the map's region lists against the species' favorite region | `map.c: loadMapSetup` | `areaResponseOf` (`area_response.hpp`); the session computes it on entering each map | `area_response_tests` | Same |
| Tired | `partner_impl.c: tickTirednessMechanics` | `tickTiredness` | `mood_tests`: at 80 | Same |
| Lifetime from unhappiness | `partner_impl.c: tickConditions` | `tickUnhappyLifetime`, `loseUnhappyLifetime` | `life_tests`: unhappy | Same |
| Natural death | `partner_impl.c: tickDeathCondition` | `tickDeath`: waits for the tamer to be free (`tamerIdle`) and for no script to be running (`scriptIdle`); the lifetime that an untreated sickness takes, too | `life_tests`: natural death, death waits | Same |
| Rebirth and inheritance | `evolution.c: reincarnatePartner`; `partner_impl.c: initializeReincarnatedPartner`; `dooa.c` | `rebirth`, `OriginalRebirthChoice` | `life_tests`: rebirth | Same |
| Battle: time, food and poop | `tamer.c: advanceBattleTime` | `passBattleTime` (the poop steps come from `toilet.poop_step_minutes`) | `a battle's minutes bring the meal closer ...`, `a battle across midnight ...` | Same |
| Battle already resolved by `packages/game/battle`: its numbers and the hourly count if an hour ends | `tamer.c: startBattle`, `advanceBattleTime` | `applyBattle` (`BattleConsequences`), `passBattleTime` | `a battle the battle system settled ...`, `a settled battle that ends an hour ...` | Same |
| Skipping hours | `partner_impl.c: skipHours` | `skipHours` | `activity_tests`: rest | Same |
| Unhappiness when skipping hours | `partner_impl.c: skipHours` | `unhappyChecksWhileSkipping`, `care.quirks.skipped_hours_unhappiness_only_at_midnight` | `activity_tests`: rest, only at midnight | Same (2026-10-20): the quirk stays by default; `false` subtracts for each multiple of 4 hours crossed |
| Training multiplier | `trn_reward.c: TRN_calculateTrainingMultiplier` | `OriginalGainFormula` | `training_tests` | Same |
| Gains and completely tired | `trn_reward.c: TRN_award*TrainingGains` | `OriginalGainFormula` | `training_tests` | Same |
| Cap on each gain: 999 | `trn_reward.c: TRN_award*TrainingGains` (only the first gain of the session); no per-gain cap in `item.c: handleFood`, `evl.c: EVL_applyEvolution` and `partner_impl.c: setReincarnateStats` | `addStatGains`, `care.limits.stat_gain_max` | `stat_gain_tests` | Approved deviation (2026-10-20): every gain stays at 999, also an item's (an item of +1000 HP gives +999), the evolution's average (rises 999 at most) and the inheritance; both effects are accepted |
| Cap on each stat: 9999 and 999 | `item.c: addWithLimit`; `evl.c: EVL_clampBaseStats` | `addStatGains`, `care.limits` | `training_tests`: stop at 9999 | Same |
| Return from the virus form: rebuilt stats | `partner.c: callDigimonRoutine` (2); `evl.c: EVL_scaleBaseStats`, `EVL_clampBaseStats` | `setStats` and the cap of `care.limits` in `packages/session` (`EvolutionAdapter`) | `packages/session`: `back from the virus form ...` | Same (2026-10-20): without the 999 per-gain cap |
| Training hour | `trn_reward.c: TRN_advanceTrainingTime` | `passTrainingHour` | `training_tests`: takes an hour | Same |
| 31-day year when training | `trn_reward.c: TRN_advanceTrainingTime` (the day goes back to 0 at 31; the clock, at 30) | — (uses the clock, 30) | — | Known gap (2026-10-20): it stays like this, without a flag |
| Sleep patterns | `partner_impl.c: SLEEP_PATTERN` | `care.sleep.patterns`, with its citation | `rules_tests`: the original's numbers | Same (2026-10-20): ruleset constants |
| Fixed stats of the rebirth babies | `partner_impl.c: initializeReincarnatedPartner`; `dooa.c: REINCARNATE_BABY_TYPE` | `life.rebirth.babies`, with its citation | `rules_tests`: the original's numbers | Same (2026-10-20): ruleset constants |
| Evolutions on waking up | `partner_impl.c: handleSleeping` | — (`packages/game/evolution`: `Evolution::wakeUp`, which the session calls on waking up) | — | Outside this module |
| Techniques after rebirth | `evolution.c: reincarnatePartner` | — (`packages/session`: `BattleAdapter` with the `Reborn` event, `partner_moves.reborn_slot`) | — | Outside this module |

## Tests

One test per rule of the sheet, in `hunger_tests.cpp`, `sleep_tests.cpp`, `toilet_tests.cpp`, `health_tests.cpp`,
`mood_tests.cpp`, `life_tests.cpp`, `activity_tests.cpp`, `training_tests.cpp` and `stat_gain_tests.cpp` (the cap on
each gain, for each path that raises a stat), with invented test species (`partner_fixture.hpp`) on the
`original` ruleset of the `game` pack. In addition:

- `rules_tests.cpp`: the original's numbers, the schemas, values that the checks reject, a changed
  ruleset value (the hunger window) and a changed strategy (no care mistakes).
- `simulation_tests.cpp`: three days at once, by hours and frame by frame give the same state and the same
  events; eight days of care with the same seed give the same partner, which comes back equal from `toYaml`/`fromYaml`.
- `need_deadlines_tests.cpp`: the deadline of each need is the frame on which its rule does the damage (one frame earlier
  nothing happens).
- `state_yaml_tests.cpp`, `species_tests.cpp` (`digimon` and `raise` tables).
- `the user's disc, extracted: ...` (slow): with `DWE_CONTENT`, all the disc's species load, their sleep
  patterns exist, each rookie goes through a day and the rebirth babies are babies.
