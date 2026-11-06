#pragma once

#include <dwe/game/battle/state.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

namespace dwe::game::battle {

// What the original shows over a fighter with a status (battle_effect.c): poison bubbles, the
// confusion's spinning objects, and the stun's countdown sign with the rings that fall from it.
// Each fighter shows one at a time (FighterData.statusFxId), stun over confusion over poison
// (battle_main.c: BTL_addStatusEffectVisual and the add/remove calls around it). The bubbles and
// the confusion roll rand() as they start, so with the original's rolls (Battle::useOriginalRolls)
// they draw from the battle's generator, in step with the original; otherwise from their own.
class StatusVisuals {
public:
  // BTL_addStatusEffectVisual's kinds.
  enum class Kind : std::uint8_t { Poison = 1, Confusion = 2, Stun = 3 };
  // BTL_tickStunEffect's states: counting down, flashing, shrinking away.
  enum class StunStage : std::uint8_t { Countdown = 0, Flash = 1, Shrink = 2 };

  // The original's pools (BTL_POISON_EFFECTS, BTL_POISON_BUBBLES, BTL_CONFUSION_EFFECTS,
  // BTL_STUN_EFFECTS, BTL_STUN_SUB_EFFECTS). Every battle starts with all of them free, frame -1
  // (btl.c: loadBattleData: BTL_initializePoisonBubble, which runs BTL_resetPoisonBubbles;
  // BTL_initializeConfusionEffect; BTL_initializeStunEffect, which runs BTL_resetStunSubEffects):
  // each Battle has its own, empty as it starts, and clear() frees them as the battle ends.
  static constexpr std::size_t PoisonSlots = 4;
  static constexpr std::size_t BubbleSlots = 12;
  static constexpr std::size_t ConfusionSlots = 4;
  static constexpr std::size_t StunSlots = 5;
  static constexpr std::size_t StunRingSlots = 25;

  // A bubble rising from a poisoned fighter for BubbleFrames frames, at a random offset of up to
  // 50 units on x and z (BTL_addPoisonBubble: rand() % 100 - 50 each).
  struct Bubble {
    FighterId fighter = 0;
    std::int32_t frame = 0;
    std::int32_t offsetX = 0;
    std::int32_t offsetZ = 0;
  };
  // BTL_addConfusionEffect: three objects circling the head; each turns by its own random angle.
  struct Confusion {
    FighterId fighter = 0;
    std::int32_t angle = 0;
    std::int32_t spin = 0;
    std::array<std::int32_t, 3> offsets{};
  };
  // BTL_addStunEffect: the countdown sign, which lasts the stun's frames as it began.
  struct Stun {
    FighterId fighter = 0;
    StunStage stage = StunStage::Countdown;
    std::int32_t frame = 0;
    std::int32_t duration = 0;
  };
  // BTL_addStunSubEffect: a ring that falls from the sign to the ground.
  struct StunRing {
    FighterId fighter = 0;
    std::int32_t frame = 0;
  };

  static constexpr std::int32_t BubbleFrames = 0x28;
  static constexpr std::int32_t PoisonCycle = 0x1E;
  static constexpr std::int32_t StunRingEvery = 6;
  static constexpr std::int32_t StunRingFrames = 0x10;
  static constexpr std::int32_t StunFlashFrames = 8;
  static constexpr std::int32_t StunShrinkFrames = 5;

  // The original's other world objects over the fighters (battle_effect.c): the finisher's aura,
  // the burst of light of an item that reaches the partner, the stat disk's spinning disk and its
  // trails, and the cure's rings. Their directions are rolled once as the battle loads
  // (loadBattleData: BTL_initializeBattleItemParticles, BTL_initializeUnk3, BTL_initializeUnk2,
  // BTL_initializeFinisherAuraModel, 184 rand() in all), from the visuals' own rolls.
  using Vec3 = std::array<std::int32_t, 3>;
  // A triangle of a spark: the centre and two corners, 4096 for 1 (BTL_FINISHER_AURA_SPARKS,
  // BTL_BUFF_RING_SPARKS), in the console's axes.
  struct SparkPair {
    Vec3 first{};
    Vec3 second{};
  };
  // BTL_addFinisherAura: frame 0 to `duration`; the third object turns by a rand() each frame it
  // is drawn (BTL_renderFinisherAura: rot.vy = rand()).
  struct FinisherAura {
    FighterId fighter = 0;
    std::int32_t frame = 0;
    std::int32_t duration = 0;
    std::int32_t spin = 0;
  };
  // BTL_addItemParticles: 20 glows from over the fighter's head, in 1/16 units and the console's
  // axes from its feet where it stood (`origin`, the battle's x and z; the ground's height its).
  struct ItemParticle {
    std::int32_t life = 0;
    Vec3 position{};
    Vec3 velocity{};
    std::int32_t brightness = 0;
  };
  static constexpr std::size_t ItemParticleCount = 0x14;
  struct ItemBurst {
    FighterId fighter = 0;
    Point origin;
    std::int32_t timer = 0;
    std::array<ItemParticle, ItemParticleCount> particles{};
  };
  // BTL_addBuffDiskEffect: the disk (DEFEFF object 5) on the fighter's feet, growing, spinning and
  // shrinking over 0x43 frames, scale 4096 for 1.
  struct BuffDisk {
    FighterId fighter = 0;
    std::int32_t frame = 0;
    std::int32_t rotationY = 0;
    std::int32_t scaleXZ = 0;
    std::int32_t scaleY = 0;
    std::int32_t targetXZ = 0;
    std::int32_t targetY = 0;
  };
  // BTL_tickBuffTrails: a streak left at the fighter's feet every frame for 0x3B frames, 8 frames
  // each; its corners in the world (x and z the battle's, y up from the feet): the first two rise
  // 100 a frame, the last two 0x25.
  struct BuffTrail {
    std::int32_t life = -1;
    std::array<Vec3, 4> corners{};
  };
  static constexpr std::size_t BuffTrailCount = 8;
  struct BuffTrails {
    FighterId fighter = 0;
    std::int32_t frame = 0;
    std::array<BuffTrail, BuffTrailCount> trails{};
  };
  // BTL_addBuffRingsEffect: five nested rings (DEFEFF objects 3, 0, 1, 2, 4) on the fighter's
  // second node for 0x39 frames, then 16 sparks in the cured status's colour for 8.
  static constexpr std::size_t BuffRingCount = 5;
  struct BuffRings {
    FighterId fighter = 0;
    std::int32_t frame = 0;
    std::int32_t kind = 0;
    std::int32_t scale = 0;
    std::int32_t target = 0;
    std::array<std::array<std::int32_t, 2>, BuffRingCount> rotations{};
  };
  // What a cure item cures, for the rings' colour (battle_main.c: BTL_healStatusEffect's kind).
  enum class CureKind : std::uint8_t { Poison = 0, Stun = 1, Confusion = 2, Flattening = 3 };

  static constexpr std::size_t FinisherAuraSlots = 2;
  static constexpr std::size_t ItemBurstSlots = 4;
  static constexpr std::size_t FinisherSparkPairs = 10;
  static constexpr std::size_t RingSparkPairs = 16;
  static constexpr std::int32_t ItemBurstFrames = 0x28;
  static constexpr std::int32_t BuffDiskFrames = 0x43;
  static constexpr std::int32_t BuffTrailSpawnFrames = 0x3B;
  static constexpr std::int32_t BuffRingsFrames = 0x41;
  static constexpr std::int32_t BuffRingsShownFrames = 0x39;
  static constexpr std::int32_t FinisherSparkFrames = 7;

  explicit StatusVisuals(std::uint64_t seed = 0);

  // BTL_addStatusEffectVisual: starts the kind's visual on the fighter unless it shows one already
  // (or its pool is full). `stunFrames` is the stun's timer, which the sign counts down.
  void show(ruleset::Random& random, FighterId fighter, Kind kind, std::int32_t stunFrames = 0);
  // BTL_removeStatusEffectVisual: ends the visual of the fighter in the kind's pool; the fighter
  // then shows none, whichever pool it was in.
  void hide(FighterId fighter, Kind kind);
  // One frame of every visual (BTL_battleTickFrame: tickObjects, after the fighters); the
  // trails read where the fighters stand.
  void tick(ruleset::Random& random, std::span<const Fighter> fighters = {});
  // Ends them all (BTL_deinitializeCombat, BTL_removeAll*Effects).
  void clear();

  // BTL_addFinisherAura: in the first free of its two slots, or none.
  std::optional<std::size_t> addFinisherAura(FighterId fighter, std::int32_t duration);
  // BTL_removeFinisherAura.
  void removeFinisherAura(std::size_t slot);
  // BTL_addItemParticles: over the head of the fighter as it stands.
  void addItemParticles(FighterId fighter, const Fighter& body);
  // BTL_addBuffDiskEffect (with BTL_addBuffTrails): its one slot, if free.
  void addBuffDisk(FighterId fighter, const Fighter& body);
  // BTL_addBuffRingsEffect: its one slot, if free.
  void addBuffRings(FighterId fighter, const Fighter& body, CureKind kind);

  // The rolls when the battle's are not the original's.
  [[nodiscard]] ruleset::Random& ownRolls() { return ownRolls_; }

  [[nodiscard]] const std::array<std::optional<Bubble>, BubbleSlots>& bubbles() const {
    return bubbles_;
  }
  [[nodiscard]] const std::array<std::optional<Confusion>, ConfusionSlots>& confusions() const {
    return confusions_;
  }
  [[nodiscard]] const std::array<std::optional<Stun>, StunSlots>& stuns() const { return stuns_; }
  [[nodiscard]] const std::array<std::optional<StunRing>, StunRingSlots>& stunRings() const {
    return stunRings_;
  }
  [[nodiscard]] const std::array<std::optional<FinisherAura>, FinisherAuraSlots>&
  finisherAuras() const {
    return finisherAuras_;
  }
  [[nodiscard]] const std::array<std::optional<ItemBurst>, ItemBurstSlots>& itemBursts() const {
    return itemBursts_;
  }
  [[nodiscard]] const std::optional<BuffDisk>& buffDisk() const { return buffDisk_; }
  [[nodiscard]] const std::optional<BuffTrails>& buffTrails() const { return buffTrails_; }
  [[nodiscard]] const std::optional<BuffRings>& buffRings() const { return buffRings_; }
  [[nodiscard]] const std::array<SparkPair, FinisherSparkPairs>& finisherSparks() const {
    return finisherSparks_;
  }
  [[nodiscard]] const std::array<SparkPair, RingSparkPairs>& ringSparks() const {
    return ringSparks_;
  }
  // BUFF_MODEL_FRAME: the frames since the battle loaded, whose parity picks the disk's palette
  // row (buff_model.c: tickBuffModel, morphBuffModel: 491 on even frames, 492 on odd).
  [[nodiscard]] std::int64_t frames() const { return frames_; }
  // The colour of the cure's sparks by kind (BTL_BUFF_RING_COLOR_R, _G, _B).
  [[nodiscard]] static Vec3 ringColor(std::int32_t kind);

  // The visual the fighter keeps (statusFxId), which may have ended by itself (a stun sign that
  // ran out before the stun did).
  [[nodiscard]] std::optional<std::size_t> shownBy(FighterId fighter) const;
  [[nodiscard]] std::size_t poisons() const;

private:
  struct Poison {
    FighterId fighter = 0;
    std::int32_t frame = 0;
  };

  void addBubble(ruleset::Random& random, FighterId fighter);
  void tickStun(std::size_t slot);
  void rollDirections();
  void tickEffects(ruleset::Random& random, std::span<const Fighter> fighters);

  std::array<std::optional<Poison>, PoisonSlots> poisons_;
  std::array<std::optional<Bubble>, BubbleSlots> bubbles_;
  std::array<std::optional<Confusion>, ConfusionSlots> confusions_;
  std::array<std::optional<Stun>, StunSlots> stuns_;
  std::array<std::optional<StunRing>, StunRingSlots> stunRings_;
  std::map<FighterId, std::size_t> shown_;
  std::array<std::optional<FinisherAura>, FinisherAuraSlots> finisherAuras_;
  std::array<std::optional<ItemBurst>, ItemBurstSlots> itemBursts_;
  std::optional<BuffDisk> buffDisk_;
  std::optional<BuffTrails> buffTrails_;
  std::optional<BuffRings> buffRings_;
  // BTL_ITEM_PARTICLE_VELOCITIES, BTL_ITEM_PARTICLE_DRAG (x and z), BTL_BUFF_TRAIL_A_X/_Z and
  // _B_X/_Z, the sparks.
  std::array<Vec3, ItemParticleCount> itemVelocities_{};
  std::array<std::array<std::int32_t, 2>, ItemParticleCount> itemDrag_{};
  std::array<std::array<std::int32_t, 4>, BuffTrailCount> trailOffsets_{};
  std::array<SparkPair, FinisherSparkPairs> finisherSparks_{};
  std::array<SparkPair, RingSparkPairs> ringSparks_{};
  std::int64_t frames_ = 0;
  ruleset::Random ownRolls_;
};

} // namespace dwe::game::battle
