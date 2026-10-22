#include <dwe/core/assert.hpp>
#include <dwe/game/story/story_orders.hpp>

#include <algorithm>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

// math.c: randomLimit(limit) is (limit * rand()) >> 15.
constexpr int RandomBits = 15;

// current + amount, with current in 0..highest, stopping at 0 and highest; never overflows.
std::int64_t addInRange(std::int64_t current, std::int64_t amount, std::int64_t highest) {
  if (amount >= highest - current) {
    return highest;
  }
  if (amount <= -current) {
    return 0;
  }
  return current + amount;
}

// current - amount, with current in 0..highest, stopping at 0 and highest; never overflows.
std::int64_t subtractInRange(std::int64_t current, std::int64_t amount, std::int64_t highest) {
  if (amount >= current) {
    return 0;
  }
  if (amount <= current - highest) {
    return highest;
  }
  return current - amount;
}

} // namespace

StoryOrders::StoryOrders(StoryStatePort& state, StoryRandomPort& random, StoryRules rules)
    : state_{&state}, random_{&random}, rules_{rules} {
  DWE_ASSERT(rules_.variableMax > 0);
}

core::Result<void> StoryOrders::setTrigger(std::int64_t trigger) {
  return checkTrigger(trigger).and_then([&] { return state_->setTrigger(trigger, true); });
}

core::Result<void> StoryOrders::unsetTrigger(std::int64_t trigger) {
  return checkTrigger(trigger).and_then([&] { return state_->setTrigger(trigger, false); });
}

core::Result<void> StoryOrders::setVariable(std::int64_t variable, std::int64_t value) {
  return writeVariable(variable, inVariableRange(value));
}

core::Result<void> StoryOrders::addToVariable(std::int64_t variable, std::int64_t amount) {
  // script_interp.c: scriptInstruction28to3F stops a sum past 255 at 255.
  return readVariable(variable).and_then([&](std::int64_t current) {
    return writeVariable(variable, addInRange(current, amount, rules_.variableMax));
  });
}

core::Result<void> StoryOrders::reduceVariable(std::int64_t variable, std::int64_t amount) {
  return readVariable(variable).and_then([&](std::int64_t current) {
    return writeVariable(variable, subtractInRange(current, amount, rules_.variableMax));
  });
}

core::Result<void> StoryOrders::copyVariable(std::int64_t source, std::int64_t target) {
  return readVariable(source).and_then(
      [&](std::int64_t value) { return writeVariable(target, value); });
}

core::Result<void> StoryOrders::storeRandom(std::int64_t variable, std::int64_t highest) {
  const std::int64_t limit = inVariableRange(highest) + 1;
  const std::int64_t r = random_->next();
  DWE_ASSERT(r >= 0 && r <= StoryRandomPort::RandomMax);
  return writeVariable(variable, (limit * r) >> RandomBits);
}

FrameWait StoryOrders::delay(std::int64_t frames) {
  return FrameWait{.frames = std::max<std::int64_t>(frames, 1), .result = {}};
}

core::Result<void> StoryOrders::checkTrigger(std::int64_t trigger) const {
  if (trigger < 0 || trigger >= state_->triggerCount()) {
    return core::fail(
        messages::formatMessage(messages::NoSuchTrigger, trigger, state_->triggerCount()));
  }
  return {};
}

core::Result<void> StoryOrders::checkVariable(std::int64_t variable) const {
  if (variable < 0 || variable >= state_->variableCount()) {
    return core::fail(
        messages::formatMessage(messages::NoSuchVariable, variable, state_->variableCount()));
  }
  return {};
}

core::Result<std::int64_t> StoryOrders::readVariable(std::int64_t variable) const {
  return checkVariable(variable).and_then([&] { return state_->variable(variable); });
}

core::Result<void> StoryOrders::writeVariable(std::int64_t variable, std::int64_t value) {
  return checkVariable(variable).and_then([&] { return state_->setVariable(variable, value); });
}

std::int64_t StoryOrders::inVariableRange(std::int64_t value) const {
  return std::clamp<std::int64_t>(value, 0, rules_.variableMax);
}

} // namespace dwe::game::story
