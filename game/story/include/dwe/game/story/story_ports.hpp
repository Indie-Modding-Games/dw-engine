#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>

// What the story orders read and change outside the runner
// (docs/architecture/interprete-historia.md, "Dónde vive"). The session implements them; the tests
// use fakes.
namespace dwe::game::story {

// The story's triggers and variables. The original has 800 triggers and 256 variables (script.h:
// ScriptState); that is the base set, and the owner of the state may hold more for mods, so it says
// how many it holds. A trigger or variable it does not hold is an error, which the script that
// asked for it sees as a Lua error.
class StoryStatePort {
public:
  StoryStatePort() = default;
  StoryStatePort(const StoryStatePort&) = delete;
  StoryStatePort& operator=(const StoryStatePort&) = delete;
  StoryStatePort(StoryStatePort&&) = delete;
  StoryStatePort& operator=(StoryStatePort&&) = delete;
  virtual ~StoryStatePort() = default;

  // How many triggers and variables the state holds: they are numbered from 0.
  [[nodiscard]] virtual std::int64_t triggerCount() const = 0;
  [[nodiscard]] virtual std::int64_t variableCount() const = 0;

  [[nodiscard]] virtual core::Result<bool> trigger(std::int64_t trigger) const = 0;
  virtual core::Result<void> setTrigger(std::int64_t trigger, bool set) = 0;
  [[nodiscard]] virtual core::Result<std::int64_t> variable(std::int64_t variable) const = 0;
  virtual core::Result<void> setVariable(std::int64_t variable, std::int64_t value) = 0;
};

// The session's random numbers, as the C library's rand() gave them to the original: 0 to
// RandomMax.
class StoryRandomPort {
public:
  static constexpr std::int64_t RandomMax = 0x7FFF;

  StoryRandomPort() = default;
  StoryRandomPort(const StoryRandomPort&) = delete;
  StoryRandomPort& operator=(const StoryRandomPort&) = delete;
  StoryRandomPort(StoryRandomPort&&) = delete;
  StoryRandomPort& operator=(StoryRandomPort&&) = delete;
  virtual ~StoryRandomPort() = default;

  // The next number, from 0 to RandomMax.
  virtual std::int64_t next() = 0;
};

} // namespace dwe::game::story
