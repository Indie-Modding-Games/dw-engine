#include <dwe/assets/yaml_reader.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/messages.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace dwe::ruleset {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

// The keys of the clock section.
constexpr std::string_view FramesPerSecondKey = "frames_per_second";
constexpr std::string_view FramesPerMinuteKey = "frames_per_minute";
constexpr std::string_view DaysPerYearKey = "days_per_year";
constexpr std::string_view YearLimitKey = "year_limit";
constexpr std::string_view StartKey = "start";
constexpr std::string_view YearKey = "year";
constexpr std::string_view DayKey = "day";
constexpr std::string_view HourKey = "hour";
constexpr std::string_view MinuteKey = "minute";

std::int32_t readCount(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    const YamlNode& node = reader.field(parent, key);
    const auto value = reader.integer<std::int32_t>(node);
    if (reader.ok() && value <= 0) {
        reader.fail(node, formatMessage(messages::NotPositive, key));
    }
    return value;
}

double readRate(YamlReader& reader, const YamlNode& parent, std::string_view key) {
    const YamlNode& node = reader.field(parent, key);
    const double value = reader.number(node);
    if (reader.ok() && !(value > 0.0)) {
        reader.fail(node, formatMessage(messages::NotPositive, key));
    }
    return value;
}

std::int32_t readBelow(YamlReader& reader, const YamlNode& parent, std::string_view key, std::int64_t limit) {
    const YamlNode& node = reader.field(parent, key);
    const auto value = reader.integer<std::int32_t>(node);
    if (reader.ok() && (value < 0 || value >= limit)) {
        reader.fail(node, formatMessage(messages::OutOfRange, key, limit - 1));
    }
    return value;
}

GameTime readStart(YamlReader& reader, const YamlNode& root, const ClockRules& rules) {
    const YamlNode& start = reader.field(root, StartKey);
    if (!reader.ok() || !reader.expectKeys(start, StartKey, {YearKey, DayKey, HourKey, MinuteKey})) {
        return rules.start;
    }
    return {.year = readBelow(reader, start, YearKey, rules.yearLimit),
            .day = readBelow(reader, start, DayKey, rules.daysPerYear),
            .hour = readBelow(reader, start, HourKey, HoursPerDay),
            .minute = readBelow(reader, start, MinuteKey, MinutesPerHour)};
}

} // namespace

core::Result<ClockRules> readClockRules(const Section& section) {
    YamlReader reader;
    const YamlNode& root = section.node;
    if (!reader.expectKeys(root, messages::ClockSectionText,
                           {FramesPerSecondKey, FramesPerMinuteKey, DaysPerYearKey, YearLimitKey, StartKey})) {
        return std::unexpected{reader.error(section.file)};
    }
    ClockRules rules{.framesPerSecond = readRate(reader, root, FramesPerSecondKey),
                     .framesPerMinute = readCount(reader, root, FramesPerMinuteKey),
                     .daysPerYear = readCount(reader, root, DaysPerYearKey),
                     .yearLimit = readCount(reader, root, YearLimitKey)};
    rules.start = readStart(reader, root, rules);
    return reader.finish(rules, section.file);
}

core::Result<ClockRules> readClockRules(const Ruleset& ruleset) {
    auto section = ruleset.requireSection(ClockSection);
    if (!section) {
        return std::unexpected{section.error()};
    }
    return readClockRules(**section);
}

GameClock::GameClock(const ClockRules& rules) : rules_(rules), elapsed_(framesAt(rules.start)) {}

void GameClock::setRules(const ClockRules& rules) {
    const GameTime now = time();
    const std::int64_t frameOfMinute = frameOfDay() % rules_.framesPerMinute;
    rules_ = rules;
    const GameTime kept{.year = std::min(now.year, rules_.yearLimit - 1),
                        .day = std::min(now.day, rules_.daysPerYear - 1),
                        .hour = now.hour,
                        .minute = now.minute};
    elapsed_ = framesAt(kept) + std::min(frameOfMinute, rules_.framesPerMinute - 1);
}

std::int64_t GameClock::framesAt(const GameTime& time) const {
    return (time.year * framesPerYear()) + (time.day * framesPerDay()) + (time.hour * framesPerHour()) +
           (time.minute * rules_.framesPerMinute);
}

GameTime GameClock::time() const {
    const std::int64_t ofDay = frameOfDay();
    const std::int64_t days = elapsed_ / framesPerDay();
    return {.year = static_cast<std::int32_t>((days / rules_.daysPerYear) % rules_.yearLimit),
            .day = static_cast<std::int32_t>(days % rules_.daysPerYear),
            .hour = static_cast<std::int32_t>(ofDay / framesPerHour()),
            .minute = static_cast<std::int32_t>((ofDay % framesPerHour()) / rules_.framesPerMinute)};
}

std::int64_t GameClock::frameOfDay() const {
    return elapsed_ % framesPerDay();
}

void GameClock::restore(std::int64_t elapsedFrames, double pendingFrames) {
    elapsed_ = std::max<std::int64_t>(elapsedFrames, 0);
    pendingFrames_ = std::max(pendingFrames, 0.0);
}

void GameClock::setSpeed(double multiplier) {
    speed_ = std::max(multiplier, 0.0);
}

ClockAdvance GameClock::advance(double realSeconds) {
    if (!running_ || !(realSeconds > 0.0)) {
        return {};
    }
    pendingFrames_ += realSeconds * rules_.framesPerSecond * speed_;
    const double whole = std::floor(pendingFrames_);
    pendingFrames_ -= whole;
    return moveTo(elapsed_ + static_cast<std::int64_t>(whole));
}

ClockAdvance GameClock::advanceFrames(std::int64_t frames) {
    if (!running_ || frames <= 0) {
        return {};
    }
    return moveTo(elapsed_ + frames);
}

ClockAdvance GameClock::jumpTo(std::int32_t hour, std::int32_t minute) {
    const std::int64_t target = ((hour % HoursPerDay) * framesPerHour()) + (minute * rules_.framesPerMinute);
    const std::int64_t today = elapsed_ - frameOfDay();
    const std::int64_t day = target < frameOfDay() ? today + framesPerDay() : today;
    ClockAdvance jumped = moveTo(day + target);
    // clock.c: advanceToTime moves the clock without the minute and hour rules of tickGameClock.
    jumped.frames = 0;
    jumped.minutes = 0;
    jumped.hours = 0;
    return jumped;
}

ClockAdvance GameClock::moveTo(std::int64_t elapsed) {
    const auto crossed = [&](std::int64_t length) { return (elapsed / length) - (elapsed_ / length); };
    const ClockAdvance advance{.frames = elapsed - elapsed_,
                               .minutes = crossed(rules_.framesPerMinute),
                               .hours = crossed(framesPerHour()),
                               .days = crossed(framesPerDay()),
                               .years = crossed(framesPerYear())};
    elapsed_ = elapsed;
    return advance;
}

} // namespace dwe::ruleset
