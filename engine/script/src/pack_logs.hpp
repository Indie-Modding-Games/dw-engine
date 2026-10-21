#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace dwe::script::detail {

// The bytes a pack may write to the log per second, and at once after writing nothing for a while: far more than a
// mod printing what it does, far less than a loop printing every frame.
inline constexpr double LogBytesPerSecond = 16.0 * 1024;
inline constexpr double LogBurstBytes = 64.0 * 1024;

// How often an error that keeps repeating is counted in the log.
inline constexpr std::chrono::seconds RepeatReportInterval{10};

// The lines that packs write to the log, limited per pack so none can flood it: each pack has a bucket of bytes that
// refills at LogBytesPerSecond up to LogBurstBytes, and an error identical to the pack's last one is counted instead
// of written again.
class PackLogs {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    // now gives the time; tests pass a clock they move by hand.
    explicit PackLogs(Now now = Clock::now);

    // Writes an information line of the pack, if its bucket allows.
    void info(std::string_view pack, std::string_view line);

    // Writes an error of the pack, if its bucket allows and it is not the pack's last error again.
    void error(std::string_view pack, std::string_view line);

    // Writes the repeats counted at least RepeatReportInterval ago, so they reach the log after the errors stop.
    // Called once per frame.
    void tick();

    // Writes every repeat counted, whenever, and whatever the buckets hold: the last word on a pack's errors is never
    // left out. Called when the VM closes. The project builds without exceptions (ADR 0007), so nothing it does can
    // throw out of the destructor that calls it.
    void flush() noexcept;

private:
    struct PackLog {
        double bytes = LogBurstBytes;
        Clock::time_point refilled;
        // Lines left out since the bucket ran dry.
        std::size_t dropped = 0;
        std::string lastError;
        // Times lastError came again since it was last written or counted.
        std::size_t repeats = 0;
        Clock::time_point repeatsCounted;
    };

    // Whether a repeat count goes through the pack's bucket, or is written whatever it holds.
    enum class Bucket : std::uint8_t { Charged, Bypassed };

    PackLog& logOf(std::string_view pack);
    // Whether the bucket of the pack has room for the line, taking it from the bucket if so.
    bool admit(PackLog& log, std::string_view pack, std::size_t bytes);
    // Writes how many times the last error repeated, if it did.
    void countRepeats(PackLog& log, std::string_view pack, Bucket bucket = Bucket::Charged);

    Now now_;
    std::map<std::string, PackLog, std::less<>> logs_;
};

} // namespace dwe::script::detail
