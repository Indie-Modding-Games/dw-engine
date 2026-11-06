#include <dwe/game/battle/status_visuals.hpp>

#include <algorithm>
#include <cstdlib>

#include "effect_math.hpp"

// battle_effect.c: the battle's world objects over the fighters besides their statuses: the
// finisher's aura, the burst of an item that reaches the partner, the stat disk with its trails and
// the cure's rings.
namespace dwe::game::battle {

namespace {

namespace math = effect_math;

constexpr std::uint32_t RandMask = 0x7FFF;
constexpr std::int32_t One = 4096;

std::int32_t rand15(ruleset::Random& random) {
  return static_cast<std::int32_t>(random.next() & RandMask);
}

StatusVisuals::Vec3 vec3Of(const math::Vector& v) {
  return {v.x, v.y, v.z};
}

// BTL_initializeItemParticleVelocities: each glow leaves straight up at 0x230 / 16 tilted by up to
// 500 either way about x and turned at random about y; the drag takes x and z back over 60 to 30
// frames, the more tilted the sooner.
constexpr std::int32_t ItemSpeed = 0x230;
constexpr std::int32_t ItemTiltRange = 1000;
constexpr std::int32_t ItemTiltHalf = 0x1F4;
constexpr std::int32_t ItemDragSlow = 0x3C;
constexpr std::int32_t ItemDragFast = 0x1E;
constexpr std::int32_t ItemTiltMost = 0x226;
// BTL_addItemParticles, BTL_tickItemParticles: 0x19 frames of life, gravity 0x36, from 0xFF0 of
// brightness down to 0.
constexpr std::int32_t ItemLife = 0x19;
constexpr std::int32_t ItemGravity = 0x36;
constexpr std::int32_t ItemBrightness = 0xFF0;
constexpr std::int32_t SubUnitShift = 4;

// BTL_initializeBuffTrails: eight directions an eighth of a turn apart (shuffled) plus up to 0x200,
// each a 0x3C-wide streak (30 either side) at 160 to 198 out, for a radius of 200.
constexpr std::int32_t TrailDirections = 8;
constexpr std::int32_t TrailSpread = 0x14;
constexpr std::int32_t TrailBase = 0x50;
constexpr std::int32_t TrailWiden = 200;
constexpr std::int32_t TrailJitter = 0x200;
constexpr std::int32_t TrailHalfWidth = 0x1E;
constexpr std::int32_t TrailRadius = 200;
constexpr std::int32_t TrailLife = 8;
constexpr std::int32_t TrailRiseTop = 100;
constexpr std::int32_t TrailRiseBottom = 0x25;

// BTL_initializeUnk2, BTL_initializeFinisherAuraModel: a spark's corners 30-59 (50-99 for the
// rings) to either side and 800-1199 out, turned at random.
constexpr std::int32_t FinisherSparkWidth = 30;
constexpr std::int32_t RingSparkWidth = 0x32;
constexpr std::int32_t SparkReach = 0x320;
constexpr std::int32_t SparkReachRange = 0x190;
constexpr std::int32_t HalfCircle = 180;
constexpr std::int32_t FullCircle = 360;

// BTL_tickBuffDisk: grows from a third over 0xF frames, holds until 0x37 and shrinks to a tenth
// by 0x43, turning 0x32 a frame; BTL_addBuffDiskEffect: radius * 0xBB8 / 200 wide and
// height * 0xB22 / 200 tall.
constexpr std::int32_t DiskGrowFrames = 0xF;
constexpr std::int32_t DiskShrinkFrom = 0x37;
constexpr std::int32_t DiskTurn = 0x32;
constexpr std::int32_t DiskWidth = 0xBB8;
constexpr std::int32_t DiskHeight = 0xB22;
constexpr std::int32_t BodyScale = 200;
constexpr std::int32_t Percent = 100;
constexpr std::int32_t DiskShrinkPercent = 10;

// BTL_tickBuffRings: the same growth, held until 0x2D and down to 70 % by 0x39; ring j turns by
// 8 - 20 j about x and (8 - 4 j) * 5 about z each frame. BTL_addBuffRingsEffect: every ring starts
// at 0x258 on both, the scale 4096 less (200 - radius) * 2.
constexpr std::int32_t RingGrowFrames = 0xF;
constexpr std::int32_t RingShrinkFrom = 0x2D;
constexpr std::int32_t RingShrinkPercent = 70;
constexpr std::int32_t RingStartAngle = 0x258;
constexpr std::int32_t RingTurnX = 8;
constexpr std::int32_t RingTurnXStep = 20;
constexpr std::int32_t RingTurnZStep = 4;
constexpr std::int32_t RingTurnZScale = 5;
constexpr std::int32_t RingBodyRadius = 0xC8;
constexpr std::int32_t RingBodyScale = 400;

constexpr std::array<StatusVisuals::Vec3, 4> RingColors{{
    {180, 20, 255},
    {100, 20, 255},
    {235, 150, 220},
    {180, 180, 180},
}};

// The two corners of a spark: (width, 0, reach) and (-width, 0, reach) turned by `rotation`.
StatusVisuals::SparkPair sparkOf(std::int32_t width, std::int32_t reach,
                                 const math::Vector& rotation) {
  const math::Matrix m = math::rotZYX(rotation);
  return {.first = vec3Of(math::toShort(math::apply(m, {.x = width, .y = 0, .z = reach}))),
          .second = vec3Of(math::toShort(math::apply(m, {.x = -width, .y = 0, .z = reach})))};
}

const Fighter* fighterAt(std::span<const Fighter> fighters, FighterId id) {
  return id < fighters.size() ? &fighters[id] : nullptr;
}

} // namespace

StatusVisuals::StatusVisuals(std::uint64_t seed) : ownRolls_(seed) {
  rollDirections();
}

// btl.c: loadBattleData: the item glows, the trails, the rings' sparks and the aura's sparks, in
// that order.
void StatusVisuals::rollDirections() {
  ruleset::Random& random = ownRolls_;
  // BTL_initializeItemParticleVelocities.
  for (std::size_t i = 0; i < ItemParticleCount; ++i) {
    const std::int32_t tilt = (rand15(random) % ItemTiltRange) - ItemTiltHalf;
    const std::int32_t turn = rand15(random) % One;
    const math::Matrix m = math::rotZYX({.x = tilt, .y = turn, .z = 0});
    const math::Vector out = math::toShort(math::apply(m, {.x = 0, .y = -ItemSpeed, .z = 0}));
    itemVelocities_.at(i) = vec3Of(out);
    const std::int32_t frames =
        math::lerp(ItemDragSlow, ItemDragFast, 0, ItemTiltMost, std::abs(tilt));
    itemDrag_.at(i) = {out.x / frames, out.z / frames};
  }
  // BTL_initializeBuffTrails: the directions drawn without repeating.
  std::array<std::int32_t, TrailDirections> pool{};
  for (std::int32_t i = 0; i < TrailDirections; ++i) {
    pool.at(static_cast<std::size_t>(i)) = i;
  }
  for (std::int32_t i = 0; i < TrailDirections; ++i) {
    const std::int32_t left = TrailDirections - i;
    const std::int32_t r = rand15(random) % left;
    const std::int32_t k = pool.at(static_cast<std::size_t>(r));
    std::shift_left(pool.begin() + r, pool.begin() + left, 1);
    const std::int32_t reach = ((rand15(random) % TrailSpread) + TrailBase) * TrailWiden / Percent;
    const std::int32_t turn = (k * One / TrailDirections) + (rand15(random) % TrailJitter);
    const math::Matrix m = math::rotZYX({.x = 0, .y = turn, .z = 0});
    const math::Vector a = math::toShort(math::apply(m, {.x = TrailHalfWidth, .y = 0, .z = reach}));
    const math::Vector b =
        math::toShort(math::apply(m, {.x = -TrailHalfWidth, .y = 0, .z = reach}));
    trailOffsets_.at(static_cast<std::size_t>(i)) = {a.x, a.z, b.x, b.z};
  }
  // BTL_initializeUnk2.
  for (SparkPair& spark : ringSparks_) {
    const std::int32_t width = (rand15(random) % RingSparkWidth) + RingSparkWidth;
    const std::int32_t reach = (rand15(random) % SparkReachRange) + SparkReach;
    const std::int32_t x = rand15(random);
    const std::int32_t y = rand15(random);
    const std::int32_t z = rand15(random);
    spark = sparkOf(width, reach, {.x = x, .y = y, .z = z});
  }
  // BTL_initializeFinisherAuraModel.
  for (SparkPair& spark : finisherSparks_) {
    const std::int32_t width = (rand15(random) % FinisherSparkWidth) + FinisherSparkWidth;
    const std::int32_t reach = (rand15(random) % SparkReachRange) + SparkReach;
    const std::int32_t x = (rand15(random) % One) * HalfCircle / FullCircle;
    const std::int32_t y = rand15(random);
    spark = sparkOf(width, reach, {.x = x, .y = y, .z = 0});
  }
}

std::optional<std::size_t> StatusVisuals::addFinisherAura(FighterId fighter,
                                                          std::int32_t duration) {
  for (std::size_t slot = 0; slot < finisherAuras_.size(); ++slot) {
    if (!finisherAuras_.at(slot)) {
      finisherAuras_.at(slot) =
          FinisherAura{.fighter = fighter, .frame = 0, .duration = duration, .spin = 0};
      return slot;
    }
  }
  return std::nullopt;
}

void StatusVisuals::removeFinisherAura(std::size_t slot) {
  if (slot < finisherAuras_.size()) {
    finisherAuras_.at(slot).reset();
  }
}

void StatusVisuals::addItemParticles(FighterId fighter, const Fighter& body) {
  const auto free = std::ranges::find_if(
      itemBursts_, [](const std::optional<ItemBurst>& slot) { return !slot.has_value(); });
  if (free == itemBursts_.end()) {
    return;
  }
  ItemBurst burst{
      .fighter = fighter, .origin = body.position, .timer = ItemBurstFrames, .particles = {}};
  for (std::size_t i = 0; i < ItemParticleCount; ++i) {
    burst.particles.at(i) = ItemParticle{.life = ItemLife,
                                         .position = {0, -body.combatant.height << SubUnitShift, 0},
                                         .velocity = itemVelocities_.at(i),
                                         .brightness = ItemBrightness};
  }
  *free = burst;
}

void StatusVisuals::addBuffDisk(FighterId fighter, const Fighter& body) {
  if (buffDisk_) {
    return;
  }
  buffDisk_ = BuffDisk{.fighter = fighter,
                       .frame = 0,
                       .rotationY = 0,
                       .scaleXZ = 0,
                       .scaleY = 0,
                       .targetXZ = body.combatant.radius * DiskWidth / BodyScale,
                       .targetY = body.combatant.height * DiskHeight / BodyScale};
  buffTrails_ = BuffTrails{.fighter = fighter, .frame = 0, .trails = {}};
}

void StatusVisuals::addBuffRings(FighterId fighter, const Fighter& body, CureKind kind) {
  if (buffRings_) {
    return;
  }
  BuffRings rings{.fighter = fighter,
                  .frame = 0,
                  .kind = static_cast<std::int32_t>(kind),
                  .scale = 0,
                  .target =
                      One - ((RingBodyRadius - body.combatant.radius) * RingBodyScale / BodyScale),
                  .rotations = {}};
  rings.rotations.fill({RingStartAngle, RingStartAngle});
  buffRings_ = rings;
}

StatusVisuals::Vec3 StatusVisuals::ringColor(std::int32_t kind) {
  return RingColors.at(static_cast<std::size_t>(std::clamp(kind, 0, 3)));
}

void StatusVisuals::tickEffects(ruleset::Random& random, std::span<const Fighter> fighters) {
  ++frames_;
  // BTL_tickFinisherAura; BTL_renderFinisherAura then rolls the outer object's turn.
  for (std::optional<FinisherAura>& aura : finisherAuras_) {
    if (!aura) {
      continue;
    }
    if (++aura->frame > aura->duration) {
      aura.reset();
      continue;
    }
    aura->spin = rand15(random);
  }
  // BTL_tickItemParticles.
  for (std::optional<ItemBurst>& burst : itemBursts_) {
    if (!burst) {
      continue;
    }
    if (--burst->timer < 0) {
      burst.reset();
      continue;
    }
    for (std::size_t i = 0; i < ItemParticleCount; ++i) {
      ItemParticle& particle = burst->particles.at(i);
      --particle.life;
      particle.velocity[0] -= itemDrag_.at(i)[0];
      particle.velocity[1] += ItemGravity;
      particle.velocity[2] -= itemDrag_.at(i)[1];
      for (std::size_t axis = 0; axis < 3; ++axis) {
        particle.position.at(axis) += particle.velocity.at(axis);
      }
      particle.brightness -= ItemBrightness / ItemLife;
    }
  }
  // BTL_tickBuffDisk.
  if (buffDisk_) {
    BuffDisk& disk = *buffDisk_;
    if (disk.frame >= BuffDiskFrames) {
      buffDisk_.reset();
      buffTrails_.reset();
    } else {
      if (disk.frame < DiskGrowFrames) {
        disk.scaleXZ = math::lerp(disk.targetXZ / 3, disk.targetXZ, 0, DiskGrowFrames, disk.frame);
        disk.scaleY = math::lerp(disk.targetY / 3, disk.targetY, 0, DiskGrowFrames, disk.frame);
      } else if (disk.frame < DiskShrinkFrom) {
        disk.scaleXZ = disk.targetXZ;
        disk.scaleY = disk.targetY;
      } else {
        disk.scaleXZ = math::lerp(disk.targetXZ, disk.targetXZ * DiskShrinkPercent / Percent,
                                  DiskShrinkFrom, BuffDiskFrames, disk.frame);
        disk.scaleY = math::lerp(disk.targetY, disk.targetY * DiskShrinkPercent / Percent,
                                 DiskShrinkFrom, BuffDiskFrames, disk.frame);
      }
      disk.rotationY += DiskTurn;
      ++disk.frame;
    }
  }
  // BTL_tickBuffTrails: a new streak at the fighter's feet where it stands now.
  if (buffTrails_) {
    BuffTrails& trails = *buffTrails_;
    const Fighter* body = fighterAt(fighters, trails.fighter);
    if (trails.frame >= 0 && trails.frame < BuffTrailSpawnFrames && body != nullptr) {
      const auto k = static_cast<std::size_t>(trails.frame % TrailDirections);
      const std::int32_t radius = body->combatant.radius;
      const std::array<std::int32_t, 4>& offset = trailOffsets_.at(k);
      // The battle's z is the console's the other way.
      const Vec3 a{body->position.x + (radius * offset[0] / TrailRadius), 0,
                   body->position.z - (radius * offset[1] / TrailRadius)};
      const Vec3 b{body->position.x + (radius * offset[2] / TrailRadius), 0,
                   body->position.z - (radius * offset[3] / TrailRadius)};
      trails.trails.at(k) = BuffTrail{.life = TrailLife, .corners = {a, b, a, b}};
    }
    for (BuffTrail& trail : trails.trails) {
      --trail.life;
      trail.corners[0][1] += TrailRiseTop;
      trail.corners[1][1] += TrailRiseTop;
      trail.corners[2][1] += TrailRiseBottom;
      trail.corners[3][1] += TrailRiseBottom;
    }
    ++trails.frame;
  }
  // BTL_tickBuffRings.
  if (buffRings_) {
    BuffRings& rings = *buffRings_;
    if (rings.frame >= BuffRingsFrames) {
      buffRings_.reset();
    } else {
      if (rings.frame < BuffRingsShownFrames) {
        if (rings.frame < RingGrowFrames) {
          rings.scale = math::lerp(rings.target / 3, rings.target, 0, RingGrowFrames, rings.frame);
        } else if (rings.frame < RingShrinkFrom) {
          rings.scale = rings.target;
        } else {
          rings.scale = math::lerp(rings.target, rings.target * RingShrinkPercent / Percent,
                                   RingShrinkFrom, BuffRingsShownFrames, rings.frame);
        }
        for (std::size_t j = 0; j < BuffRingCount; ++j) {
          const auto ring = static_cast<std::int32_t>(j);
          // The console keeps 16 bits of each angle.
          rings.rotations.at(j)[0] =
              math::toShort(rings.rotations.at(j)[0] - (RingTurnX - (ring * RingTurnXStep)));
          rings.rotations.at(j)[1] = math::toShort(
              rings.rotations.at(j)[1] + ((RingTurnX - (ring * RingTurnZStep)) * RingTurnZScale));
        }
      }
      ++rings.frame;
    }
  }
}

} // namespace dwe::game::battle
