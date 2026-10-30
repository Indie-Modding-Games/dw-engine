#include "death_effects.hpp"

#include <dwe/core/math.hpp>
#include <dwe/core/original_math.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace dwe::app {
namespace {

// murd.c: MURD_renderIris and dooa.c: DOOA_renderIrisWindow: the iris closes from 200 to 0 (in
// 128ths of the glow's scale); its ring is SPOT256's top left 64 x 64 quarter (u 0, v 0x80 of its
// page) mirrored into four, its rectangles that page's texel (0, 0x80), white; both subtracted
// (getTPage(1, 2, ...)) at 0x80. The rectangles' edge overlaps the ring by lerp(4, 1, 0x1860, 0).
constexpr std::int32_t IrisOpen = 200;
constexpr std::int32_t IrisScaleShift = 12;
constexpr std::int32_t IrisScaleDivisor = 128;
constexpr std::int32_t IrisEdgeShift = 8;
constexpr std::int32_t IrisCoversShift = 4;
constexpr std::int32_t IrisThickFrom = 4;
constexpr std::int32_t IrisThickTo = 1;
constexpr std::int32_t IrisThickRadius = 0x1860;
constexpr float IrisImageSide = 128.0F;
constexpr float IrisQuarter = 64.0F;
constexpr std::int32_t IrisTexels = 63;
constexpr std::int32_t IrisSizeShift = 10;
constexpr float IrisWhiteTexels = 3.0F;
// Far past any screen edge, in original pixels.
constexpr float ScreenBeyond = 4096.0F;

// dooa.c: DOOA_SHARD_WAVE_SCHEDULE: from frame 0x69 to 0x8C, PARTNER_WIREFRAME_TOTAL on each frame;
// each new value breaks a wave of faces.
constexpr std::int32_t WaveFrom = 0x69;
constexpr std::array<std::int8_t, 36> WaveSchedule{15, 15, 15, 15, 15, 14, 14, 14, 14, 13, 13, 13,
                                                   12, 12, 12, 11, 11, 11, 10, 10, 10, 9,  9,  8,
                                                   8,  7,  7,  6,  6,  5,  5,  4,  3,  2,  1,  0};
constexpr std::int32_t WaveUntil = 0x8D;
// DOOA_spawnShardWave: the bones from 2 break; DOOA_spawnBoneShards keeps a face of the wave when
// rand() & 3 is not 0.
constexpr std::uint32_t FirstShardBone = 2;
constexpr std::int32_t ShardKeepMask = 3;
// The partner is hidden at 0xD1 and the body is whole again.
constexpr std::int32_t BodyBackFrame = 0xD1;
constexpr std::int32_t FullShare = 16;

// DOOA_tickDissolve phase 0x65 (0xD2 to 0x135): a flash a frame comes down from 100 over the
// partner's head to the ground, half 0 of the glow, 8 frames, scale 0 to 0x14, a grey of 60 plus
// up to 99 a channel fading to 0x14, swaying 10 units to each side along 3 turns of _sin.
constexpr std::int32_t LightFrom = 0xD1;
constexpr std::int32_t LightTo = 0x135;
constexpr std::int32_t LightAbove = 100;
constexpr EvolutionFlash LightFlash{.mode = 0,
                                    .fixedDepth = -1,
                                    .length = 8,
                                    .scaleFrom = 0,
                                    .scaleTo = 0x14,
                                    .colorFrom = glm::ivec3{0},
                                    .colorTo = glm::ivec3{0x14}};
constexpr std::int32_t LightGreyMin = 60;
constexpr std::int32_t LightGreyRoll = 100;
constexpr std::int32_t LightSwayTurns = 0x600;
constexpr std::int32_t LightSway = 10;
// Phase 3 starts: the shards flicker (DOOA_toggleShardFlicker).
constexpr std::int32_t FlickerFrame = 0x135;

// DOOA_updateShards: the flicker's colours, 60 plus up to 99; the shards' own shade 0x50.
constexpr std::int32_t ShardGreyMin = 60;
constexpr std::int32_t ShardGreyRoll = 100;
constexpr std::int32_t ShardShade = 0x50;
constexpr std::int32_t MaxFallSpeed = 0x400;
constexpr std::int32_t SpinRoll = 0x155;
constexpr std::int32_t SpinMin = 0xE3;
constexpr std::int32_t SpinCheck = 80;
constexpr std::int32_t SpinStep = 100;
constexpr std::int32_t RadiusNear = 0x64000;
constexpr std::int32_t RadiusFar = 0x190000;
constexpr std::int32_t DropRoll = 0x226;
constexpr std::int32_t RiseShift = 3;
constexpr std::int32_t ShrinkDelayRoll = 20;
constexpr std::int32_t ShrinkStep = 10;
constexpr std::int32_t EggCentreAbove = 100;
constexpr std::int32_t One = 4096;

// DOOA_tickRebirth: the shards rise at 10 and close in at 100; the egg fills in from 100 to 132
// (16 from 130), the shards go at 132; flashes on the egg and sparks until 152; the egg breaks
// from frame 203, a piece on the entries of DOOA_SPARKLE_BONE_IDS that are not negative.
constexpr std::int32_t RiseFrame = 10;
constexpr std::int32_t CloseFrame = 100;
constexpr std::int32_t EggFillFrom = 100;
constexpr std::int32_t EggFillTo = 132;
constexpr std::int32_t EggFullFrom = 130;
constexpr std::int32_t HatchFrame = 132;
constexpr std::int32_t SwapFrame = 152;
constexpr std::int32_t StreaksUntil = 147;
constexpr std::int32_t StreaksFrom = 5;
constexpr std::int32_t StreaksFadeTo = 150;
constexpr std::int32_t BreakFrom = 203;
constexpr std::array<std::int8_t, 47> PieceTable{
    -1, -1, -1, -1, 0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    2,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 3,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,  5,  -1};
constexpr std::int32_t PiecesPerEgg = 6;
// The egg's flash: DOOA_FLASH_POSITION (100 over the ground), half 0, 4 frames, scale 0 to
// lerp(20, 10) * 25 * 4096 / 1000, grey lerp(120, 10) plus up to 50/120 of it, fading to a sixth.
constexpr std::int32_t EggFlashAbove = 100;
constexpr std::int32_t EggFlashScaleFrom = 20;
constexpr std::int32_t EggFlashScaleTo = 10;
constexpr std::int32_t EggFlashScaleTimes = 25;
constexpr std::int32_t EggFlashScaleOver = 1000;
constexpr std::int32_t EggFlashGreyFrom = 120;
constexpr std::int32_t EggFlashGreyTo = 10;
constexpr std::int32_t EggFlashGreySpread = 50;
constexpr std::int32_t EggFlashFade = 6;
constexpr std::int32_t EggFlashLength = 4;
constexpr std::size_t MaxFlashes = 12;

// doo2.c: a broken piece lives 41 frames, its shade from 128 down to 54; its faces move by their
// normal over 240 (speed 80 times 3) and fall faster each frame; at most 6 pieces.
constexpr std::int32_t PieceLife = 41;
constexpr std::int32_t PieceShadeSpan = 74;
constexpr std::int32_t PieceShadeSteps = 40;
constexpr std::int32_t PieceShadeMin = 54;
constexpr std::int32_t PieceSpeed = 80 * 3;
constexpr std::size_t MaxPieces = 6;

// DOO2_renderSparkStreak: from 100 over the partner, a ray of 400 to 500 tilted up by rand() %
// customRandom(-170, 1024), its side corner up to 50 off each way, grey 10 to 137, from DEFEFF
// (palette 1) at (95, 32), (95, 39), (48, 32), added.
constexpr std::int32_t StreakAbove = 100;
constexpr std::int32_t StreakTiltFrom = -170;
constexpr std::int32_t StreakTiltTo = 1024;
constexpr std::int32_t StreakFrom = 400;
constexpr std::int32_t StreakTo = 500;
constexpr std::int32_t StreakJitter = 50;
constexpr std::int32_t StreakGreyRoll = 128;
constexpr std::int32_t StreakGreyMin = 10;
constexpr glm::vec2 SparkImageSize{256.0F, 48.0F};
constexpr std::array<glm::vec2, 3> SparkTexels{glm::vec2{95.0F, 32.0F}, glm::vec2{95.0F, 39.0F},
                                               glm::vec2{48.0F, 32.0F}};

// model.c: renderWireframed's grey, WIREFRAME_COLOR_MIN + rand() % (MAX - MIN).
constexpr std::int32_t WireGreyMin = 0x37;
constexpr std::int32_t WireGreyMax = 0xFF;

constexpr float TexturedOne = 128.0F;
constexpr float TurnRadians = 2.0F * 3.14159265358979323846F;

// math.c _sin: 512 a turn, 4096 for 1.
std::int32_t sin512(std::int32_t angle) {
  constexpr std::int32_t TurnMask512 = 0x1FF;
  constexpr std::int32_t Turn512To4096 = 8;
  return core::original::rsin((angle & TurnMask512) * Turn512To4096);
}

glm::ivec3 toOriginal(glm::vec3 world) {
  const glm::vec3 original = core::fromOriginalAxes(world);
  return {static_cast<std::int32_t>(std::lround(original.x)),
          static_cast<std::int32_t>(std::lround(original.y)),
          static_cast<std::int32_t>(std::lround(original.z))};
}

glm::vec3 toWorld(glm::ivec3 original) {
  return core::fromOriginalAxes(glm::vec3{original});
}

glm::vec3 toWorld(glm::vec3 original) {
  return core::fromOriginalAxes(original);
}

float meanDepth(const ScenePrimFrame& frame, std::span<const glm::vec3> points) {
  float sum = 0.0F;
  for (const glm::vec3& point : points) {
    sum += frame.depth(point);
  }
  return sum / static_cast<float>(points.size());
}

ScenePrim facePrim(const ScenePrimFrame& frame, std::uint8_t count,
                   const std::array<glm::vec3, 4>& corners, const std::array<glm::vec2, 4>& uvs,
                   rhi::TextureId texture, glm::vec4 color) {
  ScenePrim prim{.shape = count == 4 ? ScenePrim::Shape::Quad : ScenePrim::Shape::Triangle,
                 .color = color,
                 .texture = texture,
                 .blend = ScenePrimBlend::Alpha};
  for (std::size_t i = 0; i < count; ++i) {
    prim.corners.at(i) = {.clip = frame.clip(corners.at(i)), .uv = uvs.at(i)};
  }
  return prim;
}

} // namespace

std::int32_t irisRadius(std::int32_t start, std::int32_t end, std::int32_t frame) {
  const std::int32_t at = std::min(frame, end);
  return (originalLerp(IrisOpen, 0, start, end, at) << IrisScaleShift) / IrisScaleDivisor;
}

bool irisWithinPartner(std::int32_t radius, float partnerPixels) {
  return static_cast<float>(radius >> IrisCoversShift) <= partnerPixels;
}

std::vector<ScenePrim> irisPrims(glm::vec4 centre, std::int32_t radius, rhi::TextureId spot) {
  std::vector<ScenePrim> prims;
  const auto quad = [&](glm::vec2 from, glm::vec2 to, glm::vec2 uvFrom, glm::vec2 uvTo) {
    ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                   .color = glm::vec4{1.0F},
                   .texture = spot,
                   .blend = ScenePrimBlend::Subtractive};
    prim.corners[0] = {.clip = centre, .offset = from, .uv = uvFrom};
    prim.corners[1] = {.clip = centre, .offset = {to.x, from.y}, .uv = {uvTo.x, uvFrom.y}};
    prim.corners[2] = {.clip = centre, .offset = {from.x, to.y}, .uv = {uvFrom.x, uvTo.y}};
    prim.corners[3] = {.clip = centre, .offset = to, .uv = uvTo};
    prims.push_back(prim);
  };
  // renderParticleFlash: the ring, each quarter's outer corner on the image's top left.
  const std::int32_t reach = (radius * IrisTexels) >> IrisSizeShift;
  if (reach > 0) {
    const float half = static_cast<float>(reach + 1);
    const glm::vec2 uvTo{IrisQuarter / IrisImageSide};
    for (const glm::vec2 sign : {glm::vec2{-1.0F, -1.0F}, glm::vec2{1.0F, -1.0F},
                                 glm::vec2{-1.0F, 1.0F}, glm::vec2{1.0F, 1.0F}}) {
      quad(sign * half, glm::vec2{0.0F}, glm::vec2{0.0F}, uvTo);
    }
  }
  // The rectangles, from the ring's square to the screen's edges.
  const auto edge = static_cast<float>((radius << IrisEdgeShift) >> IrisScaleShift);
  const auto thickness =
      static_cast<float>(originalLerp(IrisThickFrom, IrisThickTo, IrisThickRadius, 0, radius));
  const glm::vec2 white{IrisWhiteTexels / IrisImageSide};
  quad({-ScreenBeyond, -ScreenBeyond}, {-edge + thickness, ScreenBeyond}, glm::vec2{0.0F}, white);
  quad({edge - thickness, -ScreenBeyond}, {ScreenBeyond, ScreenBeyond}, glm::vec2{0.0F}, white);
  if (edge > 0.0F) {
    quad({-edge, -ScreenBeyond}, {edge, -edge + thickness}, glm::vec2{0.0F}, white);
    quad({-edge, edge - thickness}, {edge, ScreenBeyond}, glm::vec2{0.0F}, white);
  }
  return prims;
}

void DeathEffects::start(std::uint32_t seed) {
  // DOOA_initShardEffect, DOO2_resetShardSets.
  clear();
  random_ = ruleset::Random::original(seed);
}

void DeathEffects::clear() {
  bodyShare_ = FullShare;
  anchor_ = glm::ivec3{0};
  partnerHeight_ = 0.0F;
  shards_.clear();
  shardState_ = 0;
  shardPrevState_ = 0;
  flicker_ = false;
  shardColor_ = glm::ivec3{ShardShade};
  flashes_.clear();
  flashOffsets_.clear();
  eggSlot_.reset();
  egg_ = nullptr;
  eggShare_ = 0;
  pieceGone_.fill(false);
  eggShards_.clear();
  pieceBreaks_ = 0;
  streaks_ = 0;
}

std::int32_t DeathEffects::random() {
  return static_cast<std::int32_t>(random_.next());
}

void DeathEffects::dissolveStep(std::int32_t frame, const EvolutionBody& body,
                                const DeathPartner& partner) {
  if (frame >= WaveFrom && frame < WaveUntil) {
    // Phase 2: the body turns to wireframe a sixteenth at a time, a wave of faces breaking off at
    // each step. The shards hang from where the partner stands (DOOA_storeDigimonY).
    const auto at = static_cast<std::size_t>(frame - WaveFrom);
    bodyShare_ = WaveSchedule.at(at);
    if (at == 0 || WaveSchedule.at(at - 1) != WaveSchedule.at(at)) {
      anchor_ = toOriginal(partner.feet);
      partnerHeight_ = partner.height;
      spawnWave(body, bodyShare_);
    }
  }
  if (frame == BodyBackFrame) {
    bodyShare_ = FullShare;
  }
  if (frame > LightFrom && frame <= LightTo) {
    // Phase 0x65: the light comes down from over the partner's head.
    const glm::ivec3 feet = toOriginal(partner.feet);
    const std::int32_t top = anchor_.y - (static_cast<std::int32_t>(partnerHeight_) + LightAbove);
    EvolutionFlash flash = LightFlash;
    flash.position =
        toWorld(glm::ivec3{feet.x, originalLerp(top, 0, LightFrom, LightTo, frame), feet.z});
    for (int channel = 0; channel < 3; ++channel) {
      flash.colorFrom[channel] = (random() % LightGreyRoll) + LightGreyMin;
    }
    const std::int32_t sway =
        sin512(originalLerp(0, LightSwayTurns, LightFrom, LightTo, frame)) * LightSway / One;
    addFlash(flash, static_cast<float>(sway));
  }
  if (frame == FlickerFrame) {
    flicker_ = !flicker_;
  }
}

void DeathEffects::spawnWave(const EvolutionBody& body, std::int32_t wave) {
  // DOOA_spawnBoneShards for each bone from 2: of its object's faces, those of the wave (by their
  // place among the object's) that rand() keeps.
  for (std::uint32_t bone = FirstShardBone; bone < body.boneCount(); ++bone) {
    for (const BodyFace& face : body.boneFaces(bone)) {
      const bool kept = (random() & ShardKeepMask) != 0;
      if (!kept || wireTableEntry(face.index) != wave) {
        continue;
      }
      DeathShard shard{.count = face.count, .uvs = face.uvs, .texture = face.texture};
      std::array<glm::ivec3, 4> corners{};
      glm::ivec3 sum{0};
      for (std::size_t i = 0; i < face.count; ++i) {
        corners.at(i) = toOriginal(face.corners.at(i));
        sum += corners.at(i);
      }
      const glm::ivec3 centre = sum / static_cast<std::int32_t>(face.count);
      for (std::size_t i = 0; i < face.count; ++i) {
        shard.vertex.at(i) = corners.at(i) - centre;
      }
      shard.centre = centre - anchor_;
      shard.radius = One;
      shard.axisDistance = static_cast<std::int32_t>(
          std::sqrt(static_cast<double>(shard.centre.x) * shard.centre.x +
                    static_cast<double>(shard.centre.z) * shard.centre.z));
      shard.axisDistance = std::max(shard.axisDistance, 1);
      shards_.push_back(shard);
    }
  }
}

void DeathEffects::rebirthStep(std::int32_t frame, std::int32_t eggSlot,
                               const std::vector<std::vector<BodyFace>>* egg) {
  if (frame == 1) {
    // DOOA_getSequenceState: the egg shows as wireframe, whole.
    eggSlot_ = eggSlot;
    egg_ = egg;
    eggShare_ = 0;
    pieceGone_.fill(false);
  }
  streaks_ = 0;
  // DOOA_setShardState.
  if (frame == RiseFrame) {
    shardState_ = 1;
  }
  if (frame == CloseFrame) {
    shardState_ = 3;
  }
  if (frame > EggFillFrom && frame <= EggFillTo) {
    eggShare_ = frame >= EggFullFrom ? FullShare
                                     : originalLerp(1, FullShare, EggFillFrom, EggFillTo, frame);
  }
  if (frame == HatchFrame) {
    // DOOA_removeShardEffect and DOO2_releaseAllShardSets.
    shards_.clear();
    eggShards_.clear();
  }
  if (frame > HatchFrame && frame <= SwapFrame) {
    // Phase 0xCE: sparks while the egg is opening, and a flash on it every frame.
    if (frame < StreaksUntil) {
      streaks_ = originalLerp(StreaksFrom, 0, HatchFrame, StreaksFadeTo, frame);
    }
    EvolutionFlash flash{.position = toWorld(glm::ivec3{0, -EggFlashAbove, 0}),
                         .mode = 0,
                         .fixedDepth = -1,
                         .length = EggFlashLength,
                         .scaleFrom = 0};
    flash.scaleTo = originalLerp(EggFlashScaleFrom, EggFlashScaleTo, HatchFrame, SwapFrame, frame) *
                    EggFlashScaleTimes * One / EggFlashScaleOver;
    const std::int32_t grey =
        originalLerp(EggFlashGreyFrom, EggFlashGreyTo, HatchFrame, SwapFrame, frame);
    const std::int32_t spread = std::max((grey * EggFlashGreySpread) / EggFlashGreyFrom, 1);
    for (int channel = 0; channel < 3; ++channel) {
      flash.colorFrom[channel] = grey + (random() % spread);
    }
    flash.colorTo = glm::ivec3{grey / EggFlashFade};
    addFlash(flash, 0.0F);
  }
  if (frame >= BreakFrom && frame - BreakFrom < static_cast<std::int32_t>(PieceTable.size())) {
    // Phase 0xD0: the egg breaks around the baby.
    const std::int32_t piece = PieceTable.at(static_cast<std::size_t>(frame - BreakFrom));
    if (piece >= 0) {
      breakEggPiece(piece);
    }
  }
}

void DeathEffects::breakEggPiece(std::int32_t piece) {
  // DOO2_buildShardSet: the piece's faces fly apart; the egg no longer draws it.
  pieceGone_.at(static_cast<std::size_t>(piece)) = true;
  pieceBreaks_ += 1;
  if (egg_ == nullptr || !eggSlot_ || eggShards_.size() >= MaxPieces) {
    return;
  }
  const auto object = static_cast<std::size_t>((*eggSlot_ * PiecesPerEgg) + piece);
  if (object >= egg_->size()) {
    return;
  }
  EggShardSet set;
  for (const BodyFace& face : egg_->at(object)) {
    const glm::vec3 normal = core::fromOriginalAxes(face.normal) * static_cast<float>(One);
    set.shards.push_back({.face = face,
                          .centre = glm::ivec3{static_cast<std::int32_t>(normal.x),
                                               static_cast<std::int32_t>(normal.y),
                                               static_cast<std::int32_t>(normal.z)}});
  }
  eggShards_.push_back(std::move(set));
}

void DeathEffects::addFlash(const EvolutionFlash& flash, float offsetX) {
  if (flashes_.size() < MaxFlashes) {
    flashes_.push_back(flash);
    flashOffsets_.push_back(offsetX);
  }
}

void DeathEffects::tickObjects() {
  tickShards();
  for (std::size_t i = flashes_.size(); i-- > 0;) {
    if (++flashes_[i].progress >= flashes_[i].length) {
      flashes_.erase(flashes_.begin() + static_cast<std::ptrdiff_t>(i));
      flashOffsets_.erase(flashOffsets_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  // DOO2_tickShardSet (DOO2_releaseShardSet once its time is over), then DOO2_renderTriShard and
  // DOO2_renderQuadShard move the faces.
  std::erase_if(eggShards_, [](EggShardSet& set) { return ++set.timer >= PieceLife; });
  for (EggShardSet& set : eggShards_) {
    for (EggShard& shard : set.shards) {
      const std::array<glm::ivec3, 4> corners{
          toOriginal(shard.face.corners[0]), toOriginal(shard.face.corners[1]),
          toOriginal(shard.face.corners[2]), toOriginal(shard.face.corners[3])};
      // doo2.c reads the third corner's z where it means its y.
      std::int32_t height = corners[0].y + corners[1].y + corners[2].z;
      if (shard.face.count == 4) {
        height += corners[3].y;
      }
      height /= static_cast<std::int32_t>(shard.face.count);
      shard.frame += 1;
      shard.offset.y += shard.frame + (shard.centre.y / PieceSpeed);
      if (shard.offset.y + height > 0) {
        shard.offset.y = -height;
      } else {
        shard.offset.x += shard.centre.x / PieceSpeed;
        shard.offset.z += shard.centre.z / PieceSpeed;
      }
    }
  }
}

void DeathEffects::tickShards() {
  // DOOA_updateShards.
  if (flicker_) {
    for (int channel = 0; channel < 3; ++channel) {
      shardColor_[channel] = (random() % ShardGreyRoll) + ShardGreyMin;
    }
  }
  const bool entered = shardState_ != shardPrevState_;
  for (DeathShard& shard : shards_) {
    switch (shardState_) {
    case 0:
      // They fall to the ground.
      shard.fallSpeed = std::min(shard.fallSpeed + 1, MaxFallSpeed);
      shard.centre.y = std::min(shard.centre.y + shard.fallSpeed, 0);
      break;
    case 1: {
      // They rise in a widening spiral.
      if (entered) {
        shard.spin = 0;
        shard.spinMax = (random() % SpinRoll) + SpinMin;
        shard.radius = One;
        const std::int32_t near = RadiusNear / shard.axisDistance;
        const std::int32_t far = RadiusFar / shard.axisDistance;
        shard.targetRadius = far > near ? near + (random() % (far - near)) : near;
        shard.fallSpeed = 0;
        shard.dropDepth = random() % DropRoll;
      }
      shard.spin += shard.spinMax / SpinCheck == 0 ? 1 : shard.spinMax / SpinStep;
      shard.spin = std::min(shard.spin, shard.spinMax);
      shard.rotY += shard.spin;
      if (shard.radius < shard.targetRadius) {
        shard.radius += shard.targetRadius / SpinCheck == 0 ? 1 : shard.targetRadius / SpinStep;
      }
      shard.fallSpeed += 1;
      shard.centre.y = std::max(shard.centre.y - (shard.fallSpeed >> RiseShift), -shard.dropDepth);
      break;
    }
    case 2:
      shard.rotY += shard.spin;
      break;
    case 3:
      // They close in on the egg, each after a short wait.
      if (entered) {
        shard.delay = random() % ShrinkDelayRoll;
        shard.targetRadius = shard.radius;
      }
      if (shard.delay > 0) {
        --shard.delay;
      } else {
        shard.radius -= (shard.targetRadius / ShrinkStep) + 1;
        if (shard.radius < 0) {
          shard.radius = 0;
          shard.delay = -1;
        }
        shard.centre.y = originalLerp(-shard.dropDepth, anchor_.y - EggCentreAbove,
                                      shard.targetRadius, 0, shard.radius);
      }
      shard.rotY = (shard.rotY + shard.spin) % One;
      break;
    default:
      break;
    }
  }
  shardPrevState_ = shardState_;
}

std::vector<ScenePrim> DeathEffects::draw(const EvolutionBody& body, const DeathView& view) {
  const ScenePrimFrame& frame = view.frame;
  std::vector<SortedPrim> out;
  const auto wireGrey = [&] { return WireGreyMin + (random() % (WireGreyMax - WireGreyMin)); };

  // model.c: renderWireframed: the whole body while it is not drawn as usual.
  if (bodyShare_ != FullShare) {
    for (std::uint32_t bone = 0; bone < body.boneCount(); ++bone) {
      addWiredFaces(body.boneFaces(bone), bodyShare_, wireGrey(), frame, view.white, out);
    }
  }

  // DOOA_renderShards: each turned about the axis through the partner's feet.
  const glm::vec4 shardShade{glm::vec3{static_cast<float>(ShardShade) / TexturedOne}, 1.0F};
  const glm::vec4 flickerShade{glm::vec3{shardColor_} / TexturedOne, 1.0F};
  for (const DeathShard& shard : shards_) {
    if (shard.delay < 0) {
      continue;
    }
    const float angle = static_cast<float>(shard.rotY) * TurnRadians / static_cast<float>(One);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const glm::vec3 offset{static_cast<float>(shard.centre.x * shard.radius / One),
                           static_cast<float>(shard.centre.y),
                           static_cast<float>(shard.centre.z * shard.radius / One)};
    std::array<glm::vec3, 4> corners{};
    for (std::size_t i = 0; i < shard.count; ++i) {
      const glm::vec3 point = offset + glm::vec3{shard.vertex.at(i)};
      // RotMatrixY: x' = c x + s z, z' = -s x + c z.
      const glm::vec3 turned{(c * point.x) + (s * point.z), point.y,
                             (-s * point.x) + (c * point.z)};
      corners.at(i) = toWorld(turned + glm::vec3{anchor_});
    }
    // The flicker draws them in one colour (the glow page's white texel).
    const bool flat = flicker_ && view.glow.has_value();
    const std::array<glm::vec2, 4> white{};
    ScenePrim prim =
        facePrim(frame, shard.count, corners, flat ? white : shard.uvs,
                 flat ? *view.glow : shard.texture, flicker_ ? flickerShade : shardShade);
    out.push_back(SortedPrim{.depth = meanDepth(frame, std::span{corners.data(), shard.count}),
                             .prim = prim});
  }

  // renderEFEFlash.
  if (view.glow) {
    const glm::vec3 right = glm::vec3{glm::inverse(frame.view)[0]};
    for (std::size_t i = 0; i < flashes_.size(); ++i) {
      EvolutionFlash flash = flashes_[i];
      flash.position += right * flashOffsets_[i];
      addFlashPrims(flash, frame, view.focalLength, *view.glow, out);
    }
  }

  // DOOA_renderRebirth: the egg's whole pieces, wireframe until it fills in.
  if (eggSlot_ && egg_ != nullptr) {
    for (std::size_t piece = 0; piece < pieceGone_.size(); ++piece) {
      const auto object = static_cast<std::size_t>(*eggSlot_ * PiecesPerEgg) + piece;
      if (pieceGone_.at(piece) || object >= egg_->size()) {
        continue;
      }
      addWiredFaces(egg_->at(object), eggShare_, wireGrey(), frame, view.white, out);
    }
  }

  // DOO2_renderShardSet: the broken pieces' faces, darkening.
  for (const EggShardSet& set : eggShards_) {
    const float shade =
        static_cast<float>((((PieceLife - set.timer) * PieceShadeSpan) / PieceShadeSteps) +
                           PieceShadeMin) /
        TexturedOne;
    for (const EggShard& shard : set.shards) {
      std::array<glm::vec3, 4> corners{};
      for (std::size_t i = 0; i < shard.face.count; ++i) {
        corners.at(i) = toWorld(glm::vec3{toOriginal(shard.face.corners.at(i)) + shard.offset});
      }
      out.push_back(
          SortedPrim{.depth = meanDepth(frame, std::span{corners.data(), shard.face.count}),
                     .prim = facePrim(frame, shard.face.count, corners, shard.face.uvs,
                                      shard.face.texture, glm::vec4{glm::vec3{shade}, 1.0F})});
    }
  }

  // DOO2_renderSparkStreak: rays from over the partner.
  if (view.spark) {
    const glm::vec3 origin{0.0F, static_cast<float>(-StreakAbove), 0.0F};
    const auto between = [&](std::int32_t from, std::int32_t to) {
      return from + (random() % (to - from + 1));
    };
    for (std::int32_t i = 0; i < streaks_; ++i) {
      const std::int32_t tilt = between(StreakTiltFrom, StreakTiltTo);
      const auto pitch = static_cast<float>(tilt != 0 ? random() % tilt : 0);
      const auto yaw = static_cast<float>(random());
      const auto length = static_cast<float>(between(StreakFrom, StreakTo));
      // RotMatrixZYX with no roll on (0, 0, length).
      const float rx = pitch * TurnRadians / static_cast<float>(One);
      const float ry = yaw * TurnRadians / static_cast<float>(One);
      const glm::vec3 tip =
          origin + glm::vec3{std::sin(ry) * length, -std::sin(rx) * std::cos(ry) * length,
                             std::cos(rx) * std::cos(ry) * length};
      const glm::vec3 side =
          tip + glm::vec3{static_cast<float>(between(-StreakJitter, StreakJitter)),
                          static_cast<float>(between(-StreakJitter, StreakJitter)),
                          static_cast<float>(between(-StreakJitter, StreakJitter))};
      const float grey =
          static_cast<float>((random() % StreakGreyRoll) + StreakGreyMin) / TexturedOne;
      const std::array<glm::vec3, 3> points{toWorld(side), toWorld(origin), toWorld(tip)};
      ScenePrim ray{.shape = ScenePrim::Shape::Triangle,
                    .color = glm::vec4{glm::vec3{grey}, 1.0F},
                    .texture = *view.spark,
                    .blend = ScenePrimBlend::Additive};
      for (std::size_t corner = 0; corner < points.size(); ++corner) {
        ray.corners.at(corner) = {.clip = frame.clip(points.at(corner)),
                                  .uv = SparkTexels.at(corner) / SparkImageSize};
      }
      out.push_back(SortedPrim{.depth = meanDepth(frame, points), .prim = ray});
    }
  }

  return sortedPrims(std::move(out));
}

} // namespace dwe::app
