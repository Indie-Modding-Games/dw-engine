#pragma once

#include <dwe/game/battle/state.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

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
  // BTL_STUN_EFFECTS, BTL_STUN_SUB_EFFECTS).
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

  explicit StatusVisuals(std::uint64_t seed = 0) : ownRolls_(seed) {}

  // BTL_addStatusEffectVisual: starts the kind's visual on the fighter unless it shows one already
  // (or its pool is full). `stunFrames` is the stun's timer, which the sign counts down.
  void show(ruleset::Random& random, FighterId fighter, Kind kind, std::int32_t stunFrames = 0);
  // BTL_removeStatusEffectVisual: ends the visual of the fighter in the kind's pool; the fighter
  // then shows none, whichever pool it was in.
  void hide(FighterId fighter, Kind kind);
  // One frame of every visual (BTL_battleTickFrame: tickObjects, after the fighters).
  void tick(ruleset::Random& random);
  // Ends them all (BTL_deinitializeCombat, BTL_removeAll*Effects).
  void clear();

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

  std::array<std::optional<Poison>, PoisonSlots> poisons_;
  std::array<std::optional<Bubble>, BubbleSlots> bubbles_;
  std::array<std::optional<Confusion>, ConfusionSlots> confusions_;
  std::array<std::optional<Stun>, StunSlots> stuns_;
  std::array<std::optional<StunRing>, StunRingSlots> stunRings_;
  std::map<FighterId, std::size_t> shown_;
  ruleset::Random ownRolls_;
};

} // namespace dwe::game::battle
