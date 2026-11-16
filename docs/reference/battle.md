# Reference sheet: battle

How combat worked in the original game, according to the decomp. It is step 1 of the
[design cycle](../conventions/design-cycle.md): it describes the original, not what the mod will do.

Marks: **(verified)** means it was read in the decomp's code; **(assumption)** means it is our own interpretation
that still needs to be confirmed. Times are given in battle frames: the battle loop advances once per frame.

## What it does

Combat is in real time. The partner fights on its own, with its own AI, and the player gives it general orders
("Attack", "Distance", "Defensive") or asks it for specific techniques. How many orders it understands depends on its
Brains, and whether it obeys depends on its discipline. Each technique spends MP and accumulated "speed"; damage
depends on offense, defense and the affinity between specialties. There are status effects and a finishing attack
that charges up over time. When it ends, the partner gains stats, money, items and maybe new techniques, but it gets
tired and can be injured.

## Rules

### Loop and result

- `BTL_battleMain` sets up the battle and, every frame, runs the partner's AI, the enemies' AI and each fighter's
  status effects, until the battle ends. It returns 1 if won, 0 if fled and −1 if the partner falls **(verified)**.
- There are at most 3 enemies **(verified)**.
- **Damage is applied gradually.** Each hit is added to a pool of pending damage (cap 9999) that is subtracted from
  life at a rate that depends on its size (`damageTick`). A fighter counts as defeated when its life minus the
  pending damage reaches 0 (`BTL_isFighterDefeated`) **(verified)**.
- The partner's MP is also deducted through a pool; the enemies' is deducted immediately **(verified)**.

### Stats and their role

| Stat | In battle |
|---|---|
| HP / MP | Pools of life and of energy for techniques. At 20% HP or less the fighter uses the hurt animation. |
| Offense / defense | They go into the damage formula. Defense also scores which enemy to attack. |
| Speed | Fills the speed pool, raises dodging, shortens the finisher and gives counterattack chance. |
| Brains | Number of orders available, disobedience, aim when choosing a target, use of support and area techniques, and MP discount. |

All **(verified)** in `BTL_initializeCombat`, `BTL_calculateDamage`, `BTL_calculateHitChance` and the AI.

- **Sick or injured partner:** at the start of combat it loses 20% of offense, defense, speed and Brains, and its
  techniques cost 50% more MP (`BTL_initializeCombat`, `BTL_isMoveUsable`) **(verified)**.
- Temporary changes are undone at the end from a copy of the initial stats (`resetStatsAfterCombat`,
  `BTL_deinitializeCombat`) **(verified)**.

### Player orders

The orders are: Run, Attack, Your Call (the default), Moderate, Distance, Defensive, Change target, use one of the
three equipped techniques and finisher (`command_shout.c`) **(verified)**.

The menu grows with Brains (`BTL_initializeCombat`) **(verified)**:

| Brains | Visible orders |
|---|---|
| < 100 | Run, Your Call |
| 100–199 | + Attack |
| 200–299 | + Moderate |
| 300–399 | + Distance and Change |
| 400–499 | + Defensive |
| ≥ 500 | Run, Change, Defensive, Distance, one for each equipped technique (from the third slot to the first), Your Call. **Attack and Moderate disappear.** |

In scripted battles the Run order does not appear in the menu (`BTL_initializeCombat`) **(verified)**.

What each one does (`BTL_tickPartnerAI`, `BTL_handlePartnerMoveCommand`) **(verified)**:

- **Attack:** aggressive charge mode and picks the most powerful technique (on a tie, the one with the best affinity;
  then at random).
- **Moderate:** cautious charge mode and picks the cheapest technique in MP.
- **Your Call:** goes back to the partner's own charge mode and lets the AI choose (see below).
- **Distance:** it moves away until it is out of range of the target's longest technique, with a margin. It does not
  attack.
- **Defensive:** it stays still facing the target and relies on blocking.
- **Change:** with two enemies it alternates the target; with three, it rotates.
- **Specific technique:** it queues it if it can be used (enough MP and the other conditions of `BTL_isMoveUsable`).

**Order delay disabled:** `handleCommands` computes a delay based on discipline and immediately sets it to 0, so
orders are immediate **(verified)**.

### Obedience

- **Disobedience:** with Brains below 301, every (Brains / 2 + 1) × 20 frames the partner has a (70 − discipline)%
  chance of standing still for 100 frames. Receiving a hit takes it out of that state (`BTL_tickPartnerAI`,
  `BTL_tickFighterAction`) **(verified)**. For enemies the chance is (300 − Brains) / 4% **(verified)**.
- **Changing target when hit:** the partner turns toward whoever hit it with a chance that depends on the charge mode
  (70, 40, 30 or 0%) minus discipline; it always does so if its target has already fallen (`BTL_retargetAfterHit`)
  **(verified)**.

### Partner AI

**Target choice** (`BTL_selectFighterTarget`, `BTL_calculateTargetScore`) **(verified)**:

- Brains ≥ 300: it prefers an enemy with a status effect. If there is none, the one with the lowest score, and it
  only switches if the current one scores at least 40 more.
- Brains ≥ 150: the one with the lowest score.
- Less: the closest.
- Score = % of HP remaining + (inverted affinity × relative defense) / 10. A weak and vulnerable enemy scores low...
  in theory: `BTL_calculateTargetScore` uses the life and defense **of the fighter itself**, not the candidate's, so
  only affinity tells the enemies apart (see oddities) **(verified)**.
- `BTL_getWeakestEnemy` returns the score of the last enemy it looked at, not that of the chosen one, and that is what
  decides whether to switch target (see oddities) **(verified)**.

**"Your Call" technique choice** (`BTL_selectPartnerMove`) **(verified)**. Weighted draw among the three equipped
techniques (the fourth slot is the finisher):

- Base weight by charge mode: aggressive, by order of power (50, 20, 5); normal, 20 each; cautious, by order of MP
  cost (50, 20, 10).
- Affinity bonus against the target: from +10 (very effective) to +1 (very ineffective).
- Support techniques: + a priority counter that starts from Brains (Brains / 10 + 5) and drops over time.
- Area techniques, with Brains ≥ 200 and at least two enemies alive: + 10 or 15 per enemy depending on the mode.
- In cautious mode with less than 31% HP, the technique's range weighs in.
- An empty slot weighs 5; if it comes up, the partner charges for 80 frames instead of attacking.

**Enemy AI** (`BTL_selectEnemyMove`, `BTL_tickEnemyAI`) **(verified)**: each enemy has a priority per technique in its
map data, with extras for area and support techniques, and it launches its finisher as soon as its bar is full. Its
charge mode is random, or cautious if it is off screen.

### Techniques

- **MP cost:** the stored value × 3 (`BTL_isMoveUsable`, `BTL_setupMoveExecution`) **(verified)**.
  - Partner discount by Brains: 700–799 −5%, 800–899 −10%, 900–998 −15%, 999 −20%.
  - Sick or injured partner: × 1.5.
  - Finishers do not spend MP.
- **Power is also a speed cost:** using a technique subtracts its power from the speed pool, which does not go below
  −155 **(verified)**.
- **Range:** 1 melee, 2 ranged, 4 support on itself (`range`) **(verified)**; 3 is used as an area technique
  **(assumption, from how the AI treats it)**.
- **Specialties:** each technique has a specialty (0–6) and each Digimon up to three. The affinity between them comes
  from a 7 × 7 table whose values 20, 15, 10, 5 and 2 mean ×2, ×1.5, ×1, ×0.5 and ×0.2 (`TYPE_FACTORS`)
  **(verified)**.
- **Index blocks:** techniques below 0x3A can be learned; 0x2D is the counterattack; 0x3A to 0x70 are finishers; 0x79
  is the projectile used when flattened **(verified)**.
- **Support techniques:** five specific techniques raise offense, defense or speed between 5 and 30% during the
  battle, capped at 999 (`BTL_applyBuffMove`) **(verified)**.

### Speed and pacing

- The speed pool starts at 100 and gains speed / 100 + 1 every 2 frames, capped at 100 (`BTL_increaseSpeedBuffer`)
  **(verified)**.
- Charge modes: aggressive, acts as soon as the pool is positive; normal, waits until it has the technique's power
  (or 100); cautious, waits for 100 while backing away (`BTL_applyChargeRequirement`, `BTL_tickChargeState`)
  **(verified)**.
- Pauses: 40 frames after each attack and 80 if it cannot afford any technique **(verified)**.
- Ranged techniques look for their range distance; if they do not reach it in 80 frames, they fire anyway
  (`BTL_tickRangedAttack`) **(verified)**.
- Each step is tested before taking it (`entityCheckCollision`, which `BTL_tickFighterAction` calls every frame): it
  does not leave the combat area (a rectangle of 280 x 200 pixels around the center of the screen, tested with two
  corners of the fighter's box, one on the ground and another 200 units up), does not enter a map cell with bit 0x80
  and does not collide with the edge of another living fighter; if something prevents it, the fighter does not advance
  that frame **(verified)**.
- On a collision, whoever is walking toward a point keeps its orientation and turns at most 0x50 toward the first free
  exit (`collisionGrace`); whoever would leave the area looks for a free fan of three eighths of a turn
  (`BTL_findUnblockedRotation`); whoever backs away turns its back to the rival and, after a collision, turns in steps
  of 20 (`BTL_backAwayFromTarget`). The melee attack approaches without that test (`BTL_tickMeleeAttack`)
  **(verified)**.

### Damage

`BTL_calculateDamage` **(verified)**. Let **A** be the sum of the technique's three affinities against the defender's
specialties (10 if the slot is empty), so A / 30 is the average multiplier.

- **Normal technique:**
  - diff = offense − defense, limited to ±500.
  - damage = A × (power + diff × power / 500) / 30 × random(90..110) / 100.
  - The counterattack uses only 30% of the defense.
- **Finisher:**
  - damage = (offense + power) × A / 30, without defense.
  - Partner: × charge / 40 if the charge goes past 40 (up to × 2).
  - Enemy: × random(100..200) / 100.
  - Then, × random(90..110) / 100.
- The result stays between 1 and 9999.

### Hit chance, block and counterattack

`BTL_calculateHitChance` **(verified)**:

- **Always hits** if the defender is a Fresh or In-Training Digimon; if it is stunned, flattened or disobeying; if the
  partner is sick (applies to both sides); if the defender's speed pool is 0 or less; if the defender is in the middle
  of a technique; or if it is a finisher (which also breaks the block).
- **Always misses** if the defender is already blocking.
- **Otherwise:**
  - x = accuracy / 2 × (defender's speed − attacker's speed / 10) / 999.
  - x × 1.2 if the defender is standing still.
  - hit chance = accuracy − x, between 0 and 100%.

On a miss (`BTL_tickAttackHits`) **(verified)**:

- Against a melee attack, the defender can counterattack if it has the counterattack technique and at least 165 MP,
  with chance speed / level %.
- Otherwise, it blocks: 3 frames of invulnerability, and a blocked melee attack still does between 10 and 30% of the
  damage.

On a hit, the defender receives the technique's invulnerability frames and the status roll **(verified)**.

### Status effects

They are applied on a hit, with the technique's status chance, unless the defender is protected
(`BTL_applyMoveStatus`). They advance in `BTL_tickStatusEffects`. All **(verified)**:

| Status | Effect | Duration |
|---|---|---|
| Poison | Every 100 frames it removes between 1 and 3% of max life | Until cured or the battle ends |
| Confusion | 70% chance to attack the air on each choice; otherwise, a random fighter. It turns uncontrollably | 200–300 frames |
| Stun | It cannot act | 200–240 frames |
| Flat | After 65 frames of animation, it can only throw a projectile every 40 frames and everything hits it | 224–304 frames |

- Only one icon is shown at a time: stun before confusion and confusion before poison.
- Some items cure statuses in combat (`BTL_healStatusEffect`).
- That confusion can make enemies hit each other is an **(assumption)**.
- The confusion of any fighter, even if its time runs out, is only removed on a frame when the partner is not
  attacking (see oddities) **(verified)**.

### Finisher and its bar

**(verified)**:

- Bar goal = 3000 − speed (`BTL_initializeCombat`).
- Progress: +1 per frame; + goal × 2 / 50 for each completed attack; + goal × 3 / 50 for each block; whoever sees its
  attack blocked gains + goal × 4 / 50 (`BTL_addFinisherProgress`, `BTL_tickFighterStates`, `BTL_tickAttackHits`).
- It only fills if the fourth slot has a finisher.
- With the bar full, a button launches the finisher order (`handleCommands`). The rest of the AI stops and an aura of
  80 frames begins. During that time, each press of other buttons adds 2 to the charge, up to 80; the charge
  multiplies the damage (`BTL_startQueuedMove`, `BTL_tickFinisherChargeup`). Which exact buttons they are is an
  **(assumption)**.
- When it is launched, progress goes back to 0.

### End of combat and consequences

- **Victory:** all the enemies have finished their death animation and the partner is still alive
  (`BTL_isBattleFinished`) **(verified)**.
- **Fleeing:** disabled in scripted battles. The tamer backs away and the battle ends if the partner is still alive.
  Happiness −10, discipline −6, tiredness +2, plus the post-battle tiredness. It counts in a flee counter
  (`startBattle`) **(verified)**.
- **Defeat:** if the partner reaches 0 HP, there is a countdown during which a revive item can still save it. On
  reviving, all the fighters are reset, not just the partner: no cooldown, no planned technique, no chase, charge or
  cooldown, and the finisher is canceled (`BTL_isBattleFinished`) **(verified)**. The countdown stops while an item
  thrown by the tamer is flying (`BTL_isBattleFinished`) **(verified)**. Happiness −30, discipline −20
  (`startBattle`). Afterwards, 30% of the occupied bag slots are lost, rounding down and each with its whole stack
  (`handleItemLoss`, see the [inventory sheet](inventory.md)), and one life; with 0 lives, the remaining life goes to
  0 and the partner dies **(verified)**. See the [care sheet](care.md).
- **Items in combat:** the tamer opens the bag with its own button, not from the orders menu (`battle_tick.c:
  tamerTickBattle` → `inventory.c: addInventoryUI`); it does not open while fleeing, in battles without a tamer or
  with another item in the air. While it is open, the orders menu waits (`handleCommands` returns) but the battle
  continues (`BTL_battleMain`). Only items with `item_color` 1 or 2 are thrown (`inventory.c: selectOptionMenu`). The
  item is spent when thrown (`main.c: startThrowingItem`), leaves the hand on frame 7 of the tamer's animation, flies
  24 frames toward the partner and always arrives; the partner does not refuse it, one goes at a time and the bag
  closes (`main.c: tickThrownItem`) **(verified)**. At 0 HP only the revive ones do anything (`item.c:
  handleRestore`). Disks raise offense, defense or speed (+20 or +50), without going past 130% of its value at the
  start of combat; the Omni Disk has no such limit (`main.c: handleBuffDisks`) **(verified)**. That a frame of the
  tamer's animation lasts one combat frame is an **(assumption)**.
- **HP bars:** the partner's HUD shows its HP and MP bars with the current value (`battle_hud.c`). For enemies only an
  HP bar with its value is seen, next to it, when taking damage, and it hides 21 frames after the knockback ends; there
  is no name or MP (`battle_main.c: BTL_renderEnemyHPBars`) **(verified)**.
- **Clock:** game time and care stop during the whole battle (`tamer.c: startBattle` calls `stopGameTime`; `clock.c:
  tickGameClock` does not advance outside the field), and at the end 20 minutes are added (`advanceBattleTime`)
  **(verified)**.
- **HP and MP after losing:** no combat code touches them; the partner goes to the defeat map (`murd.c: MURD_tick`, map
  205 or 218) and that map's script (script 212, section 0x48) adds 9999 to the current HP and MP, which
  `script_value.c: enforceStatsLimits` leaves at the maximum: it comes back with everything **(verified in the
  disassembled script)**.
- **Victory, care:** happiness +2, battles +1 (counts toward [evolution](evolution.md)) **(verified)**.
- **Tiredness and hunger after winning or fleeing** (`handlePostBattleTiredness`): tiredness +5, plus an extra if MP
  ended below 80% (see oddities), and food drops 15 **(verified)**.
- **Time:** each battle advances the game clock 20 minutes (`advanceBattleTime`) **(verified)**.
- **Injury:** if anyone took a hit in the battle (not only the partner: `handleBattleEndBox` looks at
  `HAS_TAKEN_DAMAGE`, which marks any hit) and the partner was not injured, chance = tiredness − % of HP remaining
  (`handleBattleInjury`) **(verified)**.

### Rewards

`battleStatsGainsAndDrops`, `handleBattleEndBox` **(verified)**:

- **Money:** the sum of each enemy's money, capped at 999,999 in the wallet.
- **Items:** each enemy drops its item with its chance (except on one specific screen).
- **Stats:** for each stat, the highest value among the enemies (E) and the partner's (P) is taken, with a factor F
  that grows with the number of enemies (10, 12 or 16):
  - If E ≥ P: gain = E × F / (P × 10) rounded up.
  - If E < P: chance E × F × 100 / (P × 10)% of gaining 1.
  - If a stat gained nothing, there is another +1 roll depending on how the battle went: HP, % of life lost; MP and
    offense, 10 × attacks made; defense, 10 × hits received of at least 20% of life; speed, 10 × blocks + half the %
    of life lost; Brains, 5 × attacks + 5 × strong hits.
  - Gains are added as is. Caps: HP and MP 9999, the rest 999.

### Learning techniques in battle

`battleMoveLearning`, `BTL_setupMoveExecution` **(verified)**:

- During combat, up to 12 different learnable techniques used by the enemies are noted.
- After winning, each one enters a draw if the partner does not know it, if it is one of its specialties and if it is
  in its list of 16 possible techniques.
- Each candidate rolls its chance in `MOVE_LEARN_CHANCES` (per technique and per specialty slot). Of those that
  succeed, one is learned at random.
- No Brains is required. Two pairs of techniques are learned together (`learnMove`).

### Enemies that join

At the start of a battle, other visible enemies join with a chance that depends on the partner's type and the enemy's
(`FLEE_CHANCES`, 70% if the partner has no type). Two items raise or lower that chance (`handleBattleStart`)
**(verified)**.

## Data

Tables in the main executable. `dwx` extracts them to YAML from the user's disc; here only their shape is
described.

| Table | Entry | Entries | Fields |
|---|---|---|---|
| `MOVE_DATA` | 16 bytes | 122 | Range distance (squared), power, MP cost (÷ 3), invulnerability frames it causes, range (1–4), specialty, status it causes (1 poison, 2 confusion, 3 stun, 4 flat), accuracy, status chance, flags (bit 0: needs a target; bit 1: shared block of 110 frames) and two unidentified fields. |
| `MOVE_NAMES` | — | — | Names of the techniques. |
| `MOVE_LEARN_CHANCES` | 3 bytes | 58 | Chance (%) of learning each learnable technique according to the partner's specialty slot. |
| `TYPE_FACTORS` | 7 bytes | 7 | Affinity of each attacking specialty against each defending specialty, in tenths. |
| `FLEE_CHANCES` | — | 3 × 3 | Chance that an enemy joins the battle according to the types. |
| `STAT_GAIN_ENEMY_FACTORS` | — | 3 | Stat gain factor according to the number of enemies. |
| `DIGIMON_DATA` | 52 bytes | 180 | See the [evolution sheet](evolution.md). In battle the type, level, specialties, the item it drops and its chance, and the list of techniques are used. |

The combat stats of each enemy are not in a global table: they come in each map's data (life, MP, offense, defense,
speed, Brains, money, charge mode, four techniques with their priority and flee data), read in `loadMapDigimon`
(`overworld.c`) **(verified)**. The structures are in `include/dw/move.h`, `combat.h`, `entity.h` and `params.h`.

## Oddities

All **(verified)** except where indicated:

- **Broken MP tiredness.** In `handlePostBattleTiredness` the extra for MP spent divides before multiplying, so it
  almost always adds 0. The decomp itself notes it.
- **Orders without delay.** The discipline delay is computed and discarded.
- **Sickness = guaranteed hit.** With the partner sick, all the attacks in the battle hit, its own too.
- **Attack and Moderate disappear** with Brains 500 or more, when it would seem the menu only grows.
- **Counterattack with MP.** Although the counterattack does not spend MP, it requires having at least 165.
- **Loop errors** in `BTL_initializeCombat` (swapped index) and `BTL_setupMoveExecution` (a condition that is always
  met). In the low-life branch of `BTL_selectPartnerMove` the inner loop reuses the outer index **(assumption: it
  looks like a bug)**.
- In scripted battles without fleeing, enemies do not spend MP.
- **Target score with own stats.** `BTL_calculateTargetScore` uses the life and defense of the one choosing.
- **The weakest reports wrongly.** `BTL_getWeakestEnemy` returns the score of the last enemy looked at.
- **Confusion waits for the partner.** No confusion is removed while the partner attacks.
- **Injury from any hit.** The injury roll after winning is made if any fighter took a hit.
- **No time limit.** Two fighters without MP for any technique wait forever; the original has no maximum combat time.
- The tournament (`src/std/`) and versus mode (`src/vs/`) have nearly identical copies of this logic; they have not
  been compared in detail **(assumption: they may differ)**.

## Where it is in the decomp

| What | Where |
|---|---|
| Loop, AI, damage, hit chance, statuses, finisher | `src/btl/battle_main.c`: `BTL_battleMain`, `BTL_tickPartnerAI`, `BTL_tickEnemyAI`, `BTL_selectPartnerMove`, `BTL_selectEnemyMove`, `BTL_selectFighterTarget`, `BTL_calculateDamage`, `BTL_calculateHitChance`, `BTL_tickAttackHits`, `BTL_applyMoveStatus`, `BTL_tickStatusEffects`, `BTL_addFinisherProgress`, `BTL_applyBuffMove` |
| Setup, menu by Brains, end | `src/btl/battle_setup.c`: `BTL_initializeCombat`, `BTL_isBattleFinished` |
| Orders and finisher charge | `src/btl/command_shout.c`, `src/btl/command_menu.c`: `BTL_tickFinisherChargeup`; `src/main/battle_tick.c`: `handleCommands` |
| Rewards, gains, injury, learning | `src/main/battle_ui.c`: `battleStatsGainsAndDrops`, `handleBattleEndBox`, `handleBattleInjury`, `battleMoveLearning`, `resetStatsAfterCombat` |
| Care consequences | `src/main/tamer.c`: `startBattle`, `advanceBattleTime`; `src/main/partner_impl.c`: `handlePostBattleTiredness` |
| Start and enemies that join | `src/main/btl.c`: `handleBattleStart` |
| Gradual damage | `src/main/utils.c`: `damageTick` |
| Enemies per map | `src/main/overworld.c`: `loadMapDigimon` |
