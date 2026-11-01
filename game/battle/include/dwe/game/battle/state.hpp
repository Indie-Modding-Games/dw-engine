#pragma once

#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/items.hpp>
#include <dwe/game/battle/motion.hpp>
#include <dwe/game/battle/moves.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

// The sprites a flattened fighter is drawn as instead of its model (model.c: renderFlatDigimon,
// entity->flatSprite): two walking frames that take turns, its shot and its hurt frame.
enum class FlatSprite : std::uint8_t { Walk = 0, WalkAlt = 1, Shot = 2, Hurt = 3 };

// How a fighter's flattening shows (battle_main.c: BTL_applyFlattenScale): the scale of its body
// across (x and z) and up, and the paper sprite it is drawn as, if any. The scale is 1 outside the
// squash at each end of the flattening.
struct FlatLook {
  float across = 1.0F;
  float up = 1.0F;
  std::optional<FlatSprite> sprite;
  // renderFlatDigimon's entity->flatTimer: the frames the walking sprite has shown, which turns it
  // each tenth.
  std::int32_t walkFrames = 0;
};

// The body scale BTL_applyFlattenScale sets for a flattening timer (BTL_applyStretchScale,
// BTL_applySquashScale): stretches and squashes that grow faster toward the middle, the body
// gone at 4 and growing back to its size from 3 to 0. Nothing outside 0 to 63, where the original
// leaves the scale as it was.
struct FlattenScale {
  float across = 1.0F;
  float up = 1.0F;
};
[[nodiscard]] std::optional<FlattenScale> flattenScale(std::int32_t flatFrames);

// The animation a fighter shows, by the original's animation id (anim.c: startAnimation; the same
// ids for every species, the moves' from FirstMoveAnimation on): what the view plays on its model.
// The battle picks it as the original starts it; nothing here draws.
struct ShownClip {
  std::int32_t clip = 0;
  // Grows each time the original starts the clip, even the same one again (startAnimation restarts
  // it).
  std::uint32_t starts = 0;
  // The clip stands on its frame (the original clears animFlag bit 0): a stun, a finisher's aura,
  // and a hit or a block while the invulnerability lasts.
  bool frozen = false;
  // The pose it was picked for.
  Pose pose = Pose::Standing;
  // The clip's frames played (anim.c: animFrame): its root motion and its end go by it.
  std::size_t frame = 0;
  // The flattening's scale and sprite.
  FlatLook flat;
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
  // An attack whose flight the move's effect script decides (AttackFlights): the attack frame it is
  // on (0 the frame it started) and the attacker's clip, to ask again each frame where both stand.
  std::optional<std::int32_t> age;
  std::optional<std::int32_t> clip;
  // Where the effect touches the defender (addAttackObject's position), which it turns to as it
  // is hit or blocks. Without one it turns to the attacker: where it stands as the hit lands for a
  // followed effect, its place at the release otherwise.
  std::optional<Point> hitPoint;
  // The attack frames on which its effect turns the attacker to the defender (EffectLanding).
  std::vector<std::int32_t> facingTarget;
};

// The attack a fighter is performing.
struct ActiveAttack {
  MoveRef move = 0;
  std::optional<FighterId> target;
  std::int32_t frame = 0;
  std::int32_t length = 0;
  std::int32_t releaseFrame = 0;
  bool released = false;
  // The battle follows the move's effect: the attack leaves as it starts and lands when the
  // effect's hit checks touch the defender, whatever the move's reach.
  bool followed = false;
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
  // The clips of its model (motion.hpp); none for a fighter the battle has no model for, which then
  // walks at the ruleset's walk speed and keeps the ruleset's animation lengths.
  std::shared_ptr<const FighterClips> clips;
  // The root motion's fraction of a unit, in 1 / FacingScale (anim.c: anim.locX, locZ, which keep
  // 15 bits below the location): x's from 0 up, z's from 0 down (world z runs against the
  // original's, where the location rounds down).
  Point rootRemainder;
  // This frame's root step is held (anim.c: animFlag bit 1): the step ahead runs into something
  // (battle_main.c: BTL_tickFighterAction) or every way is blocked (map.c: collisionGrace).
  bool rootHeld = false;
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
  // A step backing away hit something: it turns gradually now (battle_main.c:
  // hasCollidedWhileDistanceCmd).
  bool collidedWhileBacking = false;
  bool chargingUp = false;
  bool coolingDown = false;
  std::int32_t cooldown = 0;
  std::int32_t disobeyFrames = 0;
  bool disobeying = false;

  std::optional<ActiveAttack> attack;
  bool knockedBack = false;
  // The last hit came from behind: it turned its back to the attack (battle_main.c:
  // BTL_handleHitReaction).
  bool hitFromBehind = false;
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
  ShownClip shown;

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
  // The slot of its aura in the status visuals (BTL_FINISHER_AURA_ID), which a lost finisher
  // removes; it is not cleared when the aura ends by itself.
  std::optional<std::size_t> auraSlot;
  std::int32_t charge = 0;
  bool charging = false;
};

// The item the tamer threw, on its way to the partner (main.c: tickThrownItem).
struct ThrownItemState {
  ThrownItem item;
  std::int32_t frames = 0;
};

// A tournament match's clock (std_main.c: STD_addFighterCounter, STD_tickFighterCounter): the
// seconds left, the frames counted (BATTLE_FRAME_COUNT, from 1) and the HP each side started with
// (STD_STARTING_HP), which the time-up judgement compares (STD_checkEndCondition).
struct MatchClock {
  std::int32_t seconds = 0;
  std::int64_t frames = 1;
  std::int32_t partnerStartHp = 0;
  std::int32_t enemyStartHp = 0;
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
  // Only in a tournament match.
  std::optional<MatchClock> matchClock;

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

// The clip a fighter stands with (btl.c: startBattleIdleAnimation): weak while poisoned or at a
// fifth of its HP or less.
std::int32_t stanceClipOf(const Fighter& fighter);

// The squared distance between two points.
std::int64_t distanceSquared(Point a, Point b);

} // namespace dwe::game::battle
