#include <dwe/core/log.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "control_log.hpp"

namespace {

using dwe::app::ControlLog;
using dwe::core::LogLevel;

TEST_CASE("the control log keeps the newest warnings and errors, numbered") {
  ControlLog log{3};
  CHECK(log.newest() == 0);
  CHECK(log.after(std::nullopt).entries.empty());
  log.add(LogLevel::Info, "left out");
  log.add(LogLevel::Debug, "left out too");
  log.add(LogLevel::Warning, "first");
  log.add(LogLevel::Error, "second");
  CHECK(log.newest() == 2);

  const auto all = log.after(std::nullopt);
  CHECK(all.next == 2);
  REQUIRE(all.entries.size() == 2);
  CHECK(all.entries[0].seq == 1);
  CHECK_FALSE(all.entries[0].error);
  CHECK(all.entries[0].text == "first");
  CHECK(all.entries[1].error);
  CHECK(log.after(1).entries.size() == 1);
  CHECK(log.after(2).entries.empty());

  // Past its capacity the oldest go; the numbers carry on.
  log.add(LogLevel::Warning, "third");
  log.add(LogLevel::Warning, "fourth");
  const auto kept = log.after(0);
  REQUIRE(kept.entries.size() == 3);
  CHECK(kept.entries.front().seq == 2);
  CHECK(kept.next == 4);
}

} // namespace
