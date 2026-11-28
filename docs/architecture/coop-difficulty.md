# Co-op difficulty by player count (design document)

Status: **built** (2026-11-20, ticket #361) for the enemies' stats and finisher in normal and fixed fights; §9 says
what landed and where it differs from this proposal. It builds on
[online co-op](multiplayer.md), [ADR 0024](../adr/0024-co-op-with-authoritative-host.md), the first version of co-op
battles (#351, PR #363) and the battle design for both partners in [co-op combat](coop-combat.md). Sessions of up to
four players are ticket #352 ([room browser and four players](coop-browser-4p.md)).

**Design base:** the combat redesign is defined in [Combat system design](combat-design.md), the base of all combat
documentation; its chapter 8 sets the co-op scaling target for the new system.

**Rule zero:** the single-player game does not change. With one fighting partner and nobody helping, every factor in
this document is exactly 100 % and no code path differs from today. Co-op scaling is an improvement, documented and
optional, chosen by the host.

**Audit (2026-11-27):** checked against the issue's deliverables in the [#361 audit](audits/361-audit.md); §5.5, §8,
§11 and §12.8 were completed then.

**Alternative under evaluation (2026-11-27):** §12 records the unimplemented "+20 % per player on screen and wider
contact reach" model raised in ticket #361. The tables of §5 and the implementation of §9 remain the behaviour that
shipped.

## 1. How the original sets difficulty

The original has no difficulty setting and no scaling. Every number below comes from the decomp (function names) or
from dwe's rulesets (`content/packs/game/rules/original/battle.yaml`), which port it.

### 1.1 Enemies and encounters

- **No random encounters.** Each screen has up to 8 fixed enemy placements in its `.MAP`; the screen script decides
  which appear (`SET_DIGIMON`, `STORE_RANDOM` and the clock). Touching one runs its script section, which calls
  `START_BATTLE` → `startBattle` → `handleBattleStart` → `BTL_battleMain`.
- **Enemy stats come from the placement, not the species.** HP, MP, Off, Def, Speed, Brains, moves, move weights
  (`movesPrio`) and the bits reward are per placement; only the drop item and its chance come from the species table.
  dwe reads them the same way (`enemyFromMap` in `packages/game/battle/src/core/combatant.cpp`).
- **The curve is regional, not adaptive.** Nothing scales with the partner's level or progress. Median enemy Off by
  region: Native Forest 150, Gear Savanna 340, Factorial Town 530, Mt. Infinity 680, Back Dimension 700 (HP 3400–6100).
- **Group size by join roll.** The touched enemy always fights; every other enemy on screen joins with a chance from a
  3×3 table by partner and enemy type (30–100 %, `FLEE_CHANCES`, dwe `battle.yaml` `joining`). Enemy bell gives −50 to
  the roll, Enemy repel +20. At most 3 enemies, exactly 1 partner.
- **Fixed fights (trigger 1).** Story bosses: everybody on screen joins, Run is disabled and enemies spend no MP.
  Predefined fights (script variables 0xFA–0xFD) are 45 of the 499 `START_BATTLE` calls.
- **No permanent kills.** Enemies heal fully after every battle and come back when the screen is entered again.

### 1.2 Battle pressure

- Real time at 20 ticks per second: `BTL_tickPartnerAI`, `BTL_tickEnemyAI`, hits, statuses.
- **Enemies target only the partner.** The tamer is never a fighter. An enemy only turns on another enemy that hit it
  with an area move.
- Damage: `m·(P + clamp(Off−Def, ±500)·P/500)/30` × 90–110 %, 1–9999 per hit, where `m` is the type factor (0.2, 1 or
  2 per defender specialty). No critical hits. Finishers ignore Def; enemy finishers deal ×1–2 at random.
- Enemy move choice is a weighted draw on `movesPrio` (+20 to area moves while allies live). Enemies fire their
  finisher by themselves when the meter (`3000 − Speed`) is full.
- The partner's side is shaped by Brains (menu, targeting, buffs), discipline (disobedience below 301 Brains) and its
  condition (injured or sick: −20 % to Off, Def, Speed and Brains).

### 1.3 Rewards and progression

- **Bits:** the sum of each enemy's placement `bits`.
- **Drops:** per enemy, `dropChance > rand(100)` gives its item.
- **Stat gains instead of experience** (`battleStatsGainsAndDrops`): for each stat, with E the strongest enemy's value,
  P the partner's and F = 10/12/16 for 1/2/3 enemies (`rewards.enemy_factors`): E ≥ P gives ⌈E·F/(P·10)⌉, otherwise +1
  with chance E·F·10/P %. A stat that gained 0 gets one more chance from hits, blocks and HP lost.
- **Learning in battle:** one move from the enemies' used moves, by specialty and a chance table.
- **Counters and care:** a win gives happiness +2, tiredness +5 and `battles + 1` (evolution requirements read it);
  fleeing −10 happiness and −6 discipline; a loss −30 happiness, −20 discipline, one life, and 30 % of the occupied bag
  slots. Every battle costs 20 minutes of clock and food −15.

### 1.4 Tournaments

Bracket of 8, three matches, 1 vs 1, no Run, 99 s timer. Opponent stats come from a per-cup base (D 100 … S 300)
±30 % and ×1/×2/×2.5 by level. No bits, drops or stat gains; 20 % HP and MP back between matches.

## 2. What co-op changes

| System | Single player | What co-op breaks | Where it is handled |
|---|---|---|---|
| Damage output | One partner | Two to four partners hit the same enemies: time to kill falls by about 1/N | §5 HP scaling |
| Enemy pressure | All enemy attacks on one partner | Attacks spread over N partners; each takes about 1/N | §5 Off, MP and finisher |
| Items | One bag | Every player throws from their own bag: N times the healing | §5 helper factor |
| Loss | Partner at 0 HP ends the battle | Other partners can revive or finish it | [co-op combat](coop-combat.md) §4.8 |
| Group size | Join roll by the partner's type | Whose type? More bodies make more joiners fair | §5 join bonus |
| Stat gains | Strongest enemy against the partner | A weak partner survives late enemies carried by a strong one and gains a lot | §7 risks |
| Bits and drops | One player's | Duplicating them creates money and items | §6 |
| Evolution counters | `battles` of one partner | One win per participating partner | §6 |
| Fixed and recruit fights | One partner, scripted | Bosses melt; recruit conditions read one partner | §8 |
| Tournaments | 1 vs 1 by design | Not a team format | §8: stay solo |
| Clock and world | One game | The host's world, once per battle | §6 |

The first co-op version (PR #363) already covers part of this: the battle runs on the host, only the starter's partner
fights, the other player throws items from their own bag and cheers, and bits and drops stay in the host's world
(decision 4 of [multiplayer](multiplayer.md) §6).

## 3. Strategies compared

| Strategy | For | Against | Verdict |
|---|---|---|---|
| **Enemy HP by player count** | One number, predictable, keeps each enemy hit as dangerous as in solo; the industry default (Diablo III adds health per extra player, Monster Hunter World ~150 % at two hunters and ~220–260 % at three or four) | Longer battles if overdone | **Main lever** |
| Enemy Off | Restores the per-partner pressure lost to spreading | The `Off − Def` term clamps at ±500 and type factors multiply it: a small Off change moves damage a lot and punishes weak partners | Small, only at 3–4 players, capped at 999 |
| Enemy Def | Lengthens battles | Divides the weakest partner's damage first (a Rookie may stop hurting at all); breaks the regional curve | **No** |
| Enemy MP | More moves before running dry; matters once fights last longer | None if it follows HP | Yes, with HP |
| More enemies (join bonus) | Uses the original's own mechanic; more targets for more partners; raises the stat gain factor F as the original would | The original caps at 3; more than 3 needs new placements | Yes, as a roll bonus; never above 3 |
| Extra enemies beyond 3 | More spectacle | Needs placements, AI and HUD space the original lacks; out of scope (#361 excludes a general AI change) | No (open to a mod) |
| AI aggression (shorter cooldowns, more finishers) | Restores pressure without touching stats | Cooldowns are the battle's rhythm; changing them alters identity | Only the enemy finisher fill rate |
| Rewards split | No new money or items | Smaller rewards per player feel bad | Bits: host keeps them (decision 4), optional even split |
| Rewards duplicated | Generous | Inflates money and items | Only what belongs to each partner (stat gains, learned moves, counters) |
| Scale by partner strength (level scaling) | Fair to mixed parties | The original has none; it hides the regional curve and rewards weak partners | No; see §7 for the carry risk |

Sources for other games: [Monster Hunter World multiplayer](https://monsterhunterworld.wiki.fextralife.com/Multiplayer)
(about 150 % HP at 2 players, 220–260 % at 3+; it drops back when players leave), [Diablo III
multiplayer](https://di.diablowiki.net/Multiplayer) (monster health per extra player, settled at +50 % in patch 1.0.8)
and [PC Gamer on Diablo III's hotfix](https://pcgamer.com/diablo-3-hotfix-removes-monster-damage-boost-in-co-op-inferno-teams-breathe-sigh-of-relief)
(damage scaling per player was removed: it made groups worse than solo).

## 4. What drives the scaling

| Driver | For | Against | Verdict |
|---|---|---|---|
| Players connected | Simple | Punishes a player who fights alone while the others are elsewhere | No |
| Players on the same map | Matches #351's sharing rule | A player on the map who does not fight still counts fully | Only for helpers, at a small weight |
| **Partners fighting in this battle** | Measures the real damage output | Changes when someone joins or leaves mid-battle | **Yes** |
| Partner stats and progress | Fair to uneven parties | Level scaling; hides the curve | No |

**Decision:** `N` = partners fighting on the players' side, counted at battle start and again whenever a partner joins
or leaves the battle (disconnection, Run, a late arrival). Each **helper** (a player present who does not fight, as in
PR #363) adds a small factor, because their bag and cheers are real help. A knocked out partner still counts: it can be
revived. When `N` changes mid-battle, enemy HP and MP are rescaled keeping the **fraction** they had (an enemy at 40 %
stays at 40 %), as Monster Hunter World does since Iceborne.

## 5. Proposed parameters

Percent of the original value. `N` counts fighting partners.

### 5.1 Normal battles

| Value | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| Enemy max HP | 100 | 180 | 250 | 320 |
| Enemy max MP | 100 | 150 | 200 | 250 |
| Enemy Off | 100 | 100 | 105 | 110 |
| Enemy Def, Speed, Brains | 100 | 100 | 100 | 100 |
| Enemy finisher fill rate | 100 | 125 | 150 | 175 |
| Join roll bonus (lower roll = more join; Enemy bell is −50) | 0 | −15 | −25 | −35 |
| Each helper adds to enemy HP | +15 | +15 | +15 | — |

Why these numbers: with equal partners, damage output grows about ×N while each partner takes about 1/N of the hits.
HP grows a bit slower than N (1.8, 2.5, 3.2) so battles end slightly faster than solo, which the extra enemies from the
join bonus compensate. The lost pressure comes back through the enemy finisher (more often) and, at 3 and 4 partners, a
5–10 % Off bump; Off never goes above 999 (the stat cap). Def is never scaled. In the first version (one fighter plus
helpers) only the helper line applies: one helper = 115 % HP.

### 5.2 Fixed fights and bosses (trigger 1)

| Value | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| Enemy max HP | 100 | 200 | 290 | 370 |
| Enemy Off | 100 | 105 | 110 | 115 |
| Enemy finisher fill rate | 100 | 125 | 150 | 175 |
| Join bonus | — (everyone already joins) | — | — | — |

Bosses are usually alone or few, and they cannot be fled: they get a bit more HP than normal fights so a boss still
lasts. Their MP needs no scaling (they spend none in fixed fights).

### 5.3 Rewards

| Reward | Proposal |
|---|---|
| Bits | Sum of the enemies' bits **once**, to the host's world (decision 4). Option: split evenly between the players, total unchanged. |
| Drops | One roll per enemy, as in the original. The item goes to the player whose partner landed the last hit on that enemy (the starter if unknown). A guest takes it home. |
| Stat gains | Per partner, with the original formula against the **unscaled** enemy stats (scaled HP must not inflate HP gains). Only partners that landed at least one hit or were standing at the end. |
| Learned moves | Per partner, own roll, as in solo. |
| Care and counters | Per partner (happiness, tiredness, food, `battles + 1` on a win). Clock +20 minutes once, for the world. |
| Loss | See [co-op combat](coop-combat.md) §4.8: the battle is lost only when every partner is down; each partner then loses as in solo (life, care) and each player's own bag loses its 30 %. |

### 5.4 Examples

A Gear Savanna placement (illustrative: HP 1200, MP 300, Off 340, Def 300), normal fight:

| | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP | 1200 | 2160 | 3000 | 3840 |
| MP | 300 | 450 | 600 | 750 |
| Off | 340 | 340 | 357 | 374 |
| Finisher goal (Speed 200) | 2800 ticks of fill | ~2240 | ~1870 | ~1600 |
| Chance that a second enemy of the same type joins (base 60 %, proposal: the join bonus is not built) | 60 % | 75 % | 85 % | 95 % |

Machinedramon at the summit (184:51; 9400 HP, Off 999, Def 650), fixed fight:

| | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP | 9400 | 18800 | 27260 | 34780 |
| Off | 999 | 999 (cap) | 999 | 999 |
| Stat gains for each partner | from 9400 HP, 999 Off | same | same | same |

### 5.5 What ships, by level

The values the game applies today (`coop_scaling` in `battle.yaml`, `game::battle::coopScalingFactors`). `N` counts the
partners fighting; each helper (a player on the map whose partner does not fight) adds 15 points of HP; `hard` adds 25
points of HP on top. `off` is 100 everywhere. With `N = 1` and no helper every level is 100 (rule zero).

| Normal fight | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP, standard | 100 | 180 | 250 | 320 |
| HP, hard | 100 | 205 | 275 | 345 |
| MP | 100 | 150 | 200 | 250 |
| Off | 100 | 100 | 105 | 110 |
| Finisher fill | 100 | 125 | 150 | 175 |

| Fixed fight (trigger 1) | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| HP, standard | 100 | 200 | 290 | 370 |
| HP, hard | 100 | 225 | 315 | 395 |
| MP | 100 | 100 | 100 | 100 |
| Off | 100 | 105 | 110 | 115 |
| Finisher fill | 100 | 125 | 150 | 175 |

One fighter with one helper (a partner that stays out) is 115 % HP in standard and 140 % in hard; its other values
stay at 100.

The canyon ambush (43:51, three foes, fixed) is a story trio, so each foe gets the boss table (HP ×2.0/2.9/3.7) instead
of the normal one (×1.8/2.5/3.2).

## 6. Progression and economy

- **Host's world.** Money, story, flags and the map belong to the host (decision 4). Scaling never changes bits; the
  only bits option is how the same total is split.
- **Each partner is its own progression.** Stat gains, learned moves, care and the evolution counter are applied per
  partner and travel home with the guest (PR #363 already sends the fighting partner's spoils in `BattleEnd`).
- **No new items.** Drops roll once per enemy. Items thrown come out of the thrower's own bag.
- **Evolution pace.** A partner gets `battles + 1` per won battle it fought in, the same as one solo win; co-op does
  not count more wins than battles.

## 7. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Carry (power leveling) | A fresh Rookie next to a strong partner survives Mt. Infinity fights and gains ⌈E·F/(P·10)⌉ per stat (E 700, P 100 → +7 per stat per battle) | Gains need participation (§5.3). Optional cap `coop.rewards.max_gain_per_stat` (off by default: the original also allows it if the partner survives) |
| HP above 9999 | Bosses at 3–4 players pass 9999 HP | dwe stores HP as `int32` and the 9999 cap is per hit; the HUD bars are by fraction; check that no number display assumes 4 digits |
| Off cap | Off above 999 at bosses | Clamp at 999 |
| More enemies → bigger F | At 4 players 3-foe fights are common and F = 16 | Accepted: it is the original's own reward for bigger fights; measured in §10 |
| Faster battles feel trivial | Co-op too easy | Levels `standard` and `hard` (§9) |
| Slower battles feel long | Boss fights drag at 4 | Measured target in §10 (time to win 0.9–1.3× solo) |
| Item economy | Four bags of healing | Helper factor; items are each player's own, so they are spent, not created |
| Host's ruleset governs | A guest's mod or Easy setting is ignored in a shared battle | Shown in the room lobby; documented |
| Rescaling mid-battle | A disconnect makes the boss suddenly easy | Keep the HP fraction; never heal |

## 8. Encounters that need special handling

Ids are `script:section` (or map), as in [the battle module](../modules/game/battle.md) and the story reference.
"Fixed" = trigger 1 (no Run, everyone joins, enemies spend no MP).

| Encounter | Where | Id | Class | Handling |
|---|---|---|---|---|
| Agumon (first recruit) | Native Forest / Ogre Fortress | 105:6, 48:5/6 | Recruit | Boss table. Recruit result goes to the host's world |
| Canyon bandits | Great Canyon | 43:51, 123:51 | Fixed, 3 foes | Boss table, no join bonus |
| Ogre Fortress trio | ogre03 (screen 48) | 48:52 | Fixed, 3 foes | Boss table |
| Ogremon's den | screen 137 | 137:51 (then recruit 26:51) | Fixed, multi-foe | Boss table |
| Ogremon repeat fights | gcan08_1/2, ogre03 | — | Fixed | Boss table |
| Birdramon | gcan10 | script 124, section 0x33 | Fixed recruit | Boss table |
| Etemon | Digimon Bridge, mayo08a | — | Fixed | Boss table |
| Seadramon | koda06 | — | Fixed | Boss table |
| Kokatorimon | Misty Trees, mist03 | — | Fixed, clock-gated | Boss table |
| Greymon ambush | File City | 162:57 (foe 9) | Ambush | Boss table |
| Second ambush (opens Mt. Infinity) | File City | 162:57 (foe 10) | Ambush | Boss table |
| SkullGreymon | Underground Lab | 181:51 | Fixed | Boss table |
| Giromon | Factorial Town | 153:5 | Recruit | Boss table |
| Andromon | Factorial Town | 151:5 | Recruit chain | Boss table |
| Guardromon | fact02 | — | Fixed | Boss table |
| WaruMonzaemon | Toy Mansion | 145:5 | Boss, needs a Monzaemon partner | The condition reads the **starter's** partner; boss table |
| Leomon | cave 109:5 → Gear Savanna | 75:51 | Recruit | Boss table |
| Megadramon | Mt. Infinity | 198:53 | Climb boss | Boss table |
| Machinedramon (final) | Mt. Infinity summit | 184:51 (foe 5) | Final boss | Boss table; Off already at cap |
| Machinedramon ×3 (post-game) | Back Dimension exits | 100:60, 63:60, 47:60 | Repeating boss | Boss table |
| Digitamamon (post-game) | Mt. Infinity | 184:51 with T50 | Boss | Boss table |
| Back Dimension corridors | screens 226–228 | scripts 185–187 | High-HP normal | Normal table |
| Tournaments and arena | Coliseum, arena | `startTournament` | 1 vs 1 | **No co-op**: only the entrant fights, others watch; no scaling |
| Versus | — | — | PvP | Not co-op; unchanged |

To verify before tuning (not found in the sources read): Myotismon (Grey Lord's Mansion), Devimon (51:57, mgen06),
Mamemon and Piximon. Every row can be overridden by id in the data (§9).

**What the game does today with this table.** Per-fight overrides are not built (§9): a fight gets the fixed table
when its script starts it with trigger 1, and the normal one otherwise. So every "Fixed" row above already gets its
handling; a recruit or ambush the script starts with another trigger gets the normal table until an override exists.
Tournaments and versus never scale.

## 9. Implementation

**What landed (2026-11-20).** The option is `coop_difficulty` (Game tab: off, standard by default, hard; saved as
`UserSettings::coopDifficulty`); only the host's counts. The tables live in the battle rules
(`content/packs/game/rules/original/battle.yaml`, section `coop_scaling`: `normal` and `fixed` with `hp_percent`,
`mp_percent`, `offense_percent` and `finisher_fill_percent` per partner count, plus `helper_hp_percent` and
`hard_hp_bonus_percent`), not in a new `coop.yaml`, so a mod tunes them with the rest of the battle. The pure function
is `game::battle::coopScalingFactors`; the host's `CoopBattles` puts the level and the helpers (players on the map whose
partner does not fight) in `BattleSetup::coopScaling`, and `Battle::start` scales the enemies' max HP, MP, offense and
finisher goal from their unscaled stats. `startStats` stay unscaled, so stat gains are the original's. When a partner
runs, the enemies scale again for the partners left, each keeping its HP and MP share; a knocked-out partner still
counts. Not built from the proposal below: the join roll bonus (a guest-started battle has no roll on the host to add
to), the per-fight `overrides` and the reward options of §5.3. The room lobby shows the host's level to everyone
(`online.current.coop_difficulty` and its `_key`, read by `ui/online_room/logic.lua`).

### 9.1 Setting

- `UserSettings::coopDifficulty` in `apps/game/src/user_settings.hpp` (the proposal called it `coopScaling`) with the
  values `off`, `standard` (the default) and `hard` (HP +25 points on top of each table). Shown in the Game options
  and in the room lobby. Only the host's value counts; guests see it.
- It is applied the same way as the existing difficulty (`RuleOptions` in `apps/game/src/rule_options.hpp`, changes
  through `RulesetStore`), but scaling is not a fixed change: it depends on `N`, so it is computed per battle.
- With `N = 1` and no helpers, every factor is 100 whatever the setting: single player is never touched.

### 9.2 Data

A new ruleset section `content/packs/game/rules/original/coop.yaml` with schema
`content/schemas/ruleset_coop.schema.json`:

```yaml
scaling:
  levels:
    standard:
      normal:
        hp_percent: [100, 180, 250, 320]
        mp_percent: [100, 150, 200, 250]
        off_percent: [100, 100, 105, 110]
        enemy_finisher_fill_percent: [100, 125, 150, 175]
        join_roll: [0, -15, -25, -35]
        helper_hp_percent: 15
      fixed:
        hp_percent: [100, 200, 290, 370]
        off_percent: [100, 105, 110, 115]
        enemy_finisher_fill_percent: [100, 125, 150, 175]
    hard: { extends: standard, hp_bonus_percent: 25 }
  overrides:
    "184:51": { kind: fixed }
rewards:
  bits_split: false
  max_gain_per_stat: 0   # 0 = original formula, no cap
```

A mod tunes it like any other section; `overrides` lets a pack treat a specific fight differently.

### 9.3 Where it hooks in

| What | Where | Change |
|---|---|---|
| Factors | `packages/game/battle` | Pure function `coopScaling(const CoopRules&, CoopLevel, int fighters, int helpers, bool fixed)` → `ScalingFactors`; `readCoopRules` |
| Enemy stats | `BattleAdapter::start` (`packages/session/src/battle_adapter.cpp`) | After the enemies are built and before `Battle::start`: scale `hp`, `mp` and `off` of `request.enemies`. One place covers map, story and scripted fights. Tournaments are skipped |
| Unscaled copy | `Combatant` (`combatant.hpp`) | `rewardStats`: the enemy's original stats, read by `statGains` in `settleBattle` (`aftermath.cpp`) |
| Join roll | The join roll of `mapBattleRequest` (`apps/game/src/battle_trigger.cpp`, `joining` rules) | Add `join_roll[N]`; never more than `limits.max_enemies` |
| Enemy finisher | `FinisherRules`, enemy fill in the battle tick | `enemy_fill_percent`, 100 by default |
| Mid-battle change | `Battle` | `Battle::rescaleEnemies(ScalingFactors)`, keeping each enemy's HP and MP fraction |
| Rewards | `settleBattle`, `BattleAdapter::giveRewards` / `finish` | Per-partner results (see [co-op combat](coop-combat.md) §6); bits once |
| Replication | `BattleFrame` (`packages/game/coop/protocol.hpp`) | Already carries each fighter's HP and MP; add max HP so the guests' bars are right |

Everything runs on the host: guests never compute factors, so there is nothing to desync.

### 9.4 Folder battles

The scaling runs at battle start whatever the battle system, so a [Folder](folder-battle.md) battle gets the same
scaled enemies (test "co-op difficulty scales a folder battle's enemies as it scales a classic one's"). Two differences
matter for balance: Folder adds ATK to the damage at `attack_percent`, so the offense column weighs a little more there,
and partners' offensive area techniques hit nearby allies (friendly fire), which the tables do not count. A flat damage
factor per player would apply to the Folder damage result of an enemy attacker, after DEF and cover and before the cap;
folder-battle.md, "Co-op difficulty in Folder", has the details.

## 10. Test scenarios

### 10.1 Unit (doctest, `packages/game/battle/tests`)

- With `N = 1` and no helpers every factor is 100, for every level (single player unchanged).
- The tables give the values of §5 for `N` 1–4, normal and fixed, with the `hard` bonus and with overrides.
- Off never goes above 999; HP above 9999 is stored and shown.
- Stat gains use `rewardStats`: the same gains with and without scaling.
- `rescaleEnemies` keeps the HP fraction when `N` goes up or down.
- The join bonus never makes more than 3 enemies.

### 10.2 Balance (`dwe-sim`, [simulations](../guides/simulations.md))

Extend the `battle` scenario with `--param partners=<species>,<species>...` (one to four) and the setting. Measure, over
1000 runs per case: win rate, time to win, knockouts per partner, damage taken per partner, items used.

| Case | Party | Enemy | Target against the same partner alone |
|---|---|---|---|
| Early wild | 2–4 Rookies | tunn04 digger | Win rate within ±10 points; time to win 0.9–1.3× |
| Mid wild group | 2–4 Champions | gcan08_1 trio | Same, with the join bonus on |
| Late wild | 2–4 perfect partners | mgen01 wild | Same |
| Boss | 2–4 endgame partners | Machinedramon 184:51 | Time to win 0.9–1.3×; at least one finisher from the boss per fight |
| Back Dimension | 2–4 endgame partners | corridor placement (scripts 185–187) | Damage taken per partner 0.6–1.0× |
| Mixed party (carry) | one perfect + one fresh Rookie | mgen01 wild | Report the Rookie's gains per battle with and without the cap |

### 10.3 Game (two to four PCs or the local two-instance setup of [multiplayer](multiplayer.md) §7.1)

1. Solo with the setting on: a wild fight and a boss give exactly the original numbers (compare with the oracle trace).
2. Two players, Ogre Fortress trio 48:52: boss table HP, no Run, both partners' rewards.
3. A recruit fight (Leomon 75:51) and WaruMonzaemon 145:5 with the starter's partner deciding the condition.
4. A guest disconnects mid-boss: HP fraction kept, the battle continues.
5. A loss with all partners down: each player's bag loses 30 %, each partner a life.
6. A tournament with a guest present: solo, unscaled, the guest watches.

## 11. Open decisions

1. Default of `coopScaling` in co-op sessions: `standard` (recommended) or `off`.
2. Bits: host keeps all (recommended, decision 4) or even split.
3. Stat gain cap for carried partners: off (recommended, original behaviour) or on.
4. Which model to keep: the shipped per-fighter tables (§5/§9) or the flat +20 % per player on screen (§12, not built).

**Technical recommendation (2026-11-27).** Keep the shipped model as the default and add §12 as a third level
(`linear_20`), not as a replacement, once §10.2 is measured for both:

- the shipped model counts fighting partners, which is what changes the damage dealt and taken; §12 counts bodies on the
  map, so a spectator or a Rookie raises a fight as much as an endgame partner (§12.6, §12.7);
- §12 needs three new hooks (a damage multiplier in the damage strategy, a join roll bonus, a hostile contact reach)
  while the shipped model needs none; the join roll bonus is worth building for both, since it is the original's own
  way to make bigger fights;
- a level is data in `coop_scaling`, so the host can choose either without a code path for solo play.

Order: the join roll bonus first (both models want it), then the per-fight overrides of §8, then `linear_20`, each
behind the setting and measured with the §10.2 cases before it becomes a default.

## 12. Alternative: flat +20 % per player on screen (2026-11-27)

Status: **proposal, not implemented.** It answers the idea raised in ticket #361 after the model that shipped: use one
rule a player can read ("one more player on screen = 20 % more life and damage"), and make fights with several enemies
more common by widening the field contact reach. It would replace §4–§5 only if chosen; it changes nothing that is in
the game today. Rule zero still applies: with one player on the map, every value is 100 %.

### 12.1 Counting `P`

`P` = players with a field entity on the host's current map: the host plus every guest in `coopPlayerHere` whose player
was added with `Field::addPlayerNearHost` (`apps/game/src/coop_world.cpp`, `packages/game/field/src/field.cpp`).
Connected players on another map do not count: `coopPlayerHere` already checks the map id and a pending map change.
`P` counts tamers, not Digimon partners.

Unlike §4, `P` does not ask whether a partner fights: a player who only throws items or watches counts fully. That is
the point of the alternative, but it also means a spectator makes a fight harder; §12.6 lists the mitigations. The host
has every guest's position in the field (`Field::moveOtherPlayer`), so reach can be tested against any of them, not only
the local player. All factors and rolls stay on the host, as in §9.3.

### 12.2 Parameters

Percent of the original value. `P = 1` is solo: 100 everywhere, with no code path changed. The +20 % is per
**additional** player, so `factor = 1 + 0.2·(P − 1)`.

| Value | P = 1 | P = 2 | P = 3 | P = 4 |
|---|---|---|---|---|
| Enemy max HP | 100 | 120 | 140 | 160 |
| Enemy damage dealt | 100 | 120 | 140 | 160 |
| Enemy max MP | 100 | 100 | 100 | 100 |
| Enemy finisher fill rate | 100 | 100 | 100 | 100 |
| Join roll bonus (lower roll = more joiners) | 0 | −10 | −20 | −30 |
| Hostile contact reach | 100 | 120 | 140 | 160 |

Offense, Defense, Speed and Brains stay as in the placement. The +20 % is applied to the **damage result**, not to the
Offense stat: `OriginalDamage::damage` (`packages/game/battle/src/original_damage.cpp`) multiplies by the factor for
enemy attackers after affinity, the ±500 difference cap and variance, and before the 1–9999 clamp. Scaling Offense
instead would not give +20 % damage: with Off 340, Def 300, move power 100 and affinity 1, the original hit is 3.6,
Off ×1.2 gives 4.05 (+12.6 %) and Off ×1.4 gives 4.51 (+25.2 %); at the Off cap (999) it gives nothing. Finishers are
multiplied too, including the enemy's random ×1–2. Def is never scaled, as in §3.

Fixed fights use the same HP and damage columns; they have no join roll because everyone already joins, and their
contact reach only decides who starts the fight. Tournaments, the arena and versus stay unscaled (§8).

### 12.3 A wider reach is not a join bonus

`Field::touchNpcs` builds the player's box from `rules_.player.radius` (100 in
`content/packs/game/field/rules.yaml`) one frame ahead, and `FieldNpcs::touchFromPlayer`
(`packages/game/field/src/npcs.cpp`) reports **the first** colliding autotalk NPC. Widening that box means the host, or
a guest standing near an enemy on the host's map, starts the battle sooner and can reach an NPC that was previously out
of range; it does not increase the number of enemies in the battle. Group size comes from `rollJoiningEnemies`
(`packages/game/battle/src/joining.cpp`), which reads `joining.chances` (3×3, plus `untypedChance`) and stops at
`limits.max_enemies` (3).

To actually make multi-enemy battles common, add the join roll bonus of §12.2 inside `rollJoiningEnemies`, after the
Enemy bell (−50) / Enemy repel (+20) modifiers and before the comparison with the chance. Keep the cap of 3 and the
scripted-fight rule (everyone joins). Changing the radius alone only moves the place where a fight starts, not its
size.

### 12.4 Where it hooks in

- **Data:** a third level of `coop_scaling` in `content/packs/game/rules/original/battle.yaml` (for example
  `linear_20`) with `hp_percent`, `damage_percent` and `join_roll` per player count; `content/packs/game/field/rules.yaml`
  gains a `coop_contact_reach_percent` for hostile NPCs only. The current level and solo behaviour are untouched.
- **Battle:** `game::battle::coopScalingFactors` takes `P` in this mode and returns the damage multiplier alongside the
  HP multiplier; `Battle::start` scales HP as today and the damage multiplier goes to the damage strategy. Stat gains
  keep reading the unscaled `rewardStats`.
- **Contact:** a separate pass in `Field::touchNpcs` with a larger box only for NPCs that can fight (`canFight`,
  `npcsOf` in `apps/game/src/battle_trigger.cpp`). Never change `player.radius` globally: talking, collisions and other
  NPCs would change too. The box stays axis-aligned as in the original; only its half-size grows, and only while the
  co-op option is on.
- **Rewards:** unchanged from §6 and §9 (bits once for the host, one drop roll per enemy, per-partner gains).
- **Guests:** the host checks reach against all field players; guests neither compute factors nor start a separate
  battle for an enemy owned by the host's world.

### 12.5 Comparison with the built model

| | Built (§5/§9) | Alternative §12 |
|---|---|---|
| Counter | Fighting partners plus a small helper weight | All players on the host's map |
| HP at 2/3/4 players | 180/250/320 | 120/140/160 |
| Pressure | Off 100/105/110, finisher 125/150/175 | Exact damage ×1.2/1.4/1.6, finisher unchanged |
| Group size | Join bonus planned, not built | Join bonus part of the model: −10/−20/−30 |
| Where fights start | Original touch | Wider contact for hostile NPCs |
| Feel | Longer fights, grindier bosses | Shorter fights, more enemies, original rhythm kept |

The alternative is cheaper (160 % HP instead of 320 % with four players) and easier to explain, but it puts more weight
on extra damage and on the join roll: with three enemies the reward factor is still F = 16 (§1.3), so fights get short.
The built model already shipped and is measured by §10; this one needs the same measurements before replacing it.

### 12.6 Risks and tests

| Risk | Mitigation |
|---|---|
| A spectator counts as a full player | Count only players with a field entity on the map; optional "free of menus/scenes" flag (the `offered` flag already exists for fighters); decide before implementing |
| Enemies notice the player from too far away | Reach grows only for hostile NPCs and only in co-op; `P = 1` uses the original box; keep the one-frame look-ahead |
| Damage above the cap or too spiky | Apply the multiplier before the 1–9999 clamp; test normal hits, finishers and the ±500 cap separately |
| More joiners inflate stat gains | Keep the cap of 3 and `rewardStats` unscaled; measure the F = 16 case |
| Guest latency | Reach is checked on the host against each guest's interpolated position; no guest-side trigger |
| Bosses melt | Measure Machinedramon 184:51 at `P = 4`; raise only the HP column if time-to-win drops below 0.9× solo |
| The Mega step breaks the original envelope | Read the tables per ruleset and test with `oculto_mega`; see §12.7 |

### 12.7 Interaction with the Mega step (future ruleset)

The `oculto_mega` ruleset (the Ultimate → Mega step,
[evolution](../modules/game/evolution.md#ultimate--mega-step)) changes the envelope these percentages were written
against: `species_t` as a partner is designed with 9999/9999/999/900/700/800 (HP/MP/Off/Def/Speed/Brains) and its
Infinity Cannon has power 240 with 1 iframe instead of the original 777 with 38
([extra evolutions](../modules/game/extra-evolutions.md#balance)). It reaches the original's stat caps and goes past
its regional curve, so "100 %" no longer means what it means in §1.

Consequences to check before adopting this model:

- The fixed limits of §1.2 (`Off − Def` clamped at ±500, 1–9999 damage per hit, Off capped at 999) were written for
  original placements. A flat multiplier can push a Mega's hits into the 9999 damage cap, and scaling Offense instead
  does nothing when the stat is already at 999. Apply the multiplier to the damage result and clamp once, as in §12.2.
- HP percentages apply to the enemy placement, not to the partner, so the table itself stays valid; but a Mega partner
  deletes a ×1.6 HP pool while a Rookie in the same party does not. Here one Mega plus three Rookies counts `P = 4` and
  receives the full 60 % extra HP and damage: the counter measures bodies, not power, so the objection of §4 is stronger.
- More joiners raise `F` (up to 16) for stat gains that already read unscaled stats. A Mega partner near the caps gains
  little, but a carried Rookie still gains a lot (§7).
- The Mega step lives in another ruleset: the co-op tables must be read per ruleset, with values derived from that
  ruleset's caps and placements instead of hard-coded 999/9999, and the `hard` level must be checked against Mega bosses
  as well.
- Test before enabling: the six cases of §10.2 with `oculto_mega` and a mixed party (one Mega plus three Rookies), plus
  a four-Mega party; watch time-to-win, hits that reach the damage cap and HP displayed with more than four digits. If
  the limits break, keep the built model for Mega rulesets or disable co-op scaling there.

Unit tests: `P = 1` unchanged for every setting; tables at `P = 2–4`; the damage multiplier exact on normal hits and
finishers; the join roll never above 3 enemies; reach identical to the original with one player. Balance (`dwe-sim`):
the six cases of §10.2 plus a four-player run with two helpers and a three-enemy group fight; target win rate ±10 points
and time to win 0.9–1.3× solo. Game: repeat §10.3 with the new setting, and check that a guest standing near an enemy
on the host's map starts the fight under the host's rules.

### 12.8 Examples

The same placements as §5.4, with the alternative (enemy hit: an illustrative 150 damage before the multiplier).

| Gear Savanna placement, normal fight | P = 1 | P = 2 | P = 3 | P = 4 |
|---|---|---|---|---|
| HP | 1200 | 1440 | 1680 | 1920 |
| MP | 300 | 300 | 300 | 300 |
| An enemy hit of 150 | 150 | 180 | 210 | 240 |
| Chance that a second enemy of the same type joins (base 60 %) | 60 % | 70 % | 80 % | 90 % |

| Machinedramon 184:51, fixed fight | P = 1 | P = 2 | P = 3 | P = 4 |
|---|---|---|---|---|
| HP | 9400 | 11280 | 13160 | 15040 |
| Off | 999 | 999 | 999 | 999 |
| Damage of each hit | ×1.0 | ×1.2 | ×1.4 | ×1.6 |

Against the shipped model at four players, the boss has 15040 HP instead of 34780, and each of its hits deals 60 % more
where the shipped model cannot raise its Off past the cap: fights are shorter and each hit matters more.

## 13. Related documents

- [Combat system design](combat-design.md) —the base: chapter 8, co-op combat and scaling.
- [Online co-op multiplayer](multiplayer.md) and [ADR 0024](../adr/0024-co-op-with-authoritative-host.md).
- [Co-op combat](coop-combat.md): both partners in one battle, targeting, commands and loss.
- [Rulesets](rulesets.md), [battle module](../modules/game/battle.md), [battle reference](../reference/battle.md).
- [Simulations](../guides/simulations.md).
