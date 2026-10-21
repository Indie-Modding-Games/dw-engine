#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <string>

#include "pack_logs.hpp"
#include "script_test_support.hpp"

using namespace dwe;
using script::detail::PackLogs;
using script::testing::LogCapture;

namespace {

// A clock the tests move by hand.
struct FakeClock {
    PackLogs::Clock::time_point now;

    [[nodiscard]] PackLogs::Now function() {
        return [this] { return now; };
    }
};

constexpr std::string_view Error = "mod my_mod: a handler of test.tick failed: always";

} // namespace

TEST_CASE("the repeats of an error reach the log a while after the errors stop") {
    constexpr int Errors = 5;
    const LogCapture log;
    FakeClock clock;
    PackLogs logs{clock.function()};
    for (int error = 0; error < Errors; ++error) {
        logs.error("my_mod", Error);
    }
    CHECK(LogCapture::count(Error) == 1);
    clock.now += script::detail::RepeatReportInterval / 2;
    logs.tick();
    CHECK_FALSE(log.contains("repeated"));
    clock.now += script::detail::RepeatReportInterval;
    logs.tick();
    CHECK(log.contains("mod my_mod: the last error repeated 4 times"));
    logs.flush();
    CHECK(LogCapture::count("repeated") == 1);
}

TEST_CASE("the last repeat count is written when the VM closes, however full the pack's bucket") {
    constexpr std::size_t LineBytes = 1024;
    const LogCapture log;
    FakeClock clock;
    PackLogs logs{clock.function()};
    logs.error("my_mod", Error);
    logs.error("my_mod", Error);
    // The clock stands still, so the bucket never refills.
    const std::string line(LineBytes, 'x');
    // One line more than the burst holds.
    const auto lines = (static_cast<std::size_t>(script::detail::LogBurstBytes) / LineBytes) + 1;
    for (std::size_t written = 0; written < lines; ++written) {
        logs.info("my_mod", line);
    }
    CHECK(log.contains("writes more than"));
    logs.flush();
    CHECK(log.contains("mod my_mod: the last error repeated 1 times"));
}
