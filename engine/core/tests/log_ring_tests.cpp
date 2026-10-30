#include <dwe/core/log_ring.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using dwe::core::LogLevel;
using dwe::core::LogRing;

TEST_CASE("a log ring keeps the lines as the log writes them, oldest first") {
  LogRing ring{4};
  ring.add(LogLevel::Info, "one");
  ring.add(LogLevel::Warning, "two");
  CHECK(ring.lines() == std::vector<std::string>{"[info] one", "[warning] two"});
}

TEST_CASE("a full log ring drops its oldest line") {
  LogRing ring{2};
  ring.add(LogLevel::Debug, "a");
  ring.add(LogLevel::Debug, "b");
  ring.add(LogLevel::Error, "c");
  CHECK(ring.lines() == std::vector<std::string>{"[debug] b", "[error] c"});
}

TEST_CASE("a log ring of no lines keeps nothing") {
  LogRing ring{0};
  ring.add(LogLevel::Info, "lost");
  CHECK(ring.lines().empty());
}

TEST_CASE("a kept log tail receives what the log writes") {
  const auto ring = dwe::core::keepLogTail(8);
  dwe::core::logInfo("tail {}", 7);
  const std::vector<std::string> lines = ring->lines();
  REQUIRE_FALSE(lines.empty());
  CHECK(lines.back() == "[info] tail 7");
}
