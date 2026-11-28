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
  partner in it. A guest's strip commands travel as `Command` battle actions, as in Classic, and the host's Folder
  battle turns them into orders and stances (Change included, so a guest's orders keep the foe it picked). The protocol
  also has the `Order` (an order at a named target) and `Stance` actions (protocol 13), which the host applies, but the
  guest's client does not send them yet: its strip has no other way to name a target. The battle's begin message says
  whether it plays Folder, so a guest's strip names the stances and the cancel like the host's.
- **Versus:** a versus match is not a story game and keeps Classic on both machines (it exchanges classic commands and
  compares the classic checksum).

## The strip

Folder keeps the battle screen's command strip, its icons and its skins: in a Folder battle every commanded fighter's
strip has every entry, whatever its brain, and each entry means something else.

| Strip entry | Folder |
|---|---|
| Technique 1–3, finisher | An order of that technique at the current target, which the order keeps even if Change moves on |
| Attack, Moderate, Distance, Defend | The Aggressive, Hold, Distance and Guard stances; the current one is highlighted |
| Change | Turns to the next foe: the stance follows it and the next orders aim at it; the button names it ("Target: Goburimon A") |
| Your call | Clears the queue: drops the waiting orders; the one being executed goes on |
| Run | The flee, as in Classic |

The modern skin groups the strip in three blocks: the techniques with their MP cost, the four stances side by side as
one control, then the target, clearing the queue, the bag and fleeing. The skins with the original's icon strip
(`strip: icons`) keep its places and tint the stances' frames apart. A technique the partner has not the MP for is
greyed with its cost in red before it is pressed, and the reason shows under it. The focused technique's name also
says what it reaches besides its target (around itself, around the target, the whole field) and whether it hits allies.

Foes of the same species carry a letter (A, B...) on their card and over them in the world, and every foe's HP bar
stays on through the battle; the card of the foe the next orders aim at is lit, and one that a waiting order aims at
has an orange edge.

A plate at the bottom left (`battle.folder()` in the UI API) has four rows of a fixed height, so it never grows over
the fighters: the partner's defense bar beside its stance ("Hold · forced 3s" while a break forces it); one message,
the break first, else, for five seconds, how the last order ended when it did not finish, else the partner's altered
state; the active order's technique and foe beside its five phases (processing, positioning, preparing, executing,
recovering), with a lock at the start of executing that lights once the technique has committed, from where a hit no
longer stops it (or the foe the next orders aim at while none runs); and the waiting orders, one chip each ("1
Fire Breath > A"). An order ends with a fixed word: replaced (by which move), annulled (the player cleared it),
cancelled (its foe fell), invalid (not enough MP, out of reach...) or interrupted (a hit). The line under the strip says
why the focused technique or the finisher cannot be ordered now; in the modern skin, whose buttons already name the
commands and whose move name shouts when it starts, that line shows only the reason. The finisher gauge shows how full
it is, or that it is ready.

The plate keeps to its own region so it never covers another box: in the modern skin it sits between the partner's
card and the key hints, under the enemies' cards; in the original, pixel and DevVago skins it takes the bottom left and
moves up over a cup match's strip. In every skin it gives way while the bag is open, and the whole HUD goes when the
results open. The numbers, foes' bars and shouts over the fighters move out of the HUD's boxes.

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
  is the only place that builds a system, and `classic/` and `folder/` never include each other. The `Battle` API's
  Folder entry points (`order`, `cancelOrders`, `setStance`, `orderProblem`, `defenseBarCapacity`, `folderTechnique`, and
  `partnerMoveCost` in a Folder battle) call into `folder/` directly; the frame itself only reaches Folder through the
  `BattleSystem` interface.
- `BattleSystem` (`core/battle_system.hpp`) is a strategy with the points where Folder differs: the start of the battle
  (`start`), a strip command (`command`; Run is shared and handled by `Battle::command`), the decisions of a frame
  (`tickDecisions`), how an attack that reaches a fighter resolves (`handleAttack`), what runs at the end of the frame
  (`endFrame`), the MP a commanded fighter pays for a technique (`commandedMpCost`), whom a released attack reaches
  (`reached`) and whom its effect may touch on its way (`touches`). `ClassicBattle` forwards each one to the functions
  that ran there before (`tickPartnerAi`, `tickEnemyAi`, the original hit and block); the last three are the base
  class's defaults, which are the original's code moved behind the interface. Same order, same rolls: Classic is the
  same battle bit for bit.
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
| `mechanics/actions/fighter_actions.cpp` (`beginAttack`) | A commanded fighter's MP payment asks `BattleSystem::commandedMpCost` | None: the default is the original's `partnerMpCost` |
| `mechanics/attacks/attack_hits.cpp` (`releaseAttack`, bystanders) | Whom an attack reaches and touches asks `BattleSystem::reached` and `touches` | None: the defaults return `reachedBy` and `foes` as before |
| `Battle::partnerMoveCost` | In a Folder battle, the Folder cost | None: Classic keeps the original's |
| `core/checksum.cpp` | Hashes the whole Folder state (queues, active order and target, rest, hold, break length, movement scale) | None: only when `folder` is present |

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
| 2.8 | Friendly fire of area moves | Partial | Enemies may hit each other; partners never hit each other | Catalog |
| 2.9 | Stances | Partial | `Distance` backs away from the target's longest reach; `Defend` stands and blocks; charge modes pace the waits | Stances |
| 2.9 | Guard: DEF ×2 | Missing | `Defend` blocks every hit with the speed reserve | Damage |
| 2.9 | Passive cover `DEF/1000 × 30 %` | Missing | Evasion is a speed roll, and a miss always blocks | Damage |
| 2.9 | Defense bar and break | Missing | — | Damage |
| 3.2 | Technique functions | Partial | Range (melee, ranged, area, self), buffs, status, counter, finisher | Technique data |
| 3.6 | Effects | Partial | Poison, confusion, stun, flatten, buffs; no slow, knockdown, shields, regeneration | The original's effects; new ones wait for new techniques |
| 3.8 | Elemental 1-to-1 ±20 % and STAB | Contradicts | The original's 7×7 table in tenths, multiplied per defender specialty | Damage |
| 3.8 | Vaccine/Data/Virus ±25 % | Missing | The type exists in the data but the original's damage ignores it | Damage |
| 3.10 | Technique data | Partial | Power, MP, distance, range, status, accuracy, iframes; no execution or recovery time, area size, functions | Timings, catalog |
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
| 7.4 | Block bonus by distance | Missing | Pending balance in the design | Cover (provisional) |
| 7.5 | Effect areas in pixels | Partial | Areas are the effect scripts' checks; no data field | Catalog |
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
- **Multiplayer.** Versus is Classic's. Folder's state lives in `BattleState::folder`, which the checksum includes
  when present: every field that drives the simulation (each order's slot, target and number, the active order, the
  phase and its length, the bar, its rest, the break and its length, the status hold, the commit, how the last order
  ended) and each fighter's VEL movement scale.

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
| Interrupted by a hit | A hit while preparing or executing keeps the order: after the hit reaction it goes back to positioning, without processing it again; MP already paid is spent (§2.8 "retries it"). A status effect (stun, confusion, flattening) puts it back at the front of the queue instead. The design set no commit point, so Folder takes the original's (#425): once the technique's effect has thrown the part that hits (Spit Fire's fireball, about half its animation in), a hit on the user no longer stops it; it lands and the order counts as done, with its recovery after the hit reaction. A poison tick never interrupts. A hit during a finisher's aura cuts the finisher as in the original but gives its gauge back, so the order retries it instead of failing as "finisher not charged". |
| Aggressive and range | Aggressive chases its target to point-blank for `folder.chase_frames`, then uses the technique from its reach, so a target that keeps away still gets hit (§2.9 leaves the tolerances open). The technique goes off when its preparation ends if the target is still in reach; a ranged one keeps the Classic band around its distance. |
| Technique timings | Each technique prepares and recovers by its MP cost in `folder.timings` tiers (the dearer the slower), the finishers by `folder.finisher_timing`, and `folder.techniques` gives one technique its own; VEL still cuts the preparation (§5, §6.4). The tiers read the move table's cost, not the Folder cost, so the timings did not move when the costs did. |
| Unreachable range | After `folder.give_up_frames` positioning without reaching range the order fails as unreachable (§4.6). |
| Attribute modifier (ATK) | `ATK × folder.attack_percent / 100`, provisional 200 %: at 100 % DEF outweighs ATK and a slightly stronger foe always wins. The balance probe in `folder/balance_probe_tests.cpp` (`-tc='*balance probe*' --no-skip`) plays 40 seeded duels per matchup in Classic and in Folder; at 200 % Folder's odds stay closest to Classic's and a duel lasts about a third as long. |
| Elemental base | The ±20 % steps and STAB are computed on the technique's power (§3.8 "base damage"). |
| Minimum damage | 1 for a hit, 0 for a covered hit (§2.9 "may reach 0" when covering). |
| Hit chance | Every attack that reaches a fighter lands; the defensive roll is cover. The original's evasion is not used. |
| Rest before draining | The defense bar starts draining `folder.bar_rest_frames` after the last hit. |
| Execution and processing base | `folder.preparation_frames` (VEL) and `folder.processing_frames` (INT). |
| Enemies | They keep the original's enemy AI (it chooses their techniques), but take Folder damage, cover and the bar. |
| Friendly fire | An offensive area technique hits the user's side too; every other technique never does; support and self moves reach their user (§2.8). The catalog can turn it off for one technique ("not all area attacks"): which ones is a balance call, so no entry does yet. Partners' area techniques hit co-op allies; an enemy's hit the other enemies. An enemy caught by an ally's fire never turns on it. |
| Effect areas | An area move of the table is an area around its user as wide as its distance (the reach the original uses it from), so the target the partner positioned for is inside. The catalog can centre it on the target (`around_target`) or make it `global`. Everyone in the area is reached at once, measured to the edge of their body; the effect scripts no longer add bystanders to an area technique. Lines, cones and obstacles inside an area are not modelled (§7.5 leaves them per technique). |
| MP costs | The trainer's techniques cost what `costo-mp.md` v0.2 gives (`folder.mp_costs`), from the table's power, status and chance, boost, reach and area (the catalog section below). A boosted technique keeps the boost's discount; a finisher costs nothing (the gauge pays). Enemies keep the table's costs, because they keep the original's AI, whose choices and MP pools are tuned to them. |
| Reach classes | Melee is short; a ranged technique is short up to 200 px, medium up to 500, long beyond; an area around the user reaches as far as it spreads; support is personal. The original catalogue calls every ranged move "Long" (`tecnicas.md`), which would make Spit Fire cost 80; reading the distance keeps the cheap shots cheap. Provisional. |
| Effect costs | Poison 35, confusion 40, stun 50 (§4.1); flattening, which the design's table lacks, costs as a technique lock (60): a flattened fighter can only shoot the flattened shot. The table's statuses read as short, the boosts as long, since they last the battle. Provisional. |
| Boost costs | By its strongest raise (light up to 10 %, moderate up to 20 %, strong beyond) and whether it raises one stat or several, from §8.1's rows. The design's example 4 adds two single-stat costs instead; the table rows were taken, and the ambiguity is noted here. |
| Brain discount | Folder drops the original's brain MP discount: in the design INT only processes (§6.5), and a discount would give it a second job. A sick or injured partner still pays the original's surcharge: it is the partner's condition, not a stat. |
| Block bonus by distance | The cover chance gains `cover_near_bonus_percent` points at point-blank, falling in a straight line to `cover_far_bonus_percent` at `cover_far_pixels` (provisional 10 → 0 over 800 px), measured from where the attack was released. This follows the design's proposal (the closer, the easier to block); direction and size are data, so the balance can flip it without code. Guard still always covers. |
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
| 11 | Technique data: execution and recovery by MP tier with per-technique entries; area, functions and friendly fire per technique (the catalog) | Done |
| 12 | Balance: attribute scale (provisional, from the probe), block bonus by distance and costs (`costo-mp.md`) in, all provisional | Done (values pending balance) |
| 13 | Switching a Folder Mode game back to Classic from the game: the `battle_system` option | Done |
| 14 | Targets: Change picks the foe, each order keeps it, the partner never turns on its own, and the plate names the foe of each order | Done |
| 15 | Co-op difficulty in Folder: the shipped scaling applies unchanged, and how the +20 % alternative would apply (below) | Done (documented, tested) |

## Technique catalog

Folder plays each technique with a profile (`folder::Technique`, `folder::techniqueOf` in
`folder/formulas/technique_catalog.cpp`): its functions, area, friendly fire, reach class and MP cost. It is read from
the move table by the rules of the decisions above, and the ruleset's `folder.catalog` changes any part of it for one
technique by move id:

```yaml
catalog:
  shot: {area: around_target, area_pixels: 250, functions: [offensive, control], friendly_fire: false, mp_cost: 12}
```

The keys are `functions` (offensive, defensive, support, control), `area` (single, around_self, around_target,
global), `area_pixels`, `friendly_fire`, `effect_duration` (short, medium, long) and `mp_cost`. A new technique is a row
of the move table plus, only where the reading is wrong, a catalog entry: no code per technique.

The UI reads it through `Battle::folderTechnique(slot, side)`; `Battle::partnerMoveCost` already gives the Folder cost
in a Folder battle, so the strip's MP cost follows it. The original's 56 techniques read this way (enemies keep the
original MP):

| Specialty | Technique | Functions | Status | Reach | Area (px) | Friendly fire | Original MP | Folder MP |
|---|---|---|---|---|---|---|---|---|
| Fire | Fire Tower | off+ctl | Stun 8 % | medium | single | no | 81 | 81 |
| Fire | Prominence Beam | off+ctl | Flatten 3 % | long | single | no | 183 | 195 |
| Fire | Spit Fire | off | — | medium | single | no | 30 | 50 |
| Fire | Red Inferno | off | — | long | around self 600 | yes | 171 | 163 |
| Fire | Magma Bomb | off+ctl | Confusion 4 % | long | single | no | 132 | 146 |
| Fire | Heat Laser | off+ctl | Flatten 32 % | long | around self 600 | yes | 105 | 145 |
| Fire | Inifinity Burn | off+ctl | Stun 11 % | medium | around self 400 | yes | 264 | 222 |
| Fire | Meltdown | off+ctl | Stun 24 % | long | around self 600 | yes | 318 | 232 |
| Air | Thunder Justice | off+ctl | Stun 32 % | long | single | no | 330 | 252 |
| Air | Spinning Shot | off | — | long | single | no | 150 | 177 |
| Air | Electric Cloud | off+ctl | Stun 9 % | long | single | no | 69 | 101 |
| Air | Megalo Spark | off+ctl | Stun 23 % | medium | single | no | 174 | 157 |
| Air | Static Elect | off+ctl | Stun 15 % | short | single | no | 45 | 33 |
| Air | Wind Cutter | off | — | long | single | no | 93 | 114 |
| Air | Confused Storm | off+ctl | Confusion 60 % | medium | around self 400 | yes | 216 | 162 |
| Air | Hurricane | off+ctl | Confusion 13 % | medium | around self 400 | yes | 255 | 185 |
| Ice | Giga Freeze | off+ctl | Stun 10 % | short | single | no | 120 | 85 |
| Ice | Ice Statue | off+ctl | Stun 38 % | long | single | no | 186 | 207 |
| Ice | Winter Blast | off+ctl | Stun 15 % | long | around self 600 | yes | 165 | 144 |
| Ice | Ice Needle | off+ctl | Stun 5 % | long | single | no | 78 | 101 |
| Ice | Water Blit | off | — | long | single | no | 102 | 124 |
| Ice | Aqua Magic | sup | — | personal | single | no | 36 | 68 |
| Ice | Aurora Freeze | off+ctl | Flatten 24 % | long | around self 600 | yes | 258 | 244 |
| Ice | Tear Drop | off+ctl | Flatten 46 % | medium | single | no | 42 | 76 |
| Mech | Power Crane | off | — | long | single | no | 126 | 128 |
| Mech | All Range Beam | off | — | long | around self 600 | yes | 330 | 272 |
| Mech | Metal Sprinter | off | — | medium | around self 500 | yes | 165 | 115 |
| Mech | Pulse Laser | off | — | long | single | no | 168 | 177 |
| Mech | Delete Program | off+ctl | Flatten 50 % | long | single | no | 219 | 219 |
| Mech | DG Dimension | off | — | medium | around self 400 | yes | 420 | 287 |
| Mech | Full Potential | sup | — | personal | single | no | 99 | 150 |
| Mech | Reverse Prog | off+ctl | Flatten 12 % | long | single | no | 297 | 144 |
| Earth | Poison Powder | off+ctl | Poison 62 % | long | around self 600 | yes | 171 | 157 |
| Earth | Bug | off+ctl | Flatten 35 % | long | single | no | 354 | 231 |
| Earth | Mass Morph | sup | — | personal | single | no | 30 | 105 |
| Earth | Insect Plague | off+ctl | Poison 90 % | long | single | no | 96 | 109 |
| Earth | Charm Perfume | off+ctl | Confusion 57 % | long | around self 600 | yes | 210 | 177 |
| Earth | Poison Claw | off+ctl | Poison 40 % | short | single | no | 51 | 33 |
| Earth | Danger Sting | off+ctl | Flatten 26 % | short | single | no | 102 | 63 |
| Earth | Green Trap | off+ctl | Stun 55 % | long | single | no | 147 | 181 |
| Battle | Tremar | off | — | long | around self 600 | yes | 168 | 154 |
| Battle | Muscle Charge | sup | — | personal | single | no | 66 | 90 |
| Battle | War Cry | sup | — | personal | single | no | 42 | 68 |
| Battle | Sonic Jab | off | — | short | single | no | 18 | 16 |
| Battle | Dynamite Kick | off+ctl | Stun 3 % | short | single | no | 99 | 60 |
| Battle | Counter | off+ctl+def | Confusion 10 % | short | single | no | 165 | 90 |
| Battle | Megaton Punch | off+ctl | Stun 9 % | short | single | no | 186 | 101 |
| Battle | Buster Dive | off+ctl | Confusion 14 % | long | single | no | 258 | 216 |
| Filth | Odor Spray | off+ctl | Stun 10 % | short | single | no | 75 | 32 |
| Filth | Poop Spd Toss | off+ctl | Poison 11 % | long | single | no | 96 | 101 |
| Filth | Big Poop Toss | off+ctl | Confusion 17 % | medium | single | no | 192 | 101 |
| Filth | Big Rnd Toss | off+ctl | Confusion 20 % | medium | around self 400 | yes | 282 | 142 |
| Filth | Poop Rnd Toss | off+ctl | Poison 9 % | medium | around self 400 | yes | 120 | 96 |
| Filth | Rnd Spd Toss | off+ctl | Poison 10 % | medium | around self 500 | yes | 216 | 111 |
| Filth | Horizontal Kick | off | — | short | single | no | 24 | 16 |
| Filth | Ult Poop Hell | off+ctl | Flatten 83 % | long | around self 600 | yes | 333 | 250 |

In all, 8928 MP of the original become 7656: cheap single shots get dearer (Spit Fire 30 → 50, Tear Drop 42 → 76), the
dearest techniques cheaper (DG Dimension 420 → 287, Bug 354 → 231), and boosts dearer (Mass Morph 30 → 105). The costs
are costo-mp.md's v0.2 and pending its balance (§11 there).

## Co-op difficulty in Folder

The co-op scaling that shipped ([co-op difficulty](coop-difficulty.md) §5, §9) scales the enemies' max HP, MP, offense
and finisher goal when the battle starts, whatever the battle system, so a Folder battle gets the same enemies
(test "co-op difficulty scales a folder battle's enemies as it scales a classic one's"). In Folder the offense bump
weighs a little more than in Classic, because ATK enters the damage additively at `attack_percent` (200 %): +10 % ATK
is about +20 % of ATK's share of a hit. MP scaling feeds the enemies' original AI and costs, as in Classic.

What Folder adds to co-op is friendly fire: an offensive area technique of one partner hits the partners beside it. The
scaling tables do not count it; the risk is that more partners standing together hurt each other more, which the
stances (Distance, Hold) and the catalog's `friendly_fire: false` can answer.

The flat **+20 % per player** alternative (a proposal for coop-difficulty.md on `main`, not built) applies to Folder
like this, if it is chosen:

- **HP:** the same column (120/140/160 %), at battle start, as today.
- **Damage:** multiply the Folder damage of an enemy attacker (`damageOf` in `folder/defense/defense.cpp`) by the factor
  after DEF and cover and before the damage cap. The Folder formula subtracts DEF flatly, so scaling ATK instead would
  give far more than +20 % against weak defenders and less against strong ones; scaling the result keeps the rule a
  player can read. A covered hit that comes to 0 stays 0.
- **Defense bar:** it fills with the scaled damage, so with more players a partner's defense breaks sooner. That is the
  intended pressure; the bar's capacity (`6 × DEF`) is not scaled.
- **Join roll and contact reach:** field rules, the same for both systems.

Nothing of the alternative is built in Folder until that model is chosen.

## Pending balance

- The attribute modifier's scale (`attack_percent`), tuned only against the test moves; real techniques may move it.
- The block chance bonus by distance (§7.4): direction and size (`cover_near_bonus_percent`, `cover_far_bonus_percent`,
  `cover_far_pixels`).
- The timing tiers and the finisher's timing (`folder.timings`, `folder.finisher_timing`), and per-technique entries.
- MP costs: costo-mp.md v0.2's values (`folder.mp_costs`), the reach and area thresholds, flattening's cost and the
  boosts' duration.
- Which area techniques should not hit allies (`catalog: friendly_fire: false`), and areas centred on the target.
- Co-op difficulty with Folder: the shipped tables are tuned on Classic; the +20 % alternative's damage factor goes
  on the Folder damage result.

## Remaining gaps

- **Guard** follows the nearest ally when there is one and otherwise holds; it never steps between an ally and a foe
  (the design leaves the tolerances open, §2.9 "Pending").
- **The plate's target** follows the last executed order's foe: running an order points the partner at its foe, so after
  a split order the next strip order aims at that foe, not at the last one Change picked.
- **Give-up:** positioning gives an order up after `give_up_frames` without reaching range; a preparation that ends out
  of reach goes back to positioning with a fresh count, so a foe that keeps escaping is chased until the order is
  replaced or cancelled (the design's "retries it").
- **Order and Stance actions** exist in the protocol but the guest's client sends its strip as commands (see Co-op).
- **UI:** the skins do not draw a technique's area, functions or friendly fire yet; the API gives them.
