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
  // BTL_addSourceEntityParticleFX: an effect script puts a particle on its attacker
  // (addEntityParticleFX, one draw for the node). The scripts run in the effects' engine object,
  // which the battle adds before any spark or particle (BTL_initializeEFEEngine; in the Native
  // Forest fight slot 9 of the world's objects, the first free one 15), so the particle's object
  // comes after it and ticks on the frame it appears, after the effects (tickObjects goes by
  // slot).
  void attach(ruleset::Random& random, std::int32_t timer);

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
  using FighterParticle = std::optional<std::int32_t>;

  // addEntityParticleFX: a particle on a bone of the fighter, one draw for the bone; the slot it
  // took, none when all 20 are taken (then nothing is drawn).
  FighterParticle* addFighterParticle(ruleset::Random& random, std::int32_t timer);
  // tickEntityParticleFX: one frame of a particle.
  static void tickFighterParticle(ruleset::Random& random, FighterParticle& timer);

  std::array<std::optional<SparkSlot>, SparkSlots> sparks_;
  // tickEntityParticleFX's timers.
  std::array<FighterParticle, FighterParticleSlots> fighterParticles_;
};

} // namespace dwe::game::battle
