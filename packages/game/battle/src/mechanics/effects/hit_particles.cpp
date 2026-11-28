#include <dwe/core/original_math.hpp>
#include <dwe/game/battle/hit_particles.hpp>

#include <algorithm>

namespace dwe::game::battle {

namespace {

// libc rand(): 15 bits.
constexpr std::uint32_t RandMask = 0x7FFF;
// efe.c: createParticleFX: each flash is offset by rand() % 200 - 100 on each axis and sized
// rand() % 170 + 250; a hit's third flash rand() % 200 + 300.
constexpr std::int32_t SpreadRoll = 200;
constexpr std::int32_t SpreadHalf = 100;
constexpr std::int32_t FlashSizeRoll = 170;
constexpr std::int32_t FlashSizeFrom = 250;
constexpr std::int32_t HitSizeRoll = 200;
constexpr std::int32_t HitSizeFrom = 300;
constexpr std::size_t HitFlash = 2;
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
// tickParticleFX: a hit's spark puffs clouds over its last 4 frames, endFrame + 2 of them, each at
// a random angle and the fighter's radius plus up to half of it from its root node.
constexpr std::int32_t CloudFrames = 4;
constexpr std::int32_t CloudsOver = 2;
// tickCloudFX: a cloud is gone on its 14th frame.
constexpr std::int32_t CloudLife = 0xE;
// tickEntityParticleFX: every 4th frame a particle moves to another node and takes one of the 9
// looks.
constexpr std::int32_t NodeEvery = 4;
// math.c _sin and _cos: 512 a turn; libgte's rsin and rcos 4096.
constexpr std::int32_t FixedShift = 12;

std::int32_t rand(ruleset::Random& random) {
  return static_cast<std::int32_t>(random.next() & RandMask);
}

std::int32_t sin512(std::int32_t angle) {
  return core::original::sin512(angle);
}
std::int32_t cos512(std::int32_t angle) {
  return core::original::cos512(angle);
}

} // namespace

void HitParticles::spawn(ruleset::Random& random, Spark spark, std::int32_t kind, Vec3 position,
                         FighterId fighter, std::int32_t lifetime) {
  const auto free = std::ranges::find_if(
      sparks_, [](const std::optional<SparkFx>& slot) { return !slot.has_value(); });
  if (free == sparks_.end()) {
    return;
  }
  SparkFx fx{.spark = spark, .kind = kind, .position = position, .fighter = fighter};
  const auto spread = [&random] { return (rand(random) % SpreadRoll) - SpreadHalf; };
  fx.spread[0][0] = spread();
  fx.spread[1][0] = spread();
  fx.spread[2][0] = spread();
  fx.spread[3][0] = (rand(random) % FlashSizeRoll) + FlashSizeFrom;
  fx.spread[0][1] = spread();
  fx.spread[1][1] = spread();
  fx.spread[2][1] = spread();
  fx.spread[3][1] = (rand(random) % FlashSizeRoll) + FlashSizeFrom;
  switch (spark) {
  case Spark::Plain:
    fx.timer = PlainTimer;
    break;
  case Spark::Hit:
    fx.timer = HitTimer;
    fx.spread[0][HitFlash] = spread();
    fx.spread[1][HitFlash] = spread();
    fx.spread[2][HitFlash] = spread();
    fx.spread[3][HitFlash] = (rand(random) % HitSizeRoll) + HitSizeFrom;
    break;
  case Spark::Block:
    fx.timer = BlockTimer;
    break;
  }
  if (kind < 0 || kind >= SparkKinds) {
    return;
  }
  fx.endFrame = lifetime + EndFramePadding;
  *free = fx;
}

void HitParticles::tick(ruleset::Random& random, std::span<const Fighter> fighters) {
  // The sparks were added to the world's objects
  // before the particles and clouds they leave, which then tick on the frame they appear
  // (measured: a hit on frame 87 left 3 particles on frame 96 that rolled their nodes on 98).
  for (std::optional<SparkFx>& slot : sparks_) {
    if (slot) {
      tickSpark(random, fighters, slot);
    }
  }
  for (std::optional<FighterParticle>& slot : fighterParticles_) {
    if (slot) {
      tickFighterParticle(random, slot);
    }
  }
  for (Cloud& cloud : clouds_) {
    ++cloud.state;
  }
  std::erase_if(clouds_, [](const Cloud& cloud) { return cloud.state >= CloudLife; });
}

void HitParticles::attach(ruleset::Random& random, FighterId fighter, std::int32_t timer) {
  if (std::optional<FighterParticle>* added = addFighterParticle(random, fighter, timer)) {
    tickFighterParticle(random, *added);
  }
}

void HitParticles::clear() {
  sparks_.fill(std::nullopt);
  fighterParticles_.fill(std::nullopt);
  clouds_.clear();
}

void HitParticles::tickFighterParticle(ruleset::Random& random,
                                       std::optional<FighterParticle>& slot) const {
  FighterParticle& particle = *slot;
  if (particle.timer <= 0) {
    slot.reset();
    return;
  }
  if (particle.timer % NodeEvery == 0) {
    particle.node = rollNode(random, particle.fighter);
    particle.look = rand(random) % FighterParticleLooks;
  }
  --particle.timer;
}

void HitParticles::tickSpark(ruleset::Random& random, std::span<const Fighter> fighters,
                             std::optional<SparkFx>& slot) {
  SparkFx& spark = *slot;
  ++spark.state;
  --spark.endFrame;
  if (spark.state == FirstBurst) {
    // A plain spark leaves them only on a fighter; the battle's always have one.
    const std::int32_t timer = spark.spark == Spark::Hit ? LongParticle : ShortParticle;
    if (spark.spark != Spark::Block) {
      for (int particle = 0; particle < FirstBurstParticles; ++particle) {
        addFighterParticle(random, spark.fighter, timer);
      }
    }
    return;
  }
  if (spark.state == SecondBurst) {
    if (spark.spark == Spark::Plain) {
      addFighterParticle(random, spark.fighter, ShortParticle);
      addFighterParticle(random, spark.fighter, ShortParticle);
    } else if (spark.spark == Spark::Hit) {
      addFighterParticle(random, spark.fighter, LongParticle);
      addFighterParticle(random, spark.fighter, ShortParticle);
    }
    return;
  }
  if (spark.endFrame >= 0 && spark.endFrame < CloudFrames && spark.spark == Spark::Hit) {
    // The root node's x and z: where the fighter stands (the original's z is the world's
    // negated).
    const Fighter* fighter = spark.fighter < fighters.size() ? &fighters[spark.fighter] : nullptr;
    const std::int32_t baseX = fighter != nullptr ? fighter->position.x : 0;
    const std::int32_t baseZ = fighter != nullptr ? -fighter->position.z : 0;
    const std::int32_t radius = fighter != nullptr ? fighter->combatant.radius : 0;
    const std::int32_t spread = radius >> 1;
    for (std::int32_t i = 0; i < spark.endFrame + CloudsOver; ++i) {
      const std::int32_t angle = rand(random);
      const std::int32_t roll = rand(random);
      const std::int32_t distance = radius + (spread > 0 ? roll % spread : 0);
      addCloud(baseX + ((distance * sin512(angle)) >> FixedShift),
               baseZ - ((distance * cos512(angle)) >> FixedShift));
    }
    return;
  }
  if (spark.state >= spark.timer && spark.endFrame < 0) {
    slot.reset();
  }
}

std::optional<HitParticles::FighterParticle>*
HitParticles::addFighterParticle(ruleset::Random& random, FighterId fighter, std::int32_t timer) {
  const auto free =
      std::ranges::find_if(fighterParticles_, [](const std::optional<FighterParticle>& slot) {
        return !slot.has_value();
      });
  if (free == fighterParticles_.end()) {
    return nullptr;
  }
  *free = FighterParticle{
      .fighter = fighter, .timer = timer, .node = rollNode(random, fighter), .look = 0};
  return &*free;
}

void HitParticles::addCloud(std::int32_t x, std::int32_t z) {
  if (clouds_.size() >= CloudSlots) {
    return;
  }
  clouds_.push_back({.x = x, .z = z, .state = 0});
}

std::int32_t HitParticles::rollNode(ruleset::Random& random, FighterId fighter) const {
  const std::int32_t roll = rand(random);
  const std::int32_t nodes = fighter < nodeCounts_.size() ? nodeCounts_[fighter] : 0;
  // A species with a single node never comes here in the original; it would divide by 0.
  return nodes > 1 ? (roll % (nodes - 1)) + 1 : 0;
}

std::size_t HitParticles::sparks() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      sparks_, [](const std::optional<SparkFx>& slot) { return slot.has_value(); }));
}

std::size_t HitParticles::fighterParticles() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(fighterParticles_, [](const std::optional<FighterParticle>& slot) {
        return slot.has_value();
      }));
}

} // namespace dwe::game::battle
