#pragma once

#include <dwe/game/battle/state.hpp>
#include <dwe/ruleset/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::game::battle {

// The original's hit sparks and what they leave (efe.c): the spark of light that bursts into
// particles where a technique touches a fighter (createParticleFX, tickParticleFX,
// renderParticleFX), the small particles it leaves on the fighter's nodes (addEntityParticleFX,
// tickEntityParticleFX, renderEntityParticleFX), which an effect script also puts on its attacker
// (BTL_addSourceEntityParticleFX), and the clouds of dust a hit's spark puffs around the fighter's
// feet as it ends (createCloudFX). They run with the world's objects, after the fighters of the
// frame (BTL_battleTickFrame: tickObjects), and roll rand() as they go: with the original's rolls
// (Battle::useOriginalRolls) from the battle's generator, in step with the original, else from
// their own (ownRolls). The game draws them (FieldScene).
class HitParticles {
public:
  // createParticleFX's `count`: 0 a plain spark (the gym's), 1 a hit (BTL_startHitAnimation), 2 a
  // block (BTL_tickAttackHits).
  enum class Spark : std::uint8_t { Plain = 0, Hit = 1, Block = 2 };

  // The original's pools: PARTICLE_FX_DATA, ENTITY_PARTICLE_FX_DATA and CLOUD_FX_DATA.
  static constexpr std::size_t SparkSlots = 4;
  static constexpr std::size_t FighterParticleSlots = 20;
  static constexpr std::size_t CloudSlots = 0x3C;
  // The kinds of spark with a colour (createParticleFX's `kind`, a technique's `special`).
  static constexpr std::int32_t SparkKinds = 7;
  // ENTITY_PARTICLE_FX_MODES: the looks a fighter particle takes.
  static constexpr std::int32_t FighterParticleLooks = 9;

  // A point in the original's axes and units (y down, z the other way from the world's).
  using Vec3 = std::array<std::int32_t, 3>;

  // A spark (efe.c: ParticleFX): its kind (the colour), where it burst and on whom, its frames and
  // the four spreads of its flashes (spread[0..2][i] the offset of flash i, spread[3][i] its size).
  struct SparkFx {
    Spark spark = Spark::Plain;
    std::int32_t kind = 0;
    Vec3 position{};
    FighterId fighter = 0;
    std::int32_t state = 0;
    std::int32_t timer = 0;
    std::int32_t endFrame = 0;
    std::array<std::array<std::int32_t, 3>, 4> spread{};
  };
  // A particle on a fighter's node (efe.c: EntityParticleFX): it moves to another node every
  // 4th frame and takes another look (ENTITY_PARTICLE_FX_MODES) then; its picture steps with the
  // timer.
  struct FighterParticle {
    FighterId fighter = 0;
    std::int32_t timer = 0;
    std::int32_t node = 0;
    std::int32_t look = 0;
  };
  // A cloud of dust (efe.c: CloudFXEntry): where it rose, in the original's axes, and its frame.
  struct Cloud {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t state = 0;
  };

  explicit HitParticles(std::uint64_t seed = 0) : ownRolls_(seed) {}

  // The fighters' node counts (DIGIMON_DATA boneCount), by battle id: a fighter particle sits on
  // one of the nodes after the first.
  void setNodeCounts(std::vector<std::int32_t> counts) { nodeCounts_ = std::move(counts); }

  // createParticleFX: with one of the 4 slots free, 8 draws (12 for a hit): the flashes' spreads
  // and sizes. A `kind` outside the 7 colours (a technique whose `special` is no colour) rolls
  // them and shows nothing, which keeps the slot free. `lifetime` is the original's argument (a
  // hit's: the technique's invulnerable frames + 0x10; a block's: 0x11); `position` the attack's
  // where it touched the fighter.
  void spawn(ruleset::Random& random, Spark spark, std::int32_t kind, Vec3 position,
             FighterId fighter, std::int32_t lifetime);
  // One frame of the sparks, the fighter particles and the clouds, with their draws; the
  // fighters give where the clouds rise and how wide.
  void tick(ruleset::Random& random, std::span<const Fighter> fighters = {});
  // BTL_addSourceEntityParticleFX: an effect script puts a particle on its attacker
  // (addEntityParticleFX, one draw for the node). The scripts run in the effects' engine object,
  // which the battle adds before any spark or particle (BTL_initializeEFEEngine; in the Native
  // Forest fight slot 9 of the world's objects, the first free one 15), so the particle's object
  // comes after it and ticks on the frame it appears, after the effects (tickObjects goes by
  // slot).
  void attach(ruleset::Random& random, FighterId fighter, std::int32_t timer);
  // Everything gone (the battle's end: removeAllCloudFX and the world's objects with it).
  void clear();

  // The rolls when they are not the original's.
  [[nodiscard]] ruleset::Random& ownRolls() { return ownRolls_; }

  [[nodiscard]] std::span<const std::optional<SparkFx>> sparkSlots() const { return sparks_; }
  [[nodiscard]] std::span<const std::optional<FighterParticle>> fighterParticleSlots() const {
    return fighterParticles_;
  }
  [[nodiscard]] const std::vector<Cloud>& clouds() const { return clouds_; }
  // How many sparks and fighter particles are alive.
  [[nodiscard]] std::size_t sparks() const;
  [[nodiscard]] std::size_t fighterParticles() const;
  [[nodiscard]] bool empty() const {
    return sparks() == 0 && fighterParticles() == 0 && clouds_.empty();
  }

private:
  void tickSpark(ruleset::Random& random, std::span<const Fighter> fighters,
                 std::optional<SparkFx>& slot);
  // addEntityParticleFX: a particle on a node of the fighter, one draw for the node; the slot it
  // took, none when all 20 are taken (then nothing is drawn).
  std::optional<FighterParticle>* addFighterParticle(ruleset::Random& random, FighterId fighter,
                                                     std::int32_t timer);
  // tickEntityParticleFX: one frame of a particle.
  void tickFighterParticle(ruleset::Random& random, std::optional<FighterParticle>& slot) const;
  // createCloudFX: at most 60.
  void addCloud(std::int32_t x, std::int32_t z);
  // rand() % (boneCount - 1) + 1: the original's node roll.
  [[nodiscard]] std::int32_t rollNode(ruleset::Random& random, FighterId fighter) const;

  ruleset::Random ownRolls_;
  std::vector<std::int32_t> nodeCounts_;
  std::array<std::optional<SparkFx>, SparkSlots> sparks_;
  // Every slot starts free, as efe.c: initializeEntityParticleFX leaves them (timer -1).
  std::array<std::optional<FighterParticle>, FighterParticleSlots> fighterParticles_;
  std::vector<Cloud> clouds_;
};

} // namespace dwe::game::battle
