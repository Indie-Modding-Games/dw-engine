#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// The state of the story (docs/architecture/interprete-historia.md, "StoryState"): the event
// scripts' triggers and variables, as the original keeps them in its script state (script.h:
// ScriptState), plus the room a ruleset adds for mods.
namespace dwe::game::story {

// The original's base set. Triggers 0-799 (script_interp.c: getTriggerOffset indexes 100 bytes);
// the disc names none past 716.
inline constexpr std::int32_t BaseTriggerCount = 800;
// Variables 0-255, one byte each in the original (script_interp.c: readPStat).
inline constexpr std::int32_t BaseVariableCount = 256;

// How many triggers and variables a state holds: the base set and what the ruleset adds after it
// (rules/<id>/story.yaml, `capacity`), numbered on from the base's last.
struct StoryCapacity {
  std::int32_t extraTriggers = 0;
  std::int32_t extraVariables = 0;

  [[nodiscard]] std::int32_t triggers() const { return BaseTriggerCount + extraTriggers; }
  [[nodiscard]] std::int32_t variables() const { return BaseVariableCount + extraVariables; }

  friend bool operator==(const StoryCapacity&, const StoryCapacity&) = default;
};

class StoryState {
public:
  // The base set only.
  StoryState();
  // The capacity must not be negative.
  explicit StoryState(StoryCapacity capacity);

  [[nodiscard]] StoryCapacity capacity() const { return capacity_; }
  [[nodiscard]] std::int32_t triggerCount() const { return capacity_.triggers(); }
  [[nodiscard]] std::int32_t variableCount() const { return capacity_.variables(); }

  // Grows or shrinks the state to another capacity: what fits keeps its value, the rest is
  // dropped, and what is new starts clear.
  void setCapacity(StoryCapacity capacity);

  // Whether a trigger is set; a number outside the state is an error.
  [[nodiscard]] core::Result<bool> trigger(std::int32_t trigger) const;
  // Sets or clears a trigger; a number outside the state is an error and changes nothing.
  core::Result<void> setTrigger(std::int32_t trigger, bool set);

  // A variable; a number outside the state is an error. The range of its values is the story
  // rules' (StoryRules), not the state's.
  [[nodiscard]] core::Result<std::int32_t> variable(std::int32_t variable) const;
  core::Result<void> setVariable(std::int32_t variable, std::int32_t value);

  // How many triggers are set, for the log.
  [[nodiscard]] std::size_t triggersSet() const;

  friend bool operator==(const StoryState&, const StoryState&) = default;

private:
  StoryCapacity capacity_;
  std::vector<bool> triggers_;
  std::vector<std::int32_t> variables_;
};

} // namespace dwe::game::story
