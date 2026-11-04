#include <dwe/core/log.hpp>
#include <dwe/game/fishing/fishing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "fishing_math.hpp"

// The water of the fishing: the fish that live there, the bites and the hooked fish (fish.c).
namespace dwe::game::fishing {

namespace {

// FISH_tickFish and FISH_tickSeadramon: how fast a fish fades in and out, and its fixed speeds.
constexpr std::int32_t FadeStep = 8;
constexpr float AppearSpeed = 30.0F;
constexpr float LeaveSpeed = 40.0F;
constexpr std::int32_t MaxSwimSpeed = 50;
constexpr std::int32_t MinApproachSpeed = 3;
constexpr std::int32_t SlowestSwim = 8;
constexpr std::int32_t SpeedPerDistance = 8;
constexpr std::int32_t SpeedDecayShift = 3;
// The sea serpent: its speed step, its turn per frame and how close a point of its path counts.
constexpr std::int32_t SerpentSpeedStep = 10;
constexpr std::int32_t SerpentTurn = 0x80;
constexpr std::int32_t SerpentReach = 10;
constexpr std::int32_t SerpentSpeedPerDistance = 10;
// FISHING_MAP_DATA's sea serpent times are HHMM, 9999 for none.
constexpr std::int32_t NoSeaSerpent = 9999;
constexpr std::int32_t TimeHour = 100;
// FISH_tickFishPopulation: the share of a kind is counted in 10000ths of its weight.
constexpr std::int32_t ShareScale = 10000;
// FISH_tickHookedFish: how far ahead a hooked fish looks for the shore, and how often it tries
// another way; the ripple that scares the other fish while one fights.
constexpr float ShoreProbe = 300.0F;
constexpr int ShoreTries = 30;
constexpr float FightScare = 700.0F;
constexpr float MaxNibbleStep = 10.0F;
constexpr float MinNibbleDistance = 4.0F;
constexpr std::int32_t NibbleHint = 20;
constexpr std::int32_t HintRoll = 20;
constexpr std::int32_t StruggleTurn = 0x384;
constexpr std::int32_t StruggleSoundRoll = 10;
constexpr std::int32_t TurnRoll = 10;
constexpr std::int32_t SplashRoll = 4;
constexpr std::int32_t PowerScale = 0x1000;
constexpr std::int32_t LongHold = 0x3d;
constexpr std::int32_t LongHoldEvery = 3;
constexpr std::int32_t RunRoll = 20;
constexpr std::int32_t RunRollBelow = 3;
constexpr std::int32_t TiredShare = 10;
constexpr std::int32_t TiredDivisor = 0xf;
constexpr std::int32_t FreshDivisor = 8;
constexpr Range RunSpeed{.min = 0x14, .max = 0x32};
constexpr Range RunTime{.min = 9, .max = 0x13};
constexpr std::int32_t RippleScaleUnit = 0x1000;
constexpr std::int32_t RippleSizeDivisor = 120;
constexpr float MaxLandingStep = 50.0F;
constexpr std::int32_t LandingSoundBelow = 6;
// The bobber's commands that show a nibble, a bite of a big fish and the hook taken.
constexpr std::int32_t NibbleCommand = 2;
constexpr std::int32_t BigNibbleCommand = 0xc;
constexpr std::int32_t BiteCommand = 0xb;
constexpr std::int32_t SinkCommand = 7;
// FISH_randomPointInSpot draws again while the point misses the water; a spot with no water in
// sight would hang the original, so the engine gives up after this many draws.
constexpr int SpotTries = 64;
constexpr int SpotCorners = 3;

} // namespace

std::int32_t Fishing::roll(std::int32_t min, std::int32_t max) {
  if (max < min) {
    std::swap(min, max);
  }
  return min + random_->below(max - min + 1);
}

std::optional<glm::vec2> Fishing::visibleScreen(glm::vec3 world) const {
  const std::optional<glm::vec2> screen = view_->toScreen(world);
  if (!screen) {
    return std::nullopt;
  }
  const glm::vec2 min = view_->screenMin();
  const glm::vec2 max = view_->screenMax();
  if (screen->x < min.x || screen->x > max.x || screen->y < min.y || screen->y > max.y) {
    return std::nullopt;
  }
  return screen;
}

bool Fishing::onWater(glm::vec3 world) const {
  const std::optional<glm::vec2> screen = visibleScreen(world);
  return screen && isWaterOnScreen(*screen);
}

bool Fishing::isWaterOnScreen(glm::vec2 screen) const {
  return isWater(place_.shoreline, rules_.shoreByY, glm::ivec2{screen});
}

std::optional<glm::vec3> Fishing::randomPointInSpot(std::int32_t kind) {
  const auto& spots = place_.spots;
  if (kind < 0 || std::cmp_greater_equal(kind, spots.size()) ||
      spots[static_cast<std::size_t>(kind)].points.size() < SpotCorners) {
    return std::nullopt;
  }
  const auto& points = spots[static_cast<std::size_t>(kind)].points;
  const glm::vec2 min = view_->screenMin();
  const glm::vec2 max = view_->screenMax();
  const auto last = static_cast<std::int32_t>(points.size()) - 1;
  for (int attempt = 0; attempt < SpotTries; ++attempt) {
    // Three different corners, clamped to the screen, and a point between them.
    std::array<std::int32_t, SpotCorners> picked{};
    for (std::size_t t = 0; t < SpotCorners; ++t) {
      do {
        picked[t] = roll(0, last);
      } while (std::find(picked.begin(), picked.begin() + static_cast<std::ptrdiff_t>(t),
                         picked[t]) != picked.begin() + static_cast<std::ptrdiff_t>(t));
    }
    std::array<glm::ivec2, SpotCorners> corners{};
    for (std::size_t t = 0; t < SpotCorners; ++t) {
      const glm::ivec2 p = points[static_cast<std::size_t>(picked[t])];
      corners[t] = {
          std::clamp(p.x, static_cast<std::int32_t>(min.x), static_cast<std::int32_t>(max.x)),
          std::clamp(p.y, static_cast<std::int32_t>(min.y), static_cast<std::int32_t>(max.y))};
    }
    std::int32_t t = roll(0, FixedOne);
    glm::ivec2 pos = ((corners[0] * t) + (corners[1] * (FixedOne - t))) >> FixedShift;
    t = roll(0, FixedOne);
    pos = ((corners[2] * t) + (pos * (FixedOne - t))) >> FixedShift;
    const glm::vec2 screen{pos};
    if (screen.x < min.x || screen.x > max.x || screen.y < min.y || screen.y > max.y ||
        !isWaterOnScreen(glm::vec2{pos})) {
      continue;
    }
    if (const std::optional<glm::vec3> ground = view_->toGround(screen)) {
      return ground;
    }
  }
  core::logDebug("[port:fishing] FISH_randomPointInSpot (fish.c:1559): spot {} has no water in "
                 "sight",
                 kind);
  return std::nullopt;
}

void Fishing::spawnRipple(std::int32_t kind, glm::vec3 position, float scareRadius,
                          std::int32_t spread, std::int32_t scale) {
  const std::optional<std::size_t> slot = ripples_.spawn(kind, position, scareRadius, scale);
  if (!slot) {
    return;
  }
  Ripple& ripple = ripples_.at(*slot);
  ripple.position.x += static_cast<float>(roll(-spread, spread));
  ripple.position.z += static_cast<float>(roll(-spread, spread));
  ripple.position.y = 0.0F;
  FishingEvent event{.kind = FishingEventKind::Ripple, .value = kind, .size = scale};
  event.position = ripple.position;
  out_.events.push_back(event);
}

void Fishing::tickWater() {
  ripples_.tick();
  markers_.tick();
  tickPopulation();
  tickHooked();
}

void Fishing::tickPopulation() {
  if (scaredPause_ > 0) {
    --scaredPause_;
  } else {
    if (std::cmp_less(pool_.size(), rules_.population.spawnBelow)) {
      // The kind with the fewest fish for its spot's weight; the last of equals.
      const std::size_t kinds = place_.spots.size();
      std::vector<std::int32_t> counts(kinds, 0);
      std::int32_t total = 0;
      for (const Fish& fish : pool_) {
        if (fish.kind >= 0 && std::cmp_less(fish.kind, kinds)) {
          ++counts[static_cast<std::size_t>(fish.kind)];
          ++total;
        }
      }
      std::size_t best = 0;
      std::int32_t fewest = ShareScale + 1;
      for (std::size_t i = 0; i < kinds; ++i) {
        // With no fish yet the original divides by zero; every share counts as equal.
        const std::int32_t weight = total * place_.spots[i].weight;
        const std::int32_t share = weight == 0 ? 0 : counts[i] * ShareScale / weight;
        if (share <= fewest) {
          fewest = share;
          best = i;
        }
      }
      spawnFish(static_cast<std::int32_t>(best));
    }
    if (frame_ % static_cast<std::uint32_t>(std::max(rules_.seaSerpent.checkEvery, 1)) == 0 &&
        std::ranges::any_of(place_.seaSerpentTimes,
                            [this](std::int32_t time) {
                              return time != NoSeaSerpent && time / TimeHour == input_->hour;
                            }) &&
        std::ranges::none_of(pool_, [](const Fish& f) { return f.kind == SeaSerpentKind; }) &&
        !hooked_) {
      spawnSeaSerpent();
    }
  }
  for (std::size_t i = 0; i < pool_.size();) {
    tickFish(pool_[i]);
    if (pool_[i].opacity <= 0 && pool_[i].state == FishState::Leaving) {
      pool_.erase(pool_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
}

void Fishing::spawnFish(std::int32_t kind) {
  if (std::cmp_greater_equal(pool_.size(), rules_.population.pool) ||
      std::cmp_greater_equal(kind, tables_.kinds.size())) {
    return;
  }
  const FishKind& info = tables_.kinds[static_cast<std::size_t>(kind)];
  Fish fish{.kind = kind,
            .size = roll(info.size.min, info.size.max),
            .lifetime = roll(rules_.population.lifetime.min, rules_.population.lifetime.max)};
  const std::optional<glm::vec3> from = randomPointInSpot(kind);
  const std::optional<glm::vec3> to = randomPointInSpot(kind);
  if (!from || !to) {
    return;
  }
  fish.position = *from;
  fish.yaw = yawTowards(*from, *to);
  pool_.push_back(fish);
}

void Fishing::spawnSeaSerpent() {
  if (std::cmp_greater_equal(pool_.size(), rules_.population.pool) ||
      place_.seaSerpentPath.size() < 2) {
    return;
  }
  const std::optional<glm::vec3> start = view_->toGround(glm::vec2{place_.seaSerpentPath[0]});
  const std::optional<glm::vec3> next = view_->toGround(glm::vec2{place_.seaSerpentPath[1]});
  if (!start || !next) {
    return;
  }
  Fish serpent{.kind = SeaSerpentKind,
               .size = roll(tables_.seaSerpent.size.min, tables_.seaSerpent.size.max),
               .position = *start,
               .yaw = yawTowards(*start, *next),
               .lifetime = roll(rules_.population.lifetime.min, rules_.population.lifetime.max),
               .pathStep = 1};
  // The original's turn history is one global that keeps the last serpent's turns (zero at
  // boot); a new serpent starts with its whole body along its first heading.
  serpent.turns.fill(serpent.yaw);
  core::logDebug("[port:fishing] FISH_spawnSeadramon (fish.c:2599): the sea serpent appears");
  pool_.push_back(serpent);
}

void Fishing::tickSeaSerpent(Fish& fish) {
  if (fish.pathStep >= place_.seaSerpentPath.size()) {
    fish.speed = std::clamp(fish.speed + SerpentSpeedStep, 0, MaxSwimSpeed);
    fish.position += forward(fish.yaw) * static_cast<float>(fish.speed);
    fish.opacity = std::clamp(fish.opacity - FadeStep, 0, OpaqueFish);
    if (fish.opacity <= 0) {
      fish.state = FishState::Leaving;
    }
  } else if (const std::optional<glm::vec3> target =
                 view_->toGround(glm::vec2{place_.seaSerpentPath[fish.pathStep]})) {
    fish.opacity = std::clamp(fish.opacity + FadeStep, 0, OpaqueFish);
    const std::int32_t turn =
        toOriginalAngle(wrapAngle(yawTowards(fish.position, *target) - fish.yaw));
    fish.speed = std::clamp(fish.speed + (turn != 0 ? -SerpentSpeedStep : SerpentSpeedStep), 0,
                            MaxSwimSpeed);
    fish.yaw += fromOriginalAngle(std::clamp(turn, -SerpentTurn, SerpentTurn));
    fish.position += forward(fish.yaw) * static_cast<float>(fish.speed);
    const auto distance = static_cast<std::int32_t>(groundDistance(fish.position, *target));
    if (distance <= std::max(fish.speed, SerpentReach)) {
      ++fish.pathStep;
    }
    fish.speed = std::max(distance / SerpentSpeedPerDistance, 1);
  } else {
    ++fish.pathStep;
  }
  ++fish.framesOnState;
  ++fish.animationPhase;
  pushSeaSerpentTurn(fish.turns, fish.yaw);
}

void Fishing::tickFish(Fish& fish) {
  if (fish.kind == SeaSerpentKind) {
    tickSeaSerpent(fish);
    return;
  }
  const FishKind& info = tables_.kinds[static_cast<std::size_t>(fish.kind)];
  const auto pickNextMove = [&] {
    fish.timeNextMove = roll(info.moveDelay.min, info.moveDelay.max);
    fish.state = FishState::Idle;
    fish.framesOnState = -1;
  };
  const auto swimAway = [&](glm::vec3 from) {
    fish.yaw = yawTowards(from, fish.position);
    fish.state = FishState::Leaving;
    fish.framesOnState = -1;
  };
  // checkAttractor in FISH_tickFish: the scaring ripple sends the fish away; the bait it likes
  // draws it to the bobber.
  const auto watch = [&] {
    if (const Ripple* scare = ripples_.attractor();
        scare != nullptr &&
        groundDistance(scare->position, fish.position) <= ripples_.attractRadius()) {
      scaredPause_ = rules_.population.scaredPause;
      swimAway(scare->position);
      return;
    }
    if (!info.attractedBy || *info.attractedBy != bait_) {
      return;
    }
    const float distance = groundDistance(bobber_.position, fish.position);
    if (distance <= static_cast<float>(rules_.bites.reach) ||
        distance >= static_cast<float>(rules_.bites.attractReach)) {
      return;
    }
    fish.yaw = yawTowards(fish.position, bobber_.position);
    fish.speed =
        std::clamp(static_cast<std::int32_t>(distance) / SpeedPerDistance, MinApproachSpeed,
                   std::max(MinApproachSpeed, std::min(MaxSwimSpeed, fish.size)));
    fish.state = FishState::Swimming;
    fish.framesOnState = -1;
  };
  switch (fish.state) {
  case FishState::Appearing:
    fish.position += forward(fish.yaw) * AppearSpeed;
    fish.opacity = std::clamp(fish.opacity + FadeStep, 0, OpaqueFish);
    if (fish.opacity == OpaqueFish) {
      pickNextMove();
    }
    break;
  case FishState::Idle:
    if (--fish.lifetime == 0) {
      swimAway(input_->player);
      break;
    }
    if (--fish.timeNextMove == 0) {
      if (const std::optional<glm::vec3> target = randomPointInSpot(fish.kind)) {
        fish.yaw = yawTowards(fish.position, *target);
        const auto distance = static_cast<std::int32_t>(groundDistance(fish.position, *target));
        fish.speed = std::clamp(distance / SpeedPerDistance, 0, std::min(MaxSwimSpeed, fish.size));
      }
      fish.state = FishState::Swimming;
      fish.framesOnState = -1;
      break;
    }
    watch();
    break;
  case FishState::Swimming:
    fish.position += forward(fish.yaw) * static_cast<float>(fish.speed);
    fish.speed -= fish.speed >> SpeedDecayShift;
    if (fish.speed < SlowestSwim) {
      pickNextMove();
      break;
    }
    watch();
    break;
  case FishState::Leaving:
    fish.position += forward(fish.yaw) * LeaveSpeed;
    fish.opacity = std::clamp(fish.opacity - FadeStep, 0, OpaqueFish);
    break;
  default:
    break;
  }
  ++fish.framesOnState;
  ++fish.animationPhase;
}

std::optional<std::size_t> Fishing::findFishToBite() {
  if (hooked_) {
    return std::nullopt;
  }
  const BiteRules& bites = rules_.bites;
  const std::int32_t row = bait_ - tables_.firstBait;
  for (std::size_t i = 0; i < pool_.size(); ++i) {
    const Fish& fish = pool_[i];
    if (groundDistance(fish.position, bobber_.position) > static_cast<float>(bites.reach)) {
      continue;
    }
    std::int32_t chance = 0;
    if (fish.kind == SeaSerpentKind) {
      if (frame_ % static_cast<std::uint32_t>(std::max(rules_.seaSerpent.biteEvery, 1)) != 0) {
        continue;
      }
      chance = rules_.seaSerpent.biteChance;
    } else {
      if (frame_ % static_cast<std::uint32_t>(std::max(bites.every, 1)) != 0 ||
          (fish.state != FishState::Idle && fish.state != FishState::Swimming)) {
        continue;
      }
      if (row >= 0 && std::cmp_less(row, tables_.biteChances.size())) {
        const auto& chances = tables_.biteChances[static_cast<std::size_t>(row)];
        if (std::cmp_less(fish.kind, chances.size())) {
          chance = chances[static_cast<std::size_t>(fish.kind)];
        }
      }
    }
    markers_.spawn(chance != 0, fish.position);
    emitAt(FishingEventKind::Marker, chance == 0 ? 0 : 1, fish.position);
    if (chance < roll(1, 100)) {
      continue;
    }
    return i;
  }
  return std::nullopt;
}

void Fishing::hookFish(std::size_t index) {
  markers_.clear();
  Fish fish = pool_[index];
  pool_.erase(pool_.begin() + static_cast<std::ptrdiff_t>(index));
  fish.yaw = yawTowards(fish.position, bobber_.position);
  fish.state = FishState::Nibbling;
  fish.framesOnState = 0;
  fish.animationPhase = 0;
  const HookRules& hook = rules_.hook;
  const HookTiming& timing = fish.size < hook.largeSize ? hook.small : hook.large;
  nibbleTime_ = roll(timing.nibble.min, timing.nibble.max);
  pullTime_ = timing.pull;
  power_ = fish.size + hook.powerBonus;
  powerMax_ = power_ + hook.powerMaxBonus;
  hookedEscaped_ = false;
  core::logDebug("[port:fishing] FISH_hookFish (fish.c:3115): kind {} size {} nibbles {} frames",
                 fish.kind, fish.size, nibbleTime_);
  hooked_ = fish;
}

void Fishing::hookedEscapes() {
  if (!hooked_) {
    return;
  }
  Fish& fish = *hooked_;
  hookedEscaped_ = true;
  bobberCommand(SinkCommand);
  fish.yaw = yawTowards(input_->player, fish.position);
  fish.state = FishState::Escaping;
  fish.framesOnState = -1;
}

void Fishing::hookedGrabbed() {
  if (!hooked_) {
    return;
  }
  hooked_->state = FishState::Struggling;
  hooked_->framesOnState = -1;
}

bool Fishing::tryStrike() {
  if (hooked_ && hooked_->state == FishState::Pulling && hooked_->framesOnState < pullTime_ / 2) {
    hookedGrabbed();
    return true;
  }
  hookedEscapes();
  return false;
}

void Fishing::steerHookedFromShore() {
  if (!hooked_) {
    return;
  }
  Fish& fish = *hooked_;
  for (int i = 0; i < ShoreTries; ++i) {
    if (onWater(bobber_.position + (forward(fish.yaw) * ShoreProbe))) {
      return;
    }
    if (const std::optional<glm::vec3> target = randomPointInSpot(fish.kind)) {
      fish.yaw = yawTowards(fish.position, *target);
    }
  }
}

void Fishing::tickHooked() {
  if (!hooked_) {
    return;
  }
  Fish& fish = *hooked_;
  const auto approachBobber = [&] {
    const glm::vec3 offset = bobber_.position - fish.position;
    const float distance = glm::length(glm::vec2{offset.x, offset.z});
    if (distance >= MinNibbleDistance) {
      fish.yaw = yawTowards(fish.position, bobber_.position);
      fish.position += forward(fish.yaw) * std::clamp(distance, 1.0F, MaxNibbleStep);
    }
  };
  const auto rippleScale = [&] { return (fish.size * RippleScaleUnit) / RippleSizeDivisor; };
  switch (fish.state) {
  case FishState::Nibbling:
    if (fish.framesOnState != nibbleTime_) {
      if (fish.size < rules_.hook.largeSize) {
        if (roll(1, HintRoll) == 1 && nibbleTime_ - fish.framesOnState >= NibbleHint) {
          bobberCommand(NibbleCommand);
        }
      } else if (roll(1, HintRoll) == 1) {
        bobberCommand(BigNibbleCommand);
      }
      approachBobber();
    } else {
      bobberCommand(BiteCommand);
      baitEaten_ = true;
      emit(FishingEventKind::BaitEaten, bait_);
      fish.state = FishState::Pulling;
      fish.framesOnState = -1;
    }
    break;
  case FishState::Pulling:
    approachBobber();
    if (fish.framesOnState >= pullTime_) {
      hookedEscapes();
    }
    break;
  case FishState::Struggling: {
    if (roll(0, StruggleSoundRoll) == 0) {
      sound(rules_.sounds.struggle);
    }
    if (roll(1, TurnRoll) == 1) {
      fish.yaw = yawTowards(input_->player, fish.position) +
                 fromOriginalAngle(roll(-StruggleTurn, StruggleTurn));
    }
    if (roll(1, SplashRoll) == 1) {
      spawnRipple(0, fish.position, FightScare, fish.size / 2, rippleScale());
      spawnRipple(0, fish.position + (forward(fish.yaw) * static_cast<float>(fish.size)),
                  FightScare, 0, fish.size / 2);
    }
    steerHookedFromShore();
    const std::int32_t divisor = power_ <= powerMax_ / TiredShare ? TiredDivisor : FreshDivisor;
    // The original steps in whole units.
    const std::int32_t step = fish.speed / divisor;
    bobber_.position += forward(fish.yaw) * static_cast<float>(step);
    fish.position = bobber_.position;
    const bool check = pressedFrames_ == -1 ||
                       (pressedFrames_ >= LongHold && pressedFrames_ % LongHoldEvery == 0) ||
                       roll(1, RunRoll) < RunRollBelow;
    if (check && roll(1, PowerScale) <= (power_ * PowerScale) / powerMax_) {
      fish.yaw = yawTowards(input_->player, fish.position) +
                 fromOriginalAngle(roll(-StruggleTurn, StruggleTurn));
      fish.speed = roll(RunSpeed.min, RunSpeed.max);
      nibbleTime_ = roll(RunTime.min, RunTime.max);
      fish.state = FishState::Running;
      fish.framesOnState = -1;
    } else if (state_ != RodState::Reeling) {
      hookedEscapes();
    }
    break;
  }
  case FishState::Running:
    if (fish.framesOnState % 2 == 0) {
      power_ = std::clamp(power_ - 1, 0, powerMax_);
    }
    if (roll(0, StruggleSoundRoll) == 0) {
      sound(rules_.sounds.run);
    }
    spawnRipple(0, fish.position, FightScare, 1, rippleScale());
    steerHookedFromShore();
    bobber_.position += forward(fish.yaw) * static_cast<float>(fish.speed);
    fish.position = bobber_.position;
    if (state_ != RodState::Reeling) {
      hookedEscapes();
    } else if (fish.framesOnState >= nibbleTime_) {
      hookedGrabbed();
    }
    break;
  case FishState::Escaping:
    fish.position += forward(fish.yaw) * LeaveSpeed;
    fish.opacity = std::clamp(fish.opacity - FadeStep, 0, OpaqueFish);
    if (fish.opacity == 0) {
      hooked_.reset();
      return;
    }
    break;
  case FishState::Landing: {
    // FISH_REEL_TARGET is never written: the sea serpent swims to the world's origin.
    const glm::vec3 shore{0.0F};
    const float distance = std::clamp(groundDistance(shore, fish.position), 0.0F, MaxLandingStep);
    fish.yaw = yawTowards(fish.position, shore);
    fish.position += forward(fish.yaw) * distance;
    spawnRipple(0, fish.position, FightScare, 1, rippleScale());
    if (roll(1, TurnRoll) < LandingSoundBelow) {
      sound(rules_.sounds.run);
    }
    break;
  }
  default:
    break;
  }
  ++fish.framesOnState;
  ++fish.animationPhase;
  if (fish.kind == SeaSerpentKind) {
    pushSeaSerpentTurn(fish.turns, fish.yaw);
  }
}

} // namespace dwe::game::fishing
