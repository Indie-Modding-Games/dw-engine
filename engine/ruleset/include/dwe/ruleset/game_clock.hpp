#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::ruleset {

// The section of the game clock.
inline constexpr std::string_view ClockSection = "clock";

// The calendar is fixed; how long a minute lasts and how many days a year has are rules.
inline constexpr std::int64_t MinutesPerHour = 60;
inline constexpr std::int64_t HoursPerDay = 24;

// A moment of the game's calendar.
struct GameTime {
    std::int32_t year = 0;
    std::int32_t day = 0; // of the year, from 0
    std::int32_t hour = 0;
    std::int32_t minute = 0;

    friend bool operator==(const GameTime&, const GameTime&) = default;
};

// The `clock` section. The original counts time in frames of its game loop: the care rules run every so many frames,
// so the clock keeps that unit and the systems can count in it.
struct ClockRules {
    // Frames of the original's game loop per real second at normal speed.
    double framesPerSecond = 20.0;
    // Frames per game minute.
    std::int64_t framesPerMinute = 20;
    // Days per game year.
    std::int32_t daysPerYear = 30;
    // The year goes back to 0 when it reaches this.
    std::int32_t yearLimit = 100;
    // When a new game starts.
    GameTime start{.year = 0, .day = 0, .hour = 8, .minute = 0};
};

// Reads the clock section; errors name the file, line and column.
core::Result<ClockRules> readClockRules(const Section& section);

// Reads the clock section of a ruleset, which must have one.
core::Result<ClockRules> readClockRules(const Ruleset& ruleset);

// What crossing a stretch of game time went through: how many frames, and how many minute, hour, day and year
// boundaries. A system that does something every hour loops `hours` times, so a fast-forward of days does the same as
// playing them.
struct ClockAdvance {
    std::int64_t frames = 0;
    std::int64_t minutes = 0;
    std::int64_t hours = 0;
    std::int64_t days = 0;
    std::int64_t years = 0;
};

// The game clock: the game time of the original, driven by real time, with a speed multiplier that tests, the
// autotest, simulations and the debug panel set at runtime. The partner, battle and field read it; whoever owns the
// game loop advances it.
class GameClock {
public:
    explicit GameClock(const ClockRules& rules);

    [[nodiscard]] const ClockRules& rules() const { return rules_; }

    // New rules after a ruleset change; the time of day stays.
    void setRules(const ClockRules& rules);

    [[nodiscard]] GameTime time() const;

    // Frames since midnight, the original's frame counter (clock.c: CURRENT_FRAME): rules that run "every 200 frames"
    // run when this is a multiple of 200.
    [[nodiscard]] std::int64_t frameOfDay() const;

    // Frames since year 0, day 0 at midnight.
    [[nodiscard]] std::int64_t elapsedFrames() const { return elapsed_; }

    // The fraction of a frame that real time has run past the last whole frame; advance carries it over.
    [[nodiscard]] double pendingFrames() const { return pendingFrames_; }

    // Puts the clock back at a saved moment (elapsedFrames and pendingFrames), as loading a game does. Negative
    // values are taken as 0.
    void restore(std::int64_t elapsedFrames, double pendingFrames);

    // The fast-forward multiplier: 1 is the original's speed, 0 stops the clock, 60 runs a game hour per real minute.
    // Negative values are taken as 0.
    void setSpeed(double multiplier);
    [[nodiscard]] double speed() const { return speed_; }

    // Whether game time runs; the original stops it outside the field, in menus and while fishing (clock.c:
    // stopGameTime, startGameTime). Who stops it is the caller's business.
    void setRunning(bool running) { running_ = running; }
    [[nodiscard]] bool running() const { return running_; }

    // Advances by real seconds at the current speed, while running. Fractions of a frame carry over.
    ClockAdvance advance(double realSeconds);

    // Advances by whole frames, whatever the speed, while running.
    ClockAdvance advanceFrames(std::int64_t frames);

    // Jumps to an hour and minute, on the next day if it is earlier than now, as sleeping does (clock.c:
    // advanceToTime). Only the day and year boundaries count: the original does not run the hourly rules for the
    // jumped time, the sleep rules account for it.
    ClockAdvance jumpTo(std::int32_t hour, std::int32_t minute);

private:
    [[nodiscard]] std::int64_t framesPerHour() const { return rules_.framesPerMinute * MinutesPerHour; }
    [[nodiscard]] std::int64_t framesPerDay() const { return framesPerHour() * HoursPerDay; }
    [[nodiscard]] std::int64_t framesPerYear() const { return framesPerDay() * rules_.daysPerYear; }

    // The frames from year 0, day 0 at midnight to that moment.
    [[nodiscard]] std::int64_t framesAt(const GameTime& time) const;

    ClockAdvance moveTo(std::int64_t elapsed);

    ClockRules rules_;
    std::int64_t elapsed_ = 0;
    double pendingFrames_ = 0.0;
    double speed_ = 1.0;
    bool running_ = true;
};

} // namespace dwe::ruleset
