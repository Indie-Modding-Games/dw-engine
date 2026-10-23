#include <dwe/core/log.hpp>
#include <dwe/game/story/story_routines.hpp>

#include <utility>

#include "routine_messages.hpp"

namespace dwe::game::story {

core::Result<void> RoutineTable::registerRoutine(RoutineNumber routine, RoutineHandler handler) {
  if (!handler) {
    return core::fail(messages::formatMessage(messages::RoutineEmpty, routine));
  }
  if (!handlers_.try_emplace(routine, Entry{.handler = std::move(handler)}).second) {
    return core::fail(messages::formatMessage(messages::RoutineTaken, routine));
  }
  return {};
}

bool RoutineTable::registered(RoutineNumber routine) const {
  return handlers_.contains(routine);
}

core::Result<RoutineStart> RoutineTable::call(RoutineNumber routine) const {
  const auto found = handlers_.find(routine);
  if (found == handlers_.end()) {
    core::logWarning("{}", messages::formatMessage(messages::PortRoutineUnknown, routine));
    return RoutineGoesOn{};
  }
  core::logDebug("{}", messages::formatMessage(messages::PortCallRoutine, routine));
  return found->second.handler(routine);
}

} // namespace dwe::game::story
