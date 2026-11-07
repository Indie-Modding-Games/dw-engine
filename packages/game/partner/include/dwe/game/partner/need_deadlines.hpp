#pragma once

#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/game_clock.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace dwe::game::partner {

// How soon each of the partner's needs does lasting harm if nobody answers it, in frames of the
// clock from now: what an engine improvement ranks the needs by (the stacked need bubble), read
// from the same timers the care rules run (partner_tick.cpp, docs/reference/care.md). No rule reads
// it. The harm is the first lasting one each need brings:
//   - hunger: the window closes, a care mistake (tickHunger: windowSteps, one every
//     hunger.window_step_frames);
//   - toilet: the urge runs out and the partner goes on the ground, a care mistake (tickPoop:
//     urgeFrames, one a frame; 0 once a pile waits for its scene);
//   - sleep: the wake hour comes with the partner never put to bed, a care mistake (tickSleep:
//     missSleep);
//   - injury: injury_hours hours injured turn into a sickness (tickSickness: injuredHours, one an
//     hour); none while the partner is already sick, which it cannot fall again;
//   - sickness: untreated_hours hours sick cost a life (tickSickness: sickHours, one an hour).
// Tiredness has no countdown: left alone it only costs happiness now and then and makes the
// partner hungry sooner, so it has no deadline. Neither has a need the partner does not have.
// What holds a timer back for a while (the player busy elsewhere, the hour after waking) is not
// counted.
class NeedDeadlines {
public:
  [[nodiscard]] std::optional<std::int64_t> framesToHarm(Condition need) const {
    return frames_.at(static_cast<std::size_t>(need));
  }
  void set(Condition need, std::optional<std::int64_t> frames) {
    frames_.at(static_cast<std::size_t>(need)) = frames;
  }

  friend bool operator==(const NeedDeadlines&, const NeedDeadlines&) = default;

private:
  std::array<std::optional<std::int64_t>, ConditionCount> frames_{};
};

// The deadlines of the needs the partner has now, on this clock.
[[nodiscard]] NeedDeadlines needDeadlinesOf(const PartnerState& state, const CareRules& rules,
                                            const ruleset::GameClock& clock);

} // namespace dwe::game::partner
