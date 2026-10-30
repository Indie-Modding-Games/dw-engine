#include <dwe/game/battle/particle_rolls.hpp>

#include <algorithm>

namespace dwe::game::battle {

namespace {

// efe.c: createParticleFX: four spreads of two rolls each, and four more for a hit.
constexpr int SparkDraws = 8;
constexpr int HitSparkDraws = 12;
// The frames a spark runs before it may end, by `count` (fx->timer).
constexpr std::int32_t PlainTimer = 0x13;
constexpr std::int32_t HitTimer = 0x16;
constexpr std::int32_t BlockTimer = 0xE;
// createParticleFX: fx->endFrame = lifetime + 4.
constexpr std::int32_t EndFramePadding = 4;
// tickParticleFX: on its 10th and 13th frames a spark leaves particles on the fighter.
constexpr std::int32_t FirstBurst = 10;
constexpr std::int32_t SecondBurst = 13;
constexpr int FirstBurstParticles = 3;
constexpr std::int32_t ShortParticle = 0xE;
constexpr std::int32_t LongParticle = 0x16;
// tickParticleFX: a hit's spark puffs clouds over its last 4 frames, endFrame + 2 of them, two
// rolls each (angle and distance).
constexpr std::int32_t CloudFrames = 4;
constexpr std::int32_t CloudsOver = 2;
constexpr int CloudDraws = 2;
// tickEntityParticleFX: every 4th frame a particle moves to another bone (bone and offset rolls).
constexpr std::int32_t BoneEvery = 4;
constexpr int BoneDraws = 2;

void burn(ruleset::Random& random, int draws) {
  for (int draw = 0; draw < draws; ++draw) {
    static_cast<void>(random.next());
  }
}

} // namespace

void ParticleRolls::spawn(ruleset::Random& random, Spark spark, std::int32_t lifetime,
                          bool shown) {
  const auto free = std::ranges::find_if(
      sparks_, [](const std::optional<SparkSlot>& slot) { return !slot.has_value(); });
  if (free == sparks_.end()) {
    return;
  }
  burn(random, spark == Spark::Hit ? HitSparkDraws : SparkDraws);
  if (!shown) {
    return;
  }
  std::int32_t timer = PlainTimer;
  if (spark == Spark::Hit) {
    timer = HitTimer;
  } else if (spark == Spark::Block) {
    timer = BlockTimer;
  }
  *free = SparkSlot{
      .spark = spark, .state = 0, .timer = timer, .endFrame = lifetime + EndFramePadding};
}

void ParticleRolls::tick(ruleset::Random& random) {
  // The sparks were added to the world's objects before the particles they leave, which then
  // tick on the frame they appear (measured: a hit on frame 87 left 3 particles on frame 96 that
  // rolled their bones on 98).
  for (std::optional<SparkSlot>& slot : sparks_) {
    if (slot) {
      tickSpark(random, slot);
    }
  }
  for (std::optional<std::int32_t>& timer : fighterParticles_) {
    if (!timer) {
      continue;
    }
    if (*timer <= 0) {
      timer.reset();
      continue;
    }
    if (*timer % BoneEvery == 0) {
      burn(random, BoneDraws);
    }
    --*timer;
  }
}

void ParticleRolls::tickSpark(ruleset::Random& random, std::optional<SparkSlot>& slot) {
  SparkSlot& spark = *slot;
  ++spark.state;
  --spark.endFrame;
  if (spark.state == FirstBurst) {
    // A plain spark leaves them only on a fighter; the battle's always have one.
    if (spark.spark == Spark::Plain) {
      for (int particle = 0; particle < FirstBurstParticles; ++particle) {
        addFighterParticle(random, ShortParticle);
      }
    } else if (spark.spark == Spark::Hit) {
      for (int particle = 0; particle < FirstBurstParticles; ++particle) {
        addFighterParticle(random, LongParticle);
      }
    }
    return;
  }
  if (spark.state == SecondBurst) {
    if (spark.spark == Spark::Plain) {
      addFighterParticle(random, ShortParticle);
      addFighterParticle(random, ShortParticle);
    } else if (spark.spark == Spark::Hit) {
      addFighterParticle(random, LongParticle);
      addFighterParticle(random, ShortParticle);
    }
    return;
  }
  if (spark.endFrame >= 0 && spark.endFrame < CloudFrames && spark.spark == Spark::Hit) {
    burn(random, (spark.endFrame + CloudsOver) * CloudDraws);
    return;
  }
  if (spark.state >= spark.timer && spark.endFrame < 0) {
    slot.reset();
  }
}

void ParticleRolls::addFighterParticle(ruleset::Random& random, std::int32_t timer) {
  const auto free = std::ranges::find_if(
      fighterParticles_, [](const std::optional<std::int32_t>& slot) { return !slot.has_value(); });
  if (free == fighterParticles_.end()) {
    return;
  }
  *free = timer;
  burn(random, 1);
}

std::size_t ParticleRolls::sparks() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      sparks_, [](const std::optional<SparkSlot>& slot) { return slot.has_value(); }));
}

std::size_t ParticleRolls::fighterParticles() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      fighterParticles_, [](const std::optional<std::int32_t>& slot) { return slot.has_value(); }));
}

} // namespace dwe::game::battle
