#include "ending_effects.hpp"

#include <algorithm>
#include <utility>

#include "evolution_effects.hpp"

namespace dwe::app {
namespace {

// The original's rand(): 15 bits.
constexpr std::uint32_t RandMask = 0x7FFF;

// endi.c: ENDI_updateEnding. Phase 0 fades the player's palette from frame 0 to 60; phase 1
// shrinks it to 0x999 by frame 78 and swells it to 0x1800 by 80, where the sparks leave the bones
// and rise at 450 a frame, slowing by 20 a frame; phase 3 ends the scene.
constexpr std::int32_t FadeTo = 60;
constexpr std::int32_t ShrinkFrom = 60;
constexpr std::int32_t ShrinkTo = 78;
constexpr std::int32_t SwellTo = 80;
constexpr std::int32_t ShrunkScale = 0x999;
constexpr std::int32_t SwollenScale = 0x1800;
constexpr std::int32_t RiseSpeed = -450;
constexpr std::int32_t RiseBrake = 20;
// The ending's bank in slot 8: sound 0 as it starts, 1 as the sparks leave.
constexpr std::int64_t StartSound = 0;
constexpr std::int64_t ReleaseSound = 1;

// ENDI_spawnParticle: the spark's offset in its bone's space.
constexpr std::int32_t SparkRollX = 140;
constexpr std::int32_t SparkRollYZ = 80;
// ENDI_renderParticles: each channel 60 to 129, the size 15 to 29.
constexpr std::int32_t ColorRoll = 70;
constexpr std::int32_t ColorFrom = 60;
constexpr std::int32_t SizeRoll = 15;
constexpr std::int32_t SizeFrom = 15;

OriginalPoint scaled(OriginalPoint offset, std::int32_t scale) {
  return offset * scale / EndingScaleOne;
}

} // namespace

EndingEffects::EndingEffects(std::int32_t boneCount, std::uint64_t seed)
    : random_(seed), boneCount_(std::max(boneCount, 2)) {}

std::int32_t EndingEffects::rand() {
  return static_cast<std::int32_t>(random_.next() & RandMask);
}

void EndingEffects::start() {
  // endi.c: ENDI_startParticles: ENDI_clearParticles empties the sparks, which follow the bones
  // (flag 1).
  started_ = true;
  frame_ = 0;
  phase_ = 0;
  scale_ = EndingScaleOne;
  velocity_ = 0;
  rise_ = 0;
  released_ = false;
  sparks_.clear();
  sounds_.push_back({.kind = EndingSound::Kind::Play, .sound = StartSound});
}

void EndingEffects::spawn() {
  // ENDI_spawnParticle: the 80 sparks are exactly the frames 1 to 80 that spawn one.
  if (sparks_.size() >= EndingSparkCount) {
    return;
  }
  EndingSpark spark{.bone = (frame_ % (boneCount_ - 1)) + 1};
  spark.offset.x = (rand() % SparkRollX) - (SparkRollX / 2);
  spark.offset.y = (rand() % SparkRollYZ) - (SparkRollYZ / 2);
  spark.offset.z = (rand() % SparkRollYZ) - (SparkRollYZ / 2);
  sparks_.push_back(spark);
}

// ENDI_releaseParticles: every spark keeps the world point it had on its bone. The original keeps
// it relative to the root bone's translation, read one field off (t[1..3]), and adds the same
// value back as it draws, so only the rise moves it.
void EndingEffects::release(const EndingSparkPlacer& place) {
  released_ = true;
  for (EndingSpark& spark : sparks_) {
    spark.offset = place(spark.bone, scaled(spark.offset, scale_), scale_).value_or(spark.offset);
    spark.bone = 0;
  }
}

void EndingEffects::tickFrame(const EndingSparkPlacer& place) {
  if (!started_ || frame_ < 0) {
    return;
  }
  ++frame_;
  switch (phase_) {
  case 0:
    spawn();
    if (frame_ >= FadeTo) {
      phase_ = 1;
    }
    break;
  case 1:
    spawn();
    scale_ = frame_ <= ShrinkTo
                 ? originalLerp(EndingScaleOne, ShrunkScale, ShrinkFrom, ShrinkTo, frame_)
                 : originalLerp(ShrunkScale, SwollenScale, ShrinkTo, SwellTo, frame_);
    if (frame_ >= SwellTo) {
      sounds_.push_back({.kind = EndingSound::Kind::Play, .sound = ReleaseSound});
      phase_ = 2;
      release(place);
      velocity_ = RiseSpeed;
    }
    break;
  case 2:
    // ENDI_updateEnding: ENDI_getParticleBase, base.vy plus the speed, ENDI_setParticleBase.
    rise_ += velocity_;
    velocity_ += RiseBrake;
    if (velocity_ >= 0) {
      phase_ = 3;
    }
    break;
  default:
    // stopSound, ENDI_stopParticles and the scene's object removed.
    sounds_.push_back({.kind = EndingSound::Kind::Stop});
    sparks_.clear();
    frame_ = -1;
    break;
  }
}

float EndingEffects::fade() const {
  if (!started_) {
    return 1.0F;
  }
  if (phase_ != 0) {
    return 0.0F;
  }
  return static_cast<float>(FadeTo - frame_) / static_cast<float>(FadeTo);
}

std::vector<FieldGlow> EndingEffects::glows(const EndingSparkPlacer& place) {
  std::vector<FieldGlow> out;
  out.reserve(sparks_.size());
  for (const EndingSpark& spark : sparks_) {
    std::optional<OriginalPoint> at;
    if (released_) {
      at = spark.offset + OriginalPoint{0, rise_, 0};
    } else {
      at = place(spark.bone, scaled(spark.offset, scale_), scale_);
    }
    glm::ivec3 color{0};
    color.r = (rand() % ColorRoll) + ColorFrom;
    color.g = (rand() % ColorRoll) + ColorFrom;
    color.b = (rand() % ColorRoll) + ColorFrom;
    const std::int32_t size = (rand() % SizeRoll) + SizeFrom;
    if (at) {
      out.push_back({.position = *at, .size = size, .color = color});
    }
  }
  return out;
}

} // namespace dwe::app
