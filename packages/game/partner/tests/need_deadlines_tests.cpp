#include <dwe/game/partner/need_deadlines.hpp>
#include <dwe/game/partner/partner.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <ostream>

#include "partner_fixture.hpp"

// The deadlines of the needs (need_deadlines.hpp): each one is the frame the care rules do the
// need's harm on, so playing one frame less does nothing and the next one does it.
namespace dwe::game::partner {

namespace {

using testing::has;
using testing::OriginalRuleset;
using testing::Raising;

std::optional<std::int64_t> deadlineOf(Raising& raising, Condition need) {
  return needDeadlinesOf(raising.state(), raising.rules().care, raising.clock()).framesToHarm(need);
}

// Plays to the need's deadline: nothing on the frame before it, the harm on it.
void checkHarmOnDeadline(Raising& raising, Condition need, EventKind harm,
                         const CareContext& context = {}) {
  const std::optional<std::int64_t> deadline = deadlineOf(raising, need);
  REQUIRE(deadline.has_value());
  REQUIRE(*deadline > 1);
  CHECK_FALSE(has(raising.playFrames(*deadline - 1, context), harm));
  CHECK(deadlineOf(raising, need) == 1);
  CHECK(has(raising.playFrames(1, context), harm));
}

} // namespace

TEST_CASE("need deadlines: hunger does its harm when the window closes") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.hunger.foodLevel = 0;
    state.hunger.energy = 0;
  });
  raising.playFrames(7);
  REQUIRE(raising.state().conditions.has(Condition::Hungry));
  // A window of 90 minutes, minus the frames played.
  CHECK(deadlineOf(raising, Condition::Hungry) == (90 * 20) - 7);
  checkHarmOnDeadline(raising, Condition::Hungry, EventKind::HungerWindowClosed);
}

TEST_CASE("need deadlines: the toilet does its harm when the urge runs out") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.toilet.poopLevel = 1; });
  raising.playMinutes(10);
  REQUIRE(raising.state().conditions.has(Condition::NeedsToilet));
  CHECK(deadlineOf(raising, Condition::NeedsToilet) == raising.state().toilet.urgeFrames);
  checkHarmOnDeadline(raising, Condition::NeedsToilet, EventKind::PoopedOnGround);
}

TEST_CASE("need deadlines: a pile waiting for its scene is due now") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.toilet.poopLevel = 1; });
  const CareContext field{.fieldScenes = true};
  raising.playMinutes(10, field);
  raising.playFrames(raising.state().toilet.urgeFrames, field);
  REQUIRE(raising.partner().wildPoopDue());
  CHECK(deadlineOf(raising, Condition::NeedsToilet) == 0);
}

TEST_CASE("need deadlines: sleep does its harm at the wake hour") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  // The test rookie gets sleepy at 22:00 and wakes at 7:00.
  raising.playUntil(22, 0);
  REQUIRE(raising.state().conditions.has(Condition::Sleepy));
  raising.playMinutes(30);
  CHECK(deadlineOf(raising, Condition::Sleepy) == ((8 * 60) + 30) * 20);
  checkHarmOnDeadline(raising, Condition::Sleepy, EventKind::CareMistake);
  CHECK_FALSE(raising.state().conditions.has(Condition::Sleepy));
}

TEST_CASE("need deadlines: an injury does its harm when it turns into a sickness") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) { state.conditions.set(Condition::Injured); });
  raising.playMinutes(15);
  CHECK(deadlineOf(raising, Condition::Injured) == ((12 * 60) - 15) * 20);
  checkHarmOnDeadline(raising, Condition::Injured, EventKind::GotSick);
}

TEST_CASE("need deadlines: a sickness does its harm when it costs a life") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.health.sickHours = 3;
  });
  CHECK(deadlineOf(raising, Condition::Sick) == 9 * 60 * 20);
  checkHarmOnDeadline(raising, Condition::Sick, EventKind::LostLife);
}

TEST_CASE("need deadlines: an injury on a sick partner, tiredness and absent needs have none") {
  OriginalRuleset original;
  Raising raising(original.rules());
  raising.quiet();
  for (const Condition need : {Condition::Hungry, Condition::NeedsToilet, Condition::Sleepy,
                               Condition::Tired, Condition::Injured, Condition::Sick}) {
    CAPTURE(static_cast<int>(need));
    CHECK_FALSE(deadlineOf(raising, need).has_value());
  }
  raising.edit([](PartnerState& state) {
    state.conditions.set(Condition::Sick);
    state.conditions.set(Condition::Injured);
    state.mood.tiredness = 100;
  });
  raising.playFrames(1);
  REQUIRE(raising.state().conditions.has(Condition::Tired));
  CHECK_FALSE(deadlineOf(raising, Condition::Tired).has_value());
  CHECK_FALSE(deadlineOf(raising, Condition::Injured).has_value());
  CHECK(deadlineOf(raising, Condition::Sick).has_value());
}

} // namespace dwe::game::partner
