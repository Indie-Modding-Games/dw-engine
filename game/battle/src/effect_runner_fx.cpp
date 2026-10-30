#include <dwe/core/log.hpp>
#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "effect_math.hpp"

// The technique effects' own lights and dust (battle_effect.c: the particle emitters; efe.c: the
// clouds), as EffectRunner members: what their sub-ops add, a frame of them and their draws.
namespace dwe::game::battle {

namespace {

namespace fx = effect_math;

using assets::EffectWidth;

constexpr std::int32_t ByteMask = 0xFF;
// EfeInstance: the frame, then the transform's position.
constexpr std::int32_t PositionOffset = 4;
// The frame of a free instance.
constexpr std::int32_t FreeInstance = -1;

// BTL_addParticleEmitter: the start offset is the strength * 16, the speed strength / 8 * 16 and
// the slowing strength / 160 * 16.
constexpr std::int32_t EmitterOffsetScale = 16;
constexpr std::int32_t EmitterSpeedDivisor = 8;
constexpr std::int32_t EmitterSlowingDivisor = 160;
// BTL_tickParticleEmitters: a new particle's turn on each axis, ((rand() & 0x7f) - 0x40) << 12
// / 64.
constexpr std::uint32_t TurnRollMask = 0x7F;
constexpr std::int32_t TurnRollMiddle = 0x40;
constexpr std::int32_t TurnRollScale = 64;
// The particles an emitter moves; one more stays empty and ends the search for a free one.
constexpr std::size_t EmitterParticles = 20;
// BTL_renderParticleEmitters: the ends of a particle, as sixteenths of the way from its start
// point (<< 4 over the start offset, the direction then >> 8).
constexpr std::int32_t StreakNear = 4;
constexpr std::int32_t StreakFar = 2;
constexpr std::int32_t StreakTail = 7;
constexpr std::int32_t StreakTrail = 5;
constexpr std::int32_t DirectionShift = 8;

// efe.c: tickCloudFX ends a cloud on its 14th frame; renderCloudFX's tables by frame (CLOUD_FX_U,
// CLOUD_FX_Y: the height in the world, CLOUD_FX_SCALE, CLOUD_FX_COLOR).
constexpr std::int32_t CloudFrames = 14;
constexpr std::array<std::int32_t, CloudFrames> CloudU = {0x00, 0x00, 0x20, 0x20, 0x20, 0x40, 0x40,
                                                          0x40, 0x40, 0x40, 0x60, 0x60, 0x60, 0x60};
constexpr std::array<std::int32_t, CloudFrames> CloudY = {-50, -54, -58, -62, -66, -70, -74,
                                                          -78, -82, -86, -90, -93, -96, -99};
constexpr std::array<std::int32_t, CloudFrames> CloudScale = {
    0x2000, 0x2ee0, 0x3840, 0x4000, 0x4000, 0x5000, 0x5000,
    0x5000, 0x5000, 0x5400, 0x5800, 0x5c00, 0x6000, 0x6400};
constexpr std::array<std::int32_t, CloudFrames> CloudShade = {
    0x80, 0x80, 0x80, 0x80, 0x79, 0x73, 0x6c, 0x66, 0x60, 0x59, 0x53, 0x4c, 0x46, 0x40};

constexpr std::int32_t FixedShift = 12;

std::int16_t short16(std::int32_t value) {
  return static_cast<std::int16_t>(fx::toShort(value));
}

EffectVector bytes(const EffectVector& v) {
  return {.x = v.x & ByteMask, .y = v.y & ByteMask, .z = v.z & ByteMask};
}

} // namespace

bool EffectRunner::fxOp(Slot& slot, std::string_view name, const std::vector<std::int32_t>& args) {
  const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
  if (name == "add_particle_emitter") {
    // battle_effect.c: BTL_addParticleEmitter: kind, strength, colour, frames; on the running
    // instance, in the first of the 4 free emitters.
    if (args.size() < 4 || !current_) {
      return true;
    }
    const auto free = std::ranges::find_if(
        emitters_, [](const std::optional<Emitter>& emitter) { return !emitter.has_value(); });
    if (free == emitters_.end()) {
      return true;
    }
    const std::int32_t strength = args[1];
    Emitter emitter;
    emitter.slot = index;
    emitter.instance = *current_;
    emitter.kind = static_cast<std::int8_t>(args[0]);
    emitter.color = bytes(vectorAt(slot, args[2]));
    emitter.frames = short16(args[3]);
    emitter.startOffset = short16(strength * EmitterOffsetScale);
    emitter.startVelocity = short16(strength / EmitterSpeedDivisor * EmitterOffsetScale);
    emitter.acceleration = short16(strength / EmitterSlowingDivisor * EmitterOffsetScale);
    *free = emitter;
    core::logDebug("[port:battle] BTL_addParticleEmitter (battle_effect.c:4288): slot {} kind {} "
                   "strength {} colour ({}, {}, {}) for {} frames",
                   index, emitter.kind, strength, emitter.color.x, emitter.color.y, emitter.color.z,
                   emitter.frames);
  } else if (name == "add_cloud") {
    // battle_effect.c: BTL_addCloudEffect; efe.c: createCloudFX: in the first of 60 free clouds, at
    // the point's ground x and z.
    if (args.empty()) {
      return true;
    }
    const auto free = std::ranges::find_if(
        clouds_, [](const std::optional<Cloud>& cloud) { return !cloud.has_value(); });
    if (free == clouds_.end()) {
      return true;
    }
    const EffectVector at = fx::toShort(vectorAt(slot, args[0]));
    *free = Cloud{.slot = index, .state = 0, .x = at.x, .z = at.z};
    core::logDebug("[port:battle] createCloudFX (efe.c:714): slot {} at ({}, {})", index, at.x,
                   at.z);
  } else {
    return false;
  }
  return true;
}

void EffectRunner::tickFx() {
  // battle_effect.c: BTL_tickParticleEmitters: an emitter whose instance ended stops; otherwise it
  // sends a particle from the instance's point in a random direction (three draws of the game's
  // rand()), then every particle closes in, slowing, and the emitter counts down its frames.
  for (std::optional<Emitter>& emitter : emitters_) {
    if (!emitter) {
      continue;
    }
    const Slot& owner = slots_[emitter->slot];
    if (frameOf(owner, emitter->instance) == FreeInstance) {
      emitter.reset();
      continue;
    }
    std::size_t k = 0;
    while (k < EmitterParticles && emitter->particles[k].distance > 0) {
      ++k;
    }
    if (k != EmitterParticles) {
      Particle& particle = emitter->particles[k];
      particle.distance = emitter->startOffset;
      particle.velocity = emitter->startVelocity;
      EffectVector turn;
      for (std::int32_t* axis : {&turn.x, &turn.y, &turn.z}) {
        const auto roll = static_cast<std::int32_t>(randomDraw() & TurnRollMask);
        *axis = ((roll - TurnRollMiddle) << FixedShift) / TurnRollScale;
      }
      particle.direction = fx::toShort(
          fx::apply(fx::rotZYX(turn), EffectVector{.x = 0, .y = 0, .z = emitter->startOffset}));
      particle.position = fx::toShort(
          vectorAt(owner, static_cast<std::int32_t>(emitter->instance) + PositionOffset));
    }
    for (std::size_t p = 0; p < EmitterParticles; ++p) {
      Particle& particle = emitter->particles[p];
      if (particle.distance > 0) {
        particle.distance = short16(particle.distance - particle.velocity);
        particle.velocity = short16(particle.velocity - emitter->acceleration);
      }
    }
    emitter->frames = short16(emitter->frames - 1);
    if (emitter->frames <= 0) {
      emitter.reset();
    }
  }
  // efe.c: tickCloudFX, as a world object after the effect engine's: a frame more, and the end on
  // the last.
  for (std::optional<Cloud>& cloud : clouds_) {
    if (cloud && ++cloud->state >= CloudFrames) {
      cloud.reset();
    }
  }
}

void EffectRunner::renderFx() {
  // efe.c: renderCloudFX: the frame's height, sheet frame, shade and size.
  for (const std::optional<Cloud>& cloud : clouds_) {
    if (!cloud) {
      continue;
    }
    const auto state = static_cast<std::size_t>(cloud->state);
    draws_.push_back(
        {.slot = cloud->slot,
         .shape = EffectCloud{.position = {.x = cloud->x, .y = CloudY.at(state), .z = cloud->z},
                              .u = CloudU.at(state),
                              .shade = CloudShade.at(state),
                              .scale = CloudScale.at(state)}});
  }
  // battle_effect.c: BTL_renderParticleEmitters.
  for (const std::optional<Emitter>& emitter : emitters_) {
    if (!emitter || emitter->startOffset == 0) {
      continue;
    }
    for (std::size_t p = 0; p < EmitterParticles; ++p) {
      const Particle& particle = emitter->particles[p];
      if (particle.distance <= 0) {
        continue;
      }
      const std::int32_t gone = emitter->startOffset - particle.distance;
      std::int32_t near = 0;
      std::int32_t far = 0;
      switch (emitter->kind) {
      case 0:
        near = particle.distance << StreakNear;
        far = (particle.distance + StreakTail) << StreakNear;
        break;
      case 1:
        near = particle.distance << StreakNear;
        far = particle.distance << StreakFar;
        break;
      case 2:
        near = gone << StreakNear;
        far = (gone - StreakTrail) << StreakNear;
        break;
      case 3:
        near = gone << StreakNear;
        far = gone << StreakFar;
        break;
      default:
        // The original leaves both ends unset for another kind; no script uses one.
        continue;
      }
      near /= emitter->startOffset;
      far /= emitter->startOffset;
      const auto along = [&](std::int32_t f) {
        return fx::toShort(EffectVector{
            .x = particle.position.x + ((particle.direction.x * f) >> DirectionShift),
            .y = particle.position.y + ((particle.direction.y * f) >> DirectionShift),
            .z = particle.position.z + ((particle.direction.z * f) >> DirectionShift)});
      };
      draws_.push_back({.slot = emitter->slot,
                        .shape = EffectStreak{.from = along(near),
                                              .to = along(far),
                                              .color = emitter->color,
                                              .glows = (emitter->kind & 1) == 0}});
    }
  }
}

} // namespace dwe::game::battle
