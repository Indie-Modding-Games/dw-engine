#include <dwe/core/assert.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/story/story_state.hpp>

#include <algorithm>
#include <cstddef>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

bool inRange(std::int32_t number, std::int32_t count) {
  return number >= 0 && number < count;
}

} // namespace

StoryState::StoryState() : StoryState(StoryCapacity{}) {}

StoryState::StoryState(StoryCapacity capacity) {
  setCapacity(capacity);
}

void StoryState::setCapacity(StoryCapacity capacity) {
  DWE_ASSERT(capacity.extraTriggers >= 0 && capacity.extraVariables >= 0);
  capacity_ = capacity;
  triggers_.resize(static_cast<std::size_t>(capacity.triggers()), false);
  variables_.resize(static_cast<std::size_t>(capacity.variables()), 0);
}

core::Result<bool> StoryState::trigger(std::int32_t trigger) const {
  if (!inRange(trigger, triggerCount())) {
    return core::fail(
        core::formatMessage(messages::TriggerOutOfRange, trigger, triggerCount() - 1));
  }
  return static_cast<bool>(triggers_[static_cast<std::size_t>(trigger)]);
}

core::Result<void> StoryState::setTrigger(std::int32_t trigger, bool set) {
  if (!inRange(trigger, triggerCount())) {
    return core::fail(
        core::formatMessage(messages::TriggerOutOfRange, trigger, triggerCount() - 1));
  }
  triggers_[static_cast<std::size_t>(trigger)] = set;
  return {};
}

core::Result<std::int32_t> StoryState::variable(std::int32_t variable) const {
  if (!inRange(variable, variableCount())) {
    return core::fail(
        core::formatMessage(messages::VariableOutOfRange, variable, variableCount() - 1));
  }
  return variables_[static_cast<std::size_t>(variable)];
}

core::Result<void> StoryState::setVariable(std::int32_t variable, std::int32_t value) {
  if (!inRange(variable, variableCount())) {
    return core::fail(
        core::formatMessage(messages::VariableOutOfRange, variable, variableCount() - 1));
  }
  variables_[static_cast<std::size_t>(variable)] = value;
  return {};
}

std::size_t StoryState::triggersSet() const {
  return static_cast<std::size_t>(std::ranges::count(triggers_, true));
}

} // namespace dwe::game::story
