// The clock scenario: plays the game clock for --days days at --speed in frames of uneven length, as a game loop does,
// and checks that it crossed as many minutes, hours, days and years as advancing the same frames at once. The same
// days at any speed: what the rules that run every hour or every day count on (docs/architecture/rulesets.md, "Reloj
// de juego").

#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/sim/scenario.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace dwe::sim {

namespace {

// A frame of the game loop lasts from 1/240 to 1/15 of a second, chosen by the run's seed.
constexpr std::int32_t FrameChoices = 16;
constexpr double ShortestFrameSeconds = 1.0 / 240.0;
constexpr double FrameStepSeconds = 1.0 / 240.0;
constexpr double Mismatch = 1.0;
constexpr double Match = 0.0;

// The metrics.
constexpr std::string_view DaysMetric = "days_advanced";
constexpr std::string_view HoursMetric = "hours_advanced";
constexpr std::string_view MinutesMetric = "minutes_advanced";
constexpr std::string_view ReferenceDaysMetric = "days_reference";
constexpr std::string_view MismatchMetric = "mismatch";
constexpr std::string_view LoopFramesMetric = "loop_frames";

void addTo(ruleset::ClockAdvance& total, const ruleset::ClockAdvance& step) {
    total.frames += step.frames;
    total.minutes += step.minutes;
    total.hours += step.hours;
    total.days += step.days;
    total.years += step.years;
}

bool sameCrossings(const ruleset::ClockAdvance& played, const ruleset::ClockAdvance& reference) {
    return played.frames == reference.frames && played.minutes == reference.minutes &&
           played.hours == reference.hours && played.days == reference.days && played.years == reference.years;
}

class ClockScenario final : public Scenario {
public:
    core::Result<void> setup(const ScenarioContext& context) override {
        auto rules = ruleset::readClockRules(*context.rules);
        if (!rules) {
            return std::unexpected{rules.error()};
        }
        rules_ = *rules;
        speed_ = context.speed;
        frames_ = context.days * rules_.framesPerMinute * ruleset::MinutesPerHour * ruleset::HoursPerDay;
        return {};
    }

    [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
        ruleset::GameClock reference{rules_};
        const ruleset::ClockAdvance expected = reference.advanceFrames(frames_);

        ruleset::Random random{seed};
        ruleset::GameClock clock{rules_};
        clock.setSpeed(speed_);
        ruleset::ClockAdvance played;
        std::int64_t loopFrames = 0;
        // A stopped clock never gets there: its days are what a whole frame count gives at once.
        while (speed_ > 0.0 && played.frames < frames_) {
            const double seconds = ShortestFrameSeconds + (FrameStepSeconds * random.below(FrameChoices));
            const double framesThisStep = seconds * rules_.framesPerSecond * speed_;
            if (static_cast<double>(frames_ - played.frames) < std::ceil(framesThisStep)) {
                break;
            }
            addTo(played, clock.advance(seconds));
            ++loopFrames;
        }
        // The last stretch is shorter than a frame of the loop: it goes by whole frames.
        addTo(played, clock.advanceFrames(frames_ - played.frames));
        return Metrics{
            {.name = std::string{DaysMetric}, .value = static_cast<double>(played.days)},
            {.name = std::string{HoursMetric}, .value = static_cast<double>(played.hours)},
            {.name = std::string{MinutesMetric}, .value = static_cast<double>(played.minutes)},
            {.name = std::string{ReferenceDaysMetric}, .value = static_cast<double>(expected.days)},
            {.name = std::string{MismatchMetric}, .value = sameCrossings(played, expected) ? Match : Mismatch},
            {.name = std::string{LoopFramesMetric}, .value = static_cast<double>(loopFrames)},
        };
    }

private:
    ruleset::ClockRules rules_;
    double speed_ = 1.0;
    std::int64_t frames_ = 0;
};

const ScenarioRegistration registration{{
    .name = "clock",
    .description = "Plays the game clock for --days days at --speed in uneven frames and checks it crosses the same "
                   "minutes, hours and days as advancing those frames at once.",
    .create = [] { return std::make_unique<ClockScenario>(); },
}};

} // namespace

} // namespace dwe::sim
