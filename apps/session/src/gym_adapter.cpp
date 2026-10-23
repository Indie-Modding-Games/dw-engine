#include <dwe/session/gym_adapter.hpp>

#include <string>
#include <utility>

namespace dwe::session {

ruleset::SectionChecks GymAdapter::sectionChecks() const {
  return {{std::string{game::gym::GymSectionName}, game::gym::gymSectionCheck()}};
}

core::Result<void> GymAdapter::readRules(const ruleset::Ruleset& rules) {
  auto gym = game::gym::readGymRules(rules);
  if (!gym) {
    return std::unexpected{gym.error()};
  }
  rules_ = std::move(*gym);
  return {};
}

} // namespace dwe::session
