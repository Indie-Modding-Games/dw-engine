#include <dwe/game/partner/field_animation.hpp>

#include <doctest/doctest.h>

#include <initializer_list>

// partner.c: setPartnerIdle, setPartnerSlowWalking and updateConditionAnimation.
namespace dwe::game::partner {

namespace {

constexpr MoodGestures Mood{.happyFrom = 31, .unhappyBelow = -30};

Conditions conditionsOf(std::initializer_list<Condition> list) {
  Conditions conditions;
  for (const Condition condition : list) {
    conditions.set(condition);
  }
  return conditions;
}

} // namespace

TEST_CASE("a tired, sleepy, injured or sick partner stands and walks tired") {
  CHECK(idleAnimation({}) == FieldAnimation::Idle);
  CHECK(walkAnimation({}) == FieldAnimation::Walk);
  for (const Condition condition :
       {Condition::Sleepy, Condition::Tired, Condition::Injured, Condition::Sick}) {
    CHECK(idleAnimation(conditionsOf({condition})) == FieldAnimation::IdleTired);
    CHECK(walkAnimation(conditionsOf({condition})) == FieldAnimation::WalkTired);
  }
  for (const Condition condition :
       {Condition::Hungry, Condition::NeedsToilet, Condition::Sulking}) {
    CHECK(idleAnimation(conditionsOf({condition})) == FieldAnimation::Idle);
    CHECK(walkAnimation(conditionsOf({condition})) == FieldAnimation::Walk);
  }
}

TEST_CASE("without a condition the gesture shows the mood") {
  CHECK(conditionAnimation({}, -31, Mood) == FieldAnimation::Unhappy);
  CHECK(conditionAnimation({}, -30, Mood) == FieldAnimation::Idle);
  CHECK(conditionAnimation({}, 30, Mood) == FieldAnimation::Idle);
  CHECK(conditionAnimation({}, 31, Mood) == FieldAnimation::Happy);
}

TEST_CASE("the first condition in the original's order picks the gesture") {
  CHECK(conditionAnimation(conditionsOf({Condition::Sick}), 0, Mood) == FieldAnimation::Hurt);
  CHECK(conditionAnimation(conditionsOf({Condition::Injured, Condition::Hungry}), 0, Mood) ==
        FieldAnimation::Hurt);
  CHECK(conditionAnimation(conditionsOf({Condition::NeedsToilet, Condition::Hungry}), 0, Mood) ==
        FieldAnimation::Toilet);
  CHECK(conditionAnimation(conditionsOf({Condition::Hungry, Condition::Tired}), 0, Mood) ==
        FieldAnimation::Hungry);
  CHECK(conditionAnimation(conditionsOf({Condition::Tired, Condition::Sleepy}), 0, Mood) ==
        FieldAnimation::Tired);
  CHECK(conditionAnimation(conditionsOf({Condition::Sleepy, Condition::Sulking}), 0, Mood) ==
        FieldAnimation::Sleepy);
  CHECK(conditionAnimation(conditionsOf({Condition::Sulking}), 99, Mood) ==
        FieldAnimation::Sulking);
}

TEST_CASE("the status page's model shows its first need, else stands") {
  CHECK(statusAnimation({}) == FieldAnimation::Idle);
  CHECK(statusAnimation(conditionsOf({Condition::NeedsToilet})) == FieldAnimation::Toilet);
  CHECK(statusAnimation(conditionsOf({Condition::NeedsToilet, Condition::Hungry})) ==
        FieldAnimation::Toilet);
  CHECK(statusAnimation(conditionsOf({Condition::Hungry, Condition::Sleepy})) ==
        FieldAnimation::Hungry);
  CHECK(statusAnimation(conditionsOf({Condition::Sleepy})) == FieldAnimation::Sleepy);
  CHECK(statusAnimation(conditionsOf({Condition::Sick, Condition::NeedsToilet})) ==
        FieldAnimation::Hurt);
}

TEST_CASE("the field animations have the model's names") {
  CHECK(nameOf(FieldAnimation::IdleTired) == "idle_tired");
  CHECK(nameOf(FieldAnimation::Sulking) == "sulking");
  CHECK(isIdle(FieldAnimation::IdleTired));
  CHECK_FALSE(isIdle(FieldAnimation::Happy));
}

} // namespace dwe::game::partner
