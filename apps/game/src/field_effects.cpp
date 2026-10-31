#include "field_effects.hpp"

#include <dwe/core/original_math.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <utility>

#include "evolution_effects.hpp"

namespace dwe::app {
namespace {

// efe.c: createCloudFX: CLOUD_FX_DATA has 0x3C entries; tickCloudFX ends a cloud on its 14th frame.
constexpr std::size_t MaxClouds = 0x3C;
constexpr std::int32_t CloudFrames = 0xE;
// renderCloudFX: per frame, the column of the 32 x 32 picture (CLOUD_FX_U), the height
// (CLOUD_FX_Y, not the cloud's own), the size in 4096ths of the picture (CLOUD_FX_SCALE) and the
// shade (CLOUD_FX_COLOR, 0x80 for 1).
constexpr std::array<std::int32_t, CloudFrames> CloudU{0,  0,  32, 32, 32, 64, 64,
                                                       64, 64, 64, 96, 96, 96, 96};
constexpr std::array<std::int32_t, CloudFrames> CloudY{-50, -54, -58, -62, -66, -70, -74,
                                                       -78, -82, -86, -90, -93, -96, -99};
constexpr std::array<std::int32_t, CloudFrames> CloudScale{0x2000, 0x2EE0, 0x3840, 0x4000, 0x4000,
                                                           0x5000, 0x5000, 0x5000, 0x5000, 0x5400,
                                                           0x5800, 0x5C00, 0x6000, 0x6400};
constexpr std::array<std::int32_t, CloudFrames> CloudShade{
    0x80, 0x80, 0x80, 0x80, 0x79, 0x73, 0x6C, 0x66, 0x60, 0x59, 0x53, 0x4C, 0x46, 0x40};
// CLOUD_FX_SPRITE: 32 x 32 texels at v 0x80 of the page (row 0 of the image), its point the
// middle; the GsSPRITE's scale is in 4096ths.
constexpr float CloudSide = 32.0F;
constexpr float ScaleOne = 4096.0F;
// renderSprite sorts at distance >> 4 below 0x1000.
constexpr float SpriteFarDepth = 0x1000 * 16.0F;

// efe.c: createParticleFX: PARTICLE_FX_DATA has 4 entries.
constexpr std::size_t MaxSparks = 4;
constexpr std::int32_t SpreadRoll = 200;
constexpr std::int32_t SpreadHalf = 100;
constexpr std::int32_t FlashSizeRoll = 170;
constexpr std::int32_t FlashSizeFrom = 250;
constexpr std::int32_t HitSizeRoll = 200;
constexpr std::int32_t HitSizeFrom = 300;
// The frames the particles spread over, by `count` (fx->timer).
constexpr std::array<std::int32_t, 3> SparkTimers{0x13, 0x16, 0xE};
constexpr std::int32_t SparkEndExtra = 4;
// The colours by kind (fx->r, g, b).
constexpr std::array<glm::ivec3, 7> SparkColors{
    glm::ivec3{0xFF, 0x6E, 0x46}, glm::ivec3{0xFF, 0x6E, 0x46}, glm::ivec3{0xC8, 0xC8, 0x64},
    glm::ivec3{0xCC, 0x00, 0x88}, glm::ivec3{0x32, 0x32, 0xFF}, glm::ivec3{0x96, 0x96, 0x96},
    glm::ivec3{0xFF, 0x6E, 0x6E}};
// renderParticleFX: PARTICLE_FX_FLASH_COUNT and PARTICLE_FX_PARTICLE_COUNT by `count`.
constexpr std::array<std::int32_t, 3> FlashCounts{2, 3, 2};
constexpr std::array<std::int32_t, 3> ParticleCounts{0x19, 0x30, 0x14};
// The flash shows on the spark's second frame, 0x40 texels of SPOT256 at u 0x40, v 0xC0 of its
// page (row 64 of the image), mirrored into four; its size scales by VIEWPORT_DISTANCE * 8 over
// the depth, in 1024ths of the 63 texels.
constexpr std::int32_t FlashState = 2;
constexpr std::int32_t FlashScaleTimes = 8;
constexpr std::int32_t FlashTexels = 63;
constexpr std::int32_t FlashShift = 10;
constexpr glm::vec2 GlowImageSize{128.0F, 128.0F};
constexpr glm::vec2 FlashTexel{64.0F, 64.0F};
constexpr float FlashSide = 64.0F;
// The particles spread along sin (512 a turn) of 0 to 0x6E over the timer, >> 7.
constexpr std::int32_t SpreadAngle = 0x6E;
constexpr std::int32_t SpreadShift = 7;
constexpr std::int32_t DirectionShift = 9;
constexpr std::int32_t SparkParticleSize = 0x28;
// initializeParticleFX: 50 directions, each (0, 0, (rand() % 150 + 250) * 16) turned by three
// angles of (rand() & 0x7F) - 0x40 in 64ths of a turn.
constexpr std::int32_t LengthRoll = 150;
constexpr std::int32_t LengthFrom = 0xFA;
constexpr std::int32_t LengthTimes = 0x10;
constexpr std::uint32_t AngleMask = 0x7F;
constexpr std::int32_t AngleHalf = 0x40;
constexpr std::int32_t AngleDivisor = 64;

// effect_common.c: renderFXParticle: a 16 x 16 picture at u 0, v 0xA0 of the page (row 32 of the
// image), `size` world units wide, semi-transparent additive and shaded; addFXPrim sorts it 0x37
// ordering-table entries (of 16 units) ahead of its depth, and only between entries 0x20 and
// 0x1000.
constexpr glm::vec2 EffectImageSize{256.0F, 48.0F};
constexpr glm::vec2 ParticleTexel{0.0F, 32.0F};
constexpr float ParticleTexels = 15.0F;
constexpr std::int32_t PrimPull = 0x37;
constexpr std::int32_t DepthUnit = 16;
constexpr std::int32_t OrderMin = 0x20;
constexpr std::int32_t OrderMax = 0x1000;
constexpr float ShadeOne = 128.0F;

// particle.c: tickHealingParticles: the effect lasts 44 frames; the spiral drops one particle a
// frame for 20 frames, a turn of 640 4096ths a frame, each lit for 17 frames and sized 15 to 84
// (rolled every frame); the ring drops four a frame for 39 frames, each lit for 5, on a circle
// 150 wider than the spiral, scattered by up to 30.
constexpr std::int32_t HealingFrames = 44;
constexpr std::int32_t SpiralFrames = 20;
constexpr std::int32_t SpiralTurn = 640;
constexpr std::int32_t TurnMask4096 = 0xFFF;
constexpr std::int32_t SpiralLit = 17;
constexpr std::int32_t SpiralSizeRoll = 70;
constexpr std::int32_t SpiralSizeFrom = 15;
constexpr std::int32_t RingFrames = 39;
constexpr std::int32_t RingSlots = 5;
constexpr std::int32_t RingsPerFrame = 4;
constexpr std::int32_t RingLit = 5;
constexpr std::int32_t RingWider = 150;
constexpr std::int32_t RingScatter = 60;
constexpr std::int32_t RingScatterHalf = 30;
constexpr std::int32_t RingTurn = 512;
constexpr std::int32_t RingTurnFrames = 13;
constexpr std::int32_t RingParticleSize = 24;
constexpr std::int32_t FixedShift = 12;
// addHealingParticleEffect: the spiral starts 70 over the species' height, its radius 12 / 10 of
// the species'.
constexpr std::int32_t HealingLift = 70;
constexpr std::int32_t RadiusTimes = 12;
constexpr std::int32_t RadiusOver = 10;
// renderHealingParticles: PARTICLE_COLOR1 and PARTICLE_COLOR2 by the spiral's counter (the second
// at half the size), PARTICLE_COLOR3 by the ring's.
constexpr std::array<glm::ivec3, 18> SpiralColors{
    glm::ivec3{0x02, 0x04, 0x01}, glm::ivec3{0x04, 0x06, 0x02}, glm::ivec3{0x07, 0x09, 0x03},
    glm::ivec3{0x0A, 0x0B, 0x03}, glm::ivec3{0x0C, 0x0D, 0x04}, glm::ivec3{0x0F, 0x10, 0x04},
    glm::ivec3{0x12, 0x12, 0x05}, glm::ivec3{0x15, 0x15, 0x06}, glm::ivec3{0x17, 0x17, 0x06},
    glm::ivec3{0x1A, 0x19, 0x07}, glm::ivec3{0x1D, 0x1C, 0x07}, glm::ivec3{0x1F, 0x1E, 0x08},
    glm::ivec3{0x22, 0x21, 0x09}, glm::ivec3{0x25, 0x23, 0x09}, glm::ivec3{0x27, 0x25, 0x0A},
    glm::ivec3{0x2A, 0x28, 0x0A}, glm::ivec3{0x2D, 0x2A, 0x0B}, glm::ivec3{0x30, 0x2D, 0x0C}};
constexpr std::array<glm::ivec3, 18> SpiralCoreColors{
    glm::ivec3{0x04, 0x09, 0x03}, glm::ivec3{0x0A, 0x0E, 0x05}, glm::ivec3{0x10, 0x13, 0x06},
    glm::ivec3{0x16, 0x18, 0x07}, glm::ivec3{0x1B, 0x1D, 0x09}, glm::ivec3{0x21, 0x23, 0x0A},
    glm::ivec3{0x27, 0x28, 0x0B}, glm::ivec3{0x2D, 0x2D, 0x0D}, glm::ivec3{0x33, 0x32, 0x0E},
    glm::ivec3{0x39, 0x37, 0x0F}, glm::ivec3{0x3F, 0x3D, 0x10}, glm::ivec3{0x44, 0x42, 0x12},
    glm::ivec3{0x4A, 0x47, 0x13}, glm::ivec3{0x50, 0x4C, 0x14}, glm::ivec3{0x56, 0x51, 0x16},
    glm::ivec3{0x5C, 0x57, 0x17}, glm::ivec3{0x62, 0x5C, 0x18}, glm::ivec3{0x68, 0x61, 0x1A}};
constexpr std::array<glm::ivec3, 6> RingColors{
    glm::ivec3{0x47, 0x51, 0x00}, glm::ivec3{0x60, 0x6B, 0x19}, glm::ivec3{0x7D, 0x86, 0x3D},
    glm::ivec3{0xB3, 0xBB, 0x7A}, glm::ivec3{0xD7, 0xDC, 0xB7}, glm::ivec3{0xFF, 0xFF, 0xFF}};

// butterfly.c: renderButterfly: a turn of 150 around the point every 0x3C frames, three turns up
// and down by 50 over them, 100 up; the 16 x 16 picture at v 0xB0 of the page (row 48 of the
// image) flaps between u 0 and 16 every 2 frames, 0x5400 4096ths of it wide, drawn between depths
// 0x200 and 0x10000.
constexpr std::int32_t ButterflyCycle = 0x3C;
constexpr std::int32_t ButterflyTurn = 0x200;
constexpr std::int32_t ButterflyBobTurns = 0x600;
constexpr std::int32_t ButterflyReach = 150;
constexpr std::int32_t ButterflyBob = 50;
constexpr std::int32_t ButterflyLift = 100;
constexpr glm::vec2 ButterflyImageSize{256.0F, 64.0F};
constexpr float ButterflyV = 48.0F;
constexpr float ButterflySide = 16.0F;
constexpr std::int32_t ButterflyScale = 0x5400;
constexpr float ButterflyNear = 0x200;
constexpr float ButterflyFar = 0x10000;
constexpr std::int32_t FixedOne = 4096;

// The original's rand(): 15 bits.
constexpr std::uint32_t RandMask = 0x7FFF;

// math.c _sin and _cos: 512 a turn.
constexpr std::int32_t TurnMask512 = 0x1FF;
constexpr std::int32_t Turn512To4096 = 8;
std::int32_t sin512(std::int32_t angle) {
  return core::original::rsin((angle & TurnMask512) * Turn512To4096);
}
std::int32_t cos512(std::int32_t angle) {
  return core::original::rcos((angle & TurnMask512) * Turn512To4096);
}

// The world's axes are the original's with y and z the other way.
glm::vec3 worldOf(OriginalPoint p) {
  return {static_cast<float>(p.x), static_cast<float>(-p.y), static_cast<float>(-p.z)};
}

glm::vec4 shadeOf(glm::ivec3 color) {
  return glm::vec4{glm::vec3{color} / ShadeOne, 1.0F};
}

struct Sorted {
  float depth = 0.0F;
  ScenePrim prim;
};

// A picture standing up at a clip point, `half` original pixels to each side, the texels from
// `texel` to `texel + side` of an image of `imageSize`.
ScenePrim upright(glm::vec4 clip, glm::vec2 half, glm::vec2 texel, glm::vec2 side,
                  glm::vec2 imageSize, rhi::TextureId texture, glm::vec4 color) {
  const glm::vec2 from = texel / imageSize;
  const glm::vec2 to = (texel + side) / imageSize;
  ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                 .color = color,
                 .texture = texture,
                 .blend = ScenePrimBlend::Additive,
                 .stpMask = true};
  prim.corners[0] = {.clip = clip, .offset = {-half.x, -half.y}, .uv = from};
  prim.corners[1] = {.clip = clip, .offset = {half.x, -half.y}, .uv = {to.x, from.y}};
  prim.corners[2] = {.clip = clip, .offset = {-half.x, half.y}, .uv = {from.x, to.y}};
  prim.corners[3] = {.clip = clip, .offset = {half.x, half.y}, .uv = to};
  return prim;
}

// effect_common.c: renderFXParticle.
void addParticle(std::vector<Sorted>& out, const FieldEffectView& view, OriginalPoint at,
                 std::int32_t size, glm::ivec3 color) {
  if (!view.images.particles) {
    return;
  }
  const glm::vec3 world = worldOf(at);
  const float depth = view.frame.depth(world);
  const std::int32_t order = (static_cast<std::int32_t>(depth) / DepthUnit) - PrimPull;
  if (depth <= 0.0F || order <= OrderMin || order >= OrderMax) {
    return;
  }
  const float side = static_cast<float>(size) * view.focalLength / depth;
  const glm::vec4 clip = view.frame.clipNearer(world, static_cast<float>(PrimPull * DepthUnit));
  out.push_back(
      {.depth = depth - static_cast<float>(PrimPull * DepthUnit),
       .prim = upright(clip, glm::vec2{side / 2.0F}, ParticleTexel, glm::vec2{ParticleTexels},
                       EffectImageSize, *view.images.particles, shadeOf(color))});
}

} // namespace

FieldEffects::FieldEffects(std::uint64_t seed) : random_(seed) {
  // initializeParticleFX: RotMatrixZYX of the three angles applied to (0, 0, length).
  for (glm::ivec3& direction : directions_) {
    std::array<float, 3> angles{};
    for (float& angle : angles) {
      const auto turn64 =
          static_cast<std::int32_t>(static_cast<std::uint32_t>(rand()) & AngleMask) - AngleHalf;
      angle = static_cast<float>((turn64 * FixedOne) / AngleDivisor) * 2.0F *
              std::numbers::pi_v<float> / static_cast<float>(FixedOne);
    }
    const float length = static_cast<float>(((rand() % LengthRoll) + LengthFrom) * LengthTimes);
    // (0, 0, l) turned about z (no change), then y, then x.
    const glm::vec3 turnedY{length * std::sin(angles[1]), 0.0F, length * std::cos(angles[1])};
    const glm::vec3 turned{turnedY.x, -turnedY.z * std::sin(angles[0]),
                           turnedY.z * std::cos(angles[0])};
    direction = glm::ivec3{glm::round(turned)};
  }
}

std::int32_t FieldEffects::rand() {
  return static_cast<std::int32_t>(random_.next() & RandMask);
}

void FieldEffects::addCloud(std::int32_t x, std::int32_t z, Birth birth) {
  if (clouds_.size() >= MaxClouds) {
    return;
  }
  // tickCloudFX the same frame when the maker is one of the objects.
  clouds_.push_back({.x = x, .z = z, .state = birth == Birth::InObjects ? 1 : 0});
}

void FieldEffects::addSpark(std::int32_t kind, std::int32_t mode, OriginalPoint position,
                            std::int32_t lifetime, Birth birth) {
  if (sparks_.size() >= MaxSparks) {
    return;
  }
  FieldSpark spark{.position = position, .mode = mode};
  const auto spread = [&] { return (rand() % SpreadRoll) - SpreadHalf; };
  for (std::size_t flash = 0; flash < 2; ++flash) {
    spark.spread[0][flash] = spread();
    spark.spread[1][flash] = spread();
    spark.spread[2][flash] = spread();
    spark.spread[3][flash] = (rand() % FlashSizeRoll) + FlashSizeFrom;
  }
  if (mode < 0 || mode >= static_cast<std::int32_t>(SparkTimers.size())) {
    return;
  }
  spark.timer = SparkTimers.at(static_cast<std::size_t>(mode));
  if (mode == 1) {
    spark.spread[0][2] = spread();
    spark.spread[1][2] = spread();
    spark.spread[2][2] = spread();
    spark.spread[3][2] = (rand() % HitSizeRoll) + HitSizeFrom;
  }
  if (kind < 0 || kind >= static_cast<std::int32_t>(SparkColors.size())) {
    return;
  }
  spark.color = SparkColors.at(static_cast<std::size_t>(kind));
  spark.endFrame = lifetime + SparkEndExtra;
  if (birth == Birth::InObjects) {
    // tickParticleFX the same frame.
    ++spark.state;
    --spark.endFrame;
  }
  sparks_.push_back(spark);
}

bool FieldEffects::addHealing(OriginalPoint feet, std::int32_t height, std::int32_t radius,
                              bool ring) {
  if (healing_) {
    return false;
  }
  FieldHealing healing{.frame = 0,
                       .origin = {feet.x, -height - HealingLift, feet.z},
                       .radius = (radius * RadiusTimes) / RadiusOver,
                       .ring = ring};
  healing_ = healing;
  return true;
}

void FieldEffects::setButterfly() {
  if (!butterfly_) {
    butterfly_ = FieldButterfly{};
  }
}

void FieldEffects::clear() {
  clouds_.clear();
  sparks_.clear();
  healing_.reset();
  butterfly_.reset();
  pending_ = 0.0;
}

void FieldEffects::advance(double seconds, double frameSeconds, bool inMenu) {
  if (frameSeconds <= 0.0) {
    return;
  }
  if (empty()) {
    pending_ = 0.0;
    return;
  }
  pending_ += seconds;
  while (pending_ >= frameSeconds) {
    pending_ -= frameSeconds;
    tickFrame(inMenu);
  }
}

void FieldEffects::tickFrame(bool inMenu) {
  // tickCloudFX.
  for (FieldCloud& cloud : clouds_) {
    ++cloud.state;
  }
  std::erase_if(clouds_, [](const FieldCloud& cloud) { return cloud.state >= CloudFrames; });
  // tickParticleFX: without a fighter, the sparks only count their frames.
  for (FieldSpark& spark : sparks_) {
    ++spark.state;
    --spark.endFrame;
  }
  std::erase_if(sparks_, [](const FieldSpark& spark) {
    return spark.state >= spark.timer && spark.endFrame < 0;
  });
  if (healing_) {
    tickHealing(*healing_);
    if (healing_->frame >= HealingFrames) {
      healing_.reset();
    }
  }
  // tickButterfly.
  if (butterfly_ && !inMenu) {
    ++butterfly_->frame;
  }
}

void FieldEffects::tickHealing(FieldHealing& healing) {
  ++healing.frame;
  if (healing.frame >= HealingFrames) {
    return;
  }
  const OriginalPoint& origin = healing.origin;
  if (healing.frame < SpiralFrames) {
    HealingMote& mote = healing.spiral.at(static_cast<std::size_t>(healing.frame));
    mote.counter = SpiralLit;
    // RotMatrixZYX(0, frame * 640, 0) applied to (radius, 0, 0).
    const std::int32_t turn = (healing.frame * SpiralTurn) & TurnMask4096;
    mote.position.x = ((healing.radius * core::original::rcos(turn)) >> FixedShift) + origin.x;
    mote.position.z = (-(healing.radius * core::original::rsin(turn)) >> FixedShift) + origin.z;
    mote.position.y = origin.y - originalLerp(0, origin.y, 0, SpiralFrames, healing.frame);
  }
  for (HealingMote& mote : healing.spiral) {
    --mote.counter;
    mote.size = (rand() % SpiralSizeRoll) + SpiralSizeFrom;
  }
  if (healing.frame < RingFrames) {
    for (std::int32_t i = 0; i < RingsPerFrame; ++i) {
      HealingMote& mote =
          healing.rings.at(static_cast<std::size_t>((healing.frame % RingSlots) + (i * RingSlots)));
      std::int32_t angle = originalLerp(0, RingTurn, 0, RingTurnFrames, healing.frame);
      // 512 / (i * 13 / 4): the first of the four divides by 0, which the console's divide
      // answers with -1.
      const std::int32_t step = (i * RingTurnFrames) / RingsPerFrame;
      angle += step != 0 ? RingTurn / step : -1;
      mote.counter = RingLit;
      const std::int32_t reach = healing.radius + RingWider;
      mote.position.x = origin.x + ((rand() % RingScatter) - RingScatterHalf) -
                        ((reach * sin512(angle)) >> FixedShift);
      mote.position.z = origin.z + ((rand() % RingScatter) - RingScatterHalf) +
                        ((reach * cos512(angle)) >> FixedShift);
      mote.position.y = originalLerp((rand() % RingScatter) - RingScatterHalf + origin.y, 0, 0,
                                     RingFrames, healing.frame);
    }
  }
  for (HealingMote& mote : healing.rings) {
    --mote.counter;
  }
}

std::vector<ScenePrim> FieldEffects::draw(const FieldEffectView& view) const {
  std::vector<Sorted> sorted;
  const ScenePrimFrame& frame = view.frame;

  // renderCloudFX.
  if (view.images.clouds) {
    for (const FieldCloud& cloud : clouds_) {
      const auto state = static_cast<std::size_t>(cloud.state);
      const glm::vec3 world = worldOf({cloud.x, CloudY.at(state), cloud.z});
      const float depth = frame.depth(world);
      if (depth <= 0.0F || depth >= SpriteFarDepth) {
        continue;
      }
      const float side = CloudSide * static_cast<float>(CloudScale.at(state)) / ScaleOne *
                         view.focalLength / depth;
      const auto u = static_cast<float>(CloudU.at(state));
      sorted.push_back(
          {.depth = depth,
           .prim = upright(
               frame.clip(world), glm::vec2{side / 2.0F}, {u, 0.0F}, glm::vec2{CloudSide},
               EffectImageSize, *view.images.clouds,
               glm::vec4{glm::vec3{static_cast<float>(CloudShade.at(state)) / ShadeOne}, 1.0F})});
    }
  }

  // renderParticleFX.
  for (const FieldSpark& spark : sparks_) {
    const auto mode = static_cast<std::size_t>(spark.mode);
    if (spark.state == FlashState && view.images.glow) {
      for (std::size_t i = 0; i < static_cast<std::size_t>(FlashCounts.at(mode)); ++i) {
        const glm::vec3 world =
            worldOf(spark.position + OriginalPoint{spark.spread[0].at(i), spark.spread[1].at(i),
                                                   spark.spread[2].at(i)});
        const float depth = frame.depth(world);
        const std::int32_t order = static_cast<std::int32_t>(depth) / DepthUnit;
        if (depth <= 0.0F || order <= OrderMin || order >= OrderMax) {
          continue;
        }
        const auto scale = static_cast<std::int64_t>(
            (static_cast<std::int64_t>(spark.spread[3].at(i)) *
             static_cast<std::int64_t>(view.focalLength * FlashScaleTimes)) /
            static_cast<std::int64_t>(depth));
        const std::int64_t reach = (scale * FlashTexels) >> FlashShift;
        if (reach <= 0) {
          continue;
        }
        const SceneGlow glow{.centre = frame.clip(world),
                             .half = static_cast<float>(reach + 1),
                             .color = glm::vec3{spark.color} / ShadeOne,
                             .texture = *view.images.glow,
                             .texel = FlashTexel,
                             .side = glm::vec2{FlashSide},
                             .imageSize = GlowImageSize};
        for (const ScenePrim& quarter : glowQuarters(glow)) {
          sorted.push_back({.depth = depth, .prim = quarter});
        }
      }
    }
    if (spark.state >= spark.timer) {
      continue;
    }
    const glm::ivec3 color{originalLerp(spark.color.r, 0, 0, spark.timer, spark.state),
                           originalLerp(spark.color.g, 0, 0, spark.timer, spark.state),
                           originalLerp(spark.color.b, 0, 0, spark.timer, spark.state)};
    const std::int32_t spread =
        sin512(originalLerp(0, SpreadAngle, 0, spark.timer, spark.state)) >> SpreadShift;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ParticleCounts.at(mode)); ++i) {
      const glm::ivec3& direction = directions_.at(i);
      addParticle(sorted, view,
                  spark.position + OriginalPoint{(spread * direction.x) >> DirectionShift,
                                                 (spread * direction.y) >> DirectionShift,
                                                 (spread * direction.z) >> DirectionShift},
                  SparkParticleSize, color);
    }
  }

  // renderHealingParticles.
  if (healing_) {
    for (const HealingMote& mote : healing_->spiral) {
      if (mote.counter < 0) {
        continue;
      }
      const auto shade = static_cast<std::size_t>(mote.counter);
      addParticle(sorted, view, mote.position, mote.size, SpiralColors.at(shade));
      addParticle(sorted, view, mote.position, mote.size >> 1, SpiralCoreColors.at(shade));
    }
    if (healing_->ring) {
      for (const HealingMote& mote : healing_->rings) {
        if (mote.counter >= 0) {
          addParticle(sorted, view, mote.position, RingParticleSize,
                      RingColors.at(static_cast<std::size_t>(mote.counter)));
        }
      }
    }
  }

  // renderButterfly.
  if (butterfly_ && view.butterflyCentre && view.images.butterfly) {
    const std::int32_t frameNumber = butterfly_->frame;
    std::int32_t angle = originalLerp(0, ButterflyTurn, 0, ButterflyCycle, frameNumber);
    const std::int32_t dx = (sin512(angle) * ButterflyReach) / FixedOne;
    const std::int32_t dz = (cos512(angle) * ButterflyReach) / FixedOne;
    angle = originalLerp(0, ButterflyBobTurns, 0, ButterflyCycle, frameNumber);
    const std::int32_t dy = ((sin512(angle) * ButterflyBob) / FixedOne) - ButterflyLift;
    const glm::vec3 world = *view.butterflyCentre + worldOf({dx, dy, dz});
    const float depth = frame.depth(world);
    if (depth > ButterflyNear && depth < ButterflyFar) {
      const float side =
          ButterflySide * static_cast<float>(ButterflyScale) / ScaleOne * view.focalLength / depth;
      const float u = ((frameNumber >> 1) & 1) != 0 ? ButterflySide : 0.0F;
      sorted.push_back({.depth = depth,
                        .prim = upright(frame.clip(world), glm::vec2{side / 2.0F}, {u, ButterflyV},
                                        glm::vec2{ButterflySide}, ButterflyImageSize,
                                        *view.images.butterfly, glm::vec4{1.0F})});
    }
  }

  // The ordering table: far first.
  std::ranges::stable_sort(sorted, std::ranges::greater{}, &Sorted::depth);
  std::vector<ScenePrim> out;
  out.reserve(sorted.size());
  for (Sorted& entry : sorted) {
    out.push_back(std::move(entry.prim));
  }
  return out;
}

namespace {

// createNinjamonEffect: randomLimit(10) + 12 across, + 3 down a frame.
constexpr std::int32_t SmokeStepX = 12;
constexpr std::int32_t SmokeStepY = 3;
// renderNinjamonEffect: past x 0xA0 an object starts over; 0x3C frames.
constexpr std::int32_t SmokeEdge = 0xA0;
constexpr std::int32_t SmokeFrames = 0x3C;

} // namespace

void ObjectSmoke::start(std::span<const glm::ivec2> places) {
  counter_ = 0;
  stored_.fill(glm::ivec2{0});
  std::ranges::copy(places.first(std::min(places.size(), Objects)), stored_.begin());
  for (glm::ivec2& step : steps_) {
    step.x = roll(SmokeStepX);
    step.y = roll(SmokeStepY);
  }
  active_ = true;
}

bool ObjectSmoke::frame(std::span<glm::ivec2> places) {
  if (!active_) {
    return false;
  }
  const std::size_t count = std::min(places.size(), Objects);
  for (std::size_t i = 0; i < count; ++i) {
    places[i] += steps_.at(i);
    if (places[i].x > SmokeEdge) {
      places[i] = stored_.at(i);
      steps_.at(i) = {roll(SmokeStepX), roll(SmokeStepY)};
    }
  }
  ++counter_;
  if (counter_ >= SmokeFrames) {
    active_ = false;
    return true;
  }
  return false;
}

} // namespace dwe::app
