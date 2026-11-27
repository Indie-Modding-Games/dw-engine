# Battle systems: Classic and Folder

The game has two battle systems. **Classic** is the original's battle, unchanged: the command menu, obedience, the
partner's own choice of technique, the original's damage and block. **Folder** is the battle of the
[combat system design](combat-design.md) (issue #406): the trainer orders a technique and a target, the partner executes
it autonomously, positions itself by a stance, and damage follows the design's formula with cover and a defense bar.

This page is the audit the issue asks for, the architecture that keeps both systems apart, the decisions taken where
the design leaves something open, and the phased plan with its status.

## Choosing the battle system

- A [game mode](../modules/game/modes.md) names its battle system with the `battle` key of `mode.yaml`: `classic`
  (the default when the key is missing) or `folder`. **Classic Enhanced** has `battle: classic` and offers no other.
  **Folder Mode** has `battle: folder`, and its `battles: [folder, classic]` lets the new game go back to Classic.
- The system is chosen when the save is created and stored in the save's `mode` section (`battle`). A mode that lists
  more than one lets the player switch with the **Battle system** option (`battle_system`, Game tab): the next battle
  plays the new system and the next save keeps it. The option is fixed for a mode with one system, outside a game and
  for a co-op guest. A save written before battle systems existed has no `battle` key and loads as `classic`: every
  existing game keeps the battle it had.
- A new game started from the command line (test routes, the oracle) plays `classic`, so the routes and the oracle
  keep checking the original's battle. `--battle classic|folder` sets the system of the game being played, new or
  loaded, over the mode's and the save's.
- **Co-op:** the battle runs on the host's simulation (multiplayer §6), so the host's save decides the system for every
  partner in it. A guest's strip commands travel as before and the host's battle turns them into orders and stances;
  an order at a named target travels as the `Order` battle action, a stance as `Stance` (protocol 13). The battle's
  begin message says whether it plays Folder, so a guest's strip names the stances and the cancel like the host's.
- **Versus:** a versus match is not a story game and keeps Classic on both machines (it exchanges classic commands and
  compares the classic checksum).

## The strip

Folder keeps the battle screen's command strip, its icons and its skins: in a Folder battle every commanded fighter's
strip has every entry, whatever its brain, and each entry means something else.

| Strip entry | Folder |
|---|---|
| Technique 1–3, finisher | An order of that technique at the current target, which the order keeps even if Change moves on |
| Attack, Moderate, Distance, Defend | The Aggressive, Hold, Keep away and Protect stances; the current one is highlighted |
| Change | Turns to the next foe: the stance follows it and the next orders aim at it |
| Your call | Drops the waiting orders; the one being executed goes on |
| Run | The flee, as in Classic |

A plate at the bottom left (`battle.folder()` in the UI API) shows the partner's defense bar and its break, the foe
the next orders aim at (Change picks it), the active order's technique and phase with the timed phase's progress, and
the waiting orders (an order names its foe only when it is not the current target). Under them it tells the partner's
altered state, in the skins whose partner bars carry none, and, for five seconds, how the last order ended when it
did not finish: cancelled, interrupted, invalid or replaced, with the reason (not enough MP, its foe is gone, out of
reach...). The line under the strip says why the focused technique or the finisher cannot be ordered now; in the
modern skin, whose buttons already name the commands and whose move name shouts when it starts, that line shows only
the reason.

The plate keeps to its own region so it never covers another box: in the modern skin it sits between the partner's
card and the key hints, under the enemies' cards; in the original, pixel and DevVago skins it takes the bottom left and
moves up over a cup match's strip. In every skin it gives way while the bag is open, whose boxes reach its place. Each
line keeps to one row.

## Architecture

```text
packages/game/battle/src/
├── core/        Battle, BattleContext, the BattleSystem interface and its factory (battle_systems.cpp)
├── rules/       ruleset readers (rules_reader.cpp, folder_rules_reader.cpp), moves, boosted moves, co-op scaling
├── mechanics/   what both systems share: decisions, actions, attacks, movement, status, animation, effects, player,
│                strategies (the original AI, obedience and damage)
├── classic/     ClassicBattle: the command strip, the commanded partner and enemy AI, the original hit and block
└── folder/      FolderBattle: fighters, strip, orders (order book, checks, cycle), stances, defense, formulas
```

- `BattleSystemId` (`Classic`, `Folder`) is part of `BattleSetup` (`system`). `makeBattleSystem` (`core/battle_systems.cpp`)
  is the only place that knows both systems; nothing else in the battle names one, and `classic/` and `folder/` never
  include each other.
- `BattleSystem` (`core/battle_system.hpp`) is a strategy with the points where Folder differs: the start of the battle
  (`start`), a strip command (`command`; Run is shared and handled by `Battle::command`), the decisions of a frame
  (`tickDecisions`), how an attack that reaches a fighter resolves (`handleAttack`) and what runs at the end of the frame
  (`endFrame`). `ClassicBattle` forwards each one to the functions that ran there before (`tickPartnerAi`,
  `tickEnemyAi`, the original hit and block), in the same order and with the same rolls, so Classic is the same battle
  bit for bit.
- Folder's order cycle is a state machine (`folder/orders/order_cycle.cpp`): one handler per phase (idle, processing,
  positioning, preparing, recovering) over an `OrderTurn`; `order_book.cpp` queues and ends orders and `order_checks.cpp`
  says whether one can run.
- Folder keeps its own state in `BattleState::folder` (an optional that only a Folder battle has): each fighter's order
  queue, execution phase, stance, defense bar and break. Its numbers are the ruleset's `battle.folder` group
  (`content/packs/game/rules/original/battle.yaml`), so they can be balanced without code.
- Folder reuses `mechanics/`, never the original partner AI, obedience or damage formula.
- The tests mirror the tree (`tests/core`, `tests/rules`, `tests/mechanics/...`, `tests/classic`, `tests/folder`).

### Shared code touched

| Component | Change | Effect on Classic |
|---|---|---|
| `Battle::step`, `Battle::start`, `Battle::command` | Call the battle system instead of the free functions | None: Classic calls the same functions in the same order |
| `mechanics/attacks/attack_hits.cpp` | `hit()` split into the damage roll and `landHit()`; `handleAttack` asks the system first | None: Classic runs the original path (`classic/attack_resolution.cpp`) |
| `Fighter` | `moveScalePercent` (100) for VEL's movement multiplier | None: only Folder sets it, and 100 skips the scaling |
| `BattleState` | `folder` (optional) | None: empty in Classic; not part of the checksum then |
| `BattleRules` | optional `folder` group | None: Classic never reads it |

Vanilla is checked with the battle package's tests, the session's battle tests and the oracle compare
(`_work/oracle/tools/oracle.py`), which play Classic.

## Audit

The audit compares the current battle (until now the only one, which both variants played) with the design, mechanic
by mechanic. **Implemented**: the current battle does it as designed; **partial**: it has a version of it;
**missing**: nothing like it; **contradicts**: it does the opposite. The last column says where Folder stands.

| § | Mechanic | Current battle | Notes | Folder |
|---|---|---|---|---|
| 2.2 | Technique order | Partial | `Technique1..3` commands exist, but they are standing commands, and disobedience (discipline) can ignore them | Orders |
| 2.3 | AI never swaps the ordered technique | Contradicts | `YourCall`, `Attack` and `Moderate` let the partner pick; disobedience idles; confusion picks at random | Orders |
| 2.4 | Target selection | Partial | `Change` cycles targets; the partner retargets to whoever hits it (`BTL_retargetAfterHit`) | Orders |
| 2.8 | Queue of at most 2 orders | Missing | One buffered command, delayed by discipline | Orders |
| 2.8 | No MP: movement only | Partial | No usable move waits and charges (`waitWithoutMove`) | Orders |
| 2.8 | Friendly fire of area moves | Partial | Enemies may hit each other; partners never hit each other | Pending (see decisions) |
| 2.9 | Stances | Partial | `Distance` backs away from the target's longest reach; `Defend` stands and blocks; charge modes pace the waits | Stances |
| 2.9 | Guard: DEF ×2 | Missing | `Defend` blocks every hit with the speed reserve | Damage |
| 2.9 | Passive cover `DEF/1000 × 30 %` | Missing | Evasion is a speed roll, and a miss always blocks | Damage |
| 2.9 | Defense bar and break | Missing | — | Damage |
| 3.2 | Technique functions | Partial | Range (melee, ranged, area, self), buffs, status, counter, finisher | Technique data |
| 3.6 | Effects | Partial | Poison, confusion, stun, flatten, buffs; no slow, knockdown, shields, regeneration | Pending catalogue |
| 3.8 | Elemental 1-to-1 ±20 % and STAB | Contradicts | The original's 7×7 table in tenths, multiplied per defender specialty | Damage |
| 3.8 | Vaccine/Data/Virus ±25 % | Missing | The type exists in the data but the original's damage ignores it | Damage |
| 3.10 | Technique data | Partial | Power, MP, distance, range, status, accuracy, iframes; no execution or recovery time, area size, functions | Technique data |
| 4.2 | Order cycle | Missing | Commands set a standing behaviour | Execution cycle |
| 4.3 | Execution states | Partial | Internal flags (planned, charging, cooling down, attacking) but no states the player sees | Execution cycle |
| 4.5 | Interruption, cancellation, replacement | Partial | A hit cancels the technique until it commits (the original's commit point); poison never interrupts; nothing tells the player | Execution cycle |
| 4.6 | Invalid order resolves | Partial | Ranged attacks give up after a patience; no notice | Execution cycle |
| 6.2 | One speed attribute (VEL) | Implemented | The battle only has speed | — |
| 6.2 | DEF flat subtraction | Contradicts | `(offense − defense)` scales power, clamped | Damage |
| 6.4 | VEL movement multiplier | Missing | Clips move the fighter at the original's speed | Formulas |
| 6.4 | VEL execution multiplier | Missing | — | Formulas |
| 6.5 | INT processing multiplier | Partial | Brain decides menus, MP discount and buff uses; discipline decides the command delay | Formulas |
| 7.2 | Distance and range | Implemented | Squared world distances from the move table | — |
| 7.2 | Hitboxes, size | Implemented | Body radius from the species data, contact range | — |
| 7.2 | Obstacles, collisions | Implemented | Map and fighter collision, `collisionGrace` | — |
| 7.4 | Distance does not attenuate damage | Implemented | — | — |
| 7.4 | Block bonus by distance | Missing | Pending balance in the design | Pending |
| 7.5 | Effect areas in pixels | Partial | Areas are the effect scripts' checks; no data field | Pending catalogue |
| 8 | Co-op | Partial | Allies with own commands, items and results on the host's simulation; no order queues | Co-op |
| 9 | Items | Implemented | Thrown items: HP/MP recovery, status cure, protection, revival; one in flight per player | Shared |
| 10 | Interface | Partial | Command menu, HP/MP, enemy HP bars, finisher gauge; no queue, state, defense bar | UI |
| 11 | AI executes orders | Contradicts | The partner AI decides by command style and disobeys | AI |
| 12 | Metrics and scenarios | Partial | Battle simulator (`apps/sim`) and tests | Tests per phase |

Other findings:

- **Coupling.** The original partner AI, obedience and damage are strategies, but commands, charging, waits and blocking
  are wired into the frame loop. Folder therefore replaces the frame's decisions and the attack resolution as a whole
  instead of adding more strategies.
- **Dependencies on Vanilla.** Folder uses the original's walking, animations, effects, statuses and finisher. They are
  shared engine parts, not rules; a change to them must keep Classic's tests and the oracle green.
- **Redundancy.** Speed reserve, charge modes and command delay overlap with VEL and INT in the design. Folder does not
  use them.
- **Multiplayer.** The checksum and versus are Classic's. Folder's state lives in `BattleState::folder`, which the
  checksum includes when present.

## Decisions on open points

The design marks several points as pending. Folder takes these interpretations; each is a value of the ruleset or a
small function, so it can change without touching the rest.

| Point | Decision |
|---|---|
| Orders with no order | The partner does not attack on its own: with no order it only moves by its stance (§2.3, §2.8). |
| Queue full | A new order replaces the last queued one; the one being executed is never replaced (§2.8). |
| Target defeated or gone | The order is **cancelled** with a reason; the partner keeps its stance (§2.4). |
| Target of an order | Each order keeps the foe Change had picked when it was given, so two orders can split two foes. A partner that gets hit no longer turns to the attacker (the original's `BTL_retargetAfterHit`): it only changes target when told to or when its target is gone (§2.4, §4.3). Enemies keep turning. |
| Not enough MP | The order is **invalid** and leaves the queue; the partner keeps moving by its stance (§2.8, §4.6). |
| Interrupted by a hit | A hit while preparing or executing keeps the order: after the hit reaction it goes back to positioning, without processing it again; MP already paid is spent (§2.8 "retries it"). A status effect (stun, confusion, flattening) puts it back at the front of the queue instead. The design set no commit point, so Folder takes the original's (#425): once the technique's effect has thrown the part that hits (Spit Fire's fireball, about half its animation in), a hit on the user no longer stops it; it lands and the order counts as done, with its recovery after the hit reaction. A poison tick never interrupts. |
| Aggressive and range | Aggressive chases its target to point-blank for `folder.chase_frames`, then uses the technique from its reach, so a target that keeps away still gets hit (§2.9 leaves the tolerances open). The technique goes off when its preparation ends if the target is still in reach; a ranged one keeps the Classic band around its distance. |
| Technique timings | Each technique prepares and recovers by its MP cost in `folder.timings` tiers (the dearer the slower), the finishers by `folder.finisher_timing`, and `folder.techniques` gives one technique its own; VEL still cuts the preparation (§5, §6.4). Area, functions and friendly fire per technique wait for the catalogue. |
| Unreachable range | After `folder.give_up_frames` positioning without reaching range the order fails as unreachable (§4.6). |
| Attribute modifier (ATK) | `ATK × folder.attack_percent / 100`, provisional 200 %: at 100 % DEF outweighs ATK and a slightly stronger foe always wins. The balance probe in `folder/balance_probe_tests.cpp` (`-tc='*balance probe*' --no-skip`) plays 40 seeded duels per matchup in Classic and in Folder; at 200 % Folder's odds stay closest to Classic's and a duel lasts about a third as long. |
| Elemental base | The ±20 % steps and STAB are computed on the technique's power (§3.8 "base damage"). |
| Minimum damage | 1 for a hit, 0 for a covered hit (§2.9 "may reach 0" when covering). |
| Hit chance | Every attack that reaches a fighter lands; the defensive roll is cover. The original's evasion is not used. |
| Rest before draining | The defense bar starts draining `folder.bar_rest_frames` after the last hit. |
| Execution and processing base | `folder.preparation_frames` (VEL) and `folder.processing_frames` (INT). |
| Enemies | They keep the original's enemy AI (it chooses their techniques), but take Folder damage, cover and the bar. |
| Friendly fire | Not added yet: the current area techniques have no ally flag. Pending the technique catalogue (§2.8). |
| Stat range | Stats go up to 999; the formulas normalize them to 1000 (§6.3). |
| Confusion, stun, flatten | Status effects still override orders while they last: they are effects, not AI choices. |

## Plan

Each phase ships with tests, and with screenshots where it shows.

| Phase | Content | Status |
|---|---|---|
| 0 | Audit and plan (this page) | Done |
| 1 | Separation: `BattleSystem`, `ClassicBattle`, selection by mode and save, `--battle` | Done |
| 2 | Orders: queue of 2, technique and target, validation, replacement | Done |
| 3 | Execution cycle: processing, positioning, preparing, executing, recovering, interrupted, invalid, cancelled | Done |
| 4 | Stat formulas: VEL movement and execution, INT processing, centralized in `folder_formulas` | Done |
| 5 | Damage: elemental 1-to-1, STAB, type ±25 %, flat DEF, cover, Guard, defense bar, break | Done |
| 6 | Stances: Aggressive, Hold, Distance, Guard; break defaults to Hold | Done |
| 7 | AI: executes orders, never swaps technique or target; enemies keep the original AI | Done |
| 8 | Co-op: the guest's strip becomes orders on the host; `Order` and `Stance` battle actions; one queue per partner; the guest's strip shows the Folder labels | Done |
| 9 | Items: the shared thrown items, unchanged in Folder | Done (shared) |
| 10 | UI: the strip gives orders and stances, Change picks the target; the plate shows the queue, the phase, the defense bar and the break, the altered state and how an unfinished order ended and why; the strip says why a technique cannot be ordered; the plate keeps its own region in every skin | Done |
| 11 | Technique data: execution and recovery by MP tier with per-technique entries; area, functions and friendly fire per technique | Partial (timings done) |
| 12 | Balance: attribute scale (provisional, from the probe), block bonus by distance, costs (`costo-mp.md`) | Partial |
| 13 | Switching a Folder Mode game back to Classic from the game: the `battle_system` option | Done |
| 14 | Targets: Change picks the foe, each order keeps it, the partner never turns on its own, and the plate names the foe of each order | Done |

## Pending balance

- The attribute modifier's scale (`attack_percent`), tuned only against the test moves; real techniques may move it.
- The block chance bonus by distance (§7.4).
- The timing tiers and the finisher's timing (`folder.timings`, `folder.finisher_timing`), and per-technique entries.
- MP costs: the original's costs until `costo-mp.md` is applied.
- Co-op difficulty with Folder: the co-op scaling tables are Classic's.
