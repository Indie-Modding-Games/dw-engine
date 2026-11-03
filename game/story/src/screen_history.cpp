#include <dwe/core/log.hpp>
#include <dwe/game/story/screen_history.hpp>

#include "routine_messages.hpp"

namespace dwe::game::story {

void ScreenHistory::start(std::int32_t screen, std::int32_t exit) {
  place_ = {.screen = screen, .exit = exit, .previousScreen = screen, .previousExit = exit};
}

void ScreenHistory::enter(std::int32_t screen, std::int32_t exit) {
  const std::int32_t left = place_.screen;
  place_.previousScreen = left;
  // map.c: changeMap checks every special case in order, so a later one wins.
  for (const PreviousScreenOverride& rule : rules_.overrides) {
    if ((rule.from && *rule.from != left) || rule.to != screen) {
      continue;
    }
    place_.previousScreen = rule.previous;
    if (rule.previousExit) {
      place_.previousExit = *rule.previousExit;
    }
    core::logDebug("{}", messages::formatMessage(messages::PortScreenOverride, left, screen,
                                                 rule.previous, place_.previousExit));
  }
  place_.screen = screen;
  place_.exit = exit;
  core::logDebug("{}", messages::formatMessage(messages::PortEnterScreen, screen, exit,
                                               place_.previousScreen, place_.previousExit));
}

void ScreenHistory::arrive(const ScreenArrival& arrival) {
  if (place_.screen == NoScreen) {
    core::logDebug("{}", messages::formatMessage(messages::PortScreenStart, arrival.screen,
                                                 arrival.exit.value_or(0)));
    start(arrival.screen, arrival.exit.value_or(0));
    return;
  }
  if (!arrival.exit) {
    if (place_.screen != arrival.screen) {
      core::logDebug("{}", messages::formatMessage(messages::PortScreenStart, arrival.screen, 0));
      start(arrival.screen, 0);
    }
    return;
  }
  if (arrival.walkedWarp) {
    leaveByWarp(*arrival.walkedWarp);
  } else {
    leaveByScript();
  }
  enter(arrival.screen, *arrival.exit);
}

} // namespace dwe::game::story
