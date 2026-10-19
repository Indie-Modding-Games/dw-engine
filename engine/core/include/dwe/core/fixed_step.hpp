#pragma once

#include <algorithm>
#include <cstdint>

namespace dwe::core {

// Splits real time into fixed logic ticks. The game logic always advances by the same step, whatever the frame
// rate, and rendering interpolates between the last two ticks with alpha().
class FixedStep {
public:
    // tickSeconds is the length of one logic tick; maxFrameSeconds caps a long frame (a breakpoint, a window drag)
    // so the logic does not try to catch up all at once.
    constexpr FixedStep(double tickSeconds, double maxFrameSeconds)
        : tickNanoseconds_{toNanoseconds(tickSeconds)}, maxFrameNanoseconds_{toNanoseconds(maxFrameSeconds)} {}

    // Adds the real time of a frame and returns how many ticks the logic must run now.
    constexpr int advance(double frameSeconds) {
        accumulated_ += std::clamp(toNanoseconds(frameSeconds), std::int64_t{0}, maxFrameNanoseconds_);
        int ticks = 0;
        while (accumulated_ >= tickNanoseconds_) {
            accumulated_ -= tickNanoseconds_;
            ++ticks;
        }
        return ticks;
    }

    // How far the current moment is between the last tick and the next one, from 0 to 1.
    [[nodiscard]] constexpr double alpha() const {
        return static_cast<double>(accumulated_) / static_cast<double>(tickNanoseconds_);
    }

    [[nodiscard]] constexpr double tickSeconds() const {
        return static_cast<double>(tickNanoseconds_) / NanosecondsPerSecond;
    }

private:
    static constexpr double NanosecondsPerSecond = 1e9;

    // Time is kept in whole nanoseconds: adding and subtracting doubles drifts, and a frame of exactly N ticks would
    // sometimes run N - 1.
    static constexpr std::int64_t toNanoseconds(double seconds) {
        const double nanoseconds = seconds * NanosecondsPerSecond;
        return static_cast<std::int64_t>(nanoseconds < 0.0 ? nanoseconds - 0.5 : nanoseconds + 0.5);
    }

    std::int64_t tickNanoseconds_;
    std::int64_t maxFrameNanoseconds_;
    std::int64_t accumulated_ = 0;
};

} // namespace dwe::core
