# Reference sheet: evolution

How the partner changed form in the original game, according to the decomp. It is step 1 of the
[design cycle](../conventions/design-cycle.md): it describes the original, not what the mod will do.

For players, the same information in plain language and with raising tips is in the guide
[how evolutions work](../guides/evolutions.md).

Marks: **(verified)** means it was read in the decomp's code; **(assumption)** means it is our own interpretation
that still needs to be confirmed. Digimon identifiers are indexes into the parameter table (`DIGIMON_DATA`); the name
in parentheses comes from the order of that table in `src/main/main.c`.

## What it does

The partner is born as a Fresh Digimon and goes through five levels. Every so many game hours the game looks at which
next forms it has available, scores each one according to how the partner has been raised (stats, weight, care
mistakes and an extra requirement) and, if any passes, evolves into the best one. There are also special evolutions
that depend on events (sleeping in a certain place, scolding in a specific state, accumulated dirt) and items that
force an evolution. On evolving, the stats are blended with the new form's reference values.

## Rules

### Levels and evolution counter

- There are five levels: 1 Fresh, 2 In-Training, 3 Rookie, 4 Champion, 5 Ultimate. The number comes from the `level`
  field of the parameter table **(verified)**; the level names are the usual ones in the community **(assumption)**.
- The evolution counter (`evoTimer`) counts game hours since the last level change. It goes up one unit for each clock
  hour (`clock.c`), and also when sleeping (it adds the hours of sleep), when skipping hours and in each gym training
  (`trn_reward.c`, `trn2_hud.c`) **(verified)**.
- The counter goes back to 0 only if the evolution changes level; an evolution within the same level keeps it
  (`EVL_applyEvolution`) **(verified)**.

### When it is checked

`tickConditions` (in `partner_impl.c`) looks at evolution while the game runs, the partner is in a normal state, the
tamer is free and no map transition is in progress **(verified)**. That the check is continuous and not once per hour
is what is read in the code; it is enabled by `DAYTIME_TRANSITION_FRAME` with 25 or more **(verified)**, and that this
means "without the palette fade of the time-of-day change" is an **(assumption)**. The virus bar is looked at
afterwards and does not wait for the tamer to be free **(verified)**.

| Current level | Counter threshold | Function that chooses the destination |
|---|---|---|
| 1 Fresh | 6 hours | `getFreshEvolutionTarget` |
| 2 In-Training | 24 hours | `getInTrainingEvolutionTarget` |
| 3 Rookie | 72 hours | `getRookieEvolutionTarget` |
| 4 Champion | 144 hours | `getChampionEvolutionTarget` |
| 5 Ultimate | — (does not evolve naturally) | — |

All **(verified)**. If the function finds no destination, nothing happens and it tries again at the next check.

### Fresh → In-Training

The destination is fixed: each of the four Fresh Digimon has a single next form, written in the code itself, without
looking at raising (`getFreshEvolutionTarget`) **(verified)**. The function also sets the counter to 0 every time it
is called, even if the form is not one of the four Fresh Digimon **(verified)**; see oddities.

### Scoring a candidate

Each form has up to six possible destinations (`EVOLUTION_PATHS` table). For each destination,
`calculateRequirementScore` adds points **(verified)**:

1. **Care mistakes:** 1 point if the mistakes are *at least* the requirement's value. If the destination carries the
   "maximum" mark (bit `0x10` of `flags`), the point is for *at most* that value.
2. **Weight:** 1 point if the weight is within ±5 of the requested weight.
3. **Stats:**
   - If the destination is Rookie level, 1 point if the partner's highest stat is one of those the destination marks
     as favored. For the comparison, HP and MP are divided by 10. On a tie the last stat in the order HP, MP,
     offense, defense, speed, Brains wins.
   - At the other levels, 1 point if *all* the stats reach the requested minimum (HP and MP, also divided by 10).
4. **Bonus:** 1 point if *any* of these is met: coming from a specific form, minimum discipline, minimum happiness,
   battles (minimum, or maximum if `flags` has bit `0x01`), or a minimum number of learned techniques. Learned
   techniques are counted as active bits in a 64-bit mask (`getNumMasteredMoves`).

A destination passes with **3 points or more** out of the 4 possible **(verified)**. A requirement with no value (−1)
is treated as met in the stats and ignored in the bonus **(verified)**.

**Preference for the new:** when there is already a provisional best candidate, if the evaluated destination has
already been raised at some point and the provisional one has not, the destination is set to 0 points; in the opposite
case it receives 1 extra point. The "already raised" record is saved on evolving or being reborn
(`setDigimonRaised`) **(verified)**.

### How one is chosen among several candidates

- **In-Training → Rookie:** the candidate with the most points wins; on a tie, the first in the list
  (`getInTrainingEvolutionTarget`) **(verified)**.
- **Rookie → Champion and Champion → Ultimate:** among those that pass, the one with the highest average of the stats
  its requirement asks for wins (HP and MP divided by 10) (`getRookieEvolutionTarget`,
  `getChampionEvolutionTarget`) **(verified)**. See the accumulator oddity below.
- **Fallback form:** if a Rookie has no candidate and its counter reaches **96 hours**, it evolves into species 11
  (`species_ar`) **(verified)**. Champions without a candidate stay as they are **(verified)**.

### Special evolutions

All in `handleSpecialEvolutions`, except the dirt one. They only apply if the tamer is free **(verified)**.

- **On waking** (mode 2, from `partnerTickSleep`):
  - With a 30% chance, three fixed cases of Champion to another form are tested, which require discipline 100 and,
    depending on the case, also happiness 100 and tiredness 0, 50 or more battles, or defense of 500 or more
    **(verified)**.
  - With a 50% chance, an In-Training form that wakes up on screen 1 evolves into Digimon 32 (`species_bg`)
    **(verified)**. That screen 1 is that character's bed is an **(assumption)**.
- **After praising or scolding** (mode 3, from `partnerTickPraiseScold`), and also when a Champion reaches exactly 360
  hours (`tickConditions`, without a scolding notice, so the `species_be` case does not apply); in that case the
  result replaces that of the normal choice in that same check, see oddities **(verified)**:
  - Two specific Champions with the counter at exactly 200 hours: 30% chance of evolving into another specific form
    **(verified)**.
  - Any Champion with 240 hours or more: 50% chance of evolving into species 28 (`species_bm`) **(verified)**.
  - If a Rookie that had discipline 0 and happiness −100 is scolded, a notice is activated and it evolves into
    Digimon 53 (`species_be`) (`handlePraiseScold`) **(verified)**.
- **Dirt:** each poop made on the floor (`createPoopPile`) adds 1 to the virus bar, unless it is already species 39
  (`species_p`). With the bar at **16**, the partner evolves into `species_p`; its stats and its previous form are
  saved first, and the bar goes back to 0 (`tickConditions`) **(verified)**. A script event can return it to the saved
  form (`callDigimonRoutine`, routine 2) **(verified)**; which game event does it is an **(assumption)**. Routine 10
  makes the partner evolve into the form that the script leaves in `PSTAT_254` **(verified)**.

### Evolution items

`handleEvoItems` (in `item.c`) **(verified)**:

- Items in a fixed range use the `EVOLUTION_ITEM_TARGET` table (item → destination). They only work if the
  destination is exactly one level above the partner.
- Three special items lead to three fixed forms without checking the level.
- Using an evolution item sets `HAS_USED_EVOITEM`. With that mark, the evolution **does not apply stat gains or the
  life bonus**. See oddities.
- Before that, the partner can refuse the item (`partnerWillRefuseItem` in `partner_impl.c`): it accepts an evolution
  item whose destination is 1 or 3 levels above or 2 below, a different rule from that of `handleEvoItems`
  **(verified)**; which in-game cases it covers is an **(assumption)**. That rule belongs to the item system, not to
  evolution.

### Stat gains when evolving

`EVL_applyEvolution` uses the destination's row in `EVOLUTION_STATS_GAINS` **(verified)**:

- **Normal case:** for each stat, if the partner already has at least the reference value, it adds a tenth of that
  value; if not, the stat becomes the average between the current one and the reference.
- **Scaling cases:** if the destination is Fresh or In-Training, if it is one of four specific Digimon (6, 11, 39 and
  53) or if the current partner is `species_p`, the stats are multiplied by a factor in tenths that is read from the
  row's Brains field (`EVL_scaleBaseStats`). If the partner was `species_p`, instead, each stat is rebuilt from the
  stats saved when it converted: saved + (current − saved / 2) / 2 **(verified)**. Why Digimon 6 is in that list is
  not clear **(assumption: a design case or a bug)**.
- **Caps:** HP and MP to 9999; offense, defense, speed and Brains to 999 (`EVL_clampBaseStats`) **(verified)**.
  - In `dwe`, the caps are the partner's (`care.limits` of `care.yaml`): evolution gives each stat's change without
    capping and the partner adds it with `addStatGains`, which also leaves **each gain at 999 at most** (approved
    deviation, 2026-10-20: in the original only the first gain of each training session has that cap, and evolution
    only stops at the caps above).
  - On leaving `species_p`, the stats rebuilt from the copy are restored without going through `addStatGains`, so they
    do not have the 999 per-gain cap, but they do have each stat's cap (9999 HP and MP, 999 the rest), as the original
    does with `EVL_clampBaseStats` (decided by the design owner, 2026-10-20). With a copy and high stats the
    reconstruction gives more than the cap (9999 and 9999 give 12499) and it stays at 9999.
  - The `species_p` copy belongs to the partner: its stats in `virusBackup` of the partner state (`sukaBackup*`) and
    the form in the script variable `PSTAT_SUKAMON_BACKUP_DIGIMON` (`PSTAT_5`), as in the original.
- **Also, always:** weight goes to the new form's default weight (`RAISE_DATA`), and care mistakes and battles go
  back to 0 **(verified)**.
- **Ultimate:** on reaching level 5 without an item, the remaining life goes up **96 hours** **(verified)**.
- **Tamer level:** on reaching Champion there is a 20% chance of going up a tamer level, and on reaching Ultimate
  100%, but only if the `IS_SCRIPT_PAUSED` mark is active **(verified)**. What that mark represents at this moment is
  an **(assumption)**.

### Techniques when evolving

`EVL_applyEvolution` **(verified)**:

- The new form learns the weakest technique (lowest nonzero power) in its list that falls in the block of its first
  specialty. Each specialty takes up a block of 9 techniques in the technique table. In-Training forms have no
  technique in their block (their only technique is outside all of them): the code passes the end-of-list mark to
  `learnMove` and they learn nothing **(verified)**.
- The equipped technique slots are reset: the first is left with that technique (or with the basic attack if the form
  is Fresh or In-Training) and the fourth with the last technique in the list that is in a high range of indexes.
  That this range is that of the exclusive or finisher techniques is an **(assumption)**.

### Rebirth

On dying, `reincarnatePartner` saves the dead partner's stats (for inheritance), chooses the Fresh Digimon according
to the egg chosen (`REINCARNATE_BABY_TYPE`), sets the lives to 3 and marks the Fresh Digimon as raised
**(verified)**. The egg comes out at random among the four (`rand() % 4` in `dooa.c`), and the Fresh Digimon is left
with the basic attack in the first slot and the others empty **(verified)**. The stat inheritance and the rest of
the life cycle are in the [care sheet](care.md).

## Data

Tables in the main executable. `dwx` extracts them to YAML from the user's disc; here only their shape is
described.

| Table | Entry | Entries | Fields |
|---|---|---|---|
| `EVOLUTION_REQS` | 28 bytes | 63 | Source form for the bonus; minimums of HP, MP, offense, defense, speed and Brains (for Rookie, 1 marks a favored stat); care mistakes; weight; discipline; happiness; battles; techniques; `flags` (bit 0: battles as a maximum; bit 4: mistakes as a maximum); padding. −1 means "no requirement". Indexed by the destination form. |
| `EVOLUTION_PATHS` | 11 bytes | 62 | Up to 5 source forms and up to 6 destination forms (−1 empty). Indexed by form − 1. Also used by the menu's evolution card. The row of an Ultimate (form 59) has a −2 and a 0 in the destinations; since Ultimates do not evolve, the choice never reads it **(verified)**. |
| `EVOLUTION_STATS_GAINS` | 14 bytes | 66 | Reference values of HP, MP, offense, defense, speed and Brains, and the real form that is loaded. Indexed by the destination form. |
| `EVOLUTION_ITEM_TARGET` | 1 byte | 44 | Destination form of each evolution item, indexed by item − first evolution item. `dwx` extracts it into `evolves_to` of `data/item_effects`; the optional list `item_targets` of `data/evolution` replaces it item by item, or supplies it if the ruleset does not name `item_effects`. |
| `DIGIMON_DATA` | 52 bytes | 180 | Parameters of each Digimon: name (not in the European version), bones, radius, height, type, level, three specialties, item it drops and its chance, 16 possible techniques. |
| `RAISE_DATA` | — | — | Raising data per Digimon; here only the default weight is used. See the [care sheet](care.md). |
| `REINCARNATE_BABY_TYPE` | 2 bytes | 4 | The Fresh Digimon that comes out of each egg on rebirth. |

Entry sizes computed from the `struct`s in `include/dw/evl.h` and `params.h` and from the sizes in
`config/us/symbols.txt` **(verified)**.

## Oddities

- **The evolution item "infects" the game.** `HAS_USED_EVOITEM` is only set to 0 when the game starts
  (`initializeStatusObjects`, called from `main`). After using an evolution item, all the following natural
  evolutions of that session are left without stat gains or life bonus **(verified)**. Whether loading a game fixes it
  is not clear: the mark does not seem to be part of the saved game **(assumption)**.
- **Accumulator not reset.** When choosing among Rookie or Champion candidates, the sum and the stat count are only
  reset when a candidate becomes the best. If a candidate passes but does not improve, its sum carries over into the
  next one's average **(verified)**. If a destination did not ask for any stat, the average would divide by 0
  **(assumption: we do not know if the table has any such case)**.
- **Fresh Digimon without a destination.** `getFreshEvolutionTarget` returns an uninitialized value if the form is
  not one of the four Fresh Digimon; the decomp itself points it out **(verified)**. It still sets the counter to 0,
  so a Fresh Digimon without a destination restarts its count **(verified)**. In `dwe` it is kept
  (`unlisted_baby_restarts_timer: true`): decided by the design owner (2026-10-20): faithful to the original.
- **The 360 hours override the normal choice.** When a Champion reaches exactly 360 hours, `tickConditions` calls the
  mode 3 specials after the normal choice and stores their result on top: if no special comes out, it does not evolve
  either even if the normal choice had found a destination **(verified)**. In `dwe` it is kept
  (`timer_special_overrides_normal: true`): decided by the design owner (2026-10-20): faithful to the original.
- **Stat tie at Rookie.** The last tied stat wins, not the first **(verified)**.
- **Reinforced scolding.** In `handlePraiseScold`, scolding right after a food refusal or a poop on the floor does not
  initialize the happiness change **(verified)**; the value it ends up adding is an **(assumption)**.

## Where it is in the decomp

| What | Where |
|---|---|
| Periodic check, thresholds, virus bar | `src/main/partner_impl.c`: `tickConditions` |
| Destination choice and scoring | `src/main/evolution.c`: `getFreshEvolutionTarget`, `getInTrainingEvolutionTarget`, `getRookieEvolutionTarget`, `getChampionEvolutionTarget`, `calculateRequirementScore`, `getNumMasteredMoves` |
| Special evolutions | `src/main/evolution.c`: `handleSpecialEvolutions`; called from `src/main/partner.c`: `partnerTickSleep`, `partnerTickPraiseScold` |
| `species_be` notice | `src/main/partner_impl.c`: `handlePraiseScold` |
| Poop and virus bar | `src/main/partner_impl.c`: `createPoopPile` |
| `species_p` return, evolution by script | `src/main/partner.c`, the call to the species routines (routines 2 and 10) |
| Evolution items | `src/main/item.c`: `handleEvoItems`; refusal in `src/main/partner_impl.c`: `partnerWillRefuseItem` |
| Applying the evolution (stats, techniques, life) | `src/evl/evl.c`: `EVL_applyEvolution`, `EVL_scaleBaseStats`, `EVL_clampBaseStats` |
| Counter by hours | `src/main/clock.c`; `src/main/partner_impl.c`: `skipHours`, `handleSleeping` |
| Rebirth | `src/main/evolution.c`: `reincarnatePartner`; `src/dooa/dooa.c` |
| Structures | `include/dw/evl.h`, `include/dw/params.h` |
