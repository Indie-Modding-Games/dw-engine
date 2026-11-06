#include <dwe/core/message.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstddef>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

bool inRange(std::int32_t number, std::int32_t count) {
  return number >= 0 && number < count;
}

} // namespace

core::Result<bool> StoryState::trigger(std::int32_t trigger) const {
  if (!inRange(trigger, TriggerCount)) {
    return core::fail(core::formatMessage(messages::TriggerOutOfRange, trigger, TriggerCount - 1));
  }
  return triggers_.test(static_cast<std::size_t>(trigger));
}

core::Result<void> StoryState::setTrigger(std::int32_t trigger, bool set) {
  if (!inRange(trigger, TriggerCount)) {
    return core::fail(core::formatMessage(messages::TriggerOutOfRange, trigger, TriggerCount - 1));
  }
  triggers_.set(static_cast<std::size_t>(trigger), set);
  return {};
}

core::Result<std::uint8_t> StoryState::variable(std::int32_t variable) const {
  if (!inRange(variable, VariableCount)) {
    return core::fail(
        core::formatMessage(messages::VariableOutOfRange, variable, VariableCount - 1));
  }
  return variables_[static_cast<std::size_t>(variable)];
}

core::Result<void> StoryState::setVariable(std::int32_t variable, std::uint8_t value) {
  if (!inRange(variable, VariableCount)) {
    return core::fail(
        core::formatMessage(messages::VariableOutOfRange, variable, VariableCount - 1));
  }
  variables_[static_cast<std::size_t>(variable)] = value;
  return {};
}

} // namespace dwe::game::story
