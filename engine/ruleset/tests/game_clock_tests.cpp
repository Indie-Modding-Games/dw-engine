#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <ostream>

namespace dwe::ruleset {

namespace {

// The original's clock (clock.c), as the original ruleset's clock section says.
constexpr ClockRules Original{};

constexpr std::int64_t FramesPerMinute = 20;
constexpr std::int64_t FramesPerHour = 1200;
constexpr std::int64_t FramesPerDay = 28800;

} // namespace

TEST_CASE("the clock starts at the start of the rules and counts the original's frames") {
    GameClock clock{Original};
    CHECK(clock.time() == GameTime{.year = 0, .day = 0, .hour = 8, .minute = 0});
    // clock.c: initializeClockData sets CURRENT_FRAME to 9600 for 8:00.
    CHECK(clock.frameOfDay() == 9600);

    // One real second at normal speed is 20 frames: one game minute.
    const ClockAdvance second = clock.advance(1.0);
    CHECK(second.frames == 20);
    CHECK(second.minutes == 1);
    CHECK(second.hours == 0);
    CHECK(clock.time().minute == 1);

    // Fractions of a frame carry over between ticks of 60 Hz.
    std::int64_t frames = 0;
    for (int tick = 0; tick < 60; ++tick) {
        frames += clock.advance(1.0 / 60.0).frames;
    }
    CHECK(frames == 20);
    CHECK(clock.time().minute == 2);
}

TEST_CASE("the clock crosses hours, days and years, and wraps the year") {
    GameClock clock{Original};
    const ClockAdvance toMidnight = clock.advanceFrames(16 * FramesPerHour);
    CHECK(toMidnight.hours == 16);
    CHECK(toMidnight.days == 1);
    CHECK(clock.time() == GameTime{.year = 0, .day = 1, .hour = 0, .minute = 0});
    CHECK(clock.frameOfDay() == 0);

    const ClockAdvance year = clock.advanceFrames(29 * FramesPerDay);
    CHECK(year.days == 29);
    CHECK(year.years == 1);
    CHECK(clock.time() == GameTime{.year = 1, .day = 0, .hour = 0, .minute = 0});

    // clock.c: tickGameClock: the year goes back to 0 at 100.
    clock.advanceFrames(FramesPerDay * 30 * 99);
    CHECK(clock.time().year == 0);
}

TEST_CASE("the clock fast-forwards with its speed, and stops") {
    GameClock clock{Original};
    // A game hour per real second.
    clock.setSpeed(60.0);
    const ClockAdvance hour = clock.advance(1.0);
    CHECK(hour.hours == 1);
    CHECK(hour.minutes == 60);
    CHECK(clock.time().hour == 9);

    // Days in one step: the hourly rules run as many times as hours went by.
    clock.setSpeed(3600.0);
    const ClockAdvance days = clock.advance(2.0);
    CHECK(days.hours == 120);
    CHECK(days.days == 5);
    CHECK(clock.time() == GameTime{.year = 0, .day = 5, .hour = 9, .minute = 0});

    clock.setSpeed(0.0);
    CHECK(clock.advance(10.0).frames == 0);
    clock.setSpeed(-3.0);
    CHECK(clock.speed() == 0.0);

    clock.setSpeed(1.0);
    clock.setRunning(false);
    CHECK(clock.advance(10.0).frames == 0);
    CHECK(clock.advanceFrames(100).frames == 0);
    clock.setRunning(true);
    CHECK(clock.advanceFrames(100).frames == 100);
}

TEST_CASE("jumping to an earlier hour goes to the next day, as sleeping does") {
    GameClock clock{Original};
    clock.advanceFrames(14 * FramesPerHour); // 22:00
    const ClockAdvance jump = clock.jumpTo(7, 30);
    CHECK(jump.days == 1);
    CHECK(jump.hours == 0);
    CHECK(jump.minutes == 0);
    CHECK(clock.time() == GameTime{.year = 0, .day = 1, .hour = 7, .minute = 30});
    // clock.c: advanceToTime sets CURRENT_FRAME to HOUR * 1200 + MINUTE * 20.
    CHECK(clock.frameOfDay() == (FramesPerHour * 7) + (FramesPerMinute * 30));

    const ClockAdvance later = clock.jumpTo(9, 0);
    CHECK(later.days == 0);
    CHECK(clock.time() == GameTime{.year = 0, .day = 1, .hour = 9, .minute = 0});
}

TEST_CASE("new rules keep the time of day") {
    GameClock clock{Original};
    clock.advanceFrames(FramesPerMinute * 90);
    ClockRules faster = Original;
    faster.framesPerMinute = 10;
    clock.setRules(faster);
    CHECK(clock.time() == GameTime{.year = 0, .day = 0, .hour = 9, .minute = 30});
    CHECK(clock.advanceFrames(10).minutes == 1);
}

TEST_CASE("the random numbers repeat with their seed and stay in range") {
    Random first{1234};
    Random second{1234};
    Random other{99};
    std::array<int, 6> counts{};
    bool differs = false;
    for (int roll = 0; roll < 6000; ++roll) {
        const std::int32_t value = first.below(6);
        CHECK(value == second.below(6));
        differs = differs || value != other.below(6);
        REQUIRE(value >= 0);
        REQUIRE(value < 6);
        ++counts[static_cast<std::size_t>(value)];
    }
    CHECK(differs);
    for (const int count : counts) {
        CHECK(count > 850);
        CHECK(count < 1150);
    }
    // math.c: randomLimit(1) is always 0.
    CHECK(first.below(1) == 0);
    CHECK(first.below(0) == 0);
    CHECK_FALSE(first.chance(0));
    CHECK(first.chance(100));
    first.reseed(1234);
    Random again{1234};
    CHECK(first.next() == again.next());
}

} // namespace dwe::ruleset
