#include <dwe/game/partner/need_deadlines.hpp>

#include <algorithm>

namespace dwe::game::partner {
namespace {

// The frames to the next frame of the day that is a multiple of `every`, strictly after `frame`:
// the next one a rule "every so many frames" runs on.
std::int64_t framesToNext(std::int64_t frame, std::int64_t every) {
  return every - (frame % every);
}

// The frames until a counter that steps once every `every` frames has stepped `steps` more times.
std::int64_t framesForSteps(std::int64_t frame, std::int64_t every, std::int64_t steps) {
  if (steps <= 0 || every <= 0) {
    return 0;
  }
  return framesToNext(frame, every) + ((steps - 1) * every);
}

} // namespace

NeedDeadlines needDeadlinesOf(const PartnerState& state, const CareRules& rules,
                              const ruleset::GameClock& clock) {
  NeedDeadlines deadlines;
  const Conditions& conditions = state.conditions;
  const std::int64_t frame = clock.frameOfDay();
  const std::int64_t perHour = clock.rules().framesPerMinute * ruleset::MinutesPerHour;
  const std::int64_t perDay = perHour * ruleset::HoursPerDay;
  if (conditions.has(Condition::Hungry)) {
    deadlines.set(Condition::Hungry,
                  framesForSteps(frame, rules.hunger.windowStepFrames, state.hunger.windowSteps));
  }
  if (conditions.has(Condition::NeedsToilet)) {
    deadlines.set(Condition::NeedsToilet,
                  state.toilet.wildPoopDue ? 0 : std::max(state.toilet.urgeFrames, 0));
  }
  if (conditions.has(Condition::Sleepy) && perDay > 0) {
    const std::int64_t wake = static_cast<std::int64_t>(state.sleep.wakeHour) * perHour;
    deadlines.set(Condition::Sleepy, (((wake - frame) % perDay) + perDay) % perDay);
  }
  if (conditions.has(Condition::Injured) && !conditions.has(Condition::Sick)) {
    deadlines.set(
        Condition::Injured,
        framesForSteps(frame, perHour, rules.sickness.injuryHours - state.health.injuredHours));
  }
  if (conditions.has(Condition::Sick)) {
    deadlines.set(
        Condition::Sick,
        framesForSteps(frame, perHour, rules.sickness.untreatedHours - state.health.sickHours));
  }
  return deadlines;
}

} // namespace dwe::game::partner
