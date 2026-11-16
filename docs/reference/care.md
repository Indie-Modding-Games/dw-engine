# Reference sheet: care

How the partner was raised in the original game, according to the decomp. It is step 1 of the
[design cycle](../conventions/design-cycle.md): it describes the original, not what the mod will do.

Marks: **(verified)** means it was read in the decomp's code; **(assumption)** means it is our own interpretation
that still needs to be confirmed. Unless stated otherwise, everything is in `src/main/partner_impl.c`.

## What it does

The partner lives on a clock of its own. It gets hungry at certain hours, sleepy at others, needs to go to the
bathroom, gets tired from training and fighting, and can get sick or injured. Each neglected need lowers its
happiness or discipline, and some count as **care mistakes**, which decide what it evolves into. It has a limited
lifespan in hours: when it runs out, it dies and is reborn as a Fresh Digimon with part of its stats.

## Rules

### The clock

- At normal speed, 20 frames are one game minute, 1200 an hour and 28,800 a day (`tickGameClock`, in `clock.c`)
  **(verified)**. That the game runs at about 20 frames per second, and therefore a game hour lasts one real minute,
  is an **(assumption)**.
- Every hour: the evolution counter goes up 1 and the remaining life goes down 1. Every midnight: the age goes up 1
  day (`clock.c`) **(verified)**.
- The care rules are evaluated in `tickConditions`, only with the partner in a normal state and the clock running.
  Almost all of them use fixed intervals of frames **(verified)**.

### Hunger

- **Schedule** (`setFoodTimer`): Fresh Digimon get hungry every 2 hours and In-Training ones every 3. From Rookie on,
  each species has a list of up to 8 meal hours in `RAISE_DATA` **(verified)**.
- **Hunger window** (`tickHungerMechanics`): when the hour arrives, the partner asks for food for 1.5 hours. If the
  window closes without its energy reaching its species' threshold, it adds **one care mistake** **(verified)**.
- **Energy:** it drops every hour according to the species' expenditure. With energy at 0, weight drops 1 every 10
  minutes **(verified)**.
- **Tiredness and hunger:** with high tiredness for some time, hunger arrives sooner (3 minutes more per hour)
  **(verified)**.
- **Eating** (`partnerHandleFoodFeed`, `handleFood` in `item.c`):
  - If it was hungry and its energy reaches the threshold: happiness +5, discipline +1 and the hunger goes away. The
    next meal hour is searched from the last one that was set, not from the current hour (`setFoodTimer` of the
    American version) **(verified)**.
  - If it was not hungry (overfeeding): happiness −3, discipline −2.
  - The species' favorite food gives 40% more energy and +2 happiness.
  - When hungry, if it eats its favorite or is already eating enough, it celebrates with animation 0xb
    (`startAnimation`).
  - Energy has a cap per species.
  - All **(verified)**.
- **Refusing food or items** (`partnerWillRefuseItem`) **(verified)**:
  - Some items are refused with a chance that drops with discipline: random(100) < 110 − (discipline + 10..19).
  - If it is not hungry, it refuses its favorite food 20% of the time. An item with a discipline check that passes the
    roll continues to the favorite check: the species whose favorite is that item can also refuse it.
  - The refused-favorite mark is cleared at the start of each offer: it lasts a single item.
  - After a refusal, scolding gives more discipline (see happiness and discipline).
  - Only meals count for hunger; the other items that go through `handleFood` change stats and needs but do not remove
    hunger or count as overfeeding (`partnerHandleFoodFeed`) **(verified)**.

### Sleep

- **Schedule** (`setSleepTimes`, `SLEEP_PATTERN`) **(verified)**:
  - From Rookie on, each species uses one of several patterns of 15 hours awake and 9 asleep, chosen in `RAISE_DATA`.
  - Fresh and In-Training Digimon sleep starting from the current hour: 3 or 7 hours awake and then a short nap.
- **Tiredness brings sleep forward** (`tickSleepMechanics`): every minute, tiredness × 3 / 10 accumulates; every 180
  points, 10 minutes of wakefulness are cut **(verified)**.
- **Sleepy and not going to bed** **(verified)**:
  - Happiness and discipline drop: −1/−1 every 10 minutes (Fresh), every 15 minutes (In-Training), or −2/−4 per hour
    (the rest).
  - Every hour a counter of hours without sleep goes up, which leads to sickness (see below).
  - If the wake-up hour arrives without having slept: **one care mistake**, a possible drop in the tamer level and the
    schedule restarts.
- **Sleeping** (`partnerTickSleep`, `handleSleeping`, `sleepRegen`) **(verified)**:
  - The clock jumps to the wake-up hour. Remaining life drops by the hours slept and the evolution counter goes up by
    the default sleep hours.
  - Hunger goes away and, if it needed to go to the bathroom, it leaves a poop. That poop does **not** count as a care
    mistake (`handleSleeping`) **(verified)**.
  - If it was sick: happiness −20, discipline −10, tiredness +10. If it was injured: −10, −5, +5.
  - Recovery according to the fraction of sleep completed: HP and MP between 70 and 79% of the maximum, tiredness
    drops between 80 and 99%, and weight drops a tenth of the default weight.
  - The pillow (an item) improves recovery by 20%; sleeping in a zone it likes, 20% more, and in one it does not
    like, 20% less.
  - During the first hour after waking it cannot die or get worse from sickness (`setImmortalHour`).
  - On waking, the special evolutions are tested (see the [evolution sheet](evolution.md)).
- No separate rule was found for "sleeping outside the bed"; the zone is the only thing that counts
  **(assumption: it may be in scripts)**.

### Bathroom and poop

- **Timer** (`tickPoopingMechanic`): each species has its interval in `RAISE_DATA`. It drops 1 every 10 minutes and 1
  for each meal **(verified)**.
- **Warning:** on reaching 0, the partner needs to go and there is a margin of (discipline + 20) × 1200 / 100 frames,
  that is, between 12 and 72 minutes **(verified)**.
- **On the floor** (`tickWildPoop`, `handleWildPoop`, `createPoopPile`) **(verified)**:
  - **One care mistake**, happiness −10, discipline −5, weight loss and a possible drop in the tamer level.
  - The virus bar goes up 1. At 16, the partner becomes `species_p` (see the [evolution sheet](evolution.md)).
  - The poop is saved on the map: up to 16 per map and 100 in total.
  - When the margin runs out the partner only moves to state 7; the pile, the care mistake, the mood and the weight
    arrive when it leaves it in its scene (substate 2), and the possible drop in the tamer level when it finishes it
    (substate 3).
- **In the bathroom** (`handleToilet`) **(verified)**: happiness +2, discipline +2 and a small weight loss according to
  the size of the poop. In the American version the next warning takes twice as long. The Portable Potty (an item)
  empties it without giving happiness or discipline, and the next warning takes the normal interval, without doubling
  (`handlePortaPotty` in `item.c`) **(verified)**.
- **Eating poop:** two specific species eat nearby poop, gain HP, MP and weight according to the size, and have a
  small chance of being cured (`handleEatingPoop`) **(verified)**.

### Sickness and injuries

- **Causes of sickness** (`tickSicknessMechanics`) **(verified)**:
  - With 10 or more accumulated hours without sleep, every hour there is a roll with chance (tries − 10) × 5 +
    (tiredness − 50)%. See oddities.
  - More than 10 hours in a row in a zone it does not like.
  - An injury left untreated for 12 hours becomes sickness.
  - Some foods have a chance of making it sick (`handleItemSickness` in `item.c`).
- **On getting sick:** happiness −20 **(verified)**.
- **While sick:** every hour happiness −10, discipline −5 and tiredness +5 **(verified)**. In battle it performs
  worse (see the [battle sheet](battle.md)).
- **Untreated:** after 12 hours of sickness it loses **one life** (it starts with 3) and a script event is launched
  **(verified)**. What that event shows is an **(assumption)**.
- **Injuries:** they come from won battles in which it took damage, with chance = tiredness − % of HP remaining
  (`handleBattleInjury` in `battle_ui.c`). The roll happens on the results screen, before the tamer's post-battle
  accounting (happiness, tiredness, time) **(verified)**.
- **Cures** (`handleStatusItems` in `item.c`) **(verified)**: the Bandage always cures the injury and the sickness 20%
  of the time; the Medicine cures both. With the partner fainted (HP 0) they do nothing (`handleMedicineHealing`)
  **(verified)**. There is no doctor logic in the C code **(assumption: it will be in scripts)**.
- **No lives:** if untreated sickness takes the last life, the remaining life goes to 0 and the partner dies without
  inheriting stats **(verified)**.

### Happiness and discipline

- **Ranges** (`tickConditionBoundaries`): happiness from −100 to 100; discipline and tiredness from 0 to 100; weight
  from 1 to 99 **(verified)**.
- **Scolding** (`handlePraiseScold`): discipline +2, happiness −10. Right after a refusal or a poop on the floor,
  discipline +8 without losing happiness **(verified)**. That bonus is spent: the next time an item is offered it
  resets and it accepts it (`partnerWillRefuseItem`) **(verified)**.
- **Praising:** discipline −5, happiness + (discipline / 10 + 2) **(verified)**.
- **Battles** (`startBattle` in `tamer.c`): winning, happiness +2; fleeing, happiness −10 and discipline −6; losing,
  happiness −30 and discipline −20 **(verified)**. Winning and fleeing also add the post-battle tiredness (+5, and
  fleeing +2 more); losing does not add it, but takes away a life (`handlePostBattleTiredness`) **(verified)**.
- **Zone:** every hour, +1 happiness in a zone it likes and −1 in one it does not (`tickTirednessMechanics`)
  **(verified)**.
- **Anger** (`tickUnhappinessMechanics`) **(verified)**:
  - With no other need and with negative happiness, every 10 minutes there is a chance of (|happiness| − discipline)%
    that it gets angry.
  - While angry, happiness rises on its own little by little, but **all its stats drop 2% every 15 minutes**, until
    happiness goes back to 0 or it is scolded.
- **Life:** with happiness below 80, every 4 hours it loses extra hours of life: 1 if it is between 31 and 79, and up
  to 4 the lower it gets (formula: (happiness − 80) / −50 + 1, with integer division) (`tickConditions`)
  **(verified)**.
- **Discipline in battle:** it reduces disobedience (see the [battle sheet](battle.md)).
- **Tamer level** (`clock.c`): every game year (30 days), with happiness 100 it can go up; with negative happiness it
  can go down **(verified)**.

### Tiredness

- **It goes up:** training (about 8 to 10 per session), fighting (+5), being sick (+5 per hour) and being in a zone it
  does not like (+1 per hour) **(verified)**.
- **Effects** **(verified)**:
  - At 80 or more, the tired warning appears and it loses 2 happiness every 5 minutes.
  - At 70 or more after training, happiness −3 and discipline −3.
  - At 100, training only gives 1 point.
  - With high tiredness hunger arrives sooner and the chances of getting sick and of getting injured go up.
- **It goes down:** when sleeping, in a zone it likes (−2 per hour) and with some foods **(verified)**.

### Life and age

- **Remaining life:** it starts at 360 hours (15 days) and drops 1 per hour (`initializePartner`, `clock.c`)
  **(verified)**.
- It also drops: the hours slept, each battle (it advances 20 minutes), each training (it advances 1 hour),
  unhappiness (see above) and some items (−24 hours) **(verified)**.
- It goes up: on evolving to Ultimate without an item (+96 hours) and with some foods **(verified)**.
- **Age:** days lived; it goes up at midnight, also if it is reached by skipping hours or training **(verified)**.
- **Resting or skipping hours** (`skipHours`, used by scripts): it adds the hours to the evolution counter,
  subtracts them from life, advances food, bathroom and hunger window, and drops energy **only once** by the
  species' expenditure, without moving the clock **(verified)**.

### Death and rebirth

- **Natural death** (`tickDeathCondition`): with remaining life at 0, the tamer free, no script running
  (`IS_SCRIPT_PAUSED` 1) and outside the protected hour after sleeping, the death event is launched **(verified)**.
- **Lives:** losing a battle or leaving a sickness untreated for 12 hours takes away a life; with 0 lives, the
  remaining life goes to 0 and it dies (`script_interp.c`) **(verified)**.
- **Rebirth** (`reincarnatePartner` in `evolution.c`, `initializeReincarnatedPartner`, `setReincarnateStats`; the egg
  is chosen in `dooa.c`) **(verified)**:
  - One of the four Fresh Digimon comes out, at random.
  - Stats = the Fresh Digimon's fixed ones + **a % of the dead partner's stats equal to the tamer level**, but only if
    the death was natural. Otherwise it inherits nothing.
  - Discipline and happiness to 50, life to 360, age to 0, lives to 3, mistakes and battles to 0.
  - The equipped techniques are reset. The list of learned techniques is not erased, so it seems they are kept
    **(assumption)**.

### Training

It is part of care because it spends time, tires and lowers happiness (`src/trn/`) **(verified)**:

- **Cost per session** (`TRN_advanceTrainingTime`): 1 game hour (with everything that happens in an hour),
  tiredness, energy and happiness.
- **Gain** (`TRN_calculateTrainingMultiplier`): a base per gym, multiplied by:
  - the species' aptitude for that stat: ×0.8 to ×1.1 (`trainingType` in `RAISE_DATA`);
  - level: ×0.9 (Fresh and In-Training), ×1.0 (Rookie), ×1.1 (Champion), ×1.2 (Ultimate);
  - upgraded gym: ×1.2;
  - 10 or more sessions of that stat trained as Fresh or In-Training (in the first gym; the `TRN_setup*` count them
    with level lower than 3): ×1.2 **(verified)**;
  - the training manual (an item): ×1.1.
- **Bonus roulette** (`trn_slots.c`): it multiplies the result between ×0.5 and ×10 **(verified)**.
- **Training foods:** they activate for some time a bonus for one or several stats (`setTrainingBoost` in `item.c`)
  **(verified)**. Every food calls `setTrainingBoost`, so one without a bonus erases the one there was (see oddities)
  **(verified)**.
- **Brains:** its session asks for the MP multiplier (mode 1), not the Brains one (`TRN_awardBrainTrainingGains`)
  **(verified)**. With the original's tables the two aptitudes coincide, so it is not noticeable.
- **Cap:** a gain of 1000 or more stays at 999; in the original only the first gain of each session is limited
  (`TRN_award*`) **(verified)**. Items (`handleFood`), evolution (`EVL_applyEvolution`) and the inheritance on rebirth
  (`setReincarnateStats`) do not limit each gain, only the stat (9999 and 999) **(verified)**. Approved deviation
  (2026-10-20): in the `original` ruleset every stat gain stays at 999 (`care.limits.stat_gain_max`), so a +1000 HP
  chip gives 999.
- Each session adds 1 to the evolution counter **(verified)**.

### Food and care items

- The items' effects are decided by the code: `ITEM_FUNCTIONS` gives each item's function, and only what the HP and MP
  cures heal (`HEALING_VALUES`) and the evolution destinations are in tables; the rest are constants of each
  function **(verified)**. Details in the [inventory sheet](inventory.md). Ranges: HP and MP cures, status cures,
  temporary boost disks, chips (permanent stats in exchange for life), bathroom items, food and evolution items.
- Each food can give energy, remove tiredness, change happiness, discipline, weight or life, raise stats permanently,
  cure HP or MP, activate a training bonus or have a chance of making it sick. All in a `switch` inside `handleFood`
  (`item.c`) **(verified)**. Each food also lowers the bathroom timer by 1.

### First damage from each need

What happens first if nobody attends to each need, according to the counters above. The original does not compare
these deadlines; dwe's bubble with several needs (an optional improvement, see
[the interface architecture](../architecture/ui.md)) uses them to order them.

| Need | First damage | Deadline |
|---|---|---|
| Hunger | The window closes: care mistake | What is left of the window (at most 90 minutes) |
| Bathroom | It goes on the floor: care mistake | The warning margin, (discipline + 20) × 1200 / 100 frames |
| Sleep | The wake-up hour arrives without sleeping: care mistake | Until the wake-up hour |
| Injury | It becomes sickness | 12 hours injured (none if already sick) |
| Sickness | It loses a life | 12 hours sick |
| Tiredness | None with a deadline: only happiness from time to time and hunger sooner | — |

## Data

Tables in the main executable. `dwx` will extract them to YAML from the user's disc; here only their shape is
described.

| Table | Entry | Entries | Fields |
|---|---|---|---|
| `RAISE_DATA` | 28 bytes | 66 | Per species: up to 8 meal hours, energy cap, threshold and expenditure, bathroom interval, poop size, favorite food, sleep pattern, favorite zone, training aptitude, default weight and camera offsets. |
| `SLEEP_PATTERN` | 6 bytes | 8 | Sleep patterns (hours awake and asleep). |
| `ITEM_PARA` | 32 bytes | 128 | Per item: name (not in the European version, which uses `ITEM_NAMES`), price, merit value, order, color, whether it can be dropped and an unidentified field. |
| `ITEM_FUNCTIONS` | 4 bytes | 128 | Function that each item applies. The values of each effect are in the code, not in data. |
| `HEALING_VALUES` | — | 8 bytes in total | How much each HP or MP item heals. |
| `TOILET_DATA` | — | — | Bathroom positions per map (`toilet_data.c`). |
| `REINCARNATE_BABY_TYPE` | 2 bytes | 4 | The Fresh Digimon of each egg. |

The partner's state is in `PartnerPara` (`include/dw/params.h`, 0x84 bytes) **(verified)**:

| Field | What it stores |
|---|---|
| `condition` | Flags: sleep, tired, hunger, bathroom, anger, injury, sickness |
| Sleep and wake-up hours, default hours awake and asleep, today's wakefulness | Sleep schedule |
| `sicknessCounter`, `missedSleepHours`, `sicknessTries`, `sicknessTimer`, `injuryTimer`, `timesBeingSick` | Sickness and injuries |
| `poopLevel`, `poopingTimer`, `virusBar` | Bathroom and dirt |
| `tiredness`, `subTiredness`, `tirednessSleepTimer`, `tirednessHungerTimer` | Tiredness |
| `discipline`, `happiness` | Discipline and happiness |
| `energyLevel`, `foodLevel`, `starvationTimer`, `nextHungerHour`, `emptyStomachTimer`, `refusedFavFood`, `weight` | Hunger and weight |
| `remainingLifetime`, `age` | Life and age |
| `careMistakes`, `battles`, `evoTimer` | Evolution counters |
| Training bonus (flag, value, time), upgrade counters | Training |
| Copy of stats | To come back from `species_p` |

Lives are not here, but in the partner's entity. The `RAISE_DATA` structures are in `include/dw/partner.h`.

## Oddities

- **Almost certain sickness.** The chance of getting sick is stored in an unsigned byte; a negative value becomes a
  large one and the roll always succeeds **(verified)**. That is why, with 10 accumulated hours without sleep, getting
  sick is practically certain **(assumption about the in-game effect)**.
- **Penalty that never triggers.** When sleeping, hunger is removed before it is checked for the weight loss, so that
  loss never happens **(verified)**.
- **No variation in the Fresh Digimon.** The Fresh Digimon's schedule calls a random number with limit 1, which always
  gives 0 **(verified)**.
- **Dead counter.** `subTiredness` is read and reset, but nothing raises it **(verified: no function in the decomp
  writes it except to set it to 0)**.
- **Midnight in combat.** If a battle crosses midnight, it does not add age or evolution **(verified)**.
- **Different years.** Training treats a year as 31 days and the clock as 30 **(verified)**: `TRN_advanceTrainingTime`
  (`src/trn/trn_reward.c`) resets the day to 0 on reaching 31, and `tickGameClock` (`clock.c`), on reaching 30. Known
  gap: the engine uses the clock, with 30 days, also when training, and there is no flag (design owner's decision,
  2026-10-20).
- **Fixed sleep for evolution.** Sleeping adds the default sleep hours to the evolution counter, even if it slept
  less **(verified)**.
- **Bathroom in the American version.** Only there does the bathroom double the time until the next warning
  **(verified)**.
- **Broken MP tiredness.** See the [battle sheet](battle.md). `handlePostBattleTiredness` divides before multiplying:
  only a battle that spent all the MP adds the 10 of tiredness **(verified)**.
- **Food erases the bonus.** Any food resets the training bonus, even if it gives none (`handleFood`) **(verified)**.
- **Double care mistake when skipping hours.** `skipHours`, combat and training lower the hunger window and count the
  mistake if it runs out, but do not close it; then `tickHungerMechanics` closes it and counts it again
  **(verified)**.
- **Sickness tries when skipping hours.** With the partner sick, skipping hours adds tries to the sickness roll
  **(verified)**.
- **Meal skipped on waking.** The American version's `setFoodTimer` looks for the next meal from the last meal hour it
  set (`nextHungerHour`, without day) if the current hour has not passed it; the Japanese one looks from the current
  hour. On waking, `handleSleeping` calls it with the hour from before sleeping: if the partner wakes before that
  hour, that meal is skipped **(verified)**. With the disc tables, a partner that goes to sleep at 16 with the 18
  o'clock meal pending and wakes at 1 does not get hungry again until 22 or 23 (sleep pattern 0 with meals at 3, 8,
  13, 18 and 23, or 2, 6, 10, 14, 18 and 22): 21 or 22 hours without asking for food, and energy reaches 0 and weight
  drops **(verified in the decomp; the in-game effect, assumption)**. The `original` ruleset reproduces it
  (`care.quirks.wake_searches_from_last_meal: true`); with `false` it searches from the wake-up hour.
- **Unhappiness when skipping hours.** `skipHours` checks `(HOUR + hours % 4) == 0` instead of crossing a multiple of
  4: it only subtracts life for unhappiness at midnight with groups of 4 hours **(verified)**. The `original` ruleset
  reproduces it (`care.quirks.skipped_hours_unhappiness_only_at_midnight: true`); with `false`, it subtracts once for
  each multiple of 4 hours that the skipped hours cross, as if they were played (design owner's decision,
  2026-10-20).

## Where it is in the decomp

| What | Where |
|---|---|
| Care loop | `src/main/partner_impl.c`: `tickConditions`, `tickConditionBoundaries` |
| Hunger and food | `src/main/partner_impl.c`: `setFoodTimer`, `tickHungerMechanics`, `partnerHandleFoodFeed`, `partnerWillRefuseItem`; `src/main/item.c`: `handleFood`, `addEnergy` |
| Sleep | `src/main/partner_impl.c`: `setSleepTimes`, `tickSleepMechanics`, `handleSleeping`, `sleepRegen`; `src/main/partner.c`: `partnerTickSleep` |
| Bathroom | `src/main/partner_impl.c`: `tickPoopingMechanic`, `createPoopPile`, `handleToilet`, `handleEatingPoop`; `src/main/partner.c`: `tickWildPoop`; `src/main/toilet_data.c` |
| Sickness and injuries | `src/main/partner_impl.c`: `tickSicknessMechanics`; `src/main/battle_ui.c`: `handleBattleInjury`; `src/main/item.c`: `handleStatusItems` |
| Happiness and discipline | `src/main/partner_impl.c`: `handlePraiseScold`, `tickUnhappinessMechanics`, `tickTirednessMechanics`; `src/main/tamer.c`: `startBattle` |
| Clock, life and age | `src/main/clock.c`: `tickGameClock`; `src/main/partner_impl.c`: `skipHours` |
| Death and rebirth | `src/main/partner_impl.c`: `tickDeathCondition`, `initializeReincarnatedPartner`; `src/main/evolution.c`: `reincarnatePartner`; `src/dooa/dooa.c` |
| Training | `src/trn/trn_reward.c`: `TRN_advanceTrainingTime`, `TRN_calculateTrainingMultiplier`; `src/trn/trn_slots.c`; `src/trn2/` |
| Structures | `include/dw/params.h` (`PartnerPara`), `include/dw/partner.h` (`RaiseData`), `include/dw/item.h` |
