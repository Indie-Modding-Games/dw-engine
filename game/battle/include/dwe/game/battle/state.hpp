#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/items.hpp>
#include <dwe/game/battle/moves.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::game::battle {

// Fighters are numbered as in the original: the partner is 0 and the enemies 1 to 3.
using FighterId = std::size_t;
inline constexpr FighterId PartnerId = 0;

// The player's commands, numbered as the original's (battle_tick.c: handleCommands).
enum class Command : std::uint8_t {
  None = 0,
  Flee = 1,
  Attack = 2,
  YourCall = 3,
  Moderate = 4,
  Distance = 5,
  Defend = 6,
  Change = 7,
  Technique1 = 8,
  Technique2 = 9,
  Technique3 = 10,
  Finisher = 11,
};

// What a fighter's body is doing, which the UI turns into an animation and the rules read back: a
// standing fighter is harder to hit, a walking one keeps a wider distance, an attacking one is
// always hit (the original reads its animation ids for these).
enum class Pose : std::uint8_t {
  Standing,
  Walking,
  Attacking,
  Charging,
  HitReaction,
  Blocking,
  Stunned,
  Squashed,
  Disobeying,
  Fainting,
  Fainted,
};

// A unit vector scaled by FacingScale: where a fighter looks.
struct Facing {
  std::int32_t x = 0;
  std::int32_t z = 0;
};
inline constexpr std::int32_t FacingScale = 4096;

// The stats a fighter fights with: the combatant's, after the sickness penalty and with the buffs
// of the battle.
struct FighterStats {
  std::int32_t maxHp = 0;
  std::int32_t maxMp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
};

// The status effects on a fighter and their timers (battle_main.c: BTL_tickStatusEffects).
struct StatusState {
  bool poisoned = false;
  std::int32_t poisonFrames = 0;
  bool confused = false;
  std::int32_t confusionFrames = 0;
  bool stunned = false;
  std::int32_t stunFrames = 0;
  // Flattening: the timer is -1 from the hit until its reaction ends, then counts the squash, the
  // flattened time and the recovery; `flattened` is the middle part.
  bool flattened = false;
  std::int32_t flatFrames = 0;
  // A protection item keeps new effects off for the rest of the battle.
  bool protectedFromStatus = false;

  [[nodiscard]] bool any() const { return poisoned || confused || stunned || flattened; }
};

// An attack on its way: from the moment the attacker's animation releases it until it reaches a
// fighter.
struct AttackInFlight {
  FighterId attacker = 0;
  FighterId defender = 0;
  MoveRef move = 0;
  Point origin;
  std::int32_t framesLeft = 0;
};

// The attack a fighter is performing.
struct ActiveAttack {
  MoveRef move = 0;
  std::optional<FighterId> target;
  std::int32_t frame = 0;
  std::int32_t length = 0;
  std::int32_t releaseFrame = 0;
  bool released = false;
};

struct Fighter {
  Combatant combatant;
  // The equipped moves, resolved in the catalog.
  std::array<std::optional<MoveRef>, EquippedSlots> moves{};
  FighterStats stats;
  // The stats as they came in, before the sickness penalty and the buffs (for the stat gains).
  CombatStats startStats;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  // Damage and MP spending not applied yet: the HP drains a little each frame (utils.c:
  // damageTick).
  std::int32_t pendingDamage = 0;
  std::int32_t pendingMp = 0;

  Point position;
  Facing facing;
  Pose pose = Pose::Standing;
  std::int32_t poseFrames = 0;

  std::optional<FighterId> target;
  // It goes for whoever hit it last (FIGHTER_FLAG_10).
  bool chasingAttacker = false;
  ChargeMode chargeMode = ChargeMode::Normal;

  // The plan: a queued move and how far it reaches (-1 inactive, 0 none).
  bool planned = false;
  std::optional<MoveRef> queuedMove;
  std::int32_t moveRange = 0;
  std::int32_t outOfRangeFrames = 0;
  bool chargingUp = false;
  bool coolingDown = false;
  std::int32_t cooldown = 0;
  std::int32_t disobeyFrames = 0;
  bool disobeying = false;

  std::optional<ActiveAttack> attack;
  bool knockedBack = false;
  bool blocking = false;
  // A fighter that was just hit takes no other attack until its reaction ends.
  bool struck = false;
  std::int32_t invulnerableFrames = 0;
  // The attacks that reached it, one handled a frame.
  std::vector<AttackInFlight> incoming;

  StatusState status;

  std::int32_t speedReserve = 0;
  std::int32_t buffsLeft = 0;
  std::int32_t buffPriority = 0;
  std::int32_t finisherProgress = 0;
  std::int32_t finisherGoal = 0;

  bool fainted = false;
  // Frames since its HP bar last showed damage (battle_main.c: BTL_renderEnemyHPBars); it starts
  // hidden.
  std::int32_t hpBarFrames = 0;

  // Out of the fight: the damage on its way is enough to beat it (battle_main.c:
  // BTL_isFighterDefeated).
  [[nodiscard]] bool defeated() const { return hp - pendingDamage <= 0; }
  [[nodiscard]] bool hasFinisher() const { return moves[FinisherSlot].has_value(); }
};

// The partner's side of the menu and the commands (battle_setup.c, battle_tick.c).
struct CommandState {
  std::vector<Command> menu;
  Command buffered = Command::YourCall;
  Command current = Command::YourCall;
  std::int32_t delay = 0;
  bool changeTarget = false;
  // The two last targets the change command left (COMBAT_DATA player.unk5/unk7).
  std::array<std::optional<FighterId>, 2> changeHistory{};
  std::size_t changeTurn = 0;
  // The partner's own charge mode, which the commands other than attack and moderate bring back.
  ChargeMode savedChargeMode = ChargeMode::Normal;
};

// What the partner did in this battle, for the stat gains and the outcome (COMBAT_DATA player
// counters).
struct BattleCounters {
  std::int32_t attacks = 0;
  std::int32_t bigHits = 0;
  std::int32_t blocks = 0;
  std::int32_t statusesTaken = 0;
  // Some attack hit someone (HAS_TAKEN_DAMAGE): the injury roll after a victory needs it.
  bool anyoneHit = false;
  bool partnerHit = false;
};

// The finisher that freezes everyone while its aura plays (NO_AI_FLAG, FINISHING_ENTITY).
struct FinisherState {
  std::optional<FighterId> fighter;
  std::int32_t auraFrames = 0;
  std::int32_t charge = 0;
  bool charging = false;
};

// The item the tamer threw, on its way to the partner (main.c: tickThrownItem).
struct ThrownItemState {
  ThrownItem item;
  std::int32_t frames = 0;
};

struct BattleState {
  std::vector<Fighter> fighters;
  CommandState commands;
  BattleCounters counters;
  FinisherState finisher;
  // The enemy techniques seen, the ones the partner may learn (COMBAT_DATA player.usedMoves).
  std::vector<MoveRef> seenMoves;
  std::vector<AttackInFlight> inFlight;
  std::int64_t frame = 0;
  std::int32_t sharedCooldown = 0;
  // From the flee command on, the frames the partner has been running.
  std::optional<std::int32_t> fleeFrames;
  // After the partner faints, the frames left to revive it.
  std::int32_t deathFrames = 0;
  // The tamer's bag is open: the partner takes no command meanwhile (battle_tick.c:
  // handleCommands).
  bool bagOpen = false;
  std::optional<ThrownItemState> thrownItem;
  Point tamer;
  // The fighters stay within the arena radius of this point.
  Point arenaCenter;
  bool scripted = false;
  bool tamerless = false;

  [[nodiscard]] Fighter& partner() { return fighters.front(); }
  [[nodiscard]] const Fighter& partner() const { return fighters.front(); }
  [[nodiscard]] std::size_t enemyCount() const { return fighters.size() - 1; }
  // The living fighters other than `self` (battle_main.c: BTL_getRemainingEnemies): for the partner
  // the enemies, for an enemy the partner and the other enemies.
  [[nodiscard]] std::vector<FighterId> remainingOthers(FighterId self) const;
  // The enemies not yet defeated.
  [[nodiscard]] std::int32_t livingEnemies() const;
  [[nodiscard]] bool frozenByFinisher() const { return finisher.fighter.has_value(); }
};

// The squared distance between two points.
std::int64_t distanceSquared(Point a, Point b);

} // namespace dwe::game::battle
