#include <dwe/core/log.hpp>
#include <dwe/game/field/boss_arrival.hpp>

#include <cmath>
#include <numbers>

namespace dwe::game::field {

namespace {

constexpr std::int32_t One = 4096;
constexpr std::int32_t QuarterTurn512 = 0x80;
constexpr std::int32_t Turn512 = 0x200;

// eab.c: EAB_tickBuildup counts 0 to 17 and adds a ring every 6.
constexpr std::int32_t BuildupCycle = 18;
constexpr std::int32_t BuildupRingEvery = 6;
// EAB_tickBuildupRing and EAB_tickSpawnRing: the frames a ring lives.
constexpr std::int32_t BuildupRingLife = 0x12;
constexpr std::int32_t SpawnRingLife = 9;
// EAB_renderBuildupRing and EAB_renderSpawnRing: both grow over 18 frames.
constexpr std::int32_t RingGrowth = 0x12;
// EAB_tickParticle: a particle lives until its timer reaches 0x56.
constexpr std::int32_t ParticleLife = 0x56;
// EAB_renderParticle: half the square's side grows from 8 to 0x9c4.
constexpr std::int32_t ParticleHalfFrom = 8;
constexpr std::int32_t ParticleHalfTo = 0x9c4;

// EAB_tickSpawn's frames.
constexpr std::int32_t BlackFrom = 0x2d;
constexpr std::int32_t RiseFrame = 0x32;
constexpr std::int32_t FadeBackUntil = 0x53;
constexpr std::int32_t LoopAnimationFrame = 0xd2;
constexpr std::int32_t TurnUntil = 0x123;
constexpr std::int32_t ViewBackFrame = 0x13f;
constexpr std::int32_t SettleFrame = 0x140;
constexpr std::int32_t LastRingFrame = 0x14a;
constexpr std::int32_t EndFrame = 0x154;
// The boss's animations: it rises (0x21) and roars in a loop (0x33).
constexpr std::int32_t RiseAnimation = 0x21;
constexpr std::int32_t RoarAnimation = 0x33;
// The sounds of slot 8: the charge's hum, the call, the rise and the landing.
constexpr std::int32_t HumSound = 0;
constexpr std::int32_t CallSound = 1;
constexpr std::int32_t RiseSound = 2;
constexpr std::int32_t LandSound = 3;
// eab.c: EAB_tickSpawn sets the boss's stats.current.vabId to 5: its clips' own sounds play from
// slot 5, the bank routine 51 loaded.
constexpr std::int32_t BossOwnSlot = 5;
constexpr std::int32_t BlackLevel = 0xff;
// EAB_D_8006179C: the particles' strength by frame within 18; 0 adds none.
constexpr std::array<std::int32_t, 18> ParticleStrength{1, 2, 3, 4, 5, 4, 3, 2, 1,
                                                        0, 0, 0, 0, 0, 0, 0, 0, 0};
// MAIN_D_80134C3C: the particles' colour, its green replaced.
constexpr std::array<std::int32_t, 3> ParticleBase{0x0a, 0xff, 0x0a};
// EAB_tickSpawn's camera.
constexpr std::int32_t OrbitAngleFrom = 0x71c;
constexpr std::int32_t OrbitAngleTo = 0x171c;
constexpr std::int32_t OrbitDistanceFrom = 0xdac;
constexpr std::int32_t OrbitDistanceTo = 0xbb8;
constexpr std::int32_t OrbitHeightFrom = 0x258;
constexpr std::int32_t OrbitHeightTo = 0x32;

} // namespace

std::int32_t effectLerp(std::int32_t start, std::int32_t end, std::int32_t t0, std::int32_t t1,
                        std::int32_t t) {
  if (t1 == t0) {
    return 0;
  }
  const std::int32_t range = end - start;
  std::int32_t result = (range * (t - t0)) / (t1 - t0);
  if (range >= 0) {
    result %= range + 1;
    if (result < 0) {
      result += range;
    }
  } else {
    result %= range - 1;
    if (result > 0) {
      result += range;
    }
  }
  return result + start;
}

std::int32_t sin512(std::int32_t angle) {
  const double turns = static_cast<double>(angle & (Turn512 - 1)) / Turn512;
  return static_cast<std::int32_t>(std::lround(std::sin(turns * 2.0 * std::numbers::pi) * One));
}

BossRingScale bossRingScale(const BossRing& ring) {
  const std::int32_t rise = sin512(effectLerp(0, QuarterTurn512, 1, RingGrowth, ring.timer));
  if (ring.kind == BossRingKind::Buildup) {
    // EAB_renderBuildupRing: narrows from 0x10b8 to 0x614 while it rises to 0xd98.
    return {.across = effectLerp(0x10b8, 0x614, 1, RingGrowth, ring.timer),
            .high = ((rise * 0xc3c) / One) + 0x15c};
  }
  // EAB_renderSpawnRing: narrows from 0x16cc toward 0x10b8 while it rises toward 0x366.
  return {.across = effectLerp(0x16cc, 0x10b8, 1, RingGrowth, ring.timer),
          .high = ((rise * 0x2b8) / One) + 0xae};
}

std::int32_t bossParticleHalfSide(const BossParticle& particle) {
  return effectLerp(ParticleHalfFrom, ParticleHalfTo, 0, ParticleLife, particle.timer);
}

std::array<std::int32_t, 3> bossParticleColour(const BossParticle& particle) {
  std::array<std::int32_t, 3> colour{};
  for (std::size_t i = 0; i < colour.size(); ++i) {
    colour.at(i) = effectLerp(particle.rgb.at(i), 0, 0, ParticleLife, particle.timer);
  }
  return colour;
}

BossArrivalCues BossArrival::startBuildup() {
  core::logDebug("[port:story] EAB_startBuildup (eab.c:649): the boss leaves the map, the charge "
                 "starts");
  // EAB_initializeRings frees every slot.
  rings_ = {};
  charging_ = true;
  buildupFrame_ = 0;
  bossHidden_ = true;
  BossArrivalCues cues;
  cues.heldSound = HumSound;
  return cues;
}

BossArrivalCues BossArrival::startArrival() {
  core::logDebug("[port:story] EAB_tick (eab.c:670): the boss's arrival starts");
  arrivalFrame_ = 0;
  phase_ = 0;
  arrivalOver_ = false;
  particles_.clear();
  BossArrivalCues cues;
  cues.sound = CallSound;
  return cues;
}

BossArrivalCues BossArrival::frame(glm::vec3 bossPoint,
                                   std::optional<std::int32_t> finishedAnimation) {
  BossArrivalCues cues;
  blackout_.reset();
  orbit_.reset();
  if (charging_) {
    tickBuildup();
  }
  if (arrivalFrame_) {
    tickArrival(bossPoint, finishedAnimation, cues);
  }
  tickRings();
  tickParticles();
  return cues;
}

void BossArrival::tickBuildup() {
  buildupFrame_ = (buildupFrame_ + 1) % BuildupCycle;
  if (buildupFrame_ % BuildupRingEvery == 0) {
    addRing(BossRingKind::Buildup);
  }
}

void BossArrival::tickArrival(glm::vec3 bossPoint, std::optional<std::int32_t> finishedAnimation,
                              BossArrivalCues& cues) {
  const std::int32_t frame = ++*arrivalFrame_;
  // The roar starts again each time it reaches its last frame (a later start this frame wins).
  if (finishedAnimation == RoarAnimation) {
    cues.animation = RoarAnimation;
  }
  if (phase_ == 0) {
    if (frame >= BlackFrom) {
      blackout_ = BlackLevel;
    }
    if (frame < RiseFrame) {
      return;
    }
    // The boss shows alone on black; the charge stops with its hum.
    phase_ = 1;
    bossHidden_ = false;
    othersHidden_ = true;
    charging_ = false;
    cues.animation = RiseAnimation;
    cues.stopSounds = true;
    cues.heldSound = RiseSound;
    cues.bossSoundSlot = BossOwnSlot;
  }
  if (phase_ == 1) {
    addRing(BossRingKind::Spawn);
    if (frame == LoopAnimationFrame) {
      cues.animation = RoarAnimation;
    }
    if (frame >= RiseFrame && frame < FadeBackUntil) {
      blackout_ = effectLerp(BlackLevel, 0, RiseFrame, FadeBackUntil - 1, frame);
    }
    const std::int32_t strength =
        ParticleStrength.at(static_cast<std::size_t>(frame % BuildupCycle));
    if (strength != 0) {
      addParticle(bossPoint);
      BossParticle& added = particles_.back();
      const std::int32_t green = effectLerp(50, 0xe6, 1, 5, strength);
      const std::int32_t angle = effectLerp(0, QuarterTurn512, RiseFrame, TurnUntil - 1, frame);
      // RGB8's green is a byte.
      added.rgb.at(1) = ((green * sin512(angle)) / One) & 0xFF;
    }
    if (frame >= RiseFrame && frame < TurnUntil) {
      const std::int32_t shares = effectLerp(QuarterTurn512, 0, RiseFrame, TurnUntil - 1, frame);
      solidShares_ = SolidShares - ((sin512(shares) * SolidShares) / One);
    }
    if (frame != ViewBackFrame) {
      orbitAt(frame);
    }
    if (frame < SettleFrame) {
      return;
    }
    // Everything comes back; the particles go and the landing sounds.
    phase_ = 2;
    particles_.clear();
    othersHidden_ = false;
    orbit_.reset();
    cues.stopSounds = true;
    cues.sound = LandSound;
    return;
  }
  if (frame < LastRingFrame) {
    addRing(BossRingKind::Spawn);
  }
  if (frame < EndFrame) {
    return;
  }
  core::logDebug("[port:story] EAB_tickSpawn (eab.c:349): the arrival ends on frame {}", frame);
  rings_ = {};
  arrivalFrame_.reset();
  arrivalOver_ = true;
}

void BossArrival::orbitAt(std::int32_t frame) {
  const std::int32_t angle =
      frame >= RiseFrame && frame < TurnUntil
          ? effectLerp(OrbitAngleFrom, OrbitAngleTo, RiseFrame, TurnUntil - 1, frame)
          : OrbitAngleTo;
  orbit_ = BossOrbit{
      .angle = angle,
      .distance = effectLerp(OrbitDistanceFrom, OrbitDistanceTo, RiseFrame, SettleFrame, frame),
      .height = effectLerp(OrbitHeightFrom, OrbitHeightTo, RiseFrame, SettleFrame, frame)};
}

void BossArrival::addRing(BossRingKind kind) {
  for (std::optional<BossRing>& slot : rings_) {
    if (!slot) {
      slot = BossRing{.kind = kind, .timer = 0};
      return;
    }
  }
}

void BossArrival::addParticle(glm::vec3 point) {
  if (particles_.size() >= ParticleSlots) {
    return;
  }
  particles_.push_back({.point = point, .rgb = ParticleBase, .timer = 0});
}

void BossArrival::tickRings() {
  for (std::optional<BossRing>& slot : rings_) {
    if (!slot) {
      continue;
    }
    ++slot->timer;
    const std::int32_t life = slot->kind == BossRingKind::Buildup ? BuildupRingLife : SpawnRingLife;
    if (slot->timer >= life) {
      slot.reset();
    }
  }
}

void BossArrival::tickParticles() {
  // EAB_tickParticle: one at 0x56 goes, the others count on.
  std::erase_if(particles_,
                [](const BossParticle& particle) { return particle.timer >= ParticleLife; });
  for (BossParticle& particle : particles_) {
    ++particle.timer;
  }
}

} // namespace dwe::game::field
