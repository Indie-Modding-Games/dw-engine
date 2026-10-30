#pragma once

#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace dwe::game::battle {

// The rand() draws of the original's hit sparks (efe.c: createParticleFX, tickParticleFX,
// addEntityParticleFX, tickEntityParticleFX), and nothing else of them: the game draws its own
// sparks. With the original's rolls (Battle::useOriginalRolls) the battle spends here the draws
// the original's sparks take, when they take them, so that its next decision rolls the original's
// number. A spark draws as it starts (in the fighter's hit or block) and then while it ticks with
// the world's objects, after the fighters of the frame (BTL_battleTickFrame: tickObjects).
class ParticleRolls {
public:
  // createParticleFX's `count`: 1 for a hit (BTL_startHitAnimation), 2 for a block
  // (BTL_tickAttackHits), 0 unused here.
  enum class Spark : std::uint8_t { Plain = 0, Hit = 1, Block = 2 };

  // The draws of the original's spark slots and fighter particles: 4 and 20.
  static constexpr std::size_t SparkSlots = 4;
  static constexpr std::size_t FighterParticleSlots = 20;

  // createParticleFX: with one of its 4 slots free, 8 draws (12 for a hit). `shown` is false for a
  // spark the original rolls but never shows (a technique whose `special` is no colour), which
  // keeps its slot free. `lifetime` is the original's argument (a hit's: the technique's
  // invulnerable frames + 0x10; a block's: 0x11).
  void spawn(ruleset::Random& random, Spark spark, std::int32_t lifetime, bool shown = true);
  // One frame of the sparks and of the fighter particles they leave, with their draws.
  void tick(ruleset::Random& random);

  // How many sparks and fighter particles are alive.
  [[nodiscard]] std::size_t sparks() const;
  [[nodiscard]] std::size_t fighterParticles() const;

private:
  struct SparkSlot {
    Spark spark = Spark::Plain;
    std::int32_t state = 0;
    std::int32_t timer = 0;
    std::int32_t endFrame = 0;
  };

  void tickSpark(ruleset::Random& random, std::optional<SparkSlot>& slot);
  // addEntityParticleFX: a particle on a bone of the fighter, one draw for the bone.
  void addFighterParticle(ruleset::Random& random, std::int32_t timer);

  std::array<std::optional<SparkSlot>, SparkSlots> sparks_;
  // tickEntityParticleFX's timers.
  std::array<std::optional<std::int32_t>, FighterParticleSlots> fighterParticles_;
};

} // namespace dwe::game::battle
