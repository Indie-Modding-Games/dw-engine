#include <dwe/game/partner/field_animation.hpp>

#include <array>
#include <cstddef>
#include <utility>

namespace dwe::game::partner {

namespace {

bool slowed(const Conditions& conditions) {
  return conditions.has(Condition::Sleepy) || conditions.has(Condition::Tired) ||
         conditions.has(Condition::Injured) || conditions.has(Condition::Sick);
}

} // namespace

std::string_view nameOf(FieldAnimation animation) {
  constexpr std::array<std::string_view, 13> Names{
      "idle", "idle_tired", "walk",   "walk_tired", "run",   "happy",  "unhappy",
      "hurt", "hungry",     "toilet", "sleepy",     "tired", "sulking"};
  return Names.at(std::to_underlying(animation));
}

bool isIdle(FieldAnimation animation) {
  return animation == FieldAnimation::Idle || animation == FieldAnimation::IdleTired;
}

FieldAnimation idleAnimation(const Conditions& conditions) {
  return slowed(conditions) ? FieldAnimation::IdleTired : FieldAnimation::Idle;
}

FieldAnimation walkAnimation(const Conditions& conditions) {
  return slowed(conditions) ? FieldAnimation::WalkTired : FieldAnimation::Walk;
}

FieldAnimation conditionAnimation(const Conditions& conditions, std::int32_t happiness,
                                  MoodGestures mood) {
  if (conditions.none()) {
    if (happiness < mood.unhappyBelow) {
      return FieldAnimation::Unhappy;
    }
    if (happiness >= mood.happyFrom) {
      return FieldAnimation::Happy;
    }
    return idleAnimation(conditions);
  }
  constexpr std::array<std::pair<Condition, FieldAnimation>, 6> Order{{
      {Condition::Injured, FieldAnimation::Hurt},
      {Condition::NeedsToilet, FieldAnimation::Toilet},
      {Condition::Hungry, FieldAnimation::Hungry},
      {Condition::Tired, FieldAnimation::Tired},
      {Condition::Sleepy, FieldAnimation::Sleepy},
      {Condition::Sulking, FieldAnimation::Sulking},
  }};
  // Sick shows as injured (condition 0x20 or 0x40).
  if (conditions.has(Condition::Sick)) {
    return FieldAnimation::Hurt;
  }
  for (const auto& [condition, animation] : Order) {
    if (conditions.has(condition)) {
      return animation;
    }
  }
  return idleAnimation(conditions);
}

FieldAnimation statusAnimation(const Conditions& conditions) {
  if (conditions.none()) {
    return FieldAnimation::Idle;
  }
  return conditionAnimation(conditions, 0, {});
}

} // namespace dwe::game::partner
